#include "debug/kdbg_model.hpp"

#include <algorithm>
#include <utility>

#if defined(KR16_KDBG)
#include "krc/debug/kdbg.h"
#endif

namespace kr16 {

#if defined(KR16_KDBG)

namespace {

std::uint16_t clamp_to_address(std::uint64_t value) {
    return value > 0xFFFFu ? static_cast<std::uint16_t>(0xFFFFu)
                           : static_cast<std::uint16_t>(value);
}

/// Picks the source the single-file debug views show. `main` owns the program
/// the reader was asked about; a payload without it falls back to the source
/// with the most line rows.
std::size_t choose_primary_source(const KdbgModel& model) {
    const auto main = std::find_if(
        model.functions.begin(), model.functions.end(),
        [](const KdbgFunctionRange& function) { return function.name == "main"; });
    if (main != model.functions.end()) {
        std::uint16_t best_address = 0;
        std::size_t source = 0;
        bool found = false;
        for (const auto& line : model.lines) {
            if (line.address < main->low || line.address >= main->high) {
                continue;
            }
            if (!found || line.address < best_address) {
                best_address = line.address;
                source = line.source;
                found = true;
            }
        }
        if (found && source < model.sources.size()) {
            return source;
        }
    }
    if (model.sources.empty()) {
        return 0;
    }
    std::vector<std::size_t> counts(model.sources.size(), 0);
    for (const auto& line : model.lines) {
        if (line.source < counts.size()) {
            ++counts[line.source];
        }
    }
    const auto most = std::max_element(counts.begin(), counts.end());
    if (most == counts.end() || *most == 0) {
        return 0;
    }
    return static_cast<std::size_t>(most - counts.begin());
}

}  // namespace

bool decode_kdbg_model(std::span<const std::uint8_t> image, KdbgModel& model) {
    model = KdbgModel{};

    krc::debug::KdbgImage decoded;
    std::string error;
    // `decode_kdbg_image` owns the address rule: it rejects a payload whose
    // recorded base differs from the address the container's first section
    // loads at, and accepts a payload that recorded no base.
    if (!krc::debug::decode_kdbg_image(image, decoded, error)) {
        return false;
    }

    model.sources.reserve(decoded.sources.size());
    for (const auto& source : decoded.sources) {
        model.sources.push_back(source.path);
    }

    model.functions.reserve(decoded.functions.size());
    for (const auto& function : decoded.functions) {
        if (function.start > 0xFFFFu || function.name.empty()) {
            continue;
        }
        KdbgFunctionRange range;
        range.name = function.name;
        range.low = static_cast<std::uint16_t>(function.start);
        range.high = clamp_to_address(function.end);
        if (range.high <= range.low) {
            continue;
        }
        model.functions.push_back(std::move(range));
    }
    std::sort(model.functions.begin(), model.functions.end(),
              [](const KdbgFunctionRange& left, const KdbgFunctionRange& right) {
                  return left.low < right.low;
              });

    model.lines.reserve(decoded.lines.size());
    for (const auto& line : decoded.lines) {
        if (line.line == 0 || line.address > 0xFFFFu) {
            continue;
        }
        KdbgLineRow row;
        row.address = static_cast<std::uint16_t>(line.address);
        row.source = static_cast<std::uint16_t>(line.source);
        row.line = static_cast<int>(line.line);
        model.lines.push_back(row);
    }
    std::sort(model.lines.begin(), model.lines.end(),
              [](const KdbgLineRow& left, const KdbgLineRow& right) {
                  if (left.address != right.address) {
                      return left.address < right.address;
                  }
                  return left.line < right.line;
              });
    model.lines.erase(std::unique(model.lines.begin(), model.lines.end(),
                                  [](const KdbgLineRow& left,
                                     const KdbgLineRow& right) {
                                      return left.address == right.address &&
                                             left.source == right.source &&
                                             left.line == right.line;
                                  }),
                      model.lines.end());

    model.locals.reserve(decoded.variables.size());
    for (const auto& variable : decoded.variables) {
        // The payload's offsets are already frame-pointer relative: a variable
        // at offset N lives at R4 - N, which is the shape both readers use.
        if (variable.name.empty() || variable.offset > 0x7FFFu) {
            continue;
        }
        KdbgLocalVariable local;
        local.function = variable.function;
        local.name = variable.name;
        local.type = variable.type;
        local.offset = static_cast<int>(variable.offset);
        local.size =
            variable.size == 0 ? 2 : static_cast<int>(variable.size);
        model.locals.push_back(std::move(local));
    }

    for (const auto& symbol : decoded.symbols) {
        if (!symbol.name.empty() && !symbol.type.empty()) {
            model.globals[symbol.name] = symbol.type;
        }
    }

    for (const auto& type : decoded.types) {
        if (type.name.empty()) {
            continue;
        }
        std::vector<KdbgTypeField> fields;
        fields.reserve(type.members.size());
        for (const auto& member : type.members) {
            if (member.name.empty()) {
                continue;
            }
            KdbgTypeField field;
            field.name = member.name;
            field.type = member.type;
            field.offset = static_cast<int>(member.offset);
            field.size = member.size == 0 ? 2 : static_cast<int>(member.size);
            fields.push_back(std::move(field));
        }
        model.types[type.name] = std::move(fields);
    }

    model.primary_source = choose_primary_source(model);
    return true;
}

#else

bool decode_kdbg_model(std::span<const std::uint8_t> image, KdbgModel& model) {
    (void)image;
    model = KdbgModel{};
    return false;
}

#endif

}  // namespace kr16
