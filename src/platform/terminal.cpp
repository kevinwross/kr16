#if defined(_WIN32)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "platform/terminal.hpp"

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <conio.h>
#include <io.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace kr16::platform {
namespace {

void redraw(std::ostream& output, const std::string& prompt,
            const std::string& buffer, std::size_t cursor) {
    output << '\r' << prompt << buffer << "\x1b[K";
    if (cursor < buffer.size()) {
        output << "\x1b[" << (buffer.size() - cursor) << "D";
    }
    output.flush();
}

void enable_virtual_terminal() {
#if defined(_WIN32)
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD mode = 0;
    if (GetConsoleMode(handle, &mode) != 0) {
        SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
}

}  // namespace

std::string executable_path() {
#if defined(_WIN32)
    wchar_t buffer[32768] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, 32768);
    if (length == 0) {
        return {};
    }
    return std::filesystem::path(std::wstring(buffer, length)).string();
#else
    char buffer[4096] = {};
    const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) {
        return {};
    }
    return std::string(buffer, static_cast<std::size_t>(length));
#endif
}

bool stdin_is_terminal() {
#if defined(_WIN32)
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

std::optional<std::string> read_line(
    const std::string& prompt, std::ostream& output,
    const std::vector<std::string>& history) {
    enable_virtual_terminal();
    output << prompt;
    output.flush();

    std::string buffer;
    std::size_t cursor = 0;
    std::size_t history_index = history.size();
    std::string saved;

#if defined(_WIN32)
    for (;;) {
        const int key = _getch();
        if (key == 0 || key == 224) {
            const int extended = _getch();
            switch (extended) {
                case 72:
                    if (history_index > 0) {
                        if (history_index == history.size()) {
                            saved = buffer;
                        }
                        --history_index;
                        buffer = history[history_index];
                        cursor = buffer.size();
                    }
                    break;
                case 80:
                    if (history_index < history.size()) {
                        ++history_index;
                        buffer = history_index == history.size()
                                     ? saved
                                     : history[history_index];
                        cursor = buffer.size();
                    }
                    break;
                case 75:
                    if (cursor > 0) {
                        --cursor;
                    }
                    break;
                case 77:
                    if (cursor < buffer.size()) {
                        ++cursor;
                    }
                    break;
                case 71:
                    cursor = 0;
                    break;
                case 79:
                    cursor = buffer.size();
                    break;
                case 83:
                    if (cursor < buffer.size()) {
                        buffer.erase(cursor, 1);
                    }
                    break;
                default:
                    break;
            }
            redraw(output, prompt, buffer, cursor);
            continue;
        }
        if (key == 13) {
            output << '\n';
            return buffer;
        }
        if (key == 3) {
            output << "^C\n";
            return std::string();
        }
        if (key == 4) {
            if (buffer.empty()) {
                output << '\n';
                return std::nullopt;
            }
            continue;
        }
        if (key == 8 || key == 127) {
            if (cursor > 0) {
                buffer.erase(cursor - 1, 1);
                --cursor;
            }
            redraw(output, prompt, buffer, cursor);
            continue;
        }
        if (key == 1) {
            cursor = 0;
            redraw(output, prompt, buffer, cursor);
            continue;
        }
        if (key == 5) {
            cursor = buffer.size();
            redraw(output, prompt, buffer, cursor);
            continue;
        }
        if (key == 11) {
            buffer.erase(cursor);
            redraw(output, prompt, buffer, cursor);
            continue;
        }
        if (key == 21) {
            buffer.erase(0, cursor);
            cursor = 0;
            redraw(output, prompt, buffer, cursor);
            continue;
        }
        if (key >= 32 && key < 127) {
            buffer.insert(buffer.begin() + static_cast<std::ptrdiff_t>(cursor),
                          static_cast<char>(key));
            ++cursor;
            redraw(output, prompt, buffer, cursor);
            continue;
        }
    }
#else
    termios original{};
    if (tcgetattr(STDIN_FILENO, &original) != 0) {
        std::string line;
        if (!std::getline(std::cin, line)) {
            return std::nullopt;
        }
        return line;
    }
    termios raw = original;
    raw.c_lflag &= static_cast<unsigned>(~(ICANON | ECHO));
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    struct Restore {
        termios value;
        ~Restore() { tcsetattr(STDIN_FILENO, TCSANOW, &value); }
    } restore{original};

    auto read_char = []() -> int {
        unsigned char ch = 0;
        if (read(STDIN_FILENO, &ch, 1) != 1) {
            return -1;
        }
        return ch;
    };

    for (;;) {
        int key = read_char();
        if (key < 0) {
            output << '\n';
            return std::nullopt;
        }
        if (key == 27) {
            const int next = read_char();
            if (next != '[') {
                continue;
            }
            const int code = read_char();
            if (code == 'A') {
                if (history_index > 0) {
                    if (history_index == history.size()) {
                        saved = buffer;
                    }
                    --history_index;
                    buffer = history[history_index];
                    cursor = buffer.size();
                }
            } else if (code == 'B') {
                if (history_index < history.size()) {
                    ++history_index;
                    buffer = history_index == history.size()
                                 ? saved
                                 : history[history_index];
                    cursor = buffer.size();
                }
            } else if (code == 'C') {
                if (cursor < buffer.size()) {
                    ++cursor;
                }
            } else if (code == 'D') {
                if (cursor > 0) {
                    --cursor;
                }
            } else if (code == 'H') {
                cursor = 0;
            } else if (code == 'F') {
                cursor = buffer.size();
            } else if (code == '3') {
                read_char();
                if (cursor < buffer.size()) {
                    buffer.erase(cursor, 1);
                }
            }
            redraw(output, prompt, buffer, cursor);
            continue;
        }
        if (key == '\r' || key == '\n') {
            output << '\n';
            return buffer;
        }
        if (key == 3) {
            output << "^C\n";
            return std::string();
        }
        if (key == 4) {
            if (buffer.empty()) {
                output << '\n';
                return std::nullopt;
            }
            continue;
        }
        if (key == 127 || key == 8) {
            if (cursor > 0) {
                buffer.erase(cursor - 1, 1);
                --cursor;
            }
            redraw(output, prompt, buffer, cursor);
            continue;
        }
        if (key == 1) {
            cursor = 0;
            redraw(output, prompt, buffer, cursor);
            continue;
        }
        if (key == 5) {
            cursor = buffer.size();
            redraw(output, prompt, buffer, cursor);
            continue;
        }
        if (key == 11) {
            buffer.erase(cursor);
            redraw(output, prompt, buffer, cursor);
            continue;
        }
        if (key == 21) {
            buffer.erase(0, cursor);
            cursor = 0;
            redraw(output, prompt, buffer, cursor);
            continue;
        }
        if (key >= 32 && key < 127) {
            buffer.insert(buffer.begin() + static_cast<std::ptrdiff_t>(cursor),
                          static_cast<char>(key));
            ++cursor;
            redraw(output, prompt, buffer, cursor);
            continue;
        }
    }
#endif
}

}  // namespace kr16::platform

