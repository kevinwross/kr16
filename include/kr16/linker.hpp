#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "kr16/container.hpp"

namespace kr16 {

struct LinkOptions {
    std::uint16_t text_address = 0x0100;
    std::uint16_t stack_reserve = 0x1000;
};

struct LinkResult {
    bool ok = false;
    Container image;
    std::vector<std::string> diagnostics;
};

LinkResult link_objects(const std::vector<Container>& objects,
                        const LinkOptions& options = {});
LinkResult link_files(const std::vector<std::string>& paths,
                      const LinkOptions& options = {});

}  // namespace kr16
