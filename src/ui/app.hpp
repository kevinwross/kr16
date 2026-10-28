#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "kr16/debug_session.hpp"
#include "kr16/ui/canvas.hpp"
#include "kr16/ui/input.hpp"
#include "kr16/ui/window.hpp"

namespace kr16::ui {

class App final : public Application {
public:
    App(Window& window, DebugSession& session,
        std::filesystem::path libc_source = {});
    ~App() override = default;

    void paint(Canvas& canvas) override;
    void event(const Event& event) override;
    void resize(Size size) override;

    void open(const std::filesystem::path& path);

private:
    enum class Action {
        None,
        Load,
        Run,
        Break,
        Restart,
        Reload,
        ResetStats,
        CStepOver,
        CStepIn,
        CStepOut,
        AStepOver,
        AStepIn,
        AStepOut,
    };

    struct Button {
        Rect rect;
        std::string label;
        Action action = Action::None;
        bool enabled = true;
        bool primary = false;
        int group = 0;
    };

    struct AsmRow {
        std::uint16_t address = 0;
        Rect rect;
        Rect mnemonic;
        Rect operand;
    };

    struct MemoryByte {
        Rect rect;
        std::uint16_t address = 0;
    };

    struct VarHit {
        Rect rect;
        std::string name;
        std::uint16_t address = 0;
        std::uint16_t value = 0;
        int size = 2;
        bool local = true;
        bool ok = false;
    };

    void layout();
    void draw_top(Canvas& canvas);
    void draw_source(Canvas& canvas);
    void draw_asm(Canvas& canvas);
    void draw_registers(Canvas& canvas);
    void draw_bottom(Canvas& canvas);
    void draw_status(Canvas& canvas);
    void draw_memory(Canvas& canvas);
    void draw_memory_tooltip(Canvas& canvas);
    void draw_hover_tooltip(Canvas& canvas);
    void draw_var_tooltip(Canvas& canvas);
    void draw_asm_tooltip(Canvas& canvas);
    void draw_button(Canvas& canvas, const Button& button);
    void activate(Action action);
    void start_run();
    void stop_run(const std::string& reason);
    void on_timer();
    void apply_reset_entry();
    void toggle_asm_breakpoint(Point point);
    void toggle_source_breakpoint(Point point);
    void toggle_flag(int index);
    void step_c(StepMode mode);
    void step_asm(int mode);
    void scroll_memory(int bytes);
    void start_memory_address_edit();
    void start_memory_byte_edit(int index);
    void finish_memory_edit(bool commit);
    void memory_text_input(std::string_view text);
    void memory_key(Key key);
    bool editing_memory() const;
    void start_register_edit(int index);
    void finish_register_edit(bool commit);
    void register_text_input(std::string_view text);
    void register_key(Key key);
    void start_variable_edit(int index);
    void finish_variable_edit(bool commit);
    void variable_text_input(std::string_view text);
    void variable_key(Key key);
    int hit_button(Point point) const;

    Window& window_;
    DebugSession& session_;
    std::filesystem::path libc_source_;
    std::string message_;

    Size size_{1280, 820};
    Rect top_bar_{};
    Rect step_reset_rect_{};
    Rect source_pane_{};
    Rect source_text_{};
    Rect asm_pane_{};
    Rect asm_text_{};
    Rect registers_{};
    Rect memory_{};
    Rect memory_address_rect_{};
    Rect memory_up_rect_{};
    Rect memory_down_rect_{};
    Rect bottom_{};
    Rect variables_{};
    Rect stack_{};
    Rect console_{};
    Rect status_{};

    std::vector<Button> buttons_;
    std::vector<AsmRow> asm_rows_;
    std::vector<MemoryByte> memory_bytes_;
    std::vector<VarHit> var_hits_;
    int hover_button_ = -1;
    int source_scroll_ = 0;
    int asm_scroll_ = 0;
    std::uint16_t memory_address_ = 0x0100;
    bool memory_address_editing_ = false;
    int memory_byte_editing_ = -1;
    std::uint16_t memory_byte_edit_address_ = 0;
    std::string edit_text_;
    bool edit_fresh_ = false;
    int hover_memory_byte_ = -1;
    std::uint16_t hover_memory_address_ = 0;
    int editing_register_ = -1;
    int hover_register_ = -1;
    int hover_flag_ = -1;
    int hover_var_ = -1;
    int editing_var_ = -1;
    int hover_asm_index_ = -1;
    bool hover_asm_operand_ = false;
    bool step_at_reset_ = false;
    bool hover_step_reset_ = false;
    std::uint16_t reset_breakpoint_ = 0;
    bool reset_breakpoint_owned_ = false;
    std::string var_name_;
    std::uint16_t var_address_ = 0;
    int var_size_ = 2;
    Rect register_rects_[8]{};
    Rect flag_rects_[5]{};
    int reg_cell_width_ = 100;
    Point mouse_{};
};

}  // namespace kr16::ui
