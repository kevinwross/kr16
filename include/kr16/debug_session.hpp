#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "kr16/container.hpp"
#include "kr16/cpu.hpp"
#include "kr16/host.hpp"
#include "kr16/machine.hpp"

namespace kr16 {

/// One decoded instruction in the current image.
struct SessionInstruction {
    std::uint16_t address = 0;
    std::uint8_t size = 2;
    std::string text;
    std::vector<std::uint8_t> bytes;
};

struct SessionSourceLine {
    std::string text;
    bool executable = false;
};

struct SessionFunction {
    std::string name;
    std::uint16_t low = 0;
    std::uint16_t high = 0;
};

struct SessionFrame {
    std::uint16_t pc = 0;
    int line = 0;
    std::string name;
};

struct SessionVariable {
    std::string name;
    std::string scope;
    std::string type;
    std::uint16_t address = 0;
    std::uint16_t value = 0;
    int size = 2;
    bool local = true;
    bool ok = false;
};

struct SessionField {
    std::string name;
    std::string type;
    int offset = 0;
    int size = 2;
};

enum class StepMode {
    Instruction,
    LineOver,
    LineIn,
    LineOut,
};

enum class SessionRunStop {
    Budget,      ///< The slice ended while still running.
    Halted,      ///< The CPU halted.
    Breakpoint,  ///< The new PC is an address breakpoint.
    NotRunning,  ///< run_slice() was called while stopped.
};

/// Build controls used by the windowed UI's load/compile/link settings.
struct SessionBuildOptions {
    bool debug = true;
    bool optimize = true;
    bool link_libc = true;
    bool listing = false;
};

/// Product debug model used by the windowed UI and by tests. It owns the
/// machine, CPU, container and breakpoints; callers never touch those
/// objects directly.
class DebugSession {
public:
    bool load(const std::filesystem::path& image, std::string& error);
    bool build_and_load(const std::filesystem::path& input,
                        std::string& error);
    bool build_and_load(const std::filesystem::path& input,
                        const std::filesystem::path& libc_source,
                        std::string& error);
    bool build_and_load(const std::filesystem::path& input,
                        const std::filesystem::path& libc_source,
                        const SessionBuildOptions& options,
                        std::string& error);
    bool restart(std::string& error);
    void unload();

    bool loaded() const noexcept { return loaded_; }
    const std::filesystem::path& image_path() const noexcept {
        return image_path_;
    }

    bool halted() const noexcept;
    std::string halt_reason() const;
    std::uint16_t pc() const noexcept;
    std::uint16_t sp() const noexcept;
    std::uint16_t sr() const noexcept;

    /// 0-4 are R0-R4, 5 is PC, 6 is SP, 7 is SR.
    std::uint16_t reg(int index) const;
    void set_reg(int index, std::uint16_t value);

    std::uint8_t read8(std::uint16_t address);
    void write8(std::uint16_t address, std::uint8_t value);
    std::uint16_t read16(std::uint16_t address);
    void write16(std::uint16_t address, std::uint16_t value);

    const std::vector<SessionInstruction>& disassembly() const noexcept {
        return disassembly_;
    }
    /// Add a decoded window at `address` when the current PC has been moved
    /// outside the image's normal text disassembly.
    bool ensure_disassembly_at(std::uint16_t address);
    std::uint16_t text_low() const noexcept { return text_low_; }
    std::uint32_t text_high() const noexcept { return text_high_; }

    const std::vector<SessionSourceLine>& source_lines() const noexcept {
        return source_lines_;
    }
    const std::string& source_path() const noexcept { return source_path_; }
    const std::set<int>& executable_lines() const noexcept {
        return executable_lines_;
    }
    const std::vector<SessionFunction>& functions() const noexcept {
        return functions_;
    }
    int line_at(std::uint16_t address) const;
    const SessionFunction* function_at(std::uint16_t address) const;
    std::vector<SessionFrame> frames();
    std::vector<SessionVariable> locals();
    std::vector<SessionVariable> globals();
    const std::vector<SessionField>* fields_for(
        const std::string& type) const;

    bool step_instruction();
    bool step_instruction_over();
    bool step_instruction_out();
    bool step_line(StepMode mode);
    bool seek_first_line();
    SessionRunStop run_slice(int max_instructions);
    void start_run();
    void stop_run(std::string reason);
    bool running() const noexcept { return running_; }

    const std::set<std::uint16_t>& address_breakpoints() const noexcept {
        return address_breakpoints_;
    }
    const std::set<int>& line_breakpoints() const noexcept {
        return line_breakpoints_;
    }
    void toggle_address_breakpoint(std::uint16_t address);
    void toggle_line_breakpoint(int line);
    bool has_breakpoint(std::uint16_t address) const;
    bool has_line_breakpoint(int line) const;

    const std::string& console() const noexcept { return console_; }

    std::uint64_t instructions_executed() const noexcept {
        return instructions_executed_;
    }
    std::uint64_t run_time_ms() const;
    void reset_stats();

private:
    struct LineRow {
        std::uint16_t address = 0;
        int line = 0;
    };
    struct FunctionRecord {
        SessionFunction info;
        std::vector<LineRow> rows;
    };
    struct LocalDefinition {
        std::string function;
        std::string name;
        std::string type;
        int offset = 0;
        int size = 2;
    };

    bool step_one();
    bool run_until_pc(std::uint16_t target);
    void decode_disassembly();
    void load_debug_info();
    void drain_console();
    void clear_image_state();
    int frame_depth();
    std::string function_name_at(std::uint16_t address) const;

    bool loaded_ = false;
    std::filesystem::path image_path_;
    Container container_;
    Machine machine_;
    Cpu cpu_;
    HostServices host_;
    std::vector<SessionInstruction> disassembly_;
    std::uint16_t text_low_ = 0;
    std::uint32_t text_high_ = 0;
    std::vector<SessionSourceLine> source_lines_;
    std::string source_path_;
    std::vector<SessionFunction> functions_;
    std::vector<FunctionRecord> function_records_;
    std::set<int> executable_lines_;
    std::vector<LocalDefinition> local_definitions_;
    std::map<std::string, std::string> global_types_;
    std::map<std::string, std::vector<SessionField>> type_fields_;
    std::set<std::uint16_t> address_breakpoints_;
    std::set<int> line_breakpoints_;
    std::string console_;
    std::string reason_;
    bool running_ = false;
    int run_previous_line_ = 0;
    std::uint64_t instructions_executed_ = 0;
    std::chrono::steady_clock::time_point run_started_{};
    std::uint64_t accumulated_run_us_ = 0;
};

}  // namespace kr16
