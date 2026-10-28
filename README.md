# KR16

KR16 is a 16-bit teaching computer with a complete, single-binary toolchain:

- `kr16 as` — assembler
- `kr16 dis` — disassembler
- `kr16 cc` — C compiler
- `kr16 ld` — linker
- `kr16 size` — memory budget report
- `kr16 debug` — decoded debug-information dump
- `kr16 vm` — virtual machine
- `kr16 dap` — Debug Adapter Protocol host for VS Code

The design specification is [kr16-plan.md](kr16-plan.md). The implementation
plan, milestones, test gates, and work log are in
[implementation.md](implementation.md).

## Status

Planning is complete. Implementation is beginning at milestone M0; there is
no runnable toolchain yet.

## Build

Requirements:

- CMake 3.25+
- C++20 compiler
  - Windows: Visual Studio 2026 or Ninja + MSVC
  - Linux: GCC 13+ or Clang 16+

```text
cmake --preset windows-vs2026
cmake --build --preset windows-vs2026-debug
ctest --preset windows-vs2026-debug
```

Linux:

```text
cmake --preset linux-gcc
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug
```

The Ninja presets require a developer environment with `cl`, `g++`, or
`clang++` on `PATH`.

## Tests

Tests use vendored doctest and CTest. Test labels include `unit`, `golden`,
`integration`, `regression`, `fuzz`, and `slow`.

## License

MIT. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
