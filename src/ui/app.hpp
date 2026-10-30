#pragma once

#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "kr16/debug_session.hpp"
#include "kr16/ui/canvas.hpp"
#include "kr16/ui/input.hpp"
#include "kr16/ui/window.hpp"
#include "text_field.hpp"

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
        int index = 0;
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
        Rect value_rect;
        Rect expand_rect;
        std::string name;
        std::string type;
        std::uint16_t address = 0;
        std::uint16_t value = 0;
        int size = 2;
        bool local = true;
        bool ok = false;
        bool pointer = false;
        bool char_pointer = false;
        bool watch = false;
        bool expandable = false;
        bool expanded = false;
        int indent = 0;
    };

    enum class ScrollPane {
        None,
        Source,
        Asm,
        Variables,
        Stack,
        Console,
    };

    enum class MemoryView {
        Hex,
        Ascii,
        String,
    };

    struct WatchEntry {
        std::string name;
        bool local = true;
    };

    struct GutterMenu {
        bool open = false;
        Rect rect;
        Rect toggle_rect;
        Rect run_rect;
        bool source = true;
        int line = 0;
        std::uint16_t address = 0;
        int hover = -1;
    };

    struct ScrollBarState {
        Rect track;
        Rect thumb;
        int offset = 0;
        int max_offset = 0;
        int total = 0;
        int visible = 0;
        bool active = false;
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
    void draw_settings(Canvas& canvas);
    void draw_button(Canvas& canvas, const Button& button);
    void draw_scrollbar(Canvas& canvas, ScrollBarState& bar,
                        const Rect& track, int offset, int total,
                        int visible);
    bool open_gutter_menu(Point point);
    void draw_gutter_menu(Canvas& canvas);
    void activate_gutter_menu(bool run_to);
    void cleanup_temporary_breakpoint();
    void open_settings();
    void close_settings();
    void apply_settings();
    bool prepare_arguments(std::string& error);
    void reload_current();
    void load_settings();
    void save_settings();
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
    void toggle_watch(const VarHit& hit);
    bool is_watched(const std::string& name, bool local) const;
    void toggle_watch_expanded(const VarHit& hit);
    bool watch_expanded(const std::string& name, bool local) const;
    bool begin_scroll_drag(Point point);
    void update_scroll_drag(int y);
    void set_scroll_offset(ScrollPane pane, int offset);
    ScrollBarState& scroll_bar(ScrollPane pane);
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
    Rect memory_view_rect_{};
    Rect memory_follow_rect_{};
    MemoryView memory_view_ = MemoryView::Hex;
    bool follow_pointer_active_ = false;
    std::string follow_pointer_name_;
    bool follow_pointer_local_ = true;
    std::vector<WatchEntry> watches_;
    std::set<std::string> expanded_watches_;
    Rect bottom_{};
    Rect variables_{};
    Rect stack_{};
    Rect console_{};
    Rect status_{};
    Rect settings_rect_{};
    Rect settings_modal_{};
    Rect settings_close_{};
    Rect settings_opt0_{};
    Rect settings_opt1_{};
    Rect settings_debug_{};
    Rect settings_libc_{};
    Rect settings_listing_{};
    Rect settings_args_{};
    TextField settings_args_field_;
    std::string settings_args_error_;
    SessionBuildOptions settings_{};
    std::filesystem::path current_path_;
    bool settings_open_ = false;
    bool hover_settings_ = false;
    int hover_settings_control_ = -1;
    GutterMenu gutter_menu_;
    int temporary_breakpoint_line_ = 0;
    std::uint16_t temporary_breakpoint_address_ = 0;
    bool temporary_breakpoint_owned_ = false;

    std::vector<Button> buttons_;
    std::vector<AsmRow> asm_rows_;
    std::vector<MemoryByte> memory_bytes_;
    std::vector<VarHit> var_hits_;
    int hover_button_ = -1;
    int source_scroll_ = 0;
    int asm_scroll_ = 0;
    int variables_scroll_ = 0;
    int stack_scroll_ = 0;
    int console_scroll_ = 0;
    ScrollBarState source_scroll_bar_;
    ScrollBarState asm_scroll_bar_;
    ScrollBarState variables_scroll_bar_;
    ScrollBarState stack_scroll_bar_;
    ScrollBarState console_scroll_bar_;
    ScrollPane scroll_drag_ = ScrollPane::None;
    int scroll_drag_origin_ = 0;
    int scroll_drag_start_ = 0;
    std::uint64_t last_follow_instructions_ = ~std::uint64_t{0};
    std::uint16_t last_follow_pc_ = 0;
    bool have_last_follow_pc_ = false;
    bool follow_current_ = true;
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
