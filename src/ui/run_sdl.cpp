#include "run.hpp"

#include <filesystem>
#include <string>

#include "app.hpp"
#include "kr16/build.hpp"
#include "kr16/debug_session.hpp"
#include "platform/ui/sdl/sdl_window.hpp"

namespace kr16::ui {

int run_ui(int argc, char** argv) {
    int index = 1;
    if (index < argc && std::string(argv[index]) == "--ui") {
        ++index;
    }
    const bool has_initial = index < argc;

    DebugSession session;
    sdl::SdlWindow window("KR16", 1280, 820);
    App app(window, session, kr16::find_libc_source());
    if (has_initial) {
        app.open(std::filesystem::path(argv[index]));
    }
    return window.run(app);
}

bool launch_ui_child(const std::filesystem::path& initial,
                     std::string& error) {
    (void)initial;
    error = "the UI runs in-process on Linux";
    return false;
}

}  // namespace kr16::ui
