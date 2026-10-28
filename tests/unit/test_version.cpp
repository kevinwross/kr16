#include <doctest/doctest.h>

#include "kr16/version.hpp"

TEST_CASE("version string exposes tool and format versions") {
    const std::string text = kr16::format_version();
    CHECK(text.rfind("kr16 ", 0) == 0);
    CHECK(text.find("(ISA 1, container 1, host ABI 1, debug 1)") !=
          std::string::npos);
}

TEST_CASE("format version constants match the initial contract") {
    CHECK(kr16::kIsaVersion == 1);
    CHECK(kr16::kContainerVersion == 1);
    CHECK(kr16::kHostAbiVersion == 1);
    CHECK(kr16::kDebugFormatVersion == 1);
}
