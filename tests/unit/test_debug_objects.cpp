#include <cstdint>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "kr16/container.hpp"
#include "kr16/debug_objects.hpp"

TEST_CASE("debug objects have the fixed 16/48/12 layout") {
    kr16::StringTable strings;
    kr16::DebugObjects objects;
    kr16::DebugObjectRecord object;
    object.id = 7;
    object.name = "main.c";
    object.code_address = 0x0100;
    object.code_size = 0x20;
    object.line_offset = 0;
    object.line_size = 8;
    object.types_offset = 8;
    object.types_size = 16;
    object.vars_offset = 24;
    object.vars_size = 16;
    object.frames_offset = 40;
    object.frames_size = 20;
    objects.objects.push_back(object);

    kr16::DebugSourceRecord source;
    source.path = "main.c";
    source.build_directory = "/work";
    objects.sources.push_back(source);

    const auto bytes = kr16::serialize_debug_objects(objects, strings);
    CHECK(bytes.size() == 16 + 48 + 12);

    const auto decoded = kr16::parse_debug_objects(bytes, strings);
    REQUIRE(decoded.objects.size() == 1);
    REQUIRE(decoded.sources.size() == 1);
    CHECK(decoded.objects[0].id == 7);
    CHECK(decoded.objects[0].name == "main.c");
    CHECK(decoded.objects[0].code_address == 0x0100);
    CHECK(decoded.objects[0].code_size == 0x20);
    CHECK(decoded.objects[0].line_size == 8);
    CHECK(decoded.objects[0].frames_size == 20);
    CHECK(decoded.sources[0].path == "main.c");
    CHECK(decoded.sources[0].build_directory == "/work");
}

TEST_CASE("debug objects reject malformed buffers") {
    kr16::StringTable strings;
    kr16::DebugObjects objects;
    const auto bytes = kr16::serialize_debug_objects(objects, strings);
    const auto empty = kr16::parse_debug_objects(bytes, strings);
    CHECK(empty.objects.empty());
    CHECK(empty.sources.empty());

    auto bad = bytes;
    bad[0] = 'X';
    CHECK_THROWS_AS(kr16::parse_debug_objects(bad, strings), kr16::ParseError);

    auto truncated = bytes;
    truncated.resize(12);
    CHECK_THROWS_AS(kr16::parse_debug_objects(truncated, strings),
                    kr16::ParseError);
}
