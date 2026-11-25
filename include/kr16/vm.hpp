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
    bool trace_syscalls = false;
    std::string fs_root = "sandbox";
    bool fs_root_explicit = false;
    bool fs_readonly = false;
    bool no_host_fs = false;
    std::uint32_t fs_quota = 5u * 1024u * 1024u;
    unsigned mhz = 1;
    std::string input_path;
    // Read a line from stdin when the guest reads the keyboard and the queue is
    // empty; console output is written through to stdout as it is produced.
    bool console = false;
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
    std::string syscall_trace;
    std::string error;
};

VmResult run_vm(const std::string& program_path, const VmOptions& options);

}  // namespace kr16
