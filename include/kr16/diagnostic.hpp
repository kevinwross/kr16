#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace kr16 {

class ParseError : public std::runtime_error {
public:
    ParseError(std::size_t offset, std::string message)
        : std::runtime_error(std::move(message)), offset_(offset) {}

    std::size_t offset() const noexcept { return offset_; }

private:
    std::size_t offset_;
};

}  // namespace kr16
