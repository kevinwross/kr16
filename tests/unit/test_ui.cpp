#include <doctest/doctest.h>

#include <string>
#include <variant>

#include "kr16/ui/canvas.hpp"
#include "kr16/ui/input.hpp"
#include "kr16/ui/window.hpp"

namespace {

class TestCanvas final : public kr16::ui::Canvas {
public:
    void fill_rect(const kr16::ui::Rect&, kr16::ui::Color) override {}
    void frame_rect(const kr16::ui::Rect&, kr16::ui::Color,
                    int) override {}
    void text(int, int, std::string_view, kr16::ui::Color,
              kr16::ui::FontRole) override {}
    kr16::ui::Size measure(std::string_view text,
                           kr16::ui::FontRole) override {
        return kr16::ui::Size{static_cast<int>(text.size()) * 8, 16};
    }
    int line_height(kr16::ui::FontRole) override { return 18; }
    void push_clip(const kr16::ui::Rect&) override {}
    void pop_clip() override {}
};

}  // namespace

TEST_CASE("ui geometry contains and measures rectangles") {
    const kr16::ui::Rect rect{10, 20, 110, 220};
    CHECK(rect.width() == 100);
    CHECK(rect.height() == 200);
    CHECK(rect.contains(kr16::ui::Point{10, 20}));
    CHECK(rect.contains(kr16::ui::Point{109, 219}));
    CHECK_FALSE(rect.contains(kr16::ui::Point{110, 219}));
    CHECK_FALSE(rect.contains(kr16::ui::Point{10, 220}));
    CHECK(kr16::ui::Rect{}.empty());
}

TEST_CASE("ui events are a portable variant") {
    kr16::ui::Event event = kr16::ui::MouseMove{kr16::ui::Point{12, 34}};
    REQUIRE(std::holds_alternative<kr16::ui::MouseMove>(event));
    CHECK(std::get<kr16::ui::MouseMove>(event).position.x == 12);

    event = kr16::ui::KeyDown{kr16::ui::Key::F5, false, false, false};
    REQUIRE(std::holds_alternative<kr16::ui::KeyDown>(event));
    CHECK(std::get<kr16::ui::KeyDown>(event).key == kr16::ui::Key::F5);

    event = kr16::ui::TextInput{"A"};
    REQUIRE(std::holds_alternative<kr16::ui::TextInput>(event));
    CHECK(std::get<kr16::ui::TextInput>(event).utf8 == "A");
}

TEST_CASE("ui canvas interface measures portable text") {
    TestCanvas canvas;
    const auto size = canvas.measure("hello", kr16::ui::FontRole::Mono);
    CHECK(size.width == 40);
    CHECK(size.height == 16);
    CHECK(canvas.line_height(kr16::ui::FontRole::Mono) == 18);
}
