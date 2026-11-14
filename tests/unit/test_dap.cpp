#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "kr16/dap.hpp"
#include "kr16/dap_json.hpp"
#include "kr16/assembler.hpp"
#include "kr16/compiler.hpp"
#include "kr16/linker.hpp"

TEST_CASE("DAP JSON preserves integer values") {
    const auto value = kr16::dap::JsonValue::parse(
        "{\"seq\":7,\"type\":\"request\",\"arguments\":{\"line\":42}}");
    REQUIRE(value.is_object());
    CHECK(value.find("seq")->as_integer() == 7);
    CHECK(value.find("type")->as_string() == "request");
    CHECK(value.find("arguments")->find("line")->as_integer() == 42);
    const auto round_trip =
        kr16::dap::JsonValue::parse(value.dump());
    CHECK(round_trip.find("arguments")->find("line")->as_integer() == 42);
}

TEST_CASE("DAP framing handles split and combined packets") {
    const auto first = kr16::dap::dap_encode_message("{\"seq\":1}");
    const auto second = kr16::dap::dap_encode_message("{\"seq\":2}");
    kr16::dap::DapFramer framer;
    framer.feed(first.substr(0, 5));
    CHECK_FALSE(framer.next().has_value());
    framer.feed(first.substr(5) + second);
    CHECK(framer.next().has_value());
    CHECK(*framer.next() == "{\"seq\":2}");
    CHECK_FALSE(framer.next().has_value());
}

TEST_CASE("DAP framing rejects malformed headers") {
    kr16::dap::DapFramer framer;
    framer.feed("Content-Length: nope\r\n\r\n{}");
    CHECK_THROWS_AS(framer.next(), kr16::dap::DapProtocolError);
}

TEST_CASE("DAP initialize session produces response and event") {
    kr16::dap::DapSession session;
    const auto messages = session.handle_message(
        "{\"seq\":1,\"type\":\"request\",\"command\":\"initialize\","
        "\"arguments\":{}}");
    REQUIRE(messages.size() == 2);
    const auto response = kr16::dap::JsonValue::parse(messages[0]);
    CHECK(response.find("type")->as_string() == "response");
    CHECK(response.find("command")->as_string() == "initialize");
    CHECK(response.find("success")->as_boolean());
    const auto event = kr16::dap::JsonValue::parse(messages[1]);
    CHECK(event.find("event")->as_string() == "initialized");
}


TEST_CASE("DAP launch forwards program arguments") {
    kr16::dap::DapOptions options;
    options.program =
        (std::filesystem::path(KR16_SOURCE_DIR) / "tests" / "golden" /
         "exec_min.kex")
            .string();
    kr16::dap::DapSession session(options);
    session.handle_message(
        "{\"seq\":1,\"type\":\"request\",\"command\":\"initialize\","
        "\"arguments\":{}}");
    session.handle_message(
        "{\"seq\":2,\"type\":\"request\",\"command\":\"launch\","
        "\"arguments\":{\"args\":[\"one\",\"two words\"]}}");
    session.handle_message(
        "{\"seq\":3,\"type\":\"request\",\"command\":\"configurationDone\","
        "\"arguments\":{}}");
    const auto messages = session.handle_message(
        "{\"seq\":4,\"type\":\"request\",\"command\":\"evaluate\","
        "\"arguments\":{\"expression\":\"$r1\"}}");
    REQUIRE(messages.size() == 1);
    const auto response = kr16::dap::JsonValue::parse(messages[0]);
    CHECK(response.find("success")->as_boolean());
    CHECK(response.find("body")->find("result")->as_string() == "0x0003");
}


TEST_CASE("DAP stack frame uses the compiled source path") {
    kr16::CompileOptions options;
    options.debug = true;
    options.source_name = "dap_loaded_source.c";
    const auto compiled = kr16::compile_c(
        "int main(void) { return 0; }\n", options);
    REQUIRE(compiled.ok);
    const auto assembled =
        kr16::assemble_text(compiled.assembly, kr16::AsmOptions{});
    REQUIRE(assembled.ok);
    const auto source_file =
        std::filesystem::current_path() / "dap_loaded_source.c";
    {
        std::ofstream source_output(source_file);
        source_output << "int main(void) { return 0; }\n";
    }
    const auto linked = kr16::link_objects({assembled.object});
    REQUIRE(linked.ok);
    const auto image = linked.image.write();
    const auto path = std::filesystem::current_path() / "dap_source.kex";
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(image.data()),
                     static_cast<std::streamsize>(image.size()));
    }

    kr16::dap::DapOptions dap_options;
    dap_options.program = path.string();
    kr16::dap::DapSession session(dap_options);
    session.handle_message(
        "{\"seq\":1,\"type\":\"request\",\"command\":\"initialize\","
        "\"arguments\":{}}");
    session.handle_message(
        "{\"seq\":2,\"type\":\"request\",\"command\":\"launch\","
        "\"arguments\":{\"program\":\"dap_source.kex\"}}");
    session.handle_message(
        "{\"seq\":3,\"type\":\"request\",\"command\":\"configurationDone\","
        "\"arguments\":{}}");
    const auto messages = session.handle_message(
        "{\"seq\":4,\"type\":\"request\",\"command\":\"stackTrace\","
        "\"arguments\":{\"threadId\":1}}");
    REQUIRE(messages.size() == 1);
    const auto response = kr16::dap::JsonValue::parse(messages[0]);
    const auto* frames = response.find("body");
    REQUIRE(frames != nullptr);
    const auto* stack = frames->find("stackFrames");
    REQUIRE(stack != nullptr);
    REQUIRE(stack->as_array().size() == 1);
    const auto* source = stack->as_array()[0].find("source");
    REQUIRE(source != nullptr);
    CHECK(source->find("path")->as_string().ends_with("dap_loaded_source.c"));
    CHECK(stack->as_array()[0].find("line")->as_integer() == 1);
    const auto restarted = session.handle_message(
        "{\"seq\":5,\"type\":\"request\",\"command\":\"restart\","
        "\"arguments\":{}}");
    bool restart_ok = false;
    bool restart_stopped = false;
    for (const auto& message : restarted) {
        const auto parsed = kr16::dap::JsonValue::parse(message);
        if (parsed.find("type")->as_string() == "response" &&
            parsed.find("command")->as_string() == "restart") {
            restart_ok = parsed.find("success")->as_boolean();
        }
        if (parsed.find("type")->as_string() == "event" &&
            parsed.find("event")->as_string() == "stopped") {
            restart_stopped = true;
        }
    }
    CHECK(restart_ok);
    CHECK(restart_stopped);
    const auto loaded = session.handle_message(
        "{\"seq\":6,\"type\":\"request\",\"command\":\"loadedSources\","
        "\"arguments\":{}}");
    REQUIRE(loaded.size() == 1);
    const auto loaded_body = kr16::dap::JsonValue::parse(loaded[0]);
    const auto* loaded_sources = loaded_body.find("body")->find("sources");
    REQUIRE(loaded_sources != nullptr);
    REQUIRE(loaded_sources->as_array().size() == 1);
    const auto loaded_path =
        loaded_sources->as_array()[0].find("path")->as_string();
    CHECK(loaded_path.ends_with("dap_loaded_source.c"));
    std::string escaped_path;
    for (const char ch : loaded_path) {
        if (ch == '\\' || ch == '"') {
            escaped_path.push_back('\\');
        }
        escaped_path.push_back(ch);
    }
    const std::string source_request =
        "{\"seq\":7,\"type\":\"request\",\"command\":\"source\","
        "\"arguments\":{\"source\":{\"path\":\"" + escaped_path + "\"}}}";
    const auto source_text = session.handle_message(source_request);
    REQUIRE(source_text.size() == 1);
    const auto source_body = kr16::dap::JsonValue::parse(source_text[0]);
    INFO(source_text[0]);
    CHECK(source_body.find("success")->as_boolean());
    CHECK(source_body.find("body")->find("content")->as_string().find(
              "return 0") != std::string::npos);
    const std::string breakpoint_request =
        "{\"seq\":8,\"type\":\"request\",\"command\":\"setBreakpoints\","
        "\"arguments\":{\"source\":{\"path\":\"" + escaped_path +
        "\"},\"breakpoints\":[{\"address\":\"0x0100\","
        "\"condition\":\"$pc == 0x0100\"}]}}";
    const auto conditional = session.handle_message(breakpoint_request);
    REQUIRE(conditional.size() == 1);
    CHECK(kr16::dap::JsonValue::parse(conditional[0])
              .find("success")
              ->as_boolean());
    const auto conditional_restart = session.handle_message(
        "{\"seq\":9,\"type\":\"request\",\"command\":\"restart\","
        "\"arguments\":{}}");
    bool conditional_hit = false;
    for (const auto& message : conditional_restart) {
        const auto parsed = kr16::dap::JsonValue::parse(message);
        if (parsed.find("type")->as_string() == "event" &&
            parsed.find("event")->as_string() == "stopped" &&
            parsed.find("body")->find("reason")->as_string() ==
                "breakpoint") {
            conditional_hit = true;
        }
    }
    CHECK(conditional_hit);
    std::filesystem::remove(path);
    std::filesystem::remove(source_file);
}







