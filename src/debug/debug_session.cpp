#include "kr16/debug_session.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "kr16/build.hpp"
#include "kr16/debug_info.hpp"
#include "kr16/disassembler.hpp"
#include "kr16/instruction.hpp"
#include "kr16/process.hpp"
#include "debug/kdbg_model.hpp"

namespace kr16 {
namespace {

std::string strip_disassembly_prefix(std::string text) {
    while (!text.empty() &&
           (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
        text.pop_back();
    }
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.erase(0, 1);
    }
    if (text.rfind("0x", 0) == 0) {
        const std::size_t space = text.find(' ');
        if (space != std::string::npos) {
            std::size_t start = space;
            while (start < text.size() && text[start] == ' ') {
                ++start;
            }
            text = text.substr(start);
        }
    }
    return text;
}

std::string trim_copy(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::vector<std::string> split_records(std::span<const std::uint8_t> data) {
    std::vector<std::string> result;
    std::string current;
    for (const std::uint8_t byte : data) {
        if (byte == 0) {
            if (!current.empty()) {
                result.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(static_cast<char>(byte));
        }
    }
    if (!current.empty()) {
        result.push_back(std::move(current));
    }
    return result;
}

std::vector<std::string> split_pipe(const std::string& text) {
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t end = text.find('|', begin);
        result.push_back(text.substr(
            begin, end == std::string::npos ? std::string::npos : end - begin));
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return result;
}

std::vector<std::string> read_source_lines(const std::filesystem::path& path) {
    std::vector<std::string> lines;
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) {
        return lines;
    }
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::string expanded;
        for (const char ch : line) {
            if (ch == '\t') {
                expanded.append(4, ' ');
            } else {
                expanded.push_back(ch);
            }
        }
        lines.push_back(std::move(expanded));
    }
    return lines;
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path,
                                    std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) {
        error = "cannot open " + path.string();
        return {};
    }
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

}  // namespace

void DebugSession::set_program_args(std::vector<std::string> arguments) {
    program_args_ = std::move(arguments);
}

bool DebugSession::load(const std::filesystem::path& image,
                        std::string& error) {
    clear_image_state();
    image_path_ = image;

    try {
        const auto bytes = read_file(image, error);
        if (bytes.empty() && !error.empty()) {
            return false;
        }
        image_bytes_ = bytes;
        container_ = Container::read(bytes);

        machine_ = Machine{};
        machine_.zero_ram();
        cpu_ = Cpu{};
        cpu_.reset();
        host_ = HostServices{};

        for (const auto& section : container_.sections) {
            if (!section.is_alloc() || section.type == SectionType::Nobits) {
                continue;
            }
            if (static_cast<std::uint32_t>(section.address) +
                    section.data.size() >
                0x10000u) {
                error = "section exceeds the address space";
                return false;
            }
            machine_.load_bytes(section.address, section.data);
        }

        std::uint16_t entry = container_.entry;
        if (entry < 0x0100) {
            entry = 0x0100;
        }
        std::uint16_t stack_limit = 0xE000;
        for (const auto& symbol : container_.symbols) {
            if (symbol.name == "__stack_limit") {
                stack_limit = symbol.value;
                break;
            }
        }

        ProcessStackLayout stack_layout;
        std::string stack_error;
        if (!initialize_process_stack(
                machine_, image.filename().string(), program_args_, 0xF000,
                stack_limit, stack_layout, stack_error)) {
            clear_image_state();
            error = stack_error;
            return false;
        }

        cpu_.set_pc(entry);
        cpu_.set_reg(1, stack_layout.argc);
        cpu_.set_reg(2, stack_layout.argv);
        cpu_.set_sp(stack_layout.sp);
        cpu_.set_stack_limit(stack_limit);
        cpu_.set_syscall_handler(
            [this](std::uint8_t service, std::uint16_t block, MemoryBus& bus) {
                host_.call(service, block, bus);
            });
        cpu_.set_interrupt_ack(
            [this]() { return machine_.pending_interrupt(); });

        loaded_ = true;
        decode_disassembly();
        load_debug_info();
        if (!function_records_.empty() && !source_lines_.empty()) {
            seek_first_line();
            if (!cpu_.halted()) {
                instructions_executed_ = 0;
            }
        }
        return true;
    } catch (const std::exception& problem) {
        clear_image_state();
        error = problem.what();
        return false;
    }
}

bool DebugSession::build_and_load(const std::filesystem::path& input,
                                  std::string& error) {
    return build_and_load(input, {}, error);
}

bool DebugSession::build_and_load(const std::filesystem::path& input,
                                  const std::filesystem::path& libc_source,
                                  std::string& error) {
    return build_and_load(input, libc_source, SessionBuildOptions{}, error);
}

bool DebugSession::build_and_load(const std::filesystem::path& input,
                                  const std::filesystem::path& libc_source,
                                  const SessionBuildOptions& settings,
                                  std::string& error) {
    std::string extension = input.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    if (extension == ".kr16") {
        return load(input, error);
    }
    if (!is_build_input(input)) {
        error = "unsupported input type '" + extension + "'";
        return false;
    }

    BuildOptions options;
    options.input = input;
    options.debug = settings.debug;
    options.optimize = settings.optimize;
    options.link_libc = settings.link_libc;
    options.libc_source = libc_source;
    options.command = "load";
    if (settings.listing) {
        options.listing = input;
        options.listing.replace_extension(".lst");
    }
    std::ostringstream diagnostics;
    const BuildResult built = build_program(options, diagnostics);
    if (!built.ok) {
        error = diagnostics.str();
        if (error.empty()) {
            error = "build failed";
        }
        return false;
    }
    return load(built.executable, error);
}

bool DebugSession::restart(std::string& error) {
    if (image_path_.empty()) {
        error = "no image loaded";
        return false;
    }
    const auto addresses = address_breakpoints_;
    const auto lines = line_breakpoints_;
    if (!load(image_path_, error)) {
        return false;
    }
    address_breakpoints_ = addresses;
    line_breakpoints_ = lines;
    return true;
}

void DebugSession::unload() {
    clear_image_state();
    image_path_.clear();
}

void DebugSession::clear_image_state() {
    loaded_ = false;
    container_ = Container{};
    image_bytes_.clear();
    disassembly_.clear();
    text_low_ = 0;
    text_high_ = 0;
    source_lines_.clear();
    source_path_.clear();
    functions_.clear();
    function_records_.clear();
    executable_lines_.clear();
    local_definitions_.clear();
    global_types_.clear();
    type_fields_.clear();
    address_breakpoints_.clear();
    line_breakpoints_.clear();
    console_.clear();
    reason_.clear();
    running_ = false;
    run_previous_line_ = 0;
    instructions_executed_ = 0;
    accumulated_run_us_ = 0;
}

bool DebugSession::halted() const noexcept {
    return loaded_ && cpu_.halted();
}

std::string DebugSession::halt_reason() const {
    if (!cpu_.halted()) {
        return {};
    }
    return cpu_.halt_reason().empty() ? "halted" : cpu_.halt_reason();
}

std::uint16_t DebugSession::pc() const noexcept {
    return cpu_.pc();
}

std::uint16_t DebugSession::sp() const noexcept {
    return cpu_.sp();
}

std::uint16_t DebugSession::sr() const noexcept {
    return cpu_.sr();
}

std::uint16_t DebugSession::reg(int index) const {
    switch (index) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
            return cpu_.reg(static_cast<std::uint8_t>(index));
        case 5:
            return cpu_.pc();
        case 6:
            return cpu_.sp();
        case 7:
            return cpu_.sr();
        default:
            return 0;
    }
}

void DebugSession::set_reg(int index, std::uint16_t value) {
    switch (index) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
            cpu_.set_reg(static_cast<std::uint8_t>(index), value);
            break;
        case 5:
            cpu_.set_pc(value);
            break;
        case 6:
            cpu_.set_sp(value);
            break;
        case 7:
            cpu_.set_sr(value);
            break;
        default:
            break;
    }
}

std::uint8_t DebugSession::read8(std::uint16_t address) {
    return machine_.read8(address);
}

void DebugSession::write8(std::uint16_t address, std::uint8_t value) {
    machine_.write8(address, value);
}

std::uint16_t DebugSession::read16(std::uint16_t address) {
    return machine_.read16(address);
}

void DebugSession::write16(std::uint16_t address, std::uint16_t value) {
    machine_.write16(address, value);
}

void DebugSession::decode_disassembly() {
    disassembly_.clear();
    if (!loaded_) {
        return;
    }

    std::uint32_t begin = 0;
    std::uint32_t end = 0;
    if (const Section* text = container_.find_section(".text");
        text != nullptr) {
        begin = text->address;
        end = static_cast<std::uint32_t>(text->address) +
              static_cast<std::uint32_t>(text->data.size());
    }
    if (begin < 0x0100) {
        begin = 0x0100;
    }
    if (const std::uint16_t entry = cpu_.pc(); entry < begin) {
        begin = entry;
    }
    if (end <= begin) {
        end = std::min<std::uint32_t>(0x10000, begin + 120);
    }
    if (begin & 1u) {
        ++begin;
    }

    std::uint32_t address = begin;
    while (address + 2 <= end && address + 2 <= 0x10000 &&
           disassembly_.size() < 20000) {
        const auto pc = static_cast<std::uint16_t>(address);
        const std::uint16_t first = machine_.read16(pc);
        const bool is_long = Instruction::first_word_is_32_bit(first);
        const std::uint32_t size = is_long ? 4 : 2;
        if (address + size > end || address + size > 0x10000) {
            break;
        }

        std::vector<std::uint8_t> bytes;
        bytes.push_back(static_cast<std::uint8_t>(first & 0xFF));
        bytes.push_back(static_cast<std::uint8_t>(first >> 8));
        if (is_long) {
            const std::uint16_t second =
                machine_.read16(static_cast<std::uint16_t>(pc + 2));
            bytes.push_back(static_cast<std::uint8_t>(second & 0xFF));
            bytes.push_back(static_cast<std::uint8_t>(second >> 8));
        }

        SessionInstruction instruction;
        instruction.address = pc;
        instruction.size = static_cast<std::uint8_t>(size);
        instruction.text = strip_disassembly_prefix(
            disassemble_raw(bytes, DisassemblyOptions{pc, false}));
        instruction.bytes = std::move(bytes);
        disassembly_.push_back(std::move(instruction));
        address += size;
    }

    text_low_ = static_cast<std::uint16_t>(std::min<std::uint32_t>(
        begin, 0xFFFF));
    text_high_ = std::min<std::uint32_t>(end, 0x10000);
}

bool DebugSession::ensure_disassembly_at(std::uint16_t target) {
    if (!loaded_) {
        return false;
    }
    for (const auto& instruction : disassembly_) {
        if (target >= instruction.address &&
            target < instruction.address + instruction.size) {
            return false;
        }
    }

    std::vector<SessionInstruction> window;
    std::uint32_t address = target;
    const std::uint32_t end =
        std::min<std::uint32_t>(0x10000, address + 128);
    while (address + 2 <= end && address + 2 <= 0x10000) {
        const auto pc = static_cast<std::uint16_t>(address);
        const std::uint16_t first = machine_.read16(pc);
        const bool is_long = Instruction::first_word_is_32_bit(first);
        const std::uint32_t size = is_long ? 4 : 2;
        if (address + size > end || address + size > 0x10000) {
            break;
        }

        std::vector<std::uint8_t> bytes;
        bytes.push_back(static_cast<std::uint8_t>(first & 0xFF));
        bytes.push_back(static_cast<std::uint8_t>(first >> 8));
        if (is_long) {
            const std::uint16_t second =
                machine_.read16(static_cast<std::uint16_t>(pc + 2));
            bytes.push_back(static_cast<std::uint8_t>(second & 0xFF));
            bytes.push_back(static_cast<std::uint8_t>(second >> 8));
        }

        SessionInstruction instruction;
        instruction.address = pc;
        instruction.size = static_cast<std::uint8_t>(size);
        instruction.text = strip_disassembly_prefix(
            disassemble_raw(bytes, DisassemblyOptions{pc, false}));
        instruction.bytes = std::move(bytes);
        window.push_back(std::move(instruction));
        address += size;
    }
    if (window.empty()) {
        return false;
    }

    const auto position = std::lower_bound(
        disassembly_.begin(), disassembly_.end(), target,
        [](const SessionInstruction& instruction, std::uint16_t value) {
            return instruction.address < value;
        });
    disassembly_.insert(position, window.begin(), window.end());
    return true;
}

void DebugSession::load_debug_info() {
    source_lines_.clear();
    source_path_.clear();
    functions_.clear();
    function_records_.clear();
    executable_lines_.clear();
    local_definitions_.clear();
    global_types_.clear();
    type_fields_.clear();

    // A krc-built image carries one `.kdbg` payload instead of the `.debug_*`
    // sections. Decoding it here keeps the session's model unchanged: both
    // formats fill the same members.
    KdbgModel kdbg;
    if (decode_kdbg_model(image_bytes_, kdbg)) {
        load_kdbg_debug_info(kdbg);
        load_source_text();
        return;
    }

    std::vector<LineRow> decoded_rows;
    for (const auto& section : container_.sections) {
        if (section.name != ".debug_line") {
            continue;
        }
        try {
            std::uint16_t previous = 0;
            bool have_previous = false;
            for (const auto& row : parse_debug_line(section.data)) {
                // The linker currently relocates .crt0 rows to the end of
                // .debug_line, making them appear inside the first C function.
                // Keep the table monotonic until the relocation is fixed.
                if (have_previous && row.address < previous) {
                    continue;
                }
                previous = row.address;
                have_previous = true;
                if (row.line > 0) {
                    decoded_rows.push_back(
                        LineRow{row.address, static_cast<int>(row.line)});
                }
            }
        } catch (const std::exception&) {
            // A malformed table must not take down the debugger.
        }
    }
    std::sort(decoded_rows.begin(), decoded_rows.end(),
              [](const LineRow& left, const LineRow& right) {
                  return left.address < right.address;
              });
    std::vector<LineRow> rows;
    for (const auto& row : decoded_rows) {
        if (!rows.empty() && rows.back().address == row.address) {
            rows.back().line = row.line;
        } else {
            rows.push_back(row);
        }
    }

    std::set<std::string> function_names;
    for (const auto& section : container_.sections) {
        if (section.name == ".debug_frames") {
            for (const auto& record : split_records(section.data)) {
                const auto parts = split_pipe(record);
                if (!parts.empty() && !parts[0].empty()) {
                    function_names.insert(parts[0]);
                }
            }
        } else if (section.name == ".debug_vars") {
            for (const auto& record : split_records(section.data)) {
                const auto parts = split_pipe(record);
                if (parts.size() < 4) {
                    continue;
                }
                LocalDefinition local;
                local.function = trim_copy(parts[0]);
                local.name = trim_copy(parts[1]);
                if (parts.size() >= 5) {
                    local.type = trim_copy(parts[4]);
                }
                try {
                    local.offset = std::stoi(parts[2]);
                    local.size = std::stoi(parts[3]);
                } catch (const std::exception&) {
                    continue;
                }
                if (!local.function.empty() && !local.name.empty()) {
                    function_names.insert(local.function);
                    local_definitions_.push_back(std::move(local));
                }
            }
        } else if (section.name == ".debug_globals") {
            for (const auto& record : split_records(section.data)) {
                const auto parts = split_pipe(record);
                if (parts.size() < 2) {
                    continue;
                }
                const std::string name = trim_copy(parts[0]);
                const std::string type = trim_copy(parts[1]);
                if (!name.empty() && !type.empty()) {
                    global_types_[name] = type;
                }
            }
        } else if (section.name == ".debug_types") {
            for (const auto& record : split_records(section.data)) {
                const auto parts = split_pipe(record);
                if (parts.size() < 2) {
                    continue;
                }
                const std::string name = trim_copy(parts[0]);
                if (name.empty()) {
                    continue;
                }
                std::vector<SessionField> fields;
                if (parts.size() >= 3) {
                    const std::string& encoded = parts[2];
                    std::size_t begin = 0;
                    while (begin < encoded.size()) {
                        const auto end = encoded.find(';', begin);
                        const std::string item = encoded.substr(
                            begin, end == std::string::npos
                                       ? std::string::npos
                                       : end - begin);
                        begin = end == std::string::npos ? encoded.size()
                                                         : end + 1;
                        const auto at = item.find('@');
                        const auto first_colon =
                            at == std::string::npos
                                ? std::string::npos
                                : item.find(':', at + 1);
                        const auto last_colon =
                            first_colon == std::string::npos
                                ? std::string::npos
                                : item.rfind(':');
                        if (at == std::string::npos ||
                            first_colon == std::string::npos ||
                            last_colon == first_colon) {
                            continue;
                        }
                        SessionField field;
                        field.name = trim_copy(item.substr(0, at));
                        field.type = trim_copy(item.substr(
                            first_colon + 1, last_colon - first_colon - 1));
                        try {
                            field.offset = std::stoi(
                                item.substr(at + 1, first_colon - at - 1));
                            field.size = std::stoi(
                                item.substr(last_colon + 1));
                        } catch (const std::exception&) {
                            continue;
                        }
                        if (!field.name.empty()) {
                            fields.push_back(std::move(field));
                        }
                    }
                }
                type_fields_[name] = std::move(fields);
            }
        }
    }

    for (const auto& symbol : container_.symbols) {
        if (symbol.name.empty() || symbol.name.front() == '.') {
            continue;
        }
        if (symbol.section == 0 ||
            symbol.section > container_.sections.size()) {
            continue;
        }
        const auto& section = container_.sections[symbol.section - 1];
        if (section.name != ".text" && section.name != ".crt0") {
            continue;
        }
        if (symbol.binding != SymbolBinding::Local ||
            symbol.type == SymbolType::Function) {
            function_names.insert(symbol.name);
        }
    }

    const std::uint32_t text_end =
        text_high_ > text_low_ ? text_high_ : 0x10000u;
    for (const auto& name : function_names) {
        for (const auto& symbol : container_.symbols) {
            if (symbol.name != name) {
                continue;
            }
            FunctionRecord record;
            record.info.name = name;
            record.info.low = symbol.value;
            function_records_.push_back(std::move(record));
            break;
        }
    }
    std::sort(function_records_.begin(), function_records_.end(),
              [](const FunctionRecord& left, const FunctionRecord& right) {
                  return left.info.low < right.info.low;
              });

    for (std::size_t i = 0; i < function_records_.size(); ++i) {
        auto& record = function_records_[i];
        record.info.high = static_cast<std::uint16_t>(
            i + 1 < function_records_.size()
                ? function_records_[i + 1].info.low
                : text_end);
        if (record.info.high <= record.info.low) {
            record.info.high = static_cast<std::uint16_t>(text_end);
        }
        if (record.info.high <= record.info.low) {
            record.info.low = 0;
            record.info.high = 0;
        }
    }
    function_records_.erase(
        std::remove_if(function_records_.begin(), function_records_.end(),
                       [](const FunctionRecord& record) {
                           return record.info.high <= record.info.low;
                       }),
        function_records_.end());

    for (const auto& row : rows) {
        for (auto& record : function_records_) {
            if (row.address >= record.info.low &&
                row.address < record.info.high) {
                record.rows.push_back(row);
                break;
            }
        }
    }
    for (auto& record : function_records_) {
        std::sort(record.rows.begin(), record.rows.end(),
                  [](const LineRow& left, const LineRow& right) {
                      return left.address < right.address;
                  });
        record.rows.erase(
            std::unique(record.rows.begin(), record.rows.end(),
                        [](const LineRow& left, const LineRow& right) {
                            return left.address == right.address;
                        }),
            record.rows.end());
    }

    // The compiler emits no .loc for a prologue, so the first rows of a
    // function inherit the previous function's last line. Line 0 means "no
    // source statement here". Remove this once prologue rows are explicit.
    int previous_last = 0;
    for (auto& record : function_records_) {
        if (!record.rows.empty() && previous_last != 0 &&
            record.rows.front().line == previous_last) {
            for (auto& row : record.rows) {
                if (row.line != previous_last) {
                    break;
                }
                row.line = 0;
            }
        }
        if (!record.rows.empty()) {
            previous_last = record.rows.back().line;
        }
        for (const auto& row : record.rows) {
            if (row.line > 0) {
                executable_lines_.insert(row.line);
            }
        }
    }

    functions_.clear();
    functions_.reserve(function_records_.size());
    for (const auto& record : function_records_) {
        functions_.push_back(record.info);
    }

    for (const auto& section : container_.sections) {
        if (section.name != ".debug_sources") {
            continue;
        }
        const auto records = split_records(section.data);
        if (!records.empty()) {
            source_path_ = records.front();
        }
        break;
    }
    load_source_text();
}

void DebugSession::load_source_text() {
    if (source_path_.empty()) {
        return;
    }

    std::filesystem::path source(source_path_);
    std::error_code ec;
    if (!source.is_absolute()) {
        const auto beside = image_path_.parent_path() / source;
        if (std::filesystem::exists(beside, ec)) {
            source = beside;
        }
    }
    auto text = read_source_lines(source);
    if (text.empty() && source != std::filesystem::path(source_path_)) {
        text = read_source_lines(source_path_);
    }
    source_lines_.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        source_lines_.push_back(
            SessionSourceLine{std::move(text[i]),
                              executable_lines_.count(
                                  static_cast<int>(i) + 1) != 0});
    }
}

void DebugSession::load_kdbg_debug_info(const KdbgModel& model) {
    function_records_.reserve(model.functions.size());
    for (const auto& function : model.functions) {
        FunctionRecord record;
        record.info.name = function.name;
        record.info.low = function.low;
        record.info.high = function.high;
        function_records_.push_back(std::move(record));
    }

    // Only the primary source is shown, so only its rows belong on screen.
    for (const auto& line : model.lines) {
        if (line.source != model.primary_source || line.line <= 0) {
            continue;
        }
        for (auto& record : function_records_) {
            if (line.address >= record.info.low &&
                line.address < record.info.high) {
                record.rows.push_back(LineRow{line.address, line.line});
                break;
            }
        }
    }

    for (auto& record : function_records_) {
        std::sort(record.rows.begin(), record.rows.end(),
                  [](const LineRow& left, const LineRow& right) {
                      return left.address < right.address;
                  });
        record.rows.erase(
            std::unique(record.rows.begin(), record.rows.end(),
                        [](const LineRow& left, const LineRow& right) {
                            return left.address == right.address;
                        }),
            record.rows.end());
        for (const auto& row : record.rows) {
            if (row.line > 0) {
                executable_lines_.insert(row.line);
            }
        }
    }

    functions_.clear();
    functions_.reserve(function_records_.size());
    for (const auto& record : function_records_) {
        functions_.push_back(record.info);
    }

    // KDBG records stack-pointer relative offsets; the session resolves a local
    // against the frame pointer, which the decoder already converted.
    for (const auto& local : model.locals) {
        LocalDefinition definition;
        definition.function = local.function;
        definition.name = local.name;
        definition.type = local.type;
        definition.offset = local.offset;
        definition.size = local.size;
        local_definitions_.push_back(std::move(definition));
    }
    global_types_ = model.globals;
    for (const auto& [name, fields] : model.types) {
        std::vector<SessionField> converted;
        converted.reserve(fields.size());
        for (const auto& field : fields) {
            converted.push_back(
                SessionField{field.name, field.type, field.offset, field.size});
        }
        type_fields_[name] = std::move(converted);
    }

    if (model.primary_source < model.sources.size()) {
        source_path_ = model.sources[model.primary_source];
    }
}

const SessionFunction* DebugSession::function_at(
    std::uint16_t address) const {
    for (const auto& function : functions_) {
        if (address >= function.low && address < function.high) {
            return &function;
        }
    }
    return nullptr;
}

int DebugSession::line_at(std::uint16_t address) const {
    const SessionFunction* function = function_at(address);
    if (function == nullptr) {
        return 0;
    }
    const auto record = std::find_if(
        function_records_.begin(), function_records_.end(),
        [&](const FunctionRecord& candidate) {
            return candidate.info.name == function->name &&
                   candidate.info.low == function->low;
        });
    if (record == function_records_.end() || record->rows.empty()) {
        return 0;
    }
    if (address < record->rows.front().address) {
        return 0;
    }
    const auto it = std::upper_bound(
        record->rows.begin(), record->rows.end(), address,
        [](std::uint16_t value, const LineRow& row) {
            return value < row.address;
        });
    if (it == record->rows.begin()) {
        return 0;
    }
    return (it - 1)->line;
}

std::string DebugSession::function_name_at(std::uint16_t address) const {
    if (const SessionFunction* function = function_at(address);
        function != nullptr) {
        return function->name;
    }
    for (const auto& symbol : container_.symbols) {
        if (symbol.value == address && !symbol.name.empty()) {
            return symbol.name;
        }
    }
    return "<program>";
}

int DebugSession::frame_depth() {
    int depth = 0;
    std::uint16_t fp = cpu_.reg(4);
    for (int i = 0; i < 32; ++i) {
        if (fp < 0x0100 || fp >= 0xF000) {
            break;
        }
        ++depth;
        const std::uint16_t saved = machine_.read16(fp);
        if (saved == 0) {
            break;
        }
        fp = saved;
    }
    return depth;
}

std::vector<SessionFrame> DebugSession::frames() {
    std::vector<SessionFrame> result;
    std::uint16_t pc = cpu_.pc();
    std::uint16_t fp = cpu_.reg(4);
    for (int i = 0; i < 32; ++i) {
        SessionFrame frame;
        frame.pc = pc;
        frame.name = function_name_at(pc);
        frame.line = line_at(pc);
        result.push_back(std::move(frame));
        if (fp < 0x0100 || fp >= 0xF000) {
            break;
        }
        const std::uint16_t saved = machine_.read16(fp);
        const std::uint16_t ret =
            machine_.read16(static_cast<std::uint16_t>(fp + 2));
        if (saved == 0 || ret < 0x0100) {
            break;
        }
        pc = ret;
        fp = saved;
    }
    return result;
}

std::vector<SessionVariable> DebugSession::locals() {
    std::vector<SessionVariable> result;
    const SessionFunction* function = function_at(cpu_.pc());
    if (function == nullptr) {
        return result;
    }
    const std::uint16_t fp = cpu_.reg(4);
    for (const auto& definition : local_definitions_) {
        if (definition.function != function->name) {
            continue;
        }
        SessionVariable variable;
        variable.name = definition.name;
        variable.scope = definition.function;
        variable.type = definition.type;
        variable.size = definition.size > 0 ? definition.size : 2;
        variable.local = true;
        variable.address = static_cast<std::uint16_t>(
            fp - static_cast<std::uint16_t>(definition.offset));
        variable.ok =
            fp >= 0x0100 && fp < 0xF000 && variable.address >= 0x0100 &&
            static_cast<std::uint32_t>(variable.address) +
                    static_cast<std::uint32_t>(variable.size) <=
                0xF000;
        if (variable.ok) {
            variable.value = variable.size == 1
                                 ? machine_.read8(variable.address)
                                 : machine_.read16(variable.address);
        }
        result.push_back(std::move(variable));
    }
    return result;
}

std::vector<SessionVariable> DebugSession::globals() {
    std::vector<SessionVariable> result;
    const auto is_internal = [](const std::string& name) {
        return name.rfind("__kr16_", 0) == 0 ||
               name.rfind("__heap", 0) == 0;
    };
    const auto append = [&](bool internals) {
        for (const auto& symbol : container_.symbols) {
            if (symbol.section == 0 ||
                symbol.section > container_.sections.size()) {
                continue;
            }
            if (is_internal(symbol.name) != internals) {
                continue;
            }
            const auto& section = container_.sections[symbol.section - 1];
            if (section.name != ".data" && section.name != ".bss") {
                continue;
            }
            if (symbol.value >= 0xF000) {
                continue;
            }
            SessionVariable variable;
            variable.name = symbol.name;
            variable.local = false;
            variable.address = symbol.value;
            const auto type = global_types_.find(symbol.name);
            if (type != global_types_.end()) {
                variable.type = type->second;
            }
            variable.size =
                symbol.size > 0 ? static_cast<int>(symbol.size) : 2;
            variable.ok = static_cast<std::uint32_t>(symbol.value) +
                              static_cast<std::uint32_t>(variable.size) <=
                          0xF000;
            if (variable.ok) {
                variable.value = variable.size == 1
                                     ? machine_.read8(symbol.value)
                                     : machine_.read16(symbol.value);
            }
            result.push_back(std::move(variable));
            if (result.size() >= 12) {
                return;
            }
        }
    };
    append(false);
    append(true);
    return result;
}

const std::vector<SessionField>* DebugSession::fields_for(
    const std::string& type) const {
    auto found = type_fields_.find(type);
    if (found != type_fields_.end()) {
        return &found->second;
    }
    std::string short_name = type;
    if (short_name.rfind("struct ", 0) == 0) {
        short_name = short_name.substr(7);
    } else if (short_name.rfind("union ", 0) == 0) {
        short_name = short_name.substr(6);
    }
    found = type_fields_.find(short_name);
    return found == type_fields_.end() ? nullptr : &found->second;
}

bool DebugSession::step_one() {
    if (!loaded_ || cpu_.halted()) {
        return false;
    }
    const std::uint32_t cycles = cpu_.step(machine_);
    if (cycles == 0) {
        reason_ = "halted";
        return false;
    }
    machine_.tick(cycles);
    ++instructions_executed_;
    drain_console();
    if (cpu_.halted()) {
        reason_ = halt_reason();
        return false;
    }
    return true;
}

bool DebugSession::step_instruction() {
    if (!loaded_ || running_ || cpu_.halted()) {
        return false;
    }
    if (!step_one()) {
        reason_ = halted() ? halt_reason() : "instruction failed";
        return false;
    }
    reason_ = "instruction";
    return true;
}

bool DebugSession::run_until_pc(std::uint16_t target) {
    for (std::uint64_t i = 0; i < 20'000'000; ++i) {
        if (!step_one()) {
            return false;
        }
        if (cpu_.pc() == target) {
            return true;
        }
    }
    reason_ = "instruction budget";
    return false;
}

bool DebugSession::step_instruction_over() {
    if (!loaded_ || running_ || cpu_.halted()) {
        return false;
    }
    const std::uint16_t address = cpu_.pc();
    const std::uint16_t first = machine_.read16(address);
    const bool is_long = Instruction::first_word_is_32_bit(first);
    const std::uint16_t second =
        is_long ? machine_.read16(static_cast<std::uint16_t>(address + 2))
                : 0;
    const Instruction instruction = Instruction::decode(first, second);
    const std::uint16_t next = static_cast<std::uint16_t>(
        address + (is_long ? 4 : 2));
    if (instruction.op == Op::CallRel || instruction.op == Op::CallReg) {
        if (run_until_pc(next)) {
            reason_ = "instruction over";
            return true;
        }
        return false;
    }
    return step_instruction();
}

bool DebugSession::step_instruction_out() {
    if (!loaded_ || running_ || cpu_.halted()) {
        return false;
    }
    const std::uint16_t fp = cpu_.reg(4);
    if (fp >= 0x0100 && fp <= 0xFFFC) {
        const std::uint16_t target =
            machine_.read16(static_cast<std::uint16_t>(fp + 2));
        if (target >= 0x0100) {
            if (run_until_pc(target)) {
                reason_ = "instruction out";
                return true;
            }
            return false;
        }
    }
    return step_instruction();
}

bool DebugSession::step_line(StepMode mode) {
    if (mode == StepMode::Instruction) {
        return step_instruction();
    }
    if (!loaded_ || running_ || cpu_.halted()) {
        return false;
    }

    const int start_depth = frame_depth();
    int previous = line_at(cpu_.pc());
    if (previous == 0) {
        for (std::uint64_t i = 0; i < 5'000'000; ++i) {
            if (!step_one()) {
                return false;
            }
            if (line_at(cpu_.pc()) != 0) {
                reason_ = "entered C";
                return true;
            }
        }
        reason_ = "no C source found";
        return false;
    }

    for (std::uint64_t i = 0; i < 5'000'000; ++i) {
        if (!step_one()) {
            return false;
        }
        const int current = line_at(cpu_.pc());
        if (current != 0 && current != previous) {
            const int depth = frame_depth();
            bool stop = false;
            if (mode == StepMode::LineIn) {
                stop = true;
            } else if (mode == StepMode::LineOver) {
                stop = depth <= start_depth;
            } else {
                stop = depth < start_depth;
            }
            if (stop) {
                reason_ = "line step";
                return true;
            }
        }
        if (current != 0) {
            previous = current;
        }
    }
    reason_ = "line step budget";
    return false;
}

bool DebugSession::seek_first_line() {
    if (!loaded_) {
        return false;
    }
    if (line_at(cpu_.pc()) != 0) {
        return true;
    }
    for (std::uint64_t i = 0; i < 5'000'000; ++i) {
        if (!step_one()) {
            return false;
        }
        if (line_at(cpu_.pc()) != 0) {
            reason_ = "entered C";
            return true;
        }
    }
    reason_ = "no C source found";
    return false;
}

void DebugSession::start_run() {
    if (!loaded_ || running_ || cpu_.halted()) {
        return;
    }
    running_ = true;
    run_started_ = std::chrono::steady_clock::now();
    run_previous_line_ = line_at(cpu_.pc());
    reason_ = "running";
}

void DebugSession::stop_run(std::string reason) {
    if (!running_) {
        reason_ = std::move(reason);
        return;
    }
    const auto elapsed = std::chrono::steady_clock::now() - run_started_;
    accumulated_run_us_ += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed)
            .count());
    running_ = false;
    reason_ = std::move(reason);
}

SessionRunStop DebugSession::run_slice(int max_instructions) {
    if (!running_) {
        return SessionRunStop::NotRunning;
    }
    if (max_instructions <= 0) {
        return SessionRunStop::Budget;
    }

    const auto slice_start = std::chrono::steady_clock::now();
    for (int i = 0; i < max_instructions; ++i) {
        if (!step_one()) {
            stop_run("halted");
            return SessionRunStop::Halted;
        }
        if (address_breakpoints_.count(cpu_.pc()) != 0) {
            stop_run("address breakpoint");
            return SessionRunStop::Breakpoint;
        }
        const int current_line = line_at(cpu_.pc());
        if (current_line != 0 && current_line != run_previous_line_ &&
            line_breakpoints_.count(current_line) != 0) {
            stop_run("line breakpoint");
            return SessionRunStop::Breakpoint;
        }
        if (current_line != 0) {
            run_previous_line_ = current_line;
        }
        if ((i & 0x3FF) == 0) {
            const auto elapsed = std::chrono::steady_clock::now() - slice_start;
            if (elapsed >= std::chrono::milliseconds(6)) {
                return SessionRunStop::Budget;
            }
        }
    }
    return SessionRunStop::Budget;
}

void DebugSession::toggle_address_breakpoint(std::uint16_t address) {
    if (address_breakpoints_.count(address) != 0) {
        address_breakpoints_.erase(address);
    } else {
        address_breakpoints_.insert(address);
    }
}

bool DebugSession::has_breakpoint(std::uint16_t address) const {
    return address_breakpoints_.count(address) != 0;
}

void DebugSession::toggle_line_breakpoint(int line) {
    if (line <= 0) {
        return;
    }
    int target = line;
    if (executable_lines_.count(target) == 0) {
        const auto next = executable_lines_.lower_bound(line);
        if (next == executable_lines_.end()) {
            return;
        }
        target = *next;
    }
    if (line_breakpoints_.count(target) != 0) {
        line_breakpoints_.erase(target);
    } else {
        line_breakpoints_.insert(target);
    }
}

bool DebugSession::has_line_breakpoint(int line) const {
    return line_breakpoints_.count(line) != 0;
}

std::uint64_t DebugSession::run_time_ms() const {
    std::uint64_t micros = accumulated_run_us_;
    if (running_) {
        const auto elapsed = std::chrono::steady_clock::now() - run_started_;
        micros += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(elapsed)
                .count());
    }
    return micros / 1000;
}

void DebugSession::reset_stats() {
    instructions_executed_ = 0;
    accumulated_run_us_ = 0;
    if (running_) {
        run_started_ = std::chrono::steady_clock::now();
    }
}

void DebugSession::drain_console() {
    const auto& output = machine_.console_output();
    if (output.empty()) {
        return;
    }
    console_.append(reinterpret_cast<const char*>(output.data()),
                    output.size());
    if (console_.size() > 32768) {
        console_.erase(0, console_.size() - 32768);
    }
    machine_.clear_console_output();
}

}  // namespace kr16
