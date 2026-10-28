#pragma once

#include <filesystem>
#include <string>

namespace kr16::ui {

/// Run the windowed UI. `argv[1]` may be `--ui` and `argv[2]` an optional file.
int run_ui(int argc, char** argv);

/// Spawn this executable with `--ui` and no console. Used by no-argument
/// `kr16` on Windows.
bool launch_ui_child(const std::filesystem::path& initial,
                     std::string& error);

}  // namespace kr16::ui
