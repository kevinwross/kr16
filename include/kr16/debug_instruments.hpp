#pragma once

// The four debugging instruments described in debug-instruments.md: --watch,
// --break, --watch-page and --watch-fp. kr16 addresses 16-bit words, so a watch
// covers two bytes and the page log covers 4 KiB of its 64 KiB space.
//
// They are parsed by the `vm` command and read by the runner, so they live at
// namespace scope rather than in the runner's options: threading four unused
// fields through every caller buys nothing.

#include <array>
#include <cstdint>

namespace kr16 {

struct DebugInstruments {
    bool watch = false;
    std::uint16_t watch_address = 0;
    bool breakpoint = false;
    std::uint16_t break_address = 0;
    unsigned break_limit = 1;
    unsigned break_hits = 0;
    bool page = false;
    std::uint16_t page_base = 0;
    bool page_valid = false;
    std::array<std::uint8_t, 4096> page_before{};
    bool frame_pointer = false;
};

inline DebugInstruments g_debug;

}  // namespace kr16
