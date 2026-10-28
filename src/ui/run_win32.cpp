#include "run.hpp"

#include <filesystem>
#include <string>

#include "app.hpp"
#include "kr16/build.hpp"
#include "kr16/debug_session.hpp"
#include "platform/ui/win32/win32_launcher.hpp"
#include "platform/ui/win32/win32_window.hpp"

namespace kr16::ui {

int run_ui(int argc, char** argv) {
    int index = 1;
    if (index < argc && std::string(argv[index]) == "--ui") {
        ++index;
    }
    const bool has_initial = index < argc;

    DebugSession session;
    win32::Win32Window window(L"KR16", 1280, 820);
    App app(window, session, kr16::find_libc_source());
    if (has_initial) {
        app.open(std::filesystem::path(argv[index]));
    }
    return window.run(app);
}

bool launch_ui_child(const std::filesystem::path& initial,
                     std::string& error) {
    return platform::launch_ui_child(initial, error);
}

}  // namespace kr16::ui
