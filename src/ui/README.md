# Portable UI

This directory holds the windowed debugger's layout, state, hit testing,
painting and help. It must stay portable: no `windows.h`, no `SDL3.h`, no
`RECT`/`HDC`/`COLORREF`, and no assumption that `wchar_t` is UTF-16.

Rules:

- Public strings are UTF-8 (`std::string`/`std::string_view`).
- Coordinates are logical pixels; the backend handles DPI scaling.
- The UI talks to the engine through `kr16::DebugSession`, not `Cpu`,
  `Machine` or `Container`.
- Drawing goes through `kr16::ui::Canvas`; platform services through
  `kr16::ui::Window`.
- The UI does not own OS timers, file handles, fonts or windows.

The source files will be introduced as the mockup is ported, starting with the
session boundary and then the Win32 backend for parity.
