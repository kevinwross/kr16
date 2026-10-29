#include "app.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <variant>

#include "help.hpp"

namespace kr16::ui {
namespace {

constexpr Color kBg{22, 24, 29, 255};
constexpr Color kPanel{29, 31, 38, 255};
constexpr Color kPanelAlt{35, 38, 47, 255};
constexpr Color kBorder{52, 56, 68, 255};
constexpr Color kText{214, 218, 226, 255};
constexpr Color kTextDim{118, 124, 140, 255};
constexpr Color kAccent{82, 156, 255, 255};
constexpr Color kAccentDim{44, 74, 120, 255};
constexpr Color kHighlight{38, 64, 110, 255};
constexpr Color kStmt{31, 43, 58, 255};
constexpr Color kRed{230, 90, 90, 255};
constexpr Color kGreen{130, 200, 150, 255};
constexpr Color kAmber{235, 185, 95, 255};
constexpr Color kButton{52, 56, 68, 255};
constexpr Color kButtonHover{68, 74, 90, 255};
constexpr Color kButtonOff{38, 41, 50, 255};

constexpr int kPad = 8;
constexpr int kTopBar = 36;
constexpr int kHeader = 30;
constexpr int kBottom = 150;
constexpr int kStatus = 24;
constexpr int kGutter = 50;
constexpr int kAsmGutter = 90;
constexpr int kAsmBytes = 128;
constexpr int kRegH = 74;
constexpr int kColumnHeader = 18;

// 24x24 monochrome mask derived from the supplied settings gear icon.
constexpr std::uint32_t kGearIcon[24] = {
    0x003C00, 0x006600, 0x0E4660, 0x0FC7F0, 0x310098, 0x30000C,
    0x300018, 0x187E10, 0x10C308, 0x71818E, 0xC10083, 0x810081,
    0x810081, 0xF1018F, 0x71830E, 0x10C718, 0x187C10, 0x300008,
    0x30000C, 0x1B0198, 0x0EC770, 0x044620, 0x006600, 0x003C00,
};

std::filesystem::path settings_path() {
    const char* appdata = std::getenv("APPDATA");
    if (appdata != nullptr && *appdata != 0) {
        return std::filesystem::path(appdata) / "KR16" / "settings.ini";
    }
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg != nullptr && *xdg != 0) {
        return std::filesystem::path(xdg) / "kr16" / "settings.ini";
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != 0) {
        return std::filesystem::path(home) / ".config" / "kr16" /
               "settings.ini";
    }
    return std::filesystem::current_path() / "kr16-settings.ini";
}

void read_settings(SessionBuildOptions& settings) {
    std::ifstream input(settings_path());
    std::string line;
    while (std::getline(input, line)) {
        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, equals);
        const bool enabled = line.substr(equals + 1) != "0";
        if (key == "optimize") settings.optimize = enabled;
        if (key == "debug") settings.debug = enabled;
        if (key == "libc") settings.link_libc = enabled;
        if (key == "listing") settings.listing = enabled;
    }
}

void write_settings(const SessionBuildOptions& settings) {
    const auto path = settings_path();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream output(path, std::ios::trunc);
    if (!output.good()) {
        return;
    }
    output << "optimize=" << (settings.optimize ? 1 : 0) << '\n';
    output << "debug=" << (settings.debug ? 1 : 0) << '\n';
    output << "libc=" << (settings.link_libc ? 1 : 0) << '\n';
    output << "listing=" << (settings.listing ? 1 : 0) << '\n';
}

std::string decimal_with_commas(std::uint64_t value) {
    const std::string digits = std::to_string(value);
    std::string result;
    int count = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (count != 0 && count % 3 == 0) {
            result.push_back(',');
        }
        result.push_back(*it);
        ++count;
    }
    std::reverse(result.begin(), result.end());
    return result;
}

std::string format_duration(std::uint64_t milliseconds) {
    const std::uint64_t total_seconds = milliseconds / 1000;
    std::ostringstream out;
    out << std::setfill('0') << std::setw(2) << (total_seconds / 3600)
        << ':' << std::setw(2) << ((total_seconds % 3600) / 60) << ':'
        << std::setw(2) << (total_seconds % 60) << ':' << std::setw(3)
        << (milliseconds % 1000);
    return out.str();
}

std::string format_rate(double rate) {
    std::ostringstream out;
    if (rate >= 1.0e9) {
        out << std::fixed << std::setprecision(2) << (rate / 1.0e9) << 'G';
    } else if (rate >= 1.0e6) {
        out << std::fixed << std::setprecision(2) << (rate / 1.0e6) << 'M';
    } else if (rate >= 1.0e3) {
        out << std::fixed << std::setprecision(1) << (rate / 1.0e3) << 'K';
    } else {
        out << std::fixed << std::setprecision(0) << rate;
    }
    return out.str();
}

std::string hex4(std::uint16_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << std::setfill('0')
        << std::setw(4) << value;
    return out.str();
}

void fill(Canvas& canvas, const Rect& rect, Color color) {
    canvas.fill_rect(rect, color);
}

void frame(Canvas& canvas, const Rect& rect, Color color) {
    canvas.frame_rect(rect, color);
}

void text(Canvas& canvas, int x, int y, const std::string& value,
          Color color, FontRole role = FontRole::Ui) {
    canvas.text(x, y, value, color, role);
}

void text_right(Canvas& canvas, int right, int y, const std::string& value,
                Color color, FontRole role = FontRole::Ui) {
    const Size size = canvas.measure(value, role);
    canvas.text(right - size.width, y, value, color, role);
}

int hex_digit(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

}  // namespace

App::App(Window& window, DebugSession& session,
         std::filesystem::path libc_source)
    : window_(window),
      session_(session),
      libc_source_(std::move(libc_source)) {
    load_settings();
    layout();
}

void App::layout() {
    const int width = size_.width;
    const int height = size_.height;
    top_bar_ = Rect{kPad, kPad, width - kPad, kPad + kTopBar};
    status_ = Rect{0, height - kStatus, width, height};
    const int content_top = kPad + kTopBar + kPad;
    const int content_bottom = height - kStatus - kBottom - kPad;
    const int middle = kPad + (width - 3 * kPad) / 2;

    source_pane_ = Rect{kPad, content_top, middle, content_bottom};
    asm_pane_ = Rect{middle + kPad, content_top, width - kPad,
                     content_bottom};
    source_text_ = Rect{source_pane_.left + 1, source_pane_.top + kHeader,
                        source_pane_.right - 1,
                        source_pane_.bottom - kRegH};
    memory_ = Rect{source_pane_.left + 1, source_pane_.bottom - kRegH,
                   source_pane_.right - 1, source_pane_.bottom - 1};
    memory_address_rect_ = Rect{memory_.left + 120, memory_.top + 3,
                                memory_.left + 220, memory_.top + 21};
    memory_up_rect_ = Rect{memory_.right - 54, memory_.top + 3,
                           memory_.right - 34, memory_.top + 21};
    memory_down_rect_ = Rect{memory_.right - 30, memory_.top + 3,
                             memory_.right - 10, memory_.top + 21};
    asm_text_ = Rect{asm_pane_.left + 1,
                     asm_pane_.top + kHeader + kColumnHeader,
                     asm_pane_.right - 1, asm_pane_.bottom - kRegH};
    registers_ = Rect{asm_pane_.left + 1, asm_pane_.bottom - kRegH,
                      asm_pane_.right - 1, asm_pane_.bottom - 1};
    reg_cell_width_ = std::max(100, (registers_.width() - 190) / 4);
    for (int i = 0; i < 8; ++i) {
        const int column = i % 4;
        const int row = i / 4;
        const int x = registers_.left + 10 + column * reg_cell_width_;
        const int y = registers_.top + 22 + row * 24;
        register_rects_[i] = Rect{x - 2, y - 2,
                                  x + reg_cell_width_ - 6, y + 18};
    }
    const int flags_x = registers_.right - 168;
    for (int i = 0; i < 5; ++i) {
        const int x = flags_x + 54 + i * 20;
        flag_rects_[i] =
            Rect{x, registers_.top + 3, x + 16, registers_.top + 19};
    }
    bottom_ = Rect{kPad, content_bottom + kPad, width - kPad,
                   height - kStatus - kPad};
    const int bottom_width = bottom_.width();
    variables_ = Rect{bottom_.left, bottom_.top,
                      bottom_.left + bottom_width * 38 / 100, bottom_.bottom};
    stack_ = Rect{variables_.right + kPad, bottom_.top,
                  bottom_.left + bottom_width * 66 / 100, bottom_.bottom};
    console_ = Rect{stack_.right + kPad, bottom_.top, bottom_.right,
                    bottom_.bottom};

    settings_rect_ = Rect{top_bar_.right - 142, top_bar_.top + 4,
                          top_bar_.right - 114, top_bar_.top + 32};
    settings_modal_ = Rect{std::max(kPad, (width - 520) / 2),
                           std::max(kPad, (height - 330) / 2),
                           std::max(kPad, (width - 520) / 2) + 520,
                           std::max(kPad, (height - 330) / 2) + 330};
    settings_close_ = Rect{settings_modal_.right - 34,
                           settings_modal_.top + 8,
                           settings_modal_.right - 10,
                           settings_modal_.top + 32};
    settings_opt0_ = Rect{settings_modal_.left + 220,
                          settings_modal_.top + 58,
                          settings_modal_.left + 276,
                          settings_modal_.top + 82};
    settings_opt1_ = Rect{settings_modal_.left + 286,
                          settings_modal_.top + 58,
                          settings_modal_.left + 342,
                          settings_modal_.top + 82};
    settings_debug_ = Rect{settings_modal_.left + 220,
                           settings_modal_.top + 98,
                           settings_modal_.left + 238,
                           settings_modal_.top + 116};
    settings_libc_ = Rect{settings_modal_.left + 220,
                          settings_modal_.top + 134,
                          settings_modal_.left + 238,
                          settings_modal_.top + 152};
    settings_listing_ = Rect{settings_modal_.left + 220,
                             settings_modal_.top + 170,
                             settings_modal_.left + 238,
                             settings_modal_.top + 188};

    buttons_.clear();
    int x = top_bar_.left + 8;
    const int y = top_bar_.top + 4;
    const bool busy = session_.running();
    const bool can_step =
        session_.loaded() && !session_.halted() && !busy;
    auto add = [&](Action action, const char* label, int width,
                   bool primary, bool enabled) {
        Button button;
        button.rect = Rect{x, y, x + width, y + 28};
        button.label = label;
        button.action = action;
        button.primary = primary;
        button.enabled = enabled;
        buttons_.push_back(std::move(button));
        x += width + 8;
    };
    add(Action::Load, "Load...", 88, false, !busy);
    add(Action::Run, "Run", 72, true, can_step);
    add(Action::Break, "Break", 74, false, busy);
    add(Action::Restart, "Restart", 88, false,
        session_.loaded() && !busy);
    add(Action::Reload, "Reload", 88, false,
        session_.loaded() && !busy);
    step_reset_rect_ = Rect{x, y, x + 118, y + 28};
    Button reset;
    reset.rect = Rect{top_bar_.right - 100, y, top_bar_.right - 12, y + 28};
    reset.label = "Reset stats";
    reset.action = Action::ResetStats;
    reset.enabled = true;
    buttons_.push_back(std::move(reset));

    auto add_pane = [&](Action action, const char* label, int left, int top,
                        int width, int group) {
        Button button;
        button.rect = Rect{left, top, left + width, top + 24};
        button.label = label;
        button.action = action;
        button.enabled = can_step;
        button.group = group;
        buttons_.push_back(std::move(button));
    };
    int pane_x = source_pane_.right - 184;
    const int pane_y = source_pane_.top + 3;
    add_pane(Action::CStepOver, "Over", pane_x, pane_y, 62, 1);
    pane_x += 70;
    add_pane(Action::CStepIn, "In", pane_x, pane_y, 46, 1);
    pane_x += 54;
    add_pane(Action::CStepOut, "Out", pane_x, pane_y, 52, 1);

    pane_x = asm_pane_.right - 184;
    add_pane(Action::AStepOver, "Over", pane_x, pane_y, 62, 2);
    pane_x += 70;
    add_pane(Action::AStepIn, "In", pane_x, pane_y, 46, 2);
    pane_x += 54;
    add_pane(Action::AStepOut, "Out", pane_x, pane_y, 52, 2);
}

void App::paint(Canvas& canvas) {
    fill(canvas, Rect{0, 0, size_.width, size_.height}, kBg);
    draw_top(canvas);
    draw_source(canvas);
    draw_memory(canvas);
    draw_asm(canvas);
    draw_registers(canvas);
    draw_bottom(canvas);
    draw_status(canvas);
    draw_memory_tooltip(canvas);
    draw_hover_tooltip(canvas);
    draw_var_tooltip(canvas);
    draw_asm_tooltip(canvas);
    if (settings_open_) {
        draw_settings(canvas);
    }
}

void App::draw_top(Canvas& canvas) {
    for (const auto& button : buttons_) {
        if (button.group == 0) {
            draw_button(canvas, button);
        }
    }

    fill(canvas, settings_rect_,
         hover_settings_ ? kButtonHover : kButton);
    frame(canvas, settings_rect_, hover_settings_ ? kAccent : kBorder);
    const int gear_x = settings_rect_.left + settings_rect_.width() / 2;
    const int gear_y = settings_rect_.top + settings_rect_.height() / 2;
    const int gear_left = gear_x - 12;
    const int gear_top = gear_y - 12;
    const Color gear_color = hover_settings_ ? kAccent : kText;
    for (int row = 0; row < 24; ++row) {
        for (int column = 0; column < 24; ++column) {
            if ((kGearIcon[row] & (1u << (23 - column))) != 0) {
                fill(canvas, Rect{gear_left + column, gear_top + row,
                                  gear_left + column + 1, gear_top + row + 1},
                     gear_color);
            }
        }
    }

    fill(canvas, step_reset_rect_, kPanelAlt);
    frame(canvas, step_reset_rect_,
          hover_step_reset_ ? kAccent : kBorder);
    const Rect step_box{step_reset_rect_.left + 8,
                        step_reset_rect_.top + 7,
                        step_reset_rect_.left + 22,
                        step_reset_rect_.top + 21};
    fill(canvas, step_box, kPanel);
    frame(canvas, step_box, step_at_reset_ ? kAccent : kBorder);
    if (step_at_reset_) {
        fill(canvas, Rect{step_box.left + 4, step_box.top + 4,
                          step_box.right - 4, step_box.bottom - 4},
             kAccent);
    }
    text(canvas, step_reset_rect_.left + 28, step_reset_rect_.top + 5,
         "Step @ Reset", step_at_reset_ ? kText : kTextDim,
         FontRole::Ui);

    const int y = top_bar_.top + 9;
    int x = step_reset_rect_.right + 12;
    const int reset_left = top_bar_.right - 112;
    const std::uint64_t elapsed = session_.run_time_ms();
    text(canvas, x, y, "INS", kTextDim, FontRole::MonoSmall);
    text(canvas, x + 30, y,
         decimal_with_commas(session_.instructions_executed()), kText,
         FontRole::MonoSmall);
    x += 200;
    if (x + 140 < reset_left) {
        text(canvas, x, y, "TIME", kTextDim, FontRole::MonoSmall);
        text(canvas, x + 42, y, format_duration(elapsed), kText,
             FontRole::MonoSmall);
    }
    x += 165;
    if (x + 100 < reset_left) {
        text(canvas, x, y, "IPS", kTextDim, FontRole::MonoSmall);
        std::string rate = "-";
        if (session_.instructions_executed() == 0) {
            rate = "0";
        } else if (elapsed > 0) {
            rate = format_rate(
                static_cast<double>(session_.instructions_executed()) *
                1000.0 / static_cast<double>(elapsed));
        }
        text(canvas, x + 30, y, rate, kText, FontRole::MonoSmall);
    }
    fill(canvas, Rect{top_bar_.left, top_bar_.bottom - 1, top_bar_.right,
                      top_bar_.bottom},
         kBorder);
}

void App::draw_hover_tooltip(Canvas& canvas) {
    if (editing_register_ >= 0) {
        return;
    }
    std::vector<std::string> lines;
    if (hover_register_ >= 0) {
        static const char* names[] = {"R0", "R1", "R2", "R3",
                                      "R4", "PC", "SP", "SR"};
        const std::uint16_t value = session_.reg(hover_register_);
        lines.push_back(names[hover_register_]);
        lines.push_back(hex4(value));
        std::string decimal = std::to_string(static_cast<unsigned>(value));
        const int signed_value = static_cast<std::int16_t>(value);
        if (signed_value < 0) {
            decimal += "  (" + std::to_string(signed_value) + ")";
        }
        lines.push_back(decimal);
    } else if (hover_flag_ >= 0) {
        static const char* names[] = {"Carry (C)", "Zero (Z)",
                                      "Negative (N)", "Overflow (V)",
                                      "Interrupt (I)"};
        static const std::uint16_t bits[] = {0x01, 0x02, 0x04, 0x08, 0x10};
        lines.push_back(names[hover_flag_]);
        lines.push_back((session_.sr() & bits[hover_flag_]) != 0 ? "set"
                                                                 : "clear");
    } else {
        return;
    }

    int width = 0;
    for (const auto& line : lines) {
        width = std::max(width, canvas.measure(line, FontRole::MonoSmall).width);
    }
    width += 20;
    const int height = 8 + static_cast<int>(lines.size()) * 18;
    int x = mouse_.x + 16;
    int y = mouse_.y - height - 12;
    if (y < 4) {
        y = mouse_.y + 22;
    }
    if (x + width > size_.width - 4) {
        x = size_.width - width - 4;
    }
    x = std::max(4, x);
    y = std::min(y, size_.height - height - 4);
    y = std::max(4, y);
    fill(canvas, Rect{x, y, x + width, y + height}, kPanelAlt);
    frame(canvas, Rect{x, y, x + width, y + height}, kAccent);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        text(canvas, x + 10, y + 5 + static_cast<int>(i) * 18, lines[i],
             i == 2 ? kTextDim : kText, FontRole::MonoSmall);
    }
}

void App::draw_var_tooltip(Canvas& canvas) {
    if (hover_var_ < 0 || hover_var_ >= static_cast<int>(var_hits_.size()) ||
        editing_var_ >= 0) {
        return;
    }
    const VarHit& hit = var_hits_[hover_var_];
    std::vector<std::string> lines;
    lines.push_back(hit.name + (hit.local ? "  (local)" : "  (global)"));
    if (!hit.ok) {
        lines.push_back("unavailable");
    } else if (hit.size > 2) {
        lines.push_back("aggregate");
        lines.push_back(std::to_string(hit.size) + " bytes");
    } else {
        std::ostringstream hex;
        hex << "0x" << std::hex << std::uppercase << std::setfill('0')
            << std::setw(hit.size == 1 ? 2 : 4)
            << static_cast<unsigned>(hit.size == 1 ? (hit.value & 0xFF)
                                                   : hit.value);
        lines.push_back(hex.str());
        const std::uint16_t value =
            hit.size == 1 ? static_cast<std::uint16_t>(hit.value & 0xFF)
                          : hit.value;
        std::string decimal = std::to_string(static_cast<unsigned>(value));
        const int signed_value = static_cast<std::int16_t>(value);
        if (signed_value < 0) {
            decimal += "  (" + std::to_string(signed_value) + ")";
        }
        lines.push_back(decimal);
    }
    int width = 0;
    for (const auto& line : lines) {
        width = std::max(width, canvas.measure(line, FontRole::MonoSmall).width);
    }
    width += 20;
    const int height = 8 + static_cast<int>(lines.size()) * 18;
    int x = mouse_.x + 16;
    int y = mouse_.y - height - 12;
    if (y < 4) {
        y = mouse_.y + 22;
    }
    if (x + width > size_.width - 4) {
        x = size_.width - width - 4;
    }
    x = std::max(4, x);
    y = std::min(y, size_.height - height - 4);
    y = std::max(4, y);
    fill(canvas, Rect{x, y, x + width, y + height}, kPanelAlt);
    frame(canvas, Rect{x, y, x + width, y + height}, kAccent);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        text(canvas, x + 10, y + 5 + static_cast<int>(i) * 18, lines[i],
             i == 2 ? kTextDim : kText, FontRole::MonoSmall);
    }
}

void App::draw_asm_tooltip(Canvas& canvas) {
    if (hover_asm_index_ < 0 ||
        hover_asm_index_ >= static_cast<int>(session_.disassembly().size())) {
        return;
    }
    const auto& instruction =
        session_.disassembly()[static_cast<std::size_t>(hover_asm_index_)];
    const std::size_t split = instruction.text.find(' ');
    const std::string mnemonic =
        split == std::string::npos ? instruction.text
                                   : instruction.text.substr(0, split);
    const std::string title =
        hover_asm_operand_ ? "Addressing mode" : mnemonic;
    const std::string body =
        hover_asm_operand_ ? addressing_help(instruction.text, mnemonic)
                           : instruction_help(mnemonic);

    std::vector<std::string> lines;
    lines.push_back(title);
    const int max_width = 360;
    std::string line;
    std::size_t begin = 0;
    while (begin < body.size()) {
        const std::size_t end = body.find(' ', begin);
        const std::string word =
            body.substr(begin, end == std::string::npos ? std::string::npos
                                                        : end - begin);
        if (!word.empty()) {
            const std::string candidate =
                line.empty() ? word : line + " " + word;
            if (!line.empty() &&
                canvas.measure(candidate, FontRole::MonoSmall).width >
                    max_width) {
                lines.push_back(line);
                line = word;
            } else {
                line = candidate;
            }
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    if (lines.size() == 1) {
        lines.push_back("No description available.");
    }

    int width = 0;
    for (const auto& value : lines) {
        width = std::max(
            width, canvas.measure(value, FontRole::MonoSmall).width);
    }
    width += 20;
    const int height = 8 + static_cast<int>(lines.size()) * 18;
    int x = mouse_.x + 16;
    int y = mouse_.y - height - 12;
    if (y < 4) {
        y = mouse_.y + 22;
    }
    if (x + width > size_.width - 4) {
        x = size_.width - width - 4;
    }
    x = std::max(4, x);
    y = std::min(y, size_.height - height - 4);
    y = std::max(4, y);
    fill(canvas, Rect{x, y, x + width, y + height}, kPanelAlt);
    frame(canvas, Rect{x, y, x + width, y + height}, kAccent);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        text(canvas, x + 10, y + 5 + static_cast<int>(i) * 18, lines[i],
             i == 0 ? kText : kTextDim, FontRole::MonoSmall);
    }
}

void App::draw_button(Canvas& canvas, const Button& button) {
    Color background = kButton;
    if (!button.enabled) {
        background = kButtonOff;
    } else if (hover_button_ >= 0 &&
               static_cast<std::size_t>(hover_button_) < buttons_.size() &&
               &buttons_[static_cast<std::size_t>(hover_button_)] ==
                   &button) {
        background = kButtonHover;
    }
    fill(canvas, button.rect, background);
    frame(canvas, button.rect, button.primary ? kAccent : kBorder);
    const Size measured = canvas.measure(button.label, FontRole::Ui);
    text(canvas, button.rect.left + (button.rect.width() - measured.width) / 2,
         button.rect.top + (button.rect.height() - measured.height) / 2,
         button.label, button.enabled ? kText : kTextDim, FontRole::Ui);
}

void App::draw_source(Canvas& canvas) {
    fill(canvas, source_pane_, kPanel);
    frame(canvas, source_pane_, kBorder);
    fill(canvas, Rect{source_pane_.left + 1, source_pane_.top + 1,
                      source_pane_.right - 1, source_pane_.top + kHeader},
         kPanelAlt);
    text(canvas, source_pane_.left + 10, source_pane_.top + 5,
         "C source  (line steps)", kAccent, FontRole::UiBold);
    for (const auto& button : buttons_) {
        if (button.group == 1) {
            draw_button(canvas, button);
        }
    }
    fill(canvas, Rect{source_pane_.left + 1, source_pane_.top + kHeader,
                      source_pane_.right - 1, source_pane_.top + kHeader + 1},
         kBorder);

    fill(canvas, source_text_, kPanel);
    if (!session_.loaded()) {
        text(canvas, source_text_.left + 16, source_text_.top + 16,
             "Load a .kex, .c, .s, .asm or .o file to begin.", kTextDim,
             FontRole::MonoSmall);
        return;
    }
    const auto& lines = session_.source_lines();
    if (lines.empty()) {
        text(canvas, source_text_.left + 16, source_text_.top + 16,
             "No source file is attached to this image.", kTextDim,
             FontRole::MonoSmall);
        return;
    }

    const int line_height = 20;
    const int visible =
        std::max(1, (source_text_.height() - 4) / line_height);
    const int current_line = session_.line_at(session_.pc());
    if (current_line > 0) {
        if (current_line - 1 < source_scroll_) {
            source_scroll_ = std::max(0, current_line - 4);
        } else if (current_line - 1 >= source_scroll_ + visible) {
            source_scroll_ = current_line - visible + 3;
        }
    }
    source_scroll_ = std::clamp(
        source_scroll_, 0,
        std::max(0, static_cast<int>(lines.size()) - visible));

    canvas.push_clip(source_text_);
    for (int row = 0; row < visible; ++row) {
        const int index = source_scroll_ + row;
        if (index >= static_cast<int>(lines.size())) {
            break;
        }
        const int y = source_text_.top + 2 + row * line_height;
        const bool current = index + 1 == current_line;
        if (current) {
            fill(canvas, Rect{source_text_.left, y, source_text_.right,
                              y + line_height},
                 kHighlight);
            fill(canvas, Rect{source_text_.left, y, source_text_.left + 3,
                              y + line_height},
                 kAccent);
        }
        text_right(canvas, source_text_.left + kGutter - 8, y + 1,
                   std::to_string(index + 1),
                   current ? kAccent : kTextDim, FontRole::Mono);
        if (session_.has_line_breakpoint(index + 1)) {
            fill(canvas, Rect{source_text_.left + 7, y + 7,
                              source_text_.left + 13, y + 13},
                 kRed);
        }
        text(canvas, source_text_.left + kGutter + 4, y + 1, lines[index].text,
             lines[index].executable ? kText : kTextDim, FontRole::Mono);
    }
    canvas.pop_clip();
}

void App::draw_memory(Canvas& canvas) {
    fill(canvas, memory_, kPanelAlt);
    fill(canvas, Rect{memory_.left, memory_.top, memory_.right,
                      memory_.top + 1},
         kBorder);
    text(canvas, memory_.left + 10, memory_.top + 3, "Memory", kAccent,
         FontRole::UiBold);
    text_right(canvas, memory_address_rect_.left - 8, memory_.top + 4, "ADDR",
               kTextDim, FontRole::MonoSmall);

    fill(canvas, memory_address_rect_,
         memory_address_editing_ ? kHighlight : kPanel);
    frame(canvas, memory_address_rect_,
          memory_address_editing_ ? kAccent : kBorder);
    if (memory_address_editing_) {
        text(canvas, memory_address_rect_.left + 5, memory_.top + 4, "0x",
             kTextDim, FontRole::MonoSmall);
        text(canvas, memory_address_rect_.left + 23, memory_.top + 4,
             edit_text_, kText, FontRole::MonoSmall);
        const int caret_x =
            memory_address_rect_.left + 23 +
            static_cast<int>(edit_text_.size()) * 8;
        fill(canvas, Rect{caret_x, memory_.top + 5, caret_x + 1,
                          memory_.top + 19},
             kAccent);
    } else {
        text(canvas, memory_address_rect_.left + 5, memory_.top + 4,
             hex4(memory_address_), kText, FontRole::MonoSmall);
    }

    const Rect arrows[2] = {memory_up_rect_, memory_down_rect_};
    const char* arrow_labels[2] = {"^", "v"};
    for (int i = 0; i < 2; ++i) {
        fill(canvas, arrows[i], kButton);
        frame(canvas, arrows[i], kBorder);
        const Size measured = canvas.measure(arrow_labels[i],
                                             FontRole::MonoSmall);
        text(canvas,
             arrows[i].left + (arrows[i].width() - measured.width) / 2,
             arrows[i].top + (arrows[i].height() - measured.height) / 2,
             arrow_labels[i], kText, FontRole::MonoSmall);
    }

    memory_bytes_.clear();
    if (!session_.loaded()) {
        return;
    }
    const Size digit = canvas.measure("0", FontRole::MonoSmall);
    const int char_width = std::max(4, digit.width);
    const int byte_x = memory_.left + 10 + 8 * char_width;
    const int ascii_x = byte_x + 16 * 3 * char_width + 2 * char_width;
    for (int row = 0; row < 2; ++row) {
        const std::uint16_t base = static_cast<std::uint16_t>(
            memory_address_ + static_cast<std::uint16_t>(row * 16));
        const int y = memory_.top + 26 + row * 18;
        std::ostringstream prefix;
        prefix << "0x" << std::hex << std::uppercase << std::setfill('0')
               << std::setw(4) << base << ':';
        text(canvas, memory_.left + 10, y, prefix.str(), kTextDim,
             FontRole::MonoSmall);

        std::string ascii;
        for (int i = 0; i < 16; ++i) {
            const std::uint16_t address = static_cast<std::uint16_t>(
                base + static_cast<std::uint16_t>(i));
            const std::uint8_t value = session_.read8(address);
            const int x = byte_x + i * 3 * char_width;
            const Rect cell{x, y, x + 2 * char_width, y + 18};
            memory_bytes_.push_back(MemoryByte{cell, address});
            const int index = row * 16 + i;
            const bool editing = memory_byte_editing_ == index;
            const bool hovered = hover_memory_byte_ == index;
            if (editing) {
                fill(canvas, cell, kHighlight);
                frame(canvas, cell, kAccent);
            } else if (hovered) {
                fill(canvas, cell, kStmt);
                frame(canvas, cell, kAccent);
            }
            if (editing) {
                text(canvas, x, y, edit_text_, kText, FontRole::MonoSmall);
            } else {
                std::ostringstream byte;
                byte << std::hex << std::uppercase << std::setfill('0')
                     << std::setw(2) << static_cast<unsigned>(value);
                text(canvas, x, y, byte.str(), kText, FontRole::MonoSmall);
            }
            ascii.push_back(value >= 32 && value < 127
                                ? static_cast<char>(value)
                                : '.');
        }
        text(canvas, ascii_x, y, ascii, kTextDim, FontRole::MonoSmall);
    }
}

void App::draw_memory_tooltip(Canvas& canvas) {
    if (hover_memory_byte_ < 0 || editing_memory() || !session_.loaded()) {
        return;
    }
    const std::uint8_t value = session_.read8(hover_memory_address_);
    std::ostringstream hex;
    hex << "0x" << std::hex << std::uppercase << std::setfill('0')
        << std::setw(2) << static_cast<unsigned>(value);
    std::string ascii;
    if (value >= 32 && value < 127) {
        ascii = "  '";
        ascii.push_back(static_cast<char>(value));
        ascii.push_back('\'');
    }
    const std::string lines[3] = {
        "Byte " + hex4(hover_memory_address_), hex.str() + ascii,
        std::to_string(static_cast<unsigned>(value))};
    int width = 0;
    for (const auto& line : lines) {
        width = std::max(width, canvas.measure(line, FontRole::MonoSmall).width);
    }
    width += 20;
    const int height = 8 + 3 * 18;
    int x = mouse_.x + 16;
    int y = mouse_.y - height - 12;
    if (y < 4) {
        y = mouse_.y + 22;
    }
    if (x + width > size_.width - 4) {
        x = size_.width - width - 4;
    }
    x = std::max(4, x);
    y = std::min(y, size_.height - height - 4);
    y = std::max(4, y);
    fill(canvas, Rect{x, y, x + width, y + height}, kPanelAlt);
    frame(canvas, Rect{x, y, x + width, y + height}, kAccent);
    for (int i = 0; i < 3; ++i) {
        text(canvas, x + 10, y + 5 + i * 18, lines[i],
             i == 1 ? kTextDim : kText, FontRole::MonoSmall);
    }
}

void App::draw_asm(Canvas& canvas) {
    fill(canvas, asm_pane_, kPanel);
    frame(canvas, asm_pane_, kBorder);
    fill(canvas, Rect{asm_pane_.left + 1, asm_pane_.top + 1,
                      asm_pane_.right - 1, asm_pane_.top + kHeader},
         kPanelAlt);
    text(canvas, asm_pane_.left + 10, asm_pane_.top + 5,
         "Disassembly  (instruction steps)", kAccent, FontRole::UiBold);
    for (const auto& button : buttons_) {
        if (button.group == 2) {
            draw_button(canvas, button);
        }
    }
    fill(canvas, Rect{asm_pane_.left + 1, asm_pane_.top + kHeader,
                      asm_pane_.right - 1, asm_pane_.top + kHeader + 1},
         kBorder);

    const Rect columns{asm_pane_.left + 1, asm_pane_.top + kHeader,
                       asm_pane_.right - 1, asm_text_.top};
    fill(canvas, columns, kPanelAlt);
    text_right(canvas, columns.left + kAsmGutter - 8, columns.top + 2, "ADDR",
               kTextDim, FontRole::MonoSmall);
    text(canvas, columns.left + kAsmGutter + 8, columns.top + 2, "BYTES",
         kTextDim, FontRole::MonoSmall);
    text(canvas, columns.left + kAsmGutter + kAsmBytes + 8, columns.top + 2,
         "INSTRUCTION", kTextDim, FontRole::MonoSmall);
    fill(canvas, Rect{columns.left, columns.bottom - 1, columns.right,
                      columns.bottom},
         kBorder);

    fill(canvas, asm_text_, kPanel);
    const auto& instructions = session_.disassembly();
    if (instructions.empty()) {
        text(canvas, asm_text_.left + 16, asm_text_.top + 16,
             "No instructions loaded.", kTextDim, FontRole::MonoSmall);
        return;
    }

    const int line_height = 20;
    const int visible = std::max(1, (asm_text_.height() - 2) / line_height);
    int pc_index = -1;
    for (std::size_t i = 0; i < instructions.size(); ++i) {
        if (instructions[i].address == session_.pc()) {
            pc_index = static_cast<int>(i);
            break;
        }
    }
    if (pc_index >= 0) {
        if (pc_index < asm_scroll_) {
            asm_scroll_ = std::max(0, pc_index - 3);
        } else if (pc_index >= asm_scroll_ + visible) {
            asm_scroll_ = pc_index - visible + 3;
        }
    }
    asm_scroll_ = std::clamp(
        asm_scroll_, 0,
        std::max(0, static_cast<int>(instructions.size()) - visible));

    asm_rows_.clear();
    canvas.push_clip(asm_text_);
    for (int row = 0; row < visible; ++row) {
        const int index = asm_scroll_ + row;
        if (index >= static_cast<int>(instructions.size())) {
            break;
        }
        const auto& instruction = instructions[index];
        const int y = asm_text_.top + row * line_height;
        const bool current = instruction.address == session_.pc();
        const Rect line_rect{asm_text_.left, y, asm_text_.right,
                             y + line_height};
        const int text_x = asm_text_.left + kAsmGutter + kAsmBytes + 8;
        const std::size_t split = instruction.text.find(' ');
        const std::string mnemonic =
            split == std::string::npos ? instruction.text
                                       : instruction.text.substr(0, split);
        const Size mnemonic_size =
            canvas.measure(mnemonic, FontRole::Mono);
        const Size text_size =
            canvas.measure(instruction.text, FontRole::Mono);
        const Size space_size = canvas.measure(" ", FontRole::Mono);
        const Rect mnemonic_rect{text_x, y,
                                 text_x + mnemonic_size.width,
                                 y + line_height};
        Rect operand_rect{};
        if (split != std::string::npos) {
            operand_rect = Rect{text_x + mnemonic_size.width +
                                    space_size.width,
                                y, text_x + text_size.width,
                                y + line_height};
        }
        if (current) {
            fill(canvas, line_rect, kHighlight);
            fill(canvas, Rect{line_rect.left, line_rect.top,
                              line_rect.left + 3, line_rect.bottom},
                 kAccent);
        }
        if (hover_asm_index_ == index) {
            const Rect& highlighted =
                hover_asm_operand_ ? operand_rect : mnemonic_rect;
            if (!highlighted.empty()) {
                fill(canvas, highlighted, kStmt);
                frame(canvas, highlighted, kAccent);
            }
        }
        asm_rows_.push_back(
            AsmRow{instruction.address, line_rect, mnemonic_rect,
                   operand_rect});
        text_right(canvas, asm_text_.left + kAsmGutter - 8, y + 1,
                   hex4(instruction.address), kTextDim, FontRole::Mono);
        std::string bytes;
        for (std::size_t b = 0; b < instruction.bytes.size(); ++b) {
            if (b != 0) {
                bytes.push_back(' ');
            }
            std::ostringstream byte;
            byte << std::hex << std::uppercase << std::setfill('0')
                 << std::setw(2)
                 << static_cast<unsigned>(instruction.bytes[b]);
            bytes += byte.str();
        }
        text(canvas, asm_text_.left + kAsmGutter + 8, y + 1, bytes,
             current ? kText : kTextDim, FontRole::Mono);
        text(canvas, text_x, y + 1, instruction.text,
             current ? kText : kTextDim, FontRole::Mono);
        if (session_.has_breakpoint(instruction.address)) {
            fill(canvas, Rect{asm_text_.left + 12, y + 3,
                              asm_text_.left + 26, y + 17},
                 kRed);
        }
    }
    canvas.pop_clip();
}

void App::draw_registers(Canvas& canvas) {
    fill(canvas, registers_, kPanelAlt);
    fill(canvas, Rect{registers_.left, registers_.top, registers_.right,
                      registers_.top + 1},
         kBorder);
    text(canvas, registers_.left + 10, registers_.top + 3, "Registers",
         kAccent, FontRole::UiBold);

    static const char* names[] = {"R0", "R1", "R2", "R3",
                                  "R4", "PC", "SP", "SR"};
    for (int i = 0; i < 8; ++i) {
        const Rect cell = register_rects_[i];
        const bool editing = editing_register_ == i;
        const bool hovered = hover_register_ == i;
        if (editing) {
            fill(canvas, cell, kHighlight);
            frame(canvas, cell, kAccent);
        } else if (hovered) {
            fill(canvas, cell, kStmt);
            frame(canvas, cell, kAccent);
        }
        const int x = cell.left + 2;
        const int y = cell.top + 2;
        text(canvas, x, y, names[i], kTextDim, FontRole::MonoSmall);
        if (editing) {
            text(canvas, x + 28, y, "0x", kTextDim, FontRole::MonoSmall);
            text(canvas, x + 44, y, edit_text_, kText, FontRole::MonoSmall);
            const int caret_x = x + 44 + static_cast<int>(edit_text_.size()) * 7;
            fill(canvas, Rect{caret_x, cell.top + 3, caret_x + 1,
                              cell.bottom - 3},
                 kAccent);
        } else {
            text(canvas, x + 28, y, hex4(session_.reg(i)), kText,
                 FontRole::MonoSmall);
        }
    }

    const int flags_x = registers_.right - 168;
    text(canvas, flags_x, registers_.top + 4, "FLAGS:", kTextDim,
         FontRole::MonoSmall);
    static const char* flag_names[] = {"C", "Z", "N", "V", "I"};
    const std::uint16_t bits[] = {0x01, 0x02, 0x04, 0x08, 0x10};
    for (int i = 0; i < 5; ++i) {
        const Rect box = flag_rects_[i];
        const bool set = (session_.sr() & bits[i]) != 0;
        fill(canvas, box, set ? kGreen : kPanel);
        frame(canvas, box, hover_flag_ == i ? kAccent : kBorder);
        text(canvas, box.left + 4, box.top + 1, flag_names[i],
             set ? kBg : kTextDim, FontRole::MonoSmall);
    }
}

void App::draw_bottom(Canvas& canvas) {
    auto panel = [&](const Rect& area, const std::string& title) {
        fill(canvas, area, kPanel);
        frame(canvas, area, kBorder);
        text(canvas, area.left + 8, area.top + 5, title, kAccent,
             FontRole::UiBold);
    };
    panel(variables_, "Variables");
    panel(stack_, "Call stack");
    panel(console_, "Console");

    const int line_height = 18;
    const auto scrollbar = [&](const Rect& area, int offset, int total,
                               int visible) {
        if (total <= visible || visible <= 0) {
            return;
        }
        const int top = area.top + 24;
        const int bottom = area.bottom - 4;
        const int track = std::max(1, bottom - top);
        const int thumb = std::max(12, track * visible / total);
        const int max_offset = std::max(1, total - visible);
        const int y = top + (track - thumb) * offset / max_offset;
        fill(canvas, Rect{area.right - 6, top, area.right - 1, bottom},
             kPanelAlt);
        fill(canvas, Rect{area.right - 5, y, area.right - 1, y + thumb},
             kAccent);
    };

    var_hits_.clear();
    const auto locals = session_.locals();
    const auto globals = session_.globals();
    struct VariableRow {
        int kind;  // 0 header, 1 empty, 2 local, 3 spacer, 4 global
        int index;
    };
    std::vector<VariableRow> rows;
    rows.push_back(VariableRow{0, 0});
    if (locals.empty()) {
        rows.push_back(VariableRow{1, -1});
    } else {
        for (std::size_t i = 0; i < locals.size(); ++i) {
            rows.push_back(VariableRow{2, static_cast<int>(i)});
        }
    }
    rows.push_back(VariableRow{3, -1});
    rows.push_back(VariableRow{0, 1});
    for (std::size_t i = 0; i < globals.size(); ++i) {
        rows.push_back(VariableRow{4, static_cast<int>(i)});
    }

    int y = variables_.top + 26;
    const int variable_bottom = variables_.bottom - 4;
    const int variable_visible =
        std::max(0, (variable_bottom - y) / line_height);
    variables_scroll_ = std::clamp(
        variables_scroll_, 0,
        std::max(0, static_cast<int>(rows.size()) - variable_visible));

    auto draw_variable = [&](const SessionVariable& variable) {
        if (y + line_height > variable_bottom) {
            return;
        }
        const int index = static_cast<int>(var_hits_.size());
        const Rect row{variables_.left + 8, y - 2, variables_.right - 12,
                       y + 16};
        var_hits_.push_back(VarHit{row, variable.name, variable.address,
                                   variable.value, variable.size,
                                   variable.local, variable.ok});
        const bool editing = editing_var_ == index;
        const bool hovered = hover_var_ == index;
        if (editing) {
            fill(canvas, row, kHighlight);
            frame(canvas, row, kAccent);
        } else if (hovered) {
            fill(canvas, row, kStmt);
            frame(canvas, row, kAccent);
        }
        text(canvas, variables_.left + 16, y, variable.name, kText,
             FontRole::MonoSmall);
        std::string value;
        if (editing) {
            value = "0x" + edit_text_;
        } else if (variable.size > 2) {
            value = std::to_string(variable.size) + " bytes";
        } else if (!variable.ok) {
            value = "?";
        } else if (variable.size == 1) {
            std::ostringstream byte;
            byte << "0x" << std::hex << std::uppercase << std::setfill('0')
                 << std::setw(2)
                 << static_cast<unsigned>(variable.value & 0xFF);
            value = byte.str();
        } else {
            value = hex4(variable.value);
        }
        text_right(canvas, variables_.right - 12, y, value,
                   editing ? kText : (variable.ok ? kText : kTextDim),
                   FontRole::MonoSmall);
        y += line_height;
    };

    for (std::size_t i = static_cast<std::size_t>(variables_scroll_);
         i < rows.size(); ++i) {
        if (y + line_height > variable_bottom) {
            break;
        }
        const auto& row = rows[i];
        if (row.kind == 0) {
            text(canvas, variables_.left + 8, y,
                 row.index == 0 ? "Locals" : "Globals", kTextDim,
                 FontRole::MonoSmall);
            y += line_height;
        } else if (row.kind == 1) {
            text(canvas, variables_.left + 16, y, "(none)", kTextDim,
                 FontRole::MonoSmall);
            y += line_height;
        } else if (row.kind == 2) {
            draw_variable(locals[static_cast<std::size_t>(row.index)]);
        } else if (row.kind == 3) {
            y += 4;
        } else {
            draw_variable(globals[static_cast<std::size_t>(row.index)]);
        }
    }
    scrollbar(variables_, variables_scroll_,
              static_cast<int>(rows.size()), variable_visible);

    y = stack_.top + 26;
    const auto frames = session_.frames();
    const int stack_bottom = stack_.bottom - 4;
    const int stack_visible =
        std::max(0, (stack_bottom - y) / line_height);
    stack_scroll_ = std::clamp(
        stack_scroll_, 0,
        std::max(0, static_cast<int>(frames.size()) - stack_visible));
    if (frames.empty()) {
        text(canvas, stack_.left + 10, y, "(none)", kTextDim,
             FontRole::MonoSmall);
    } else {
        for (std::size_t i = static_cast<std::size_t>(stack_scroll_);
             i < frames.size(); ++i) {
            if (y + line_height > stack_bottom) {
                break;
            }
            std::string label =
                "#" + std::to_string(i) + "  " + frames[i].name;
            if (frames[i].line > 0) {
                label += "  line " + std::to_string(frames[i].line);
            }
            text(canvas, stack_.left + 10, y, label, kText,
                 FontRole::MonoSmall);
            y += line_height;
        }
    }
    scrollbar(stack_, stack_scroll_, static_cast<int>(frames.size()),
              stack_visible);

    y = console_.top + 26;
    const std::string& output = session_.console();
    std::vector<std::string> lines;
    std::string current;
    for (const char ch : output) {
        if (ch == '\n') {
            lines.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    const int console_bottom = console_.bottom - 4;
    const int console_visible =
        std::max(0, (console_bottom - y) / line_height);
    const int console_max =
        std::max(0, static_cast<int>(lines.size()) - console_visible);
    console_scroll_ = std::clamp(console_scroll_, 0, console_max);
    const int start = console_max - console_scroll_;
    if (lines.empty()) {
        text(canvas, console_.left + 10, y, "(no output)", kTextDim,
             FontRole::MonoSmall);
    } else {
        for (int i = start; i < static_cast<int>(lines.size()); ++i) {
            if (y + line_height > console_bottom) {
                break;
            }
            text(canvas, console_.left + 10, y, lines[i], kText,
                 FontRole::MonoSmall);
            y += line_height;
        }
    }
    scrollbar(console_, start, static_cast<int>(lines.size()),
              console_visible);
}

void App::draw_settings(Canvas& canvas) {
    fill(canvas, settings_modal_, kPanel);
    canvas.frame_rect(settings_modal_, kAccent, 2);
    text(canvas, settings_modal_.left + 18, settings_modal_.top + 10,
         "Settings", kAccent, FontRole::UiBold);

    fill(canvas, settings_close_,
         hover_settings_control_ == 99 ? kButtonHover : kButton);
    frame(canvas, settings_close_,
          hover_settings_control_ == 99 ? kAccent : kBorder);
    text(canvas, settings_close_.left + 7, settings_close_.top + 2, "x",
         kText, FontRole::UiBold);

    const int label_x = settings_modal_.left + 18;
    text(canvas, label_x, settings_modal_.top + 58, "Optimization",
         kText, FontRole::MonoSmall);
    fill(canvas, settings_opt0_,
         settings_.optimize ? kButton : kButtonHover);
    frame(canvas, settings_opt0_,
          hover_settings_control_ == 0 ? kAccent : kBorder);
    text(canvas, settings_opt0_.left + 16, settings_opt0_.top + 3, "O0",
         settings_.optimize ? kTextDim : kText, FontRole::MonoSmall);
    fill(canvas, settings_opt1_,
         settings_.optimize ? kButtonHover : kButton);
    frame(canvas, settings_opt1_,
          hover_settings_control_ == 1 ? kAccent : kBorder);
    text(canvas, settings_opt1_.left + 16, settings_opt1_.top + 3, "O1",
         settings_.optimize ? kText : kTextDim, FontRole::MonoSmall);

    auto checkbox = [&](const Rect& box, bool enabled, int id,
                        const char* label, int y) {
        fill(canvas, box, enabled ? kAccent : kPanel);
        frame(canvas, box,
              hover_settings_control_ == id ? kAccent : kBorder);
        if (enabled) {
            fill(canvas, Rect{box.left + 4, box.top + 4, box.right - 4,
                              box.bottom - 4},
                 kPanel);
        }
        text(canvas, box.right + 12, y, label, kText, FontRole::MonoSmall);
    };
    checkbox(settings_debug_, settings_.debug, 2, "Debug info (-g)",
             settings_modal_.top + 96);
    checkbox(settings_libc_, settings_.link_libc, 3, "Link libc",
             settings_modal_.top + 132);
    checkbox(settings_listing_, settings_.listing, 4, "Write assembler listing",
             settings_modal_.top + 168);

    if (settings_.debug && settings_.optimize) {
        text(canvas, label_x, settings_modal_.top + 216,
             "Debug info keeps frame locals but disables the IR optimizer.",
             kAmber, FontRole::MonoSmall);
    }
    text(canvas, label_x, settings_modal_.top + 244,
         "Changes apply immediately and rebuild the current source.",
         kTextDim, FontRole::MonoSmall);
    text(canvas, label_x, settings_modal_.top + 266,
         "Settings are saved for the next KR16 session.",
         kTextDim, FontRole::MonoSmall);
}

void App::load_settings() {
    read_settings(settings_);
}

void App::save_settings() {
    write_settings(settings_);
}

void App::open_settings() {
    settings_open_ = true;
    hover_settings_control_ = -1;
    window_.invalidate();
}

void App::close_settings() {
    settings_open_ = false;
    hover_settings_control_ = -1;
    window_.invalidate();
}

void App::apply_settings() {
    save_settings();
    if (current_path_.empty()) {
        message_ = "settings saved; no source loaded";
        window_.invalidate();
        return;
    }
    const std::string extension =
        current_path_.extension().string();
    if (extension == ".kex") {
        message_ = "settings apply to compile inputs";
        window_.invalidate();
        return;
    }
    reload_current();
}

void App::reload_current() {
    if (current_path_.empty()) {
        message_ = "no source to rebuild";
        window_.invalidate();
        return;
    }
    if (session_.running()) {
        stop_run("paused");
    }
    std::string error;
    if (!session_.build_and_load(current_path_, libc_source_, settings_,
                                 error)) {
        message_ = "rebuild failed";
        window_.show_error("KR16",
                           error.empty() ? "settings rebuild failed" : error);
        window_.invalidate();
        return;
    }
    source_scroll_ = 0;
    asm_scroll_ = 0;
    variables_scroll_ = 0;
    stack_scroll_ = 0;
    console_scroll_ = 0;
    window_.set_title("KR16 - " + current_path_.filename().string());
    layout();
    if (step_at_reset_) {
        apply_reset_entry();
    }
    message_ = "settings applied; rebuilt";
    window_.invalidate();
}

void App::draw_status(Canvas& canvas) {
    fill(canvas, status_, kPanelAlt);
    fill(canvas, Rect{status_.left, status_.top, status_.right,
                      status_.top + 1},
         kBorder);
    std::string left;
    if (session_.loaded()) {
        const auto* function = session_.function_at(session_.pc());
        left = "pc=" + hex4(session_.pc()) + "  ";
        left += function != nullptr ? function->name : "<program>";
        left += "  line " + std::to_string(session_.line_at(session_.pc()));
        left += "  instr " +
                std::to_string(session_.instructions_executed());
        left += "  time " + format_duration(session_.run_time_ms());
    } else {
        left = "no image loaded";
    }
    if (!message_.empty()) {
        left += "   [" + message_ + "]";
    }
    text(canvas, status_.left + 8, status_.top + 3, left, kTextDim,
         FontRole::MonoSmall);
    text_right(canvas, status_.right - 8, status_.top + 3,
               "F5 run   F6 break", kTextDim, FontRole::MonoSmall);
}

void App::resize(Size size) {
    size_ = size;
    layout();
    window_.invalidate();
}

void App::event(const Event& event) {
    if (settings_open_) {
        if (const auto* move = std::get_if<MouseMove>(&event);
            move != nullptr) {
            mouse_ = move->position;
            int hover = -1;
            if (settings_close_.contains(move->position)) hover = 99;
            else if (settings_opt0_.contains(move->position)) hover = 0;
            else if (settings_opt1_.contains(move->position)) hover = 1;
            else if (settings_debug_.contains(move->position)) hover = 2;
            else if (settings_libc_.contains(move->position)) hover = 3;
            else if (settings_listing_.contains(move->position)) hover = 4;
            if (hover != hover_settings_control_) {
                hover_settings_control_ = hover;
                window_.invalidate();
            }
            return;
        }
        if (const auto* down = std::get_if<MouseDown>(&event);
            down != nullptr) {
            if (down->button != MouseButton::Left) {
                return;
            }
            if (settings_close_.contains(down->position) ||
                !settings_modal_.contains(down->position)) {
                close_settings();
                return;
            }
            if (settings_opt0_.contains(down->position)) {
                if (settings_.optimize) {
                    settings_.optimize = false;
                    apply_settings();
                }
            } else if (settings_opt1_.contains(down->position)) {
                if (!settings_.optimize) {
                    settings_.optimize = true;
                    apply_settings();
                }
            } else if (settings_debug_.contains(down->position)) {
                settings_.debug = !settings_.debug;
                apply_settings();
            } else if (settings_libc_.contains(down->position)) {
                settings_.link_libc = !settings_.link_libc;
                apply_settings();
            } else if (settings_listing_.contains(down->position)) {
                settings_.listing = !settings_.listing;
                apply_settings();
            }
            window_.invalidate();
            return;
        }
        if (std::holds_alternative<MouseWheel>(event)) {
            return;
        }
        if (const auto* key = std::get_if<KeyDown>(&event); key != nullptr) {
            if (key->key == Key::Escape) {
                close_settings();
            }
            return;
        }
        if (std::holds_alternative<TextInput>(event)) {
            return;
        }
        if (std::holds_alternative<Timer>(event)) {
            on_timer();
        }
        return;
    }
    if (const auto* move = std::get_if<MouseMove>(&event); move != nullptr) {
        mouse_ = move->position;
        const bool gear_hover = settings_rect_.contains(move->position);
        if (gear_hover != hover_settings_) {
            hover_settings_ = gear_hover;
            window_.invalidate();
        }
        const int hover = hit_button(move->position);
        if (hover != hover_button_) {
            hover_button_ = hover;
            window_.invalidate();
        }
        const bool step_hover = step_reset_rect_.contains(move->position);
        if (step_hover != hover_step_reset_) {
            hover_step_reset_ = step_hover;
            window_.invalidate();
        }
        int register_index = -1;
        for (int i = 0; i < 8; ++i) {
            if (register_rects_[i].contains(move->position)) {
                register_index = i;
                break;
            }
        }
        int flag_index = -1;
        for (int i = 0; i < 5; ++i) {
            if (flag_rects_[i].contains(move->position)) {
                flag_index = i;
                break;
            }
        }
        if (register_index != hover_register_ ||
            flag_index != hover_flag_) {
            hover_register_ = register_index;
            hover_flag_ = flag_index;
            window_.invalidate();
        }
        int var_index = -1;
        for (std::size_t i = 0; i < var_hits_.size(); ++i) {
            if (var_hits_[i].rect.contains(move->position)) {
                var_index = static_cast<int>(i);
                break;
            }
        }
        if (var_index != hover_var_) {
            hover_var_ = var_index;
            window_.invalidate();
        }
        int help_index = -1;
        bool help_operand = false;
        for (std::size_t i = 0; i < asm_rows_.size(); ++i) {
            if (asm_rows_[i].mnemonic.contains(move->position)) {
                help_index = static_cast<int>(i);
                help_operand = false;
                break;
            }
            if (asm_rows_[i].operand.contains(move->position)) {
                help_index = static_cast<int>(i);
                help_operand = true;
                break;
            }
        }
        if (help_index != hover_asm_index_ ||
            help_operand != hover_asm_operand_) {
            hover_asm_index_ = help_index;
            hover_asm_operand_ = help_operand;
            window_.invalidate();
        }
        int memory_byte = -1;
        for (std::size_t i = 0; i < memory_bytes_.size(); ++i) {
            if (memory_bytes_[i].rect.contains(move->position)) {
                memory_byte = static_cast<int>(i);
                hover_memory_address_ = memory_bytes_[i].address;
                break;
            }
        }
        if (memory_byte != hover_memory_byte_) {
            hover_memory_byte_ = memory_byte;
            window_.invalidate();
        }
        return;
    }
    if (const auto* leave = std::get_if<MouseLeave>(&event);
        leave != nullptr) {
        (void)leave;
        hover_memory_byte_ = -1;
        hover_step_reset_ = false;
        hover_settings_ = false;
        window_.invalidate();
        return;
    }
    if (const auto* down = std::get_if<MouseDown>(&event); down != nullptr) {
        if (down->button != MouseButton::Left) {
            return;
        }
        if (settings_rect_.contains(down->position)) {
            open_settings();
            return;
        }
        if (editing_memory()) {
            const bool in_memory = memory_.contains(down->position);
            finish_memory_edit(true);
            if (in_memory) {
                return;
            }
        }
        if (memory_.contains(down->position)) {
            if (memory_address_rect_.contains(down->position)) {
                start_memory_address_edit();
                return;
            }
            if (memory_up_rect_.contains(down->position)) {
                scroll_memory(-16);
                return;
            }
            if (memory_down_rect_.contains(down->position)) {
                scroll_memory(16);
                return;
            }
            for (std::size_t i = 0; i < memory_bytes_.size(); ++i) {
                if (memory_bytes_[i].rect.contains(down->position)) {
                    start_memory_byte_edit(static_cast<int>(i));
                    return;
                }
            }
            return;
        }
        if (editing_var_ >= 0) {
            const bool in_variables = variables_.contains(down->position);
            finish_variable_edit(true);
            if (in_variables) {
                return;
            }
        }
        for (std::size_t i = 0; i < var_hits_.size(); ++i) {
            if (var_hits_[i].rect.contains(down->position)) {
                start_variable_edit(static_cast<int>(i));
                return;
            }
        }
        if (editing_register_ >= 0) {
            finish_register_edit(true);
        }
        for (int i = 0; i < 5; ++i) {
            if (flag_rects_[i].contains(down->position)) {
                toggle_flag(i);
                return;
            }
        }
        for (int i = 0; i < 8; ++i) {
            if (register_rects_[i].contains(down->position)) {
                start_register_edit(i);
                return;
            }
        }
        if (step_reset_rect_.contains(down->position)) {
            step_at_reset_ = !step_at_reset_;
            if (step_at_reset_) {
                if (session_.running()) {
                    stop_run("paused");
                }
                apply_reset_entry();
            } else {
                if (reset_breakpoint_owned_ &&
                    session_.has_breakpoint(reset_breakpoint_)) {
                    session_.toggle_address_breakpoint(reset_breakpoint_);
                }
                reset_breakpoint_owned_ = false;
                message_ = "step at reset disabled";
                window_.invalidate();
            }
            return;
        }
        const int index = hit_button(down->position);
        if (index >= 0) {
            activate(buttons_[static_cast<std::size_t>(index)].action);
            return;
        }
        if (source_text_.contains(down->position)) {
            toggle_source_breakpoint(down->position);
            return;
        }
        toggle_asm_breakpoint(down->position);
        return;
    }
    if (const auto* wheel = std::get_if<MouseWheel>(&event);
        wheel != nullptr) {
        if (memory_.contains(wheel->position)) {
            scroll_memory(wheel->delta > 0 ? -16 : 16);
            return;
        }
        const int steps = wheel->delta > 0 ? -3 : 3;
        if (variables_.contains(wheel->position)) {
            variables_scroll_ += steps;
            variables_scroll_ = std::max(0, variables_scroll_);
            window_.invalidate();
            return;
        }
        if (stack_.contains(wheel->position)) {
            stack_scroll_ += steps;
            stack_scroll_ = std::max(0, stack_scroll_);
            window_.invalidate();
            return;
        }
        if (console_.contains(wheel->position)) {
            console_scroll_ -= steps;
            console_scroll_ = std::max(0, console_scroll_);
            window_.invalidate();
            return;
        }
        if (source_text_.contains(wheel->position)) {
            source_scroll_ += steps;
            source_scroll_ = std::max(0, source_scroll_);
            window_.invalidate();
        } else if (asm_text_.contains(wheel->position)) {
            asm_scroll_ += steps;
            asm_scroll_ = std::max(0, asm_scroll_);
            window_.invalidate();
        }
        return;
    }
    if (const auto* key = std::get_if<KeyDown>(&event); key != nullptr) {
        if (editing_var_ >= 0) {
            variable_key(key->key);
            return;
        }
        if (editing_register_ >= 0) {
            register_key(key->key);
            return;
        }
        if (editing_memory()) {
            memory_key(key->key);
            return;
        }
        if (key->key == Key::F5) {
            activate(Action::Run);
        } else if (key->key == Key::F6) {
            activate(Action::Break);
        }
        return;
    }
    if (const auto* input = std::get_if<TextInput>(&event); input != nullptr) {
        if (editing_var_ >= 0) {
            variable_text_input(input->utf8);
        } else if (editing_register_ >= 0) {
            register_text_input(input->utf8);
        } else {
            memory_text_input(input->utf8);
        }
        return;
    }
    if (std::holds_alternative<Timer>(event)) {
        on_timer();
    }
}

void App::activate(Action action) {
    switch (action) {
        case Action::Load:
            window_.open_file(
                [this](std::optional<std::filesystem::path> path) {
                    if (path.has_value()) {
                        open(*path);
                    }
                },
                "Open a KR16 image or source file", "");
            break;
        case Action::Run:
            start_run();
            break;
        case Action::Break:
            stop_run("paused");
            break;
        case Action::Restart: {
            if (session_.running()) {
                stop_run("paused");
            }
            std::string error;
            if (!session_.restart(error)) {
                message_ = "restart failed";
                window_.show_error("KR16", error);
            } else {
                message_ = "restarted";
                if (step_at_reset_) {
                    apply_reset_entry();
                }
            }
            layout();
            window_.invalidate();
            break;
        }
        case Action::Reload: {
            if (session_.running()) {
                stop_run("paused");
            }
            std::string error;
            if (!session_.restart(error)) {
                message_ = "reload failed";
                window_.show_error("KR16", error);
            } else {
                message_ = "image reloaded";
                if (step_at_reset_) {
                    apply_reset_entry();
                }
            }
            layout();
            window_.invalidate();
            break;
        }
        case Action::ResetStats:
            session_.reset_stats();
            message_ = "stats reset";
            window_.invalidate();
            break;
        case Action::CStepOver:
            step_c(StepMode::LineOver);
            break;
        case Action::CStepIn:
            step_c(StepMode::LineIn);
            break;
        case Action::CStepOut:
            step_c(StepMode::LineOut);
            break;
        case Action::AStepOver:
            step_asm(1);
            break;
        case Action::AStepIn:
            step_asm(0);
            break;
        case Action::AStepOut:
            step_asm(2);
            break;
        case Action::None:
            break;
    }
}

void App::start_run() {
    if (!session_.loaded() || session_.halted() || session_.running()) {
        return;
    }
    session_.start_run();
    message_ = "running";
    layout();
    window_.set_timer(8, true);
    window_.invalidate();
}

void App::stop_run(const std::string& reason) {
    if (!session_.running()) {
        return;
    }
    session_.stop_run(reason);
    layout();
    window_.set_timer(8, false);
    message_ = reason;
    window_.invalidate();
}

void App::on_timer() {
    if (!session_.running()) {
        layout();
        window_.set_timer(8, false);
        return;
    }
    const SessionRunStop stop = session_.run_slice(200000);
    if (stop != SessionRunStop::Budget) {
        layout();
        window_.set_timer(8, false);
        message_ = stop == SessionRunStop::Halted ? "halted" : "paused";
    }
    window_.invalidate();
}

void App::apply_reset_entry() {
    if (!step_at_reset_) {
        return;
    }
    if (!session_.loaded()) {
        message_ = "step at reset: no image loaded";
        window_.invalidate();
        return;
    }
    if (session_.running()) {
        stop_run("paused");
    }
    const std::uint16_t vector = session_.read16(0xFFFE);
    const std::uint16_t start = vector == 0 ? 0x0100 : vector;
    const bool already_owned =
        reset_breakpoint_owned_ && reset_breakpoint_ == start &&
        session_.has_breakpoint(start);
    if (!already_owned) {
        if (reset_breakpoint_owned_ &&
            session_.has_breakpoint(reset_breakpoint_)) {
            session_.toggle_address_breakpoint(reset_breakpoint_);
        }
        if (!session_.has_breakpoint(start)) {
            session_.toggle_address_breakpoint(start);
            reset_breakpoint_owned_ = true;
        } else {
            reset_breakpoint_owned_ = false;
        }
    }
    reset_breakpoint_ = start;
    session_.set_reg(5, start);
    message_ = vector == 0 ? "no BIOS; breakpoint at 0x0100"
                           : "reset vector " + hex4(vector);
    layout();
    window_.invalidate();
}

int App::hit_button(Point point) const {
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        if (buttons_[i].enabled && buttons_[i].rect.contains(point)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void App::toggle_asm_breakpoint(Point point) {
    if (!session_.loaded() || !asm_text_.contains(point)) {
        return;
    }
    for (const auto& row : asm_rows_) {
        if (row.rect.contains(point)) {
            session_.toggle_address_breakpoint(row.address);
            window_.invalidate();
            return;
        }
    }
}

void App::toggle_source_breakpoint(Point point) {
    if (!session_.loaded() || !source_text_.contains(point) ||
        point.x >= source_text_.left + kGutter) {
        return;
    }
    const int row = (point.y - source_text_.top - 2) / 20;
    const int line = source_scroll_ + row + 1;
    if (line < 1 ||
        line > static_cast<int>(session_.source_lines().size())) {
        return;
    }
    session_.toggle_line_breakpoint(line);
    window_.invalidate();
}

void App::step_c(StepMode mode) {
    if (!session_.loaded() || session_.running() || session_.halted()) {
        return;
    }
    const bool ok = session_.step_line(mode);
    message_ = ok ? "line step" : (session_.halted() ? "halted" : "step failed");
    layout();
    window_.invalidate();
}

void App::step_asm(int mode) {
    if (!session_.loaded() || session_.running() || session_.halted()) {
        return;
    }
    bool ok = false;
    if (mode == 0) {
        ok = session_.step_instruction();
    } else if (mode == 1) {
        ok = session_.step_instruction_over();
    } else {
        ok = session_.step_instruction_out();
    }
    message_ = ok ? "instruction step"
                  : (session_.halted() ? "halted" : "step failed");
    layout();
    window_.invalidate();
}

bool App::editing_memory() const {
    return memory_address_editing_ || memory_byte_editing_ >= 0;
}

void App::scroll_memory(int bytes) {
    if (editing_memory()) {
        return;
    }
    const int next = std::clamp(
        static_cast<int>(memory_address_) + bytes, 0, 0xFFE0);
    memory_address_ = static_cast<std::uint16_t>(next);
    hover_memory_byte_ = -1;
    window_.invalidate();
}

void App::start_memory_address_edit() {
    if (!session_.loaded() || session_.running()) {
        return;
    }
    memory_address_editing_ = true;
    memory_byte_editing_ = -1;
    std::ostringstream text;
    text << std::hex << std::uppercase << std::setfill('0') << std::setw(4)
         << memory_address_;
    edit_text_ = text.str();
    edit_fresh_ = true;
    window_.invalidate();
}

void App::start_memory_byte_edit(int index) {
    if (!session_.loaded() || session_.running() || index < 0 ||
        index >= static_cast<int>(memory_bytes_.size())) {
        return;
    }
    const std::uint16_t address = memory_bytes_[index].address;
    memory_address_editing_ = false;
    memory_byte_editing_ = index;
    memory_byte_edit_address_ = address;
    std::ostringstream text;
    text << std::hex << std::uppercase << std::setfill('0') << std::setw(2)
         << static_cast<unsigned>(session_.read8(address));
    edit_text_ = text.str();
    edit_fresh_ = true;
    window_.invalidate();
}

void App::finish_memory_edit(bool commit) {
    if (!editing_memory()) {
        return;
    }
    if (!commit) {
        memory_address_editing_ = false;
        memory_byte_editing_ = -1;
        edit_text_.clear();
        edit_fresh_ = false;
        window_.invalidate();
        return;
    }
    unsigned value = 0;
    bool valid = !edit_text_.empty();
    for (const char ch : edit_text_) {
        const int digit = hex_digit(ch);
        if (digit < 0) {
            valid = false;
            break;
        }
        value = value * 16 + static_cast<unsigned>(digit);
    }
    if (!valid) {
        return;
    }
    if (memory_address_editing_) {
        memory_address_ = static_cast<std::uint16_t>(
            std::min<unsigned>(value, 0xFFE0));
    } else if (memory_byte_editing_ >= 0) {
        if (value <= 0xFF) {
            session_.write8(memory_byte_edit_address_,
                            static_cast<std::uint8_t>(value));
        }
    }
    memory_address_editing_ = false;
    memory_byte_editing_ = -1;
    edit_text_.clear();
    edit_fresh_ = false;
    window_.invalidate();
}

void App::memory_text_input(std::string_view input) {
    if (!editing_memory()) {
        return;
    }
    const int maximum = memory_address_editing_ ? 4 : 2;
    for (const char ch : input) {
        if (hex_digit(ch) < 0) {
            continue;
        }
        if (edit_fresh_) {
            edit_text_.clear();
            edit_fresh_ = false;
        }
        if (static_cast<int>(edit_text_.size()) < maximum) {
            edit_text_.push_back(static_cast<char>(std::toupper(
                static_cast<unsigned char>(ch))));
        }
    }
    window_.invalidate();
}

void App::memory_key(Key key) {
    if (!editing_memory()) {
        return;
    }
    if (key == Key::Enter) {
        finish_memory_edit(true);
    } else if (key == Key::Escape) {
        finish_memory_edit(false);
    } else if (key == Key::Backspace) {
        if (edit_fresh_) {
            edit_text_.clear();
            edit_fresh_ = false;
        } else if (!edit_text_.empty()) {
            edit_text_.pop_back();
        }
        window_.invalidate();
    }
}

void App::register_key(Key key) {
    if (editing_register_ < 0) {
        return;
    }
    if (key == Key::Enter) {
        finish_register_edit(true);
    } else if (key == Key::Escape) {
        finish_register_edit(false);
    } else if (key == Key::Backspace) {
        if (edit_fresh_) {
            edit_text_.clear();
            edit_fresh_ = false;
        } else if (!edit_text_.empty()) {
            edit_text_.pop_back();
        }
        window_.invalidate();
    }
}

void App::start_register_edit(int index) {
    if (!session_.loaded() || session_.running() || index < 0 ||
        index >= 8) {
        return;
    }
    editing_register_ = index;
    memory_address_editing_ = false;
    memory_byte_editing_ = -1;
    std::ostringstream text;
    text << std::hex << std::uppercase << std::setfill('0') << std::setw(4)
         << session_.reg(index);
    edit_text_ = text.str();
    edit_fresh_ = true;
    window_.invalidate();
}

void App::finish_register_edit(bool commit) {
    if (editing_register_ < 0) {
        return;
    }
    if (commit && !edit_text_.empty()) {
        unsigned value = 0;
        bool valid = true;
        for (const char ch : edit_text_) {
            const int digit = hex_digit(ch);
            if (digit < 0) {
                valid = false;
                break;
            }
            value = value * 16 + static_cast<unsigned>(digit);
        }
        if (valid && value <= 0xFFFF) {
            if (editing_register_ == 7) {
                value &= 0x1Fu;
            }
            session_.set_reg(editing_register_,
                             static_cast<std::uint16_t>(value));
        }
    }
    editing_register_ = -1;
    edit_text_.clear();
    edit_fresh_ = false;
    window_.invalidate();
}

void App::register_text_input(std::string_view input) {
    if (editing_register_ < 0) {
        return;
    }
    for (const char ch : input) {
        if (hex_digit(ch) < 0) {
            continue;
        }
        if (edit_fresh_) {
            edit_text_.clear();
            edit_fresh_ = false;
        }
        if (edit_text_.size() < 4) {
            edit_text_.push_back(static_cast<char>(std::toupper(
                static_cast<unsigned char>(ch))));
        }
    }
    window_.invalidate();
}

void App::toggle_flag(int index) {
    if (!session_.loaded() || session_.running() || index < 0 ||
        index >= 5) {
        return;
    }
    static const std::uint16_t bits[] = {0x01, 0x02, 0x04, 0x08, 0x10};
    session_.set_reg(7, static_cast<std::uint16_t>(
        session_.sr() ^ bits[index]));
    window_.invalidate();
}

void App::start_variable_edit(int index) {
    if (!session_.loaded() || session_.running() || index < 0 ||
        index >= static_cast<int>(var_hits_.size())) {
        return;
    }
    const VarHit& hit = var_hits_[index];
    if (!hit.ok || hit.size > 2) {
        return;
    }
    editing_var_ = index;
    editing_register_ = -1;
    memory_address_editing_ = false;
    memory_byte_editing_ = -1;
    var_name_ = hit.name;
    var_address_ = hit.address;
    var_size_ = hit.size;
    std::ostringstream text;
    text << std::hex << std::uppercase << std::setfill('0')
         << std::setw(var_size_ == 1 ? 2 : 4)
         << static_cast<unsigned>(hit.size == 1 ? (hit.value & 0xFF)
                                                : hit.value);
    edit_text_ = text.str();
    edit_fresh_ = true;
    window_.invalidate();
}

void App::finish_variable_edit(bool commit) {
    if (editing_var_ < 0) {
        return;
    }
    if (commit && !edit_text_.empty()) {
        unsigned value = 0;
        bool valid = true;
        for (const char ch : edit_text_) {
            const int digit = hex_digit(ch);
            if (digit < 0) {
                valid = false;
                break;
            }
            value = value * 16 + static_cast<unsigned>(digit);
        }
        if (valid) {
            if (var_size_ == 1 && value <= 0xFF) {
                session_.write8(var_address_,
                                static_cast<std::uint8_t>(value));
            } else if (var_size_ == 2 && value <= 0xFFFF) {
                session_.write16(var_address_,
                                 static_cast<std::uint16_t>(value));
            }
        }
    }
    editing_var_ = -1;
    edit_text_.clear();
    edit_fresh_ = false;
    window_.invalidate();
}

void App::variable_text_input(std::string_view input) {
    if (editing_var_ < 0) {
        return;
    }
    const int maximum = var_size_ == 1 ? 2 : 4;
    for (const char ch : input) {
        if (hex_digit(ch) < 0) {
            continue;
        }
        if (edit_fresh_) {
            edit_text_.clear();
            edit_fresh_ = false;
        }
        if (static_cast<int>(edit_text_.size()) < maximum) {
            edit_text_.push_back(static_cast<char>(std::toupper(
                static_cast<unsigned char>(ch))));
        }
    }
    window_.invalidate();
}

void App::variable_key(Key key) {
    if (editing_var_ < 0) {
        return;
    }
    if (key == Key::Enter) {
        finish_variable_edit(true);
    } else if (key == Key::Escape) {
        finish_variable_edit(false);
    } else if (key == Key::Backspace) {
        if (edit_fresh_) {
            edit_text_.clear();
            edit_fresh_ = false;
        } else if (!edit_text_.empty()) {
            edit_text_.pop_back();
        }
        window_.invalidate();
    }
}

void App::open(const std::filesystem::path& path) {
    if (session_.running()) {
        stop_run("paused");
    }
    std::string error;
    if (!session_.build_and_load(path, libc_source_, settings_, error)) {
        message_ = "load failed";
        window_.show_error("KR16", error.empty() ? "cannot load file" : error);
        return;
    }
    current_path_ = path;
    source_scroll_ = 0;
    asm_scroll_ = 0;
    variables_scroll_ = 0;
    stack_scroll_ = 0;
    console_scroll_ = 0;
    memory_address_ = 0x0100;
    memory_address_editing_ = false;
    memory_byte_editing_ = -1;
    editing_register_ = -1;
    hover_register_ = -1;
    hover_flag_ = -1;
    hover_var_ = -1;
    editing_var_ = -1;
    hover_asm_index_ = -1;
    hover_asm_operand_ = false;
    hover_memory_byte_ = -1;
    message_ = "loaded";
    window_.set_title("KR16 - " + path.filename().string());
    layout();
    if (step_at_reset_) {
        apply_reset_entry();
    }
    window_.invalidate();
}

}  // namespace kr16::ui
