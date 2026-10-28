#include "kr16/cli.hpp"

#include <filesystem>
#include <iostream>
#include <string>

#include "kr16/build.hpp"

#if defined(KR16_HAS_UI)
#include "ui/run.hpp"
#endif

int main(int argc, char** argv) {
#if defined(KR16_HAS_UI)
    if (argc >= 2 && std::string(argv[1]) == "--ui") {
        return kr16::ui::run_ui(argc, argv);
    }
#if defined(_WIN32)
    if (argc < 2) {
        std::string error;
        if (kr16::ui::launch_ui_child({}, error)) {
            return 0;
        }
        std::cerr << "kr16: cannot start the UI: " << error << '\n';
        std::cerr << "kr16: use `kr16 help` for commands or `kr16 shell`\n";
        return 1;
    }
    if (argc == 2 && !kr16::cli::is_known_command(argv[1])) {
        const std::filesystem::path candidate(argv[1]);
        std::error_code fs_error;
        const bool image =
            candidate.extension() == ".kex" || kr16::is_build_input(candidate);
        if (image && std::filesystem::exists(candidate, fs_error)) {
            std::string error;
            if (kr16::ui::launch_ui_child(candidate, error)) {
                return 0;
            }
            std::cerr << "kr16: cannot start the UI: " << error << '\n';
            return 1;
        }
    }
#else
    if (argc < 2) {
        return kr16::ui::run_ui(argc, argv);
    }
    if (argc == 2 && !kr16::cli::is_known_command(argv[1])) {
        const std::filesystem::path candidate(argv[1]);
        std::error_code fs_error;
        const bool image =
            candidate.extension() == ".kex" || kr16::is_build_input(candidate);
        if (image && std::filesystem::exists(candidate, fs_error)) {
            return kr16::ui::run_ui(argc, argv);
        }
    }
#endif
#else
    if (argc < 2) {
        std::cout << kr16::cli::usage_text();
        std::cerr << "kr16: the windowed UI is not built on this platform; "
                     "use `kr16 shell` for the interactive shell\n";
        return 2;
    }
#endif
    return kr16::cli::run(argc, argv);
}
