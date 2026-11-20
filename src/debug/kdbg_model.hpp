#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace kr16 {

/// One function range from a decoded KDBG payload, `[low, high)`.
struct KdbgFunctionRange {
    std::string name;
    std::uint16_t low = 0;
    std::uint16_t high = 0;
};

/// One statement address and the source line it belongs to. `source` indexes
/// KdbgModel::sources.
struct KdbgLineRow {
    std::uint16_t address = 0;
    std::uint16_t source = 0;
    int line = 0;
};

/// A local variable with a frame-pointer relative `offset`: the variable lives
/// at `R4 - offset`, the shape the session and the DAP adapter already use.
struct KdbgLocalVariable {
    std::string function;
    std::string name;
    std::string type;
    int offset = 0;
    int size = 2;
};

struct KdbgTypeField {
    std::string name;
    std::string type;
    int offset = 0;
    int size = 2;
};

/// The product debug model decoded from a `.kdbg` payload. It mirrors what the
/// `.debug_*` sections provide, so the two readers keep one shape.
struct KdbgModel {
    std::vector<std::string> sources;
    std::vector<KdbgFunctionRange> functions;
    std::vector<KdbgLineRow> lines;
    std::vector<KdbgLocalVariable> locals;
    std::map<std::string, std::string> globals;
    std::map<std::string, std::vector<KdbgTypeField>> types;
    /// The source the debug views show: the file that owns `main`.
    std::size_t primary_source = 0;
};

/// Decodes `.kdbg` from a `.kex` image. Returns false when the image carries no
/// `.kdbg`, when the payload is malformed, or when it records an image base
/// different from the one the container loads at — the caller then falls back
/// to the `.debug_*` sections. A build without the KDBG reader always returns
/// false.
bool decode_kdbg_model(std::span<const std::uint8_t> image, KdbgModel& model);

}  // namespace kr16
