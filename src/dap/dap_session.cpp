#include "kr16/dap.hpp"

#include <charconv>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kr16/container.hpp"
#include "kr16/cpu.hpp"
#include "kr16/dap_json.hpp"
#include "kr16/debug_info.hpp"
#include "kr16/debug_objects.hpp"
#include "kr16/disassembler.hpp"
#include "kr16/host.hpp"
#include "kr16/process.hpp"
#include "kr16/instruction.hpp"
#include "kr16/machine.hpp"

namespace kr16::dap {
namespace {

constexpr std::int64_t kThreadId = 1;
constexpr std::int64_t kRegistersReference = 1;
constexpr std::int64_t kGlobalsReference = 2;
constexpr std::int64_t kLocalsReference = 3;

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) {
        throw std::runtime_error("cannot open " + path);
    }
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

std::string base_name(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

struct DapDebugVar {
    std::string function;
    std::string name;
    int offset = 0;
    int size = 2;
};

std::vector<DapDebugVar> parse_debug_vars(const Container& container) {
    std::vector<DapDebugVar> result;
    const auto* section = container.find_section(".debug_vars");
    if (section == nullptr) {
        return result;
    }
    std::size_t begin = 0;
    while (begin < section->data.size()) {
        std::size_t end = begin;
        while (end < section->data.size() && section->data[end] != 0) {
            ++end;
        }
        const std::string entry(
            reinterpret_cast<const char*>(section->data.data() + begin),
            end - begin);
        begin = end + 1;
        const auto first = entry.find('|');
        const auto second =
            first == std::string::npos ? std::string::npos
                                       : entry.find('|', first + 1);
        const auto third = second == std::string::npos
                               ? std::string::npos
                               : entry.find('|', second + 1);
        if (first == std::string::npos || second == std::string::npos ||
            third == std::string::npos) {
            continue;
        }
        DapDebugVar variable;
        variable.function = entry.substr(0, first);
        variable.name = entry.substr(first + 1, second - first - 1);
        try {
            variable.offset = std::stoi(entry.substr(second + 1,
                                                     third - second - 1));
            variable.size = std::stoi(entry.substr(third + 1));
        } catch (const std::exception&) {
            continue;
        }
        result.push_back(std::move(variable));
    }
    return result;
}

std::string format_hex(std::uint16_t value) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string result = "0x0000";
    for (int i = 0; i < 4; ++i) {
        result[2 + (3 - i)] =
            kHex[(static_cast<unsigned>(value) >> (4 * i)) & 0xFu];
    }
    return result;
}

std::string debug_function_at(const Container& container,
                              std::uint16_t pc) {
    std::set<std::string> functions;
    for (const auto& variable : parse_debug_vars(container)) {
        functions.insert(variable.function);
    }
    std::string best;
    std::uint32_t best_address = 0;
    for (const auto& symbol : container.symbols) {
        if (functions.count(symbol.name) == 0) {
            continue;
        }
        const auto address = static_cast<std::uint32_t>(symbol.value);
        if (address <= pc && (best.empty() || address >= best_address)) {
            best = symbol.name;
            best_address = address;
        }
    }
    return best;
}

std::string base64_encode(const std::vector<std::uint8_t>& bytes) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::uint32_t a = bytes[i];
        const std::uint32_t b = i + 1 < bytes.size() ? bytes[i + 1] : 0;
        const std::uint32_t c = i + 2 < bytes.size() ? bytes[i + 2] : 0;
        const std::uint32_t combined = (a << 16) | (b << 8) | c;
        result.push_back(kAlphabet[(combined >> 18) & 0x3F]);
        result.push_back(kAlphabet[(combined >> 12) & 0x3F]);
        result.push_back(i + 1 < bytes.size()
                             ? kAlphabet[(combined >> 6) & 0x3F]
                             : '=');
        result.push_back(i + 2 < bytes.size() ? kAlphabet[combined & 0x3F]
                                              : '=');
    }
    return result;
}

std::optional<std::uint16_t> parse_address(const JsonValue* value) {
    if (value == nullptr) {
        return std::nullopt;
    }
    if (value->is_integer()) {
        const auto raw = value->as_integer();
        if (raw < 0 || raw > 0xFFFF) {
            return std::nullopt;
        }
        return static_cast<std::uint16_t>(raw);
    }
    if (!value->is_string()) {
        return std::nullopt;
    }
    std::string_view text = value->as_string();
    int base = 10;
    if (text.size() > 2 && text[0] == '0' &&
        (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text.remove_prefix(2);
    }
    if (text.empty()) {
        return std::nullopt;
    }
    unsigned long long raw = 0;
    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), raw, base);
    if (result.ec != std::errc{} ||
        result.ptr != text.data() + text.size() || raw > 0xFFFFu) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(raw);
}

std::int64_t integer_or(const JsonValue* object, std::string_view key,
                        std::int64_t fallback) {
    if (object == nullptr) {
        return fallback;
    }
    const JsonValue* value = object->find(key);
    if (value == nullptr || !value->is_integer()) {
        return fallback;
    }
    return value->as_integer();
}

std::string string_or(const JsonValue* object, std::string_view key,
                      const std::string& fallback) {
    if (object == nullptr) {
        return fallback;
    }
    const JsonValue* value = object->find(key);
    if (value == nullptr || !value->is_string()) {
        return fallback;
    }
    return value->as_string();
}

std::uint16_t symbol_value(const Container& container, const std::string& name,
                           std::uint16_t fallback) {
    for (const auto& symbol : container.symbols) {
        if (symbol.name == name) {
            return static_cast<std::uint16_t>(symbol.value);
        }
    }
    return fallback;
}

std::string function_name(const Container& container, std::uint16_t pc) {
    const Symbol* containing = nullptr;
    for (const auto& symbol : container.symbols) {
        if (symbol.type != SymbolType::Function) {
            continue;
        }
        const auto start = static_cast<std::uint32_t>(symbol.value);
        const auto end = start + (symbol.size == 0 ? 1u : symbol.size);
        if (pc >= start && pc < end) {
            if (containing == nullptr || symbol.value > containing->value) {
                containing = &symbol;
            }
        }
    }
    if (containing != nullptr) {
        return containing->name;
    }
    for (const auto& symbol : container.symbols) {
        if (symbol.value == pc && !symbol.name.empty()) {
            return symbol.name;
        }
    }
    return {};
}

int line_for_address(const std::vector<DebugLineRow>& rows,
                     std::uint16_t address) {
    for (const auto& row : rows) {
        if (row.address == address) {
            return static_cast<int>(row.line);
        }
    }
    int line = 0;
    std::uint16_t best = 0;
    bool found = false;
    for (const auto& row : rows) {
        if (row.address <= address && (!found || row.address >= best)) {
            best = row.address;
            line = static_cast<int>(row.line);
            found = true;
        }
    }
    return found ? line : 0;
}

std::optional<std::uint16_t> address_for_line(
    const std::vector<DebugLineRow>& rows, int line) {
    if (line <= 0) {
        return std::nullopt;
    }
    for (const auto& row : rows) {
        if (static_cast<int>(row.line) == line) {
            return row.address;
        }
    }
    return std::nullopt;
}

std::vector<DebugLineRow> load_line_rows(const Container& container) {
    const Section* section = container.find_section(".debug_line");
    if (section == nullptr) {
        return {};
    }
    try {
        return parse_debug_line(section->data);
    } catch (const std::exception&) {
        return {};
    }
}

std::string resolve_source_path(const std::string& path, const std::string& program) {
    if (path.empty()) {
        return {};
    }
    std::error_code ec;
    std::filesystem::path candidate(path);
    if (candidate.is_absolute() && std::filesystem::exists(candidate, ec)) {
        return candidate.lexically_normal().string();
    }
    const auto program_path = std::filesystem::path(program);
    std::vector<std::filesystem::path> bases;
    bases.push_back(std::filesystem::current_path(ec));
    bases.push_back(program_path.parent_path());
    bases.push_back(program_path.parent_path().parent_path());
    for (const auto& base : bases) {
        if (base.empty()) {
            continue;
        }
        const auto resolved = base / candidate;
        if (std::filesystem::exists(resolved, ec)) {
            return resolved.lexically_normal().string();
        }
    }
    if (candidate.is_absolute()) {
        return candidate.lexically_normal().string();
    }
    return std::filesystem::absolute(candidate, ec).lexically_normal().string();
}

std::string load_source_path(const Container& container) {
    const Section* objects = container.find_section(".debug_objects");
    const Section* strings = container.find_section(".debug_str");
    if (objects != nullptr && strings != nullptr) {
        try {
            const auto table = StringTable::parse(strings->data);
            const auto decoded = parse_debug_objects(objects->data, table);
            if (!decoded.sources.empty()) {
                return decoded.sources.front().path;
            }
        } catch (const std::exception&) {
        }
    }
    if (const Section* sources = container.find_section(".debug_sources")) {
        std::string path;
        for (const auto byte : sources->data) {
            if (byte == 0) {
                if (!path.empty()) {
                    return path;
                }
                continue;
            }
            path.push_back(static_cast<char>(byte));
        }
    }
    return {};
}

std::optional<std::uint16_t> condition_value(const Cpu& cpu,
                                             std::string_view text) {
    while (!text.empty() && text.front() == ' ') {
        text.remove_prefix(1);
    }
    while (!text.empty() && text.back() == ' ') {
        text.remove_suffix(1);
    }
    if (!text.empty() && text.front() == '$') {
        text.remove_prefix(1);
        if (text == "pc") {
            return cpu.pc();
        }
        if (text == "sp") {
            return cpu.sp();
        }
        if (text == "sr") {
            return cpu.sr();
        }
        if (text.size() == 2 && (text[0] == 'r' || text[0] == 'R') &&
            text[1] >= '0' && text[1] <= '4') {
            return cpu.reg(static_cast<std::uint8_t>(text[1] - '0'));
        }
        return std::nullopt;
    }
    unsigned value = 0;
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    int base = 10;
    if (text.size() > 2 && text[0] == '0' &&
        (text[1] == 'x' || text[1] == 'X')) {
        begin += 2;
        base = 16;
    }
    const auto result = std::from_chars(begin, end, value, base);
    if (result.ec != std::errc{} || result.ptr != end || value > 0xFFFF) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(value);
}

bool breakpoint_condition(const Cpu& cpu, std::string_view condition) {
    std::size_t begin = 0;
    while (begin < condition.size() && condition[begin] == ' ') {
        ++begin;
    }
    std::size_t end = condition.size();
    while (end > begin && condition[end - 1] == ' ') {
        --end;
    }
    condition = condition.substr(begin, end - begin);
    if (condition.empty()) {
        return true;
    }
    std::size_t op = std::string::npos;
    std::string operation;
    for (std::size_t i = 0; i < condition.size(); ++i) {
        if (i + 1 < condition.size() &&
            (condition.substr(i, 2) == "==" ||
             condition.substr(i, 2) == "!=" ||
             condition.substr(i, 2) == "<=" ||
             condition.substr(i, 2) == ">=")) {
            op = i;
            operation = std::string(condition.substr(i, 2));
            break;
        }
        if (condition[i] == '<' || condition[i] == '>') {
            op = i;
            operation = std::string(1, condition[i]);
            break;
        }
    }
    if (op == std::string::npos) {
        return false;
    }
    const auto lhs = condition_value(cpu, condition.substr(0, op));
    const auto rhs = condition_value(cpu, condition.substr(op + operation.size()));
    if (!lhs.has_value() || !rhs.has_value()) {
        return false;
    }
    if (operation == "==") {
        return *lhs == *rhs;
    }
    if (operation == "!=") {
        return *lhs != *rhs;
    }
    if (operation == "<") {
        return *lhs < *rhs;
    }
    if (operation == "<=") {
        return *lhs <= *rhs;
    }
    if (operation == ">") {
        return *lhs > *rhs;
    }
    return *lhs >= *rhs;
}
struct Breakpoint {
    int id = 0;
    std::uint16_t address = 0;
    int line = 0;
    std::string condition;
    std::string hit_condition;
    mutable int hits = 0;
};

enum class RunKind {
    Breakpoint,
    Step,
    Exited,
    Limit,
};

struct RunResult {
    RunKind kind = RunKind::Step;
    Breakpoint breakpoint;
};

}  // namespace

struct DapSession::Impl {
    explicit Impl(DapOptions opts) : options(std::move(opts)) {
        stop_on_entry = options.stop_on_entry;
    }

    DapOptions options;
    Machine machine;
    Cpu cpu;
    HostServices host{true};
    Container container;
    bool has_container = false;
    std::vector<DebugLineRow> line_rows;
    std::string source_path;
    std::uint16_t entry = 0;
    bool launched = false;
    bool configured = false;
    bool started = false;
    bool terminated = false;
    bool stop_requested = false;
    bool paused = false;
    bool stop_on_entry = true;
    bool initialized_event_sent = false;
    std::uint16_t last_stop_address = 0;
    std::string last_stop_reason;
    std::int64_t next_seq = 1;
    int next_breakpoint_id = 1;
    std::map<std::uint16_t, Breakpoint> breakpoints;

    std::vector<std::string> handle_message(std::string_view message);

    JsonValue make_response(const JsonValue& request, std::string_view command,
                            bool success, JsonValue body,
                            const std::string& error_message = {}) {
        JsonValue response = JsonValue::object();
        response.set("seq", JsonValue::integer(next_seq++));
        response.set("type", JsonValue::string("response"));
        std::int64_t request_seq = 0;
        if (const JsonValue* seq = request.find("seq");
            seq != nullptr && seq->is_integer()) {
            request_seq = seq->as_integer();
        }
        response.set("request_seq", JsonValue::integer(request_seq));
        response.set("success", JsonValue::boolean(success));
        response.set("command", JsonValue::string(std::string(command)));
        if (!body.is_null()) {
            response.set("body", std::move(body));
        }
        if (!error_message.empty()) {
            response.set("message", JsonValue::string(error_message));
        }
        return response;
    }

    JsonValue make_event(std::string_view name, JsonValue body) {
        JsonValue event = JsonValue::object();
        event.set("seq", JsonValue::integer(next_seq++));
        event.set("type", JsonValue::string("event"));
        event.set("event", JsonValue::string(std::string(name)));
        if (!body.is_null()) {
            event.set("body", std::move(body));
        }
        return event;
    }

    void append_response(std::vector<std::string>& out,
                         const JsonValue& request, std::string_view command,
                         bool success, JsonValue body = JsonValue::null(),
                         const std::string& error_message = {}) {
        out.push_back(
            make_response(request, command, success, std::move(body),
                          error_message)
                .dump());
    }

    void append_event(std::vector<std::string>& out, std::string_view name,
                      JsonValue body = JsonValue::null()) {
        out.push_back(make_event(name, std::move(body)).dump());
    }

    void append_stopped(std::vector<std::string>& out, const char* reason,
                        const std::string& description = {},
                        const Breakpoint* breakpoint = nullptr) {
        JsonValue body = JsonValue::object();
        body.set("reason", JsonValue::string(reason));
        body.set("threadId", JsonValue::integer(kThreadId));
        body.set("allThreadsStopped", JsonValue::boolean(true));
        if (!description.empty()) {
            body.set("description", JsonValue::string(description));
        }
        if (breakpoint != nullptr) {
            JsonValue ids = JsonValue::array();
            ids.push(JsonValue::integer(breakpoint->id));
            body.set("hitBreakpointIds", std::move(ids));
        }
        JsonValue disassembly_state = body;
        disassembly_state.set("pc", JsonValue::string(format_hex(cpu.pc())));
        append_event(out, "stopped", std::move(body));
        append_event(out, "kr16.stopped", std::move(disassembly_state));
    }

    void append_termination(std::vector<std::string>& out) {
        terminated = true;
        paused = false;
        JsonValue exited = JsonValue::object();
        const auto code = static_cast<std::int64_t>(cpu.reg(0) & 0xFFu);
        exited.set("exitCode", JsonValue::integer(code));
        append_event(out, "exited", std::move(exited));
        append_event(out, "terminated");
    }

    bool at_breakpoint(std::uint16_t address, Breakpoint& result) const {
        const auto iteration = breakpoints.find(address);
        if (iteration == breakpoints.end()) {
            return false;
        }
        if (!breakpoint_condition(cpu, iteration->second.condition)) {
            return false;
        }
        if (!iteration->second.hit_condition.empty()) {
            int target = 0;
            const auto& text = iteration->second.hit_condition;
            const auto parsed = std::from_chars(text.data(),
                                             text.data() + text.size(),
                                             target);
            if (parsed.ec == std::errc{} && target > 0) {
                ++iteration->second.hits;
                if (iteration->second.hits < target) {
                    return false;
                }
            }
        }
        result = iteration->second;
        return true;
    }

    int add_breakpoint(std::uint16_t address, int line,
                       std::string condition = {},
                       std::string hit_condition = {}) {
        const auto existing = breakpoints.find(address);
        if (existing != breakpoints.end()) {
            existing->second.line = line;
            existing->second.condition = std::move(condition);
            existing->second.hit_condition = std::move(hit_condition);
            existing->second.hits = 0;
            return existing->second.id;
        }
        Breakpoint breakpoint;
        breakpoint.id = next_breakpoint_id++;
        breakpoint.address = address;
        breakpoint.line = line;
        breakpoint.condition = std::move(condition);
        breakpoint.hit_condition = std::move(hit_condition);
        breakpoints.emplace(address, breakpoint);
        return breakpoint.id;
    }

    void reset_machine() {
        machine = Machine{};
        machine.zero_ram();
        cpu = Cpu{};
        cpu.reset();
        cpu.set_syscall_handler(
            [this](std::uint8_t service, std::uint16_t block,
                   MemoryBus& bus) { host.call(service, block, bus); });
        container = Container{};
        has_container = false;
        line_rows.clear();
        source_path.clear();
        entry = 0;
    }

    void load_program() {
        if (options.program.empty()) {
            throw std::runtime_error("no program specified");
        }
        reset_machine();
        const auto bytes = read_file(options.program);
        std::uint16_t stack_limit = 0xE000;
        std::uint16_t start = options.base;
        std::uint16_t stack_pointer = 0xF000;

        if (options.raw) {
            machine.load_bytes(options.base, bytes);
        } else {
            container = Container::read(bytes);
            has_container = true;
            for (const auto& section : container.sections) {
                if ((section.flags & SectionAlloc) == 0 ||
                    section.type == SectionType::Nobits) {
                    continue;
                }
                const auto end = static_cast<std::uint32_t>(section.address) +
                                 static_cast<std::uint32_t>(
                                     section.data.size());
                if (end > 0x10000u) {
                    throw std::runtime_error(
                        "section exceeds the 16-bit address space");
                }
                machine.load_bytes(section.address, section.data);
            }
            start = container.entry;
            line_rows = load_line_rows(container);
            source_path = load_source_path(container);
            source_path = resolve_source_path(source_path, options.program);
            stack_limit = symbol_value(container, "__stack_limit", 0xE000);
        }

        if (!options.raw || !options.program_args.empty()) {
            ProcessStackLayout layout;
            std::string stack_error;
            const std::string program_name =
                std::filesystem::path(options.program).filename().string();
            if (!initialize_process_stack(
                    machine, program_name, options.program_args, 0xF000,
                    stack_limit, layout, stack_error)) {
                throw std::runtime_error(stack_error);
            }
            cpu.set_reg(1, layout.argc);
            cpu.set_reg(2, layout.argv);
            stack_pointer = layout.sp;
        }

        if (!options.no_bios && !options.bios_path.empty()) {
            const auto bios = read_file(options.bios_path);
            if (bios.size() > Machine::kRomSize) {
                throw std::runtime_error(
                    "BIOS image is larger than 3.75 KiB");
            }
            machine.load_rom(bios);
            const auto reset = machine.read16(0xFFFE);
            if (reset == 0) {
                throw std::runtime_error("BIOS reset vector is zero");
            }
            start = reset;
        } else if (start < 0x0100) {
            throw std::runtime_error(
                "program entry is below the first page (0x0100)");
        }

        entry = start;
        cpu.set_pc(start);
        cpu.set_sp(stack_pointer);
        cpu.set_stack_limit(stack_limit);
        launched = true;
        started = false;
        terminated = false;
        paused = true;
        last_stop_address = start;
        last_stop_reason.clear();
    }

    void start_ready(std::vector<std::string>& out) {
        if (!launched || !configured || started) {
            return;
        }
        started = true;
        paused = true;
        last_stop_address = cpu.pc();
        Breakpoint breakpoint;
        if (at_breakpoint(cpu.pc(), breakpoint)) {
            last_stop_reason = "breakpoint";
            append_stopped(out, "breakpoint", {}, &breakpoint);
        } else if (stop_on_entry) {
            last_stop_reason = "entry";
            append_stopped(out, "entry", {});
        } else {
            last_stop_reason.clear();
        }
    }

    RunResult run_until(std::optional<std::uint16_t> target,
                        bool check_breakpoints) {
        RunResult result;
        const auto limit =
            options.max_run_instructions == 0
                ? std::uint64_t{1}
                : options.max_run_instructions;
        std::uint64_t steps = 0;
        bool first = true;
        while (steps < limit) {
            if (check_breakpoints && !first) {
                Breakpoint breakpoint;
                if (at_breakpoint(cpu.pc(), breakpoint)) {
                    result.kind = RunKind::Breakpoint;
                    result.breakpoint = breakpoint;
                    return result;
                }
            }
            first = false;
            const auto used = cpu.step(machine);
            if (used == 0) {
                result.kind = RunKind::Exited;
                return result;
            }
            machine.tick(used);
            ++steps;
            if (cpu.halted()) {
                result.kind = RunKind::Exited;
                return result;
            }
            if (target.has_value() && cpu.pc() == *target) {
                result.kind = RunKind::Step;
                return result;
            }
        }
        result.kind = RunKind::Limit;
        return result;
    }

    void flush_console() {
        const auto& output = machine.console_output();
        if (!output.empty()) {
            std::cout.write(
                reinterpret_cast<const char*>(output.data()),
                static_cast<std::streamsize>(output.size()));
            std::cout.flush();
            machine.clear_console_output();
        }
    }

    void report_run(std::vector<std::string>& out, const RunResult& result,
                    const char* step_reason) {
        flush_console();
        switch (result.kind) {
            case RunKind::Breakpoint:
                paused = true;
                last_stop_address = cpu.pc();
                last_stop_reason = "breakpoint";
                append_stopped(out, "breakpoint", {}, &result.breakpoint);
                return;
            case RunKind::Step:
                paused = true;
                last_stop_address = cpu.pc();
                last_stop_reason = step_reason;
                append_stopped(out, step_reason, {});
                return;
            case RunKind::Exited:
                append_termination(out);
                return;
            case RunKind::Limit:
                paused = true;
                last_stop_address = cpu.pc();
                last_stop_reason = "pause";
                append_stopped(out, "pause",
                               "execution instruction limit reached", nullptr);
                return;
        }
    }

    void run_continue(std::vector<std::string>& out) {
        paused = false;
        const auto result = run_until(std::nullopt, true);
        report_run(out, result, "step");
    }

    void run_single_instruction(std::vector<std::string>& out) {
        paused = false;
        if (cpu.halted()) {
            append_termination(out);
            return;
        }
        const auto used = cpu.step(machine);
        if (used == 0) {
            flush_console();
            append_termination(out);
            return;
        }
        machine.tick(used);
        if (cpu.halted()) {
            flush_console();
            append_termination(out);
            return;
        }
        flush_console();
        paused = true;
        last_stop_address = cpu.pc();
        last_stop_reason = "step";
        append_stopped(out, "step", {});
    }

    Instruction decode_current() {
        const auto pc = cpu.pc();
        const auto first = machine.read16(pc);
        const bool is_long = Instruction::first_word_is_32_bit(first);
        const auto second =
            is_long ? machine.read16(static_cast<std::uint16_t>(pc + 2))
                    : std::uint16_t{0};
        return Instruction::decode(first, second);
    }

    void run_next(std::vector<std::string>& out) {
        const auto instruction = decode_current();
        if (instruction.op == Op::CallRel || instruction.op == Op::CallReg) {
            const auto target = static_cast<std::uint16_t>(
                cpu.pc() + instruction.size_bytes);
            paused = false;
            report_run(out, run_until(target, true), "step");
            return;
        }
        run_single_instruction(out);
    }

    void run_out(std::vector<std::string>& out) {
        std::optional<std::uint16_t> target;
        const auto sp = cpu.sp();
        if (sp < 0xF000 && sp <= 0xFFFE) {
            const auto candidate = machine.read16(sp);
            if (candidate >= 0x0100 && candidate != cpu.pc()) {
                target = candidate;
            }
        }
        if (!target.has_value()) {
            run_single_instruction(out);
            return;
        }
        paused = false;
        report_run(out, run_until(target, true), "step");
    }

    std::vector<std::string> handle_initialize(const JsonValue& request);
    std::vector<std::string> handle_launch(const JsonValue& request,
                                           std::string_view command);
    std::vector<std::string> handle_set_breakpoints(const JsonValue& request,
                                                    const JsonValue* arguments);
    std::vector<std::string> handle_set_function_breakpoints(
        const JsonValue& request, const JsonValue* arguments);
    std::vector<std::string> handle_configuration_done(
        const JsonValue& request);
    std::vector<std::string> handle_threads(const JsonValue& request);
    std::vector<std::string> handle_stack_trace(const JsonValue& request);
    std::vector<std::string> handle_loaded_sources(const JsonValue& request);
    std::vector<std::string> handle_source(const JsonValue& request,
                                             const JsonValue* arguments);
    std::vector<std::string> handle_scopes(const JsonValue& request);
    std::vector<std::string> handle_variables(const JsonValue& request,
                                              const JsonValue* arguments);
    std::vector<std::string> handle_set_variable(const JsonValue& request,
                                                const JsonValue* arguments);
    std::vector<std::string> handle_read_memory(const JsonValue& request,
                                                 const JsonValue* arguments);
    std::vector<std::string> handle_disassemble(const JsonValue& request,
                                                 const JsonValue* arguments);
    std::vector<std::string> handle_evaluate(const JsonValue& request,
                                              const JsonValue* arguments);
    std::vector<std::string> handle_continue(const JsonValue& request);
    std::vector<std::string> handle_restart(const JsonValue& request);
    std::vector<std::string> handle_pause(const JsonValue& request);
    std::vector<std::string> handle_step(const JsonValue& request,
                                         std::string_view command);
    std::vector<std::string> handle_terminate(const JsonValue& request);
    std::vector<std::string> handle_disconnect(const JsonValue& request);
};

std::vector<std::string> DapSession::Impl::handle_initialize(
    const JsonValue& request) {
    std::vector<std::string> out;
    JsonValue body = JsonValue::object();
    body.set("supportsConfigurationDoneRequest", JsonValue::boolean(true));
    body.set("supportsTerminateRequest", JsonValue::boolean(true));
    body.set("supportsReadMemoryRequest", JsonValue::boolean(true));
    body.set("supportsDisassembleRequest", JsonValue::boolean(true));
    body.set("supportsEvaluateForHovers", JsonValue::boolean(true));
    body.set("supportsSingleThreadExecutionRequests",
             JsonValue::boolean(true));
    body.set("supportsConditionalBreakpoints", JsonValue::boolean(true));
    body.set("supportsHitConditionalBreakpoints", JsonValue::boolean(true));
    body.set("supportsFunctionBreakpoints", JsonValue::boolean(true));
    body.set("supportsStepBack", JsonValue::boolean(false));
    body.set("supportsRestartRequest", JsonValue::boolean(true));
    body.set("supportsSetVariable", JsonValue::boolean(true));
    body.set("supportsLoadedSourcesRequest", JsonValue::boolean(true));
    body.set("supportsDelayedStackTraceLoading", JsonValue::boolean(false));
    body.set("exceptionBreakpointFilters", JsonValue::array());
    append_response(out, request, "initialize", true, std::move(body));
    if (!initialized_event_sent) {
        initialized_event_sent = true;
        append_event(out, "initialized");
    }
    return out;
}

std::vector<std::string> DapSession::Impl::handle_launch(
    const JsonValue& request, std::string_view command) {
    std::vector<std::string> out;
    const JsonValue* arguments = request.find("arguments");
    if (arguments != nullptr && !arguments->is_object()) {
        append_response(out, request, command, false, JsonValue::null(),
                        "launch arguments must be an object");
        return out;
    }
    const auto program = string_or(arguments, "program", {});
    if (!program.empty()) {
        options.program = program;
    }
    if (const JsonValue* args = arguments != nullptr
                                    ? arguments->find("args")
                                    : nullptr;
        args != nullptr) {
        if (!args->is_array()) {
            append_response(out, request, command, false, JsonValue::null(),
                            "launch args must be an array");
            return out;
        }
        options.program_args.clear();
        for (const auto& item : args->as_array()) {
            if (!item.is_string()) {
                append_response(out, request, command, false,
                                JsonValue::null(),
                                "launch args must contain strings");
                return out;
            }
            options.program_args.push_back(item.as_string());
        }
    }
    if (const JsonValue* raw = arguments != nullptr
                                   ? arguments->find("raw")
                                   : nullptr;
        raw != nullptr && raw->is_boolean()) {
        options.raw = raw->as_boolean();
    }
    if (const auto base = parse_address(arguments != nullptr
                                            ? arguments->find("base")
                                            : nullptr);
        base.has_value()) {
        options.base = *base;
    }
    if (const JsonValue* no_bios = arguments != nullptr
                                       ? arguments->find("noBios")
                                       : nullptr;
        no_bios != nullptr && no_bios->is_boolean()) {
        options.no_bios = no_bios->as_boolean();
    }
    if (const JsonValue* stop = arguments != nullptr
                                    ? arguments->find("stopOnEntry")
                                    : nullptr;
        stop != nullptr && stop->is_boolean()) {
        stop_on_entry = stop->as_boolean();
    }
    try {
        load_program();
    } catch (const std::exception& error) {
        append_response(out, request, command, false, JsonValue::null(),
                        error.what());
        return out;
    }
    append_response(out, request, command, true, JsonValue::object());
    start_ready(out);
    return out;
}

std::vector<std::string> DapSession::Impl::handle_set_function_breakpoints(
    const JsonValue& request, const JsonValue* arguments) {
    std::vector<std::string> out;
    JsonValue reported = JsonValue::array();
    const JsonValue* items = arguments != nullptr
                                 ? arguments->find("breakpoints")
                                 : nullptr;
    if (!launched) {
        append_response(out, request, "setFunctionBreakpoints", false,
                        JsonValue::null(), "program is not loaded");
        return out;
    }
    if (items != nullptr && items->is_array()) {
        for (const auto& item : items->as_array()) {
            JsonValue function_entry = JsonValue::object();
            const auto name = string_or(&item, "name", {});
            const Symbol* found = nullptr;
            for (const auto& symbol : container.symbols) {
                if (symbol.name == name) {
                    found = &symbol;
                    break;
                }
            }
            if (found == nullptr) {
                function_entry.set("verified", JsonValue::boolean(false));
                function_entry.set("message", JsonValue::string("function not found"));
            } else {
                auto address = static_cast<std::uint16_t>(found->value);
                // Prefer the first source line after the prologue, when
                // compiler line information is available.
                for (const auto& row : line_rows) {
                    if (row.address >= found->value &&
                        row.address < static_cast<std::uint32_t>(found->value) +
                                          0x400u) {
                        address = row.address;
                        break;
                    }
                }
                const auto id =
                    add_breakpoint(address, line_for_address(line_rows, address));
                function_entry.set("id", JsonValue::integer(id));
                function_entry.set("verified", JsonValue::boolean(true));
                function_entry.set("line",
                          JsonValue::integer(
                              line_for_address(line_rows, address)));
            }
            reported.push(std::move(function_entry));
        }
    }
    JsonValue body = JsonValue::object();
    body.set("breakpoints", std::move(reported));
    append_response(out, request, "setFunctionBreakpoints", true,
                    std::move(body));
    return out;
}

std::vector<std::string> DapSession::Impl::handle_set_breakpoints(
    const JsonValue& request, const JsonValue* arguments) {
    std::vector<std::string> out;
    if (arguments == nullptr || !arguments->is_object()) {
        append_response(out, request, "setBreakpoints", false,
                        JsonValue::null(), "missing setBreakpoints arguments");
        return out;
    }
    breakpoints.clear();
    JsonValue reported = JsonValue::array();

    auto report_line_breakpoint = [&](int line, std::string condition,
                                      std::string hit_condition) {
        JsonValue breakpoint_json = JsonValue::object();
        const auto address = address_for_line(line_rows, line);
        if (address.has_value()) {
            const auto id = add_breakpoint(*address, line,
                                          std::move(condition),
                                          std::move(hit_condition));
            breakpoint_json.set("id", JsonValue::integer(id));
            breakpoint_json.set("verified", JsonValue::boolean(true));
            breakpoint_json.set("line", JsonValue::integer(line));
        } else {
            breakpoint_json.set("verified", JsonValue::boolean(false));
            breakpoint_json.set("line", JsonValue::integer(line));
            breakpoint_json.set("message",
                      JsonValue::string(
                          "no debug line row maps to this line"));
        }
        reported.push(std::move(breakpoint_json));
    };

    const JsonValue* items = arguments->find("breakpoints");
    if (items != nullptr && items->is_array()) {
        for (const auto& item : items->as_array()) {
            if (!item.is_object()) {
                JsonValue breakpoint_json = JsonValue::object();
                breakpoint_json.set("verified", JsonValue::boolean(false));
                breakpoint_json.set("message",
                          JsonValue::string("breakpoint must be an object"));
                reported.push(std::move(breakpoint_json));
                continue;
            }
            auto line = integer_or(&item, "line", 0);
            int line_number = 0;
            if (line > 0 && line <= 0xFFFF) {
                line_number = static_cast<int>(line);
            }
            const std::string condition = string_or(&item, "condition", {});
            const std::string hit_condition =
                string_or(&item, "hitCondition", {});
            const auto address = parse_address(item.find("address"));
            if (address.has_value()) {
                const auto id = add_breakpoint(
                    *address,
                    line_number > 0
                        ? line_number
                        : line_for_address(line_rows, *address),
                    condition, hit_condition);
                JsonValue breakpoint_json = JsonValue::object();
                breakpoint_json.set("id", JsonValue::integer(id));
                breakpoint_json.set("verified", JsonValue::boolean(true));
                breakpoint_json.set("line",
                          JsonValue::integer(line_number > 0
                                                 ? line_number
                                                 : line_for_address(
                                                       line_rows, *address)));
                reported.push(std::move(breakpoint_json));
            } else if (line_number > 0) {
                report_line_breakpoint(line_number, condition, hit_condition);
            } else {
                JsonValue breakpoint_json = JsonValue::object();
                breakpoint_json.set("verified", JsonValue::boolean(false));
                breakpoint_json.set("message",
                          JsonValue::string(
                              "breakpoint needs a line or address"));
                reported.push(std::move(breakpoint_json));
            }
        }
    } else {
        const JsonValue* lines = arguments->find("lines");
        if (lines != nullptr && lines->is_array()) {
            for (const auto& line : lines->as_array()) {
                if (!line.is_integer()) {
                    continue;
                }
                const auto value = line.as_integer();
                if (value > 0 && value <= 0xFFFF) {
                    report_line_breakpoint(static_cast<int>(value), {}, {});
                }
            }
        }
    }

    JsonValue body = JsonValue::object();
    body.set("breakpoints", std::move(reported));
    append_response(out, request, "setBreakpoints", true, std::move(body));
    return out;
}

std::vector<std::string> DapSession::Impl::handle_configuration_done(
    const JsonValue& request) {
    std::vector<std::string> out;
    configured = true;
    append_response(out, request, "configurationDone", true,
                    JsonValue::object());
    start_ready(out);
    return out;
}

std::vector<std::string> DapSession::Impl::handle_threads(
    const JsonValue& request) {
    std::vector<std::string> out;
    JsonValue thread = JsonValue::object();
    thread.set("id", JsonValue::integer(kThreadId));
    thread.set("name", JsonValue::string("main"));
    JsonValue threads = JsonValue::array();
    threads.push(std::move(thread));
    JsonValue body = JsonValue::object();
    body.set("threads", std::move(threads));
    append_response(out, request, "threads", true, std::move(body));
    return out;
}

std::vector<std::string> DapSession::Impl::handle_stack_trace(
    const JsonValue& request) {
    std::vector<std::string> out;
    if (!launched) {
        append_response(out, request, "stackTrace", false, JsonValue::null(),
                        "program is not loaded");
        return out;
    }
    JsonValue frames = JsonValue::array();
    auto add_frame = [&](std::uint16_t frame_pc, std::int64_t id) {
        auto line = line_for_address(line_rows, frame_pc);
        if (line <= 0 && !source_path.empty()) {
            line = 1;
        }
        JsonValue frame = JsonValue::object();
        frame.set("id", JsonValue::integer(id));
        auto name = function_name(container, frame_pc);
        if (name.empty()) {
            name = "<program>";
        }
        frame.set("name", JsonValue::string(std::move(name)));
        frame.set("line", JsonValue::integer(line));
        frame.set("column", JsonValue::integer(0));
        frame.set("instructionPointerReference",
                  JsonValue::string(format_hex(frame_pc)));
        if (!source_path.empty()) {
            JsonValue source = JsonValue::object();
            source.set("name", JsonValue::string(base_name(source_path)));
            source.set("path", JsonValue::string(source_path));
            frame.set("source", std::move(source));
        }
        frames.push(std::move(frame));
    };

    std::uint16_t frame_pc = cpu.pc();
    std::uint16_t frame_pointer = cpu.reg(4);
    std::int64_t frame_id = 1;
    add_frame(frame_pc, frame_id);
    for (int depth = 0; depth < 15; ++depth) {
        if (frame_pointer < 0x0100 || frame_pointer >= 0xEFFE) {
            break;
        }
        const auto saved_pointer = machine.read16(frame_pointer);
        const auto return_address =
            machine.read16(static_cast<std::uint16_t>(frame_pointer + 2));
        if (saved_pointer == 0 || return_address < 0x0100) {
            break;
        }
        frame_pc = return_address;
        frame_pointer = saved_pointer;
        add_frame(frame_pc, ++frame_id);
    }
    JsonValue body = JsonValue::object();
    body.set("stackFrames", std::move(frames));
    body.set("totalFrames", JsonValue::integer(frame_id));
    append_response(out, request, "stackTrace", true, std::move(body));
    return out;
}

std::vector<std::string> DapSession::Impl::handle_scopes(
    const JsonValue& request) {
    std::vector<std::string> out;
    JsonValue scope = JsonValue::object();
    scope.set("name", JsonValue::string("Registers"));
    scope.set("variablesReference", JsonValue::integer(kRegistersReference));
    scope.set("expensive", JsonValue::boolean(false));
    JsonValue scopes = JsonValue::array();
    scopes.push(std::move(scope));
    if (has_container) {
        JsonValue globals = JsonValue::object();
        globals.set("name", JsonValue::string("Globals"));
        globals.set("variablesReference", JsonValue::integer(kGlobalsReference));
        globals.set("expensive", JsonValue::boolean(true));
        scopes.push(std::move(globals));
        if (!debug_function_at(container, cpu.pc()).empty()) {
            JsonValue locals = JsonValue::object();
            locals.set("name", JsonValue::string("Locals"));
            locals.set("variablesReference", JsonValue::integer(kLocalsReference));
            locals.set("expensive", JsonValue::boolean(false));
            scopes.push(std::move(locals));
        }
    }
    JsonValue body = JsonValue::object();
    body.set("scopes", std::move(scopes));
    append_response(out, request, "scopes", true, std::move(body));
    return out;
}

std::vector<std::string> DapSession::Impl::handle_loaded_sources(
    const JsonValue& request) {
    std::vector<std::string> out;
    JsonValue sources = JsonValue::array();
    if (!source_path.empty()) {
        JsonValue source = JsonValue::object();
        source.set("name", JsonValue::string(base_name(source_path)));
        source.set("path", JsonValue::string(source_path));
        sources.push(std::move(source));
    }
    JsonValue body = JsonValue::object();
    body.set("sources", std::move(sources));
    append_response(out, request, "loadedSources", true, std::move(body));
    return out;
}

std::vector<std::string> DapSession::Impl::handle_source(
    const JsonValue& request, const JsonValue* arguments) {
    std::vector<std::string> out;
    std::string path = source_path;
    if (const JsonValue* source = arguments != nullptr
                                      ? arguments->find("source")
                                      : nullptr;
        source != nullptr) {
        if (const JsonValue* requested = source->find("path");
            requested != nullptr && requested->is_string()) {
            path = requested->as_string();
        }
    }
    if (path.empty() || path != source_path) {
        append_response(out, request, "source", false, JsonValue::null(),
                        "unknown source");
        return out;
    }
    try {
        const auto bytes = read_file(path);
        JsonValue body = JsonValue::object();
        body.set("content", JsonValue::string(
                                std::string(bytes.begin(), bytes.end())));
        body.set("mimeType", JsonValue::string("text/plain"));
        append_response(out, request, "source", true, std::move(body));
    } catch (const std::exception& error) {
        append_response(out, request, "source", false, JsonValue::null(),
                        error.what());
    }
    return out;
}
std::vector<std::string> DapSession::Impl::handle_variables(
    const JsonValue& request, const JsonValue* arguments) {
    std::vector<std::string> out;
    const auto reference =
        integer_or(arguments, "variablesReference", 0);
    JsonValue variables = JsonValue::array();

    auto add_register = [&](const char* name, std::uint16_t value) {
        JsonValue variable = JsonValue::object();
        variable.set("name", JsonValue::string(name));
        variable.set("value", JsonValue::string(format_hex(value)));
        variable.set("type", JsonValue::string("uint16"));
        variable.set("variablesReference", JsonValue::integer(0));
        variables.push(std::move(variable));
    };

    if (reference == kRegistersReference) {
        constexpr const char* kNames[] = {"R0", "R1", "R2", "R3", "R4"};
        for (std::uint8_t index = 0; index < 5; ++index) {
            add_register(kNames[index], cpu.reg(index));
        }
        add_register("SP", cpu.sp());
        add_register("SR", cpu.sr());
        add_register("PC", cpu.pc());
    } else if (reference == kGlobalsReference && has_container) {
        auto in_data = [&](std::uint32_t address) {
            for (const auto& section : container.sections) {
                if (section.name != ".data" && section.name != ".bss") {
                    continue;
                }
                const auto begin = section.address;
                const auto end =
                    begin + static_cast<std::uint32_t>(section.byte_size());
                if (address >= begin && address < end) {
                    return true;
                }
            }
            return false;
        };
        for (const auto& symbol : container.symbols) {
            const auto address = static_cast<std::uint32_t>(symbol.value);
            if (address > 0xFFFF || !in_data(address)) {
                continue;
            }
            JsonValue variable = JsonValue::object();
            variable.set("name", JsonValue::string(symbol.name));
            variable.set(
                "value",
                JsonValue::string(format_hex(machine.read16(
                    static_cast<std::uint16_t>(address)))));
            variable.set("type", JsonValue::string("uint16"));
            variable.set("variablesReference", JsonValue::integer(0));
            variables.push(std::move(variable));
        }
    } else if (reference == kLocalsReference && has_container) {
        const std::string function =
            debug_function_at(container, cpu.pc());
        const auto frame = cpu.reg(4);
        for (const auto& local : parse_debug_vars(container)) {
            if (local.function != function) {
                continue;
            }
            const auto address = static_cast<std::uint16_t>(
                frame - static_cast<std::uint16_t>(local.offset));
            JsonValue variable = JsonValue::object();
            variable.set("name", JsonValue::string(local.name));
            variable.set(
                "value",
                JsonValue::string(format_hex(
                    local.size == 1 ? machine.read8(address)
                                    : machine.read16(address))));
            variable.set("type",
                         JsonValue::string(local.size == 1 ? "uint8"
                                                           : "uint16"));
            variable.set("variablesReference", JsonValue::integer(0));
            variables.push(std::move(variable));
        }
    }

    JsonValue body = JsonValue::object();
    body.set("variables", std::move(variables));
    append_response(out, request, "variables", true, std::move(body));
    return out;
}

std::vector<std::string> DapSession::Impl::handle_set_variable(
    const JsonValue& request, const JsonValue* arguments) {
    std::vector<std::string> out;
    if (!launched) {
        append_response(out, request, "setVariable", false, JsonValue::null(),
                        "program is not loaded");
        return out;
    }
    const auto reference = integer_or(arguments, "variablesReference", 0);
    const std::string name = string_or(arguments, "name", {});
    const auto value = parse_address(arguments != nullptr
                                         ? arguments->find("value")
                                         : nullptr);
    if (name.empty() || !value.has_value()) {
        append_response(out, request, "setVariable", false, JsonValue::null(),
                        "invalid variable name or value");
        return out;
    }
    bool found = false;
    if (reference == kRegistersReference) {
        if (name == "R0" || name == "r0") {
            cpu.set_reg(0, *value);
        } else if (name == "R1" || name == "r1") {
            cpu.set_reg(1, *value);
        } else if (name == "R2" || name == "r2") {
            cpu.set_reg(2, *value);
        } else if (name == "R3" || name == "r3") {
            cpu.set_reg(3, *value);
        } else if (name == "R4" || name == "r4") {
            cpu.set_reg(4, *value);
        } else if (name == "SP" || name == "sp") {
            cpu.set_sp(*value);
        } else if (name == "SR" || name == "sr") {
            cpu.set_sr(*value);
        } else if (name == "PC" || name == "pc") {
            cpu.set_pc(*value);
        } else {
            append_response(out, request, "setVariable", false,
                            JsonValue::null(), "unknown register");
            return out;
        }
        found = true;
    } else if (reference == kGlobalsReference && has_container) {
        for (const auto& symbol : container.symbols) {
            if (symbol.name != name || symbol.section == 0 ||
                symbol.section > container.sections.size()) {
                continue;
            }
            const auto& section = container.sections[symbol.section - 1];
            if (section.name != ".data" && section.name != ".bss") {
                continue;
            }
            machine.write8(symbol.value,
                           static_cast<std::uint8_t>(*value & 0xFF));
            machine.write8(static_cast<std::uint16_t>(symbol.value + 1),
                           static_cast<std::uint8_t>(*value >> 8));
            found = true;
            break;
        }
    } else if (reference == kLocalsReference && has_container) {
        const std::string function =
            debug_function_at(container, cpu.pc());
        for (const auto& local : parse_debug_vars(container)) {
            if (local.function != function || local.name != name) {
                continue;
            }
            const auto address = static_cast<std::uint16_t>(
                cpu.reg(4) - local.offset);
            machine.write8(address,
                           static_cast<std::uint8_t>(*value & 0xFF));
            if (local.size > 1) {
                machine.write8(static_cast<std::uint16_t>(address + 1),
                               static_cast<std::uint8_t>(*value >> 8));
            }
            found = true;
            break;
        }
    }
    if (!found) {
        append_response(out, request, "setVariable", false, JsonValue::null(),
                        "variable not found");
        return out;
    }
    JsonValue body = JsonValue::object();
    body.set("value", JsonValue::string(format_hex(*value)));
    body.set("type", JsonValue::string("uint16"));
    body.set("variablesReference", JsonValue::integer(0));
    append_response(out, request, "setVariable", true, std::move(body));
    return out;
}
std::vector<std::string> DapSession::Impl::handle_read_memory(
    const JsonValue& request, const JsonValue* arguments) {
    std::vector<std::string> out;
    if (!launched) {
        append_response(out, request, "readMemory", false, JsonValue::null(),
                        "program is not loaded");
        return out;
    }
    const auto base = arguments != nullptr
                          ? parse_address(arguments->find("memoryReference"))
                          : std::nullopt;
    if (!base.has_value()) {
        append_response(out, request, "readMemory", false, JsonValue::null(),
                        "invalid memory reference");
        return out;
    }
    const auto offset = integer_or(arguments, "offset", 0);
    auto count = integer_or(arguments, "count", 16);
    if (offset < 0 || count < 0 || count > 256) {
        append_response(out, request, "readMemory", false, JsonValue::null(),
                        "invalid read range");
        return out;
    }
    const auto address = static_cast<std::uint32_t>(*base) +
                         static_cast<std::uint32_t>(offset);
    if (address + static_cast<std::uint32_t>(count) > 0x10000) {
        append_response(out, request, "readMemory", false, JsonValue::null(),
                        "read range exceeds the address space");
        return out;
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(static_cast<std::size_t>(count));
    for (std::int64_t i = 0; i < count; ++i) {
        bytes.push_back(machine.read8(
            static_cast<std::uint16_t>(address + static_cast<std::uint32_t>(i))));
    }
    JsonValue body = JsonValue::object();
    body.set("address", JsonValue::string(format_hex(
                            static_cast<std::uint16_t>(address))));
    body.set("data", JsonValue::string(base64_encode(bytes)));
    append_response(out, request, "readMemory", true, std::move(body));
    return out;
}

std::vector<std::string> DapSession::Impl::handle_disassemble(
    const JsonValue& request, const JsonValue* arguments) {
    std::vector<std::string> out;
    if (!launched) {
        append_response(out, request, "disassemble", false, JsonValue::null(),
                        "program is not loaded");
        return out;
    }
    const auto base = arguments != nullptr
                          ? parse_address(arguments->find("memoryReference"))
                          : std::nullopt;
    if (!base.has_value()) {
        append_response(out, request, "disassemble", false, JsonValue::null(),
                        "invalid memory reference");
        return out;
    }
    auto count = integer_or(arguments, "instructionCount", 10);
    if (count < 0) {
        append_response(out, request, "disassemble", false, JsonValue::null(),
                        "invalid instruction count");
        return out;
    }
    if (count > 512) {
        count = 512;
    }
    JsonValue instructions = JsonValue::array();
    std::uint32_t address = *base;
    for (std::int64_t i = 0; i < count; ++i) {
        if (address + 2 > 0x10000) {
            break;
        }
        const auto pc = static_cast<std::uint16_t>(address);
        const auto first = machine.read16(pc);
        const bool is_long = Instruction::first_word_is_32_bit(first);
        const auto size = is_long ? 4u : 2u;
        if (address + size > 0x10000) {
            break;
        }
        std::vector<std::uint8_t> bytes;
        bytes.push_back(static_cast<std::uint8_t>(first & 0xFF));
        bytes.push_back(static_cast<std::uint8_t>(first >> 8));
        if (is_long) {
            const auto second = machine.read16(static_cast<std::uint16_t>(pc + 2));
            bytes.push_back(static_cast<std::uint8_t>(second & 0xFF));
            bytes.push_back(static_cast<std::uint8_t>(second >> 8));
        }
        const auto text = disassemble_raw(
            bytes, DisassemblyOptions{pc, false});
        std::string instruction = text;
        while (!instruction.empty() &&
               (instruction.back() == '\n' || instruction.back() == '\r' ||
                instruction.back() == ' ')) {
            instruction.pop_back();
        }
        const auto first_space = instruction.find("  ");
        if (first_space != std::string::npos) {
            instruction = instruction.substr(first_space + 2);
        }
        JsonValue instruction_entry = JsonValue::object();
        instruction_entry.set("address", JsonValue::string(format_hex(pc)));
        instruction_entry.set("instruction", JsonValue::string(instruction));
        instruction_entry.set(
            "instructionBytes",
            JsonValue::string(base64_encode(bytes)));
        instructions.push(std::move(instruction_entry));
        address += size;
    }
    JsonValue body = JsonValue::object();
    body.set("instructions", std::move(instructions));
    append_response(out, request, "disassemble", true, std::move(body));
    return out;
}

std::vector<std::string> DapSession::Impl::handle_evaluate(
    const JsonValue& request, const JsonValue* arguments) {
    std::vector<std::string> out;
    const auto expression = string_or(arguments, "expression", {});
    std::string name = expression;
    for (auto& ch : name) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (name == "$syscall_trace" || name == "$syscalls") {
        JsonValue body = JsonValue::object();
        body.set("result", JsonValue::string(host.trace()));
        body.set("type", JsonValue::string("string"));
        body.set("variablesReference", JsonValue::integer(0));
        append_response(out, request, "evaluate", true, std::move(body));
        return out;
    }
    std::optional<std::uint16_t> value;
    if (name == "$pc") value = cpu.pc();
    else if (name == "$sp") value = cpu.sp();
    else if (name == "$sr") value = cpu.sr();
    else if (name == "$r0") value = cpu.reg(0);
    else if (name == "$r1") value = cpu.reg(1);
    else if (name == "$r2") value = cpu.reg(2);
    else if (name == "$r3") value = cpu.reg(3);
    else if (name == "$r4") value = cpu.reg(4);
    else {
        std::string_view text = expression;
        int base = 10;
        if (text.size() > 2 && text[0] == '0' &&
            (text[1] == 'x' || text[1] == 'X')) {
            base = 16;
            text.remove_prefix(2);
        }
        unsigned long long parsed = 0;
        const auto result = std::from_chars(
            text.data(), text.data() + text.size(), parsed, base);
        if (result.ec == std::errc{} &&
            result.ptr == text.data() + text.size() && parsed <= 0xFFFF) {
            value = static_cast<std::uint16_t>(parsed);
        }
    }
    if (!value.has_value()) {
        append_response(out, request, "evaluate", false, JsonValue::null(),
                        "unsupported expression");
        return out;
    }
    JsonValue body = JsonValue::object();
    body.set("result", JsonValue::string(format_hex(*value)));
    body.set("type", JsonValue::string("uint16"));
    body.set("variablesReference", JsonValue::integer(0));
    append_response(out, request, "evaluate", true, std::move(body));
    return out;
}

std::vector<std::string> DapSession::Impl::handle_restart(
    const JsonValue& request) {
    std::vector<std::string> out;
    if (!launched) {
        append_response(out, request, "restart", false, JsonValue::null(),
                        "program is not loaded");
        return out;
    }
    try {
        load_program();
    } catch (const std::exception& error) {
        append_response(out, request, "restart", false, JsonValue::null(),
                        error.what());
        return out;
    }
    append_response(out, request, "restart", true, JsonValue::object());
    start_ready(out);
    return out;
}

std::vector<std::string> DapSession::Impl::handle_continue(
    const JsonValue& request) {
    std::vector<std::string> out;
    if (!launched) {
        append_response(out, request, "continue", false, JsonValue::null(),
                        "program is not loaded");
        return out;
    }
    if (terminated || cpu.halted()) {
        append_response(out, request, "continue", false, JsonValue::null(),
                        "program has terminated");
        return out;
    }
    append_response(out, request, "continue", true, JsonValue::object());
    run_continue(out);
    return out;
}

std::vector<std::string> DapSession::Impl::handle_pause(
    const JsonValue& request) {
    std::vector<std::string> out;
    if (!launched) {
        append_response(out, request, "pause", false, JsonValue::null(),
                        "program is not loaded");
        return out;
    }
    append_response(out, request, "pause", true, JsonValue::object());
    if (!terminated && !cpu.halted()) {
        paused = true;
        last_stop_address = cpu.pc();
        last_stop_reason = "pause";
        append_stopped(out, "pause", "paused by request", nullptr);
    }
    return out;
}

std::vector<std::string> DapSession::Impl::handle_step(
    const JsonValue& request, std::string_view command) {
    std::vector<std::string> out;
    if (!launched) {
        append_response(out, request, command, false, JsonValue::null(),
                        "program is not loaded");
        return out;
    }
    if (terminated || cpu.halted()) {
        append_response(out, request, command, false, JsonValue::null(),
                        "program has terminated");
        return out;
    }
    append_response(out, request, command, true, JsonValue::object());
    if (command == "next") {
        run_next(out);
    } else if (command == "stepOut") {
        run_out(out);
    } else {
        run_single_instruction(out);
    }
    return out;
}

std::vector<std::string> DapSession::Impl::handle_terminate(
    const JsonValue& request) {
    std::vector<std::string> out;
    append_response(out, request, "terminate", true, JsonValue::object());
    if (!terminated) {
        terminated = true;
        paused = false;
        append_event(out, "terminated");
    }
    return out;
}

std::vector<std::string> DapSession::Impl::handle_disconnect(
    const JsonValue& request) {
    std::vector<std::string> out;
    append_response(out, request, "disconnect", true, JsonValue::object());
    stop_requested = true;
    terminated = true;
    paused = false;
    return out;
}

std::vector<std::string> DapSession::Impl::handle_message(
    std::string_view message) {
    JsonValue request;
    try {
        request = JsonValue::parse(message);
    } catch (const JsonError& error) {
        throw DapProtocolError(std::string("invalid JSON request: ") +
                               error.what());
    }
    if (!request.is_object()) {
        throw DapProtocolError("DAP message must be a JSON object");
    }
    const JsonValue* type = request.find("type");
    if (type == nullptr || !type->is_string() ||
        type->as_string() != "request") {
        return {};
    }
    const JsonValue* command_value = request.find("command");
    if (command_value == nullptr || !command_value->is_string()) {
        std::vector<std::string> out;
        append_response(out, request, "<invalid>", false, JsonValue::null(),
                        "request is missing a command");
        return out;
    }
    const std::string command = command_value->as_string();
    const JsonValue* arguments = request.find("arguments");
    if (arguments != nullptr && !arguments->is_object()) {
        std::vector<std::string> out;
        append_response(out, request, command, false, JsonValue::null(),
                        "arguments must be an object");
        return out;
    }

    if (command == "initialize") {
        return handle_initialize(request);
    }
    if (command == "launch" || command == "attach") {
        return handle_launch(request, command);
    }
    if (command == "setBreakpoints") {
        return handle_set_breakpoints(request, arguments);
    }
    if (command == "setFunctionBreakpoints") {
        return handle_set_function_breakpoints(request, arguments);
    }
    if (command == "setExceptionBreakpoints") {
        std::vector<std::string> out;
        JsonValue body = JsonValue::object();
        body.set("breakpoints", JsonValue::array());
        append_response(out, request, command, true, std::move(body));
        return out;
    }
    if (command == "configurationDone") {
        return handle_configuration_done(request);
    }
    if (command == "threads") {
        return handle_threads(request);
    }
    if (command == "stackTrace") {
        return handle_stack_trace(request);
    }
    if (command == "loadedSources") {
        return handle_loaded_sources(request);
    }
    if (command == "source") {
        return handle_source(request, arguments);
    }
    if (command == "scopes") {
        return handle_scopes(request);
    }
    if (command == "variables") {
        return handle_variables(request, arguments);
    }
    if (command == "setVariable") {
        return handle_set_variable(request, arguments);
    }
    if (command == "readMemory") {
        return handle_read_memory(request, arguments);
    }
    if (command == "disassemble") {
        return handle_disassemble(request, arguments);
    }
    if (command == "evaluate") {
        return handle_evaluate(request, arguments);
    }
    if (command == "continue") {
        return handle_continue(request);
    }
    if (command == "restart") {
        return handle_restart(request);
    }
    if (command == "pause") {
        return handle_pause(request);
    }
    if (command == "next" || command == "stepIn" ||
        command == "stepOut") {
        return handle_step(request, command);
    }
    if (command == "terminate") {
        return handle_terminate(request);
    }
    if (command == "disconnect") {
        return handle_disconnect(request);
    }

    std::vector<std::string> out;
    append_response(out, request, command, false, JsonValue::null(),
                    "unsupported request: " + command);
    return out;
}

DapSession::DapSession(DapOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

DapSession::~DapSession() = default;

DapSession::DapSession(DapSession&&) noexcept = default;

DapSession& DapSession::operator=(DapSession&&) noexcept = default;

std::vector<std::string> DapSession::handle_message(
    std::string_view message) {
    return impl_->handle_message(message);
}

bool DapSession::should_stop() const noexcept {
    return impl_->stop_requested;
}

bool DapSession::terminated() const noexcept { return impl_->terminated; }

bool DapSession::launched() const noexcept { return impl_->launched; }

std::int64_t DapSession::last_sequence() const noexcept {
    return impl_->next_seq;
}

}  // namespace kr16::dap




















