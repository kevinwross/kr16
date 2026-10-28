#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kr16 {

struct VmOptions {
    bool raw = false;
    std::uint16_t base = 0x0100;
    std::string bios_path;
    bool no_bios = false;
    std::uint64_t max_cycles = 0;
    bool headless = false;
    bool trace = false;
    bool stats = false;
    bool realtime = false;
    unsigned mhz = 1;
    std::string input_path;
    std::vector<std::string> program_args;
};

struct VmResult {
    bool ok = false;
    int exit_code = 0;
    std::uint64_t cycles = 0;
    std::uint64_t instructions = 0;
    bool timed_out = false;
    std::string output;
    std::string trace;
    std::string error;
};

VmResult run_vm(const std::string& program_path, const VmOptions& options);

}  // namespace kr16
