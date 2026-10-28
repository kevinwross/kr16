#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "kr16/diagnostic.hpp"

namespace kr16 {

class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> data) : data_(data) {}

    std::size_t position() const noexcept { return position_; }
    std::size_t remaining() const noexcept { return data_.size() - position_; }
    bool empty() const noexcept { return position_ >= data_.size(); }

    std::uint8_t read_u8() {
        require(1);
        return data_[position_++];
    }

    std::uint16_t read_u16() {
        require(2);
        const auto low = static_cast<std::uint16_t>(data_[position_]);
        const auto high = static_cast<std::uint16_t>(data_[position_ + 1]);
        position_ += 2;
        return static_cast<std::uint16_t>(low | (high << 8));
    }

    std::uint32_t read_u32() {
        require(4);
        const auto b0 = static_cast<std::uint32_t>(data_[position_]);
        const auto b1 = static_cast<std::uint32_t>(data_[position_ + 1]);
        const auto b2 = static_cast<std::uint32_t>(data_[position_ + 2]);
        const auto b3 = static_cast<std::uint32_t>(data_[position_ + 3]);
        position_ += 4;
        return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    }

    std::int32_t read_i32() { return static_cast<std::int32_t>(read_u32()); }

    std::vector<std::uint8_t> read_bytes(std::size_t count) {
        require(count);
        std::vector<std::uint8_t> result(data_.begin() +
                                             static_cast<std::ptrdiff_t>(position_),
                                         data_.begin() + static_cast<std::ptrdiff_t>(
                                                              position_ + count));
        position_ += count;
        return result;
    }

    std::string read_string(std::size_t count) {
        require(count);
        std::string result(reinterpret_cast<const char*>(data_.data() + position_),
                           count);
        position_ += count;
        return result;
    }

    void skip(std::size_t count) {
        require(count);
        position_ += count;
    }

    void seek(std::size_t position) {
        if (position > data_.size()) {
            throw ParseError(position, "seek past end of input");
        }
        position_ = position;
    }

    std::span<const std::uint8_t> span(std::size_t offset,
                                       std::size_t count) const {
        if (offset > data_.size() || count > data_.size() - offset) {
            throw ParseError(offset, "range past end of input");
        }
        return data_.subspan(offset, count);
    }

private:
    void require(std::size_t count) const {
        if (count > data_.size() - position_) {
            throw ParseError(position_, "unexpected end of input");
        }
    }

    std::span<const std::uint8_t> data_;
    std::size_t position_ = 0;
};

class ByteWriter {
public:
    void write_u8(std::uint8_t value) { data_.push_back(value); }

    void write_u16(std::uint16_t value) {
        data_.push_back(static_cast<std::uint8_t>(value & 0xFF));
        data_.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    }

    void write_u32(std::uint32_t value) {
        data_.push_back(static_cast<std::uint8_t>(value & 0xFF));
        data_.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        data_.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
        data_.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
    }

    void write_i32(std::int32_t value) {
        write_u32(static_cast<std::uint32_t>(value));
    }

    void write_bytes(std::span<const std::uint8_t> bytes) {
        data_.insert(data_.end(), bytes.begin(), bytes.end());
    }

    void write_bytes(const std::vector<std::uint8_t>& bytes) {
        data_.insert(data_.end(), bytes.begin(), bytes.end());
    }

    void write_zeros(std::size_t count) {
        data_.insert(data_.end(), count, std::uint8_t{0});
    }

    void align(std::size_t alignment) {
        if (alignment <= 1) {
            return;
        }
        const std::size_t remainder = data_.size() % alignment;
        if (remainder != 0) {
            write_zeros(alignment - remainder);
        }
    }

    void patch_u32(std::size_t offset, std::uint32_t value) {
        if (offset > data_.size() || data_.size() - offset < 4) {
            throw ParseError(offset, "patch past end of output");
        }
        data_[offset] = static_cast<std::uint8_t>(value & 0xFF);
        data_[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
        data_[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
        data_[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    }

    void patch_bytes(std::size_t offset,
                     std::span<const std::uint8_t> bytes) {
        if (offset > data_.size() || bytes.size() > data_.size() - offset) {
            throw ParseError(offset, "patch past end of output");
        }
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            data_[offset + i] = bytes[i];
        }
    }

    std::size_t position() const noexcept { return data_.size(); }
    const std::vector<std::uint8_t>& data() const noexcept { return data_; }
    std::vector<std::uint8_t> take() { return std::move(data_); }

private:
    std::vector<std::uint8_t> data_;
};

}  // namespace kr16
