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
    const auto paged_stack = session.handle_message(
        "{\"seq\":4,\"type\":\"request\",\"command\":\"stackTrace\","
        "\"arguments\":{\"threadId\":1,\"startFrame\":0,\"levels\":1}}");
    REQUIRE(paged_stack.size() == 1);
    const auto paged_body = kr16::dap::JsonValue::parse(paged_stack[0]);
    CHECK(paged_body.find("body")->find("stackFrames")->as_array().size() == 1);
    CHECK(paged_body.find("body")->find("totalFrames")->as_integer() >= 1);
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
    const std::string hit_request =
        "{\"seq\":12,\"type\":\"request\",\"command\":\"setBreakpoints\","
        "\"arguments\":{\"source\":{\"path\":\"" + escaped_path +
        "\"},\"breakpoints\":[{\"address\":\"0x0100\","
        "\"hitCondition\":\"2\"}]}}";
    (void)session.handle_message(hit_request);
    auto stopped_reason = [&](const std::string& request) {
        const auto messages = session.handle_message(request);
        for (const auto& message : messages) {
            const auto parsed = kr16::dap::JsonValue::parse(message);
            if (parsed.find("type")->as_string() == "event" &&
                parsed.find("event")->as_string() == "stopped") {
                return parsed.find("body")->find("reason")->as_string();
            }
        }
        return std::string();
    };
    CHECK(stopped_reason(
              "{\"seq\":13,\"type\":\"request\",\"command\":\"restart\","
              "\"arguments\":{}}") == "entry");
    CHECK(stopped_reason(
              "{\"seq\":14,\"type\":\"request\",\"command\":\"restart\","
              "\"arguments\":{}}") == "breakpoint");
    const auto set_value = session.handle_message(
        "{\"seq\":10,\"type\":\"request\",\"command\":\"setVariable\","
        "\"arguments\":{\"variablesReference\":1,\"name\":\"R0\","
        "\"value\":\"0x1234\"}}");
    REQUIRE(set_value.size() == 1);
    CHECK(kr16::dap::JsonValue::parse(set_value[0])
              .find("success")
              ->as_boolean());
    const auto read_back = session.handle_message(
        "{\"seq\":11,\"type\":\"request\",\"command\":\"evaluate\","
        "\"arguments\":{\"expression\":\"$r0\",\"context\":\"repl\"}}");
    REQUIRE(read_back.size() == 1);
    CHECK(kr16::dap::JsonValue::parse(read_back[0])
              .find("body")
              ->find("result")
              ->as_string() == "0x1234");
    auto read_pc = [&]() {
        const auto response = session.handle_message(
            "{\"seq\":15,\"type\":\"request\",\"command\":\"evaluate\","
            "\"arguments\":{\"expression\":\"$pc\",\"context\":\"repl\"}}");
        return kr16::dap::JsonValue::parse(response[0])
            .find("body")->find("result")->as_string();
    };
    const auto pc_before = read_pc();
    (void)session.handle_message(
        "{\"seq\":16,\"type\":\"request\",\"command\":\"stepIn\","
        "\"arguments\":{\"threadId\":1}}");
    const auto pc_after = read_pc();
    CHECK(pc_after != pc_before);
    const auto back = session.handle_message(
        "{\"seq\":17,\"type\":\"request\",\"command\":\"stepBack\","
        "\"arguments\":{\"threadId\":1}}");
    CHECK(kr16::dap::JsonValue::parse(back[0]).find("success")->as_boolean());
    CHECK(read_pc() == pc_before);
    std::filesystem::remove(path);
    std::filesystem::remove(source_file);
}













TEST_CASE("DAP reads memory disassembles and sets function breakpoints") {
    const auto assembled = kr16::assemble_text(
        ".text\n.global _start\n_start:\n  MOV R0, #0x1234\n  HLT\n",
        kr16::AsmOptions{});
    REQUIRE(assembled.ok);
    const auto linked = kr16::link_objects({assembled.object});
    REQUIRE(linked.ok);
    const auto bytes = linked.image.write();
    const auto path = std::filesystem::current_path() / "dap_views.kex";
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    kr16::dap::DapOptions options;
    options.program = path.string();
    kr16::dap::DapSession session(options);
    session.handle_message(
        "{\"seq\":1,\"type\":\"request\",\"command\":\"initialize\","
        "\"arguments\":{}}");
    session.handle_message(
        "{\"seq\":2,\"type\":\"request\",\"command\":\"launch\","
        "\"arguments\":{}}");
    session.handle_message(
        "{\"seq\":3,\"type\":\"request\",\"command\":\"configurationDone\","
        "\"arguments\":{}}");

    const auto memory = session.handle_message(
        "{\"seq\":4,\"type\":\"request\",\"command\":\"readMemory\","
        "\"arguments\":{\"memoryReference\":\"0x0100\",\"count\":2}}");
    REQUIRE(memory.size() == 1);
    const auto memory_response = kr16::dap::JsonValue::parse(memory[0]);
    CHECK(memory_response.find("success")->as_boolean());
    CHECK(memory_response.find("body")->find("address")->as_string() ==
          "0x0100");
    CHECK_FALSE(
        memory_response.find("body")->find("data")->as_string().empty());

    const auto disassembled = session.handle_message(
        "{\"seq\":5,\"type\":\"request\",\"command\":\"disassemble\","
        "\"arguments\":{\"memoryReference\":\"0x0100\","
        "\"instructionCount\":2}}");
    REQUIRE(disassembled.size() == 1);
    const auto disassembly = kr16::dap::JsonValue::parse(disassembled[0]);
    REQUIRE(disassembly.find("success")->as_boolean());
    const auto* instructions = disassembly.find("body")->find("instructions");
    REQUIRE(instructions != nullptr);
    REQUIRE(instructions->as_array().size() == 2);
    CHECK(instructions->as_array()[0]
              .find("instruction")
              ->as_string()
              .find("MOV") != std::string::npos);

    const auto breakpoints = session.handle_message(
        "{\"seq\":6,\"type\":\"request\","
        "\"command\":\"setFunctionBreakpoints\","
        "\"arguments\":{\"breakpoints\":[{\"name\":\"_start\"},"
        "{\"name\":\"missing\"}]}}");
    REQUIRE(breakpoints.size() == 1);
    const auto breakpoint_response = kr16::dap::JsonValue::parse(breakpoints[0]);
    REQUIRE(breakpoint_response.find("success")->as_boolean());
    const auto* entries =
        breakpoint_response.find("body")->find("breakpoints");
    REQUIRE(entries != nullptr);
    REQUIRE(entries->as_array().size() == 2);
    CHECK(entries->as_array()[0].find("verified")->as_boolean());
    CHECK_FALSE(entries->as_array()[1].find("verified")->as_boolean());
    std::filesystem::remove(path);
}
