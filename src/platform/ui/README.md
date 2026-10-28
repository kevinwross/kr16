# UI platform backends

Each backend implements `kr16::ui::Canvas` and `kr16::ui::Window`, owns the
native event loop, and translates native input into `kr16::ui::Event`.

Planned layout:

```
src/platform/ui/win32/   Win32 window, GDI canvas, native dialogs, clock
src/platform/ui/sdl/     SDL3 window, software canvas, native dialogs, clock
```

The Win32 backend is the parity reference. The SDL3 backend is the Linux
backend for x64 and ARM64, and may later replace GDI on Windows if exact
cross-platform rendering is wanted.

Keep all host headers and handles in this directory. `src/ui` and
`include/kr16/ui` must remain portable.
