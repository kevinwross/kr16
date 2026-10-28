#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "kr16/dap.hpp"
#include "kr16/dap_json.hpp"

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
