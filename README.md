# KR16

KR16 is a 16-bit teaching computer with a single-binary toolchain:

- `kr16 as` — assembler
- `kr16 dis` — disassembler
- `kr16 cc` — teaching-subset C compiler
- `kr16 build` — one-step compile, assemble, and link of a single file
- `kr16 ld` — linker
- `kr16 size` — memory budget report
- `kr16 debug` — decoded debug-information dump
- `kr16 vm` — virtual machine
- `kr16 dap` — minimal Debug Adapter Protocol TCP server

The design specification is [kr16-plan.md](kr16-plan.md). The implementation
plan, milestones, test gates, and work log are in
[implementation.md](implementation.md).

## Status

The **1.0.0 teaching-toolchain release** is published at
<https://github.com/kevinwross/kr16/releases/tag/v1.0.0>. It includes the
Windows zip, VS Code VSIX, and SHA256SUMS.

Milestones M0–M12 are implemented. The working pipeline is:

```text
C or assembly → .o → .kex → VM
```

The C compiler currently implements a teaching subset: `int`/`char`/`_Bool`, pointers,
arrays, functions and prototypes, object/function macros, `#`/`##`, conditionals,
quoted and built-in system headers, `-I` paths, `stdarg.h` variadics,
local/global/static variables, qualifiers, `if`/`else`, `while`, `do`/`while`,
`for`, `switch`, `enum`, `typedef`, `struct`/`union` (including by-value
parameters/returns and aggregate initializers), compound assignments, and
`goto` labels. A small `libc.s` provides `putchar`, `puts`, `getchar`,
`printf` (`%d`, `%x`, `%s`, `%c`, `%%`), string functions, `atoi`, `abs`, heap
allocation, and `exit`.

`long` is a **16-bit alias** for `int`; see
[`docs/decisions/0001-16-bit-long.md`](docs/decisions/0001-16-bit-long.md).
Strict C99 conformance, the full preprocessor (`#line`, pragmas), and the full
`printf` family are deferred beyond 1.0.

The DAP TCP server, live VS Code debug session, locals/globals scopes,
multi-frame stack walking, memory/disassembly/evaluate views, and terminal
console separation are implemented. See
[debug-with-vscode.md](debug-with-vscode.md) for installing the extension and
configuring a debug session.

## Build

Requirements:

- CMake 3.25+
- C++20 compiler
  - Windows: Visual Studio 2026 or Ninja + MSVC
  - Linux: GCC 13+ or Clang 16+

Windows with Ninja + MSVC (from a developer command prompt):

```text
cmake --preset windows-ninja
cmake --build --preset windows-ninja-debug
ctest --preset windows-ninja-debug
```

Visual Studio generator:

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

## Quick start

Assembly:

```text
kr16 as hello.s -o hello.o
kr16 ld hello.o -o hello.kex
kr16 vm hello.kex
```

C, one step (compiles, assembles, links `resources/libc.s`, records debug info):

```text
kr16 build hello.c
kr16 vm hello.kex
```

`build` writes `hello.s`, `hello.o`, and `hello.libc.o` next to the source.
Add `-l` for a listing, `--run` to run the result, `-o path` to name the image,
and `-I dir` for include paths. It also accepts `.s`, `.asm`, and `.o` input.

The same steps by hand:

```text
kr16 cc hello.c -S -o hello.s
kr16 as hello.s -o hello.o
kr16 as resources/libc.s -o libc.o
kr16 ld libc.o hello.o -o hello.kex
kr16 vm hello.kex
```

Debug information:

```text
kr16 debug hello.kex --lines
```

## Windowed debugger and shell

On Windows, `kr16` with no arguments opens the windowed debugger. Passing an
existing `.kex`, `.c`, `.s`, `.asm`, or `.o` file also opens it with that file
loaded. The UI has source and disassembly panes, linked stepping, registers and
flags, variables, a memory strip, a call stack and the console; see
[`docs/research/ui-integration-plan.md`](docs/research/ui-integration-plan.md).

On Linux, the UI builds with static SDL3 fetched by CMake. Install the X11 /
Wayland development headers first, for example:

```text
sudo apt install libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
  libxi-dev libxfixes-dev libxss-dev libxkbcommon-dev libwayland-dev \
  libegl1-mesa-dev libgles2-mesa-dev libdrm-dev libgbm-dev
```

Both the Windows and Linux windows embed subsetted JetBrains Mono and Inter
glyphs, so no font package is needed at runtime. Set `-DKR16_BUILD_UI=OFF` for
a headless build or CI job that only needs the CLI.

## Packaging

Build the Linux AppImage (x64 or ARM64, depending on the host):

```text
bash packaging/linux/package-appimage.sh
```

It writes `dist/KR16-<arch>.AppImage`. The AppImage contains the GUI, the CLI,
the embedded fonts and `resources/libc.s`, so it needs no separate resource
directory. The Windows `kr16.exe` is also self-contained: it embeds the fonts
and falls back to an embedded `libc.s` when no `resources/` directory is
present. The tag workflow in `.github/workflows/release.yml` publishes the
Windows exe and both AppImages.

The interactive shell remains available explicitly:

```text
kr16 shell
```

```text
kr16 C:\work> help
kr16 C:\work> as hello.s -o hello.o
kr16 C:\work> ld hello.o -o hello.kex
kr16 C:\work> build hello.c
kr16 C:\work> load hello.c
```

`load` accepts `.c`, `.asm`, `.s`, `.o`, or `.kex`. It compiles, assembles,
and links as needed, writes a sibling `.kex`, and runs it. `build` runs the
same pipeline without executing the program.

Shell commands include `cd`, `pwd`, `clear`, `history`, `run`, `help`, and
`quit`/`exit`. History is limited to 100 entries and stored in
`~/.kr16_history` (or `%USERPROFILE%\.kr16_history`). Piped input runs without
prompts, so scripted shell sessions work too.

`dap` refuses to block the shell unless forced:

```text
kr16 C:\work> dap --force hello.kex
```

## Tests

Tests use vendored doctest and CTest. Test labels include `unit`, `golden`,
`integration`, `regression`, `fuzz`, and `slow`. The current suite includes
assembler/disassembler tests, CPU/device/interrupt tests, container fixtures,
linker relocation tests, VM boot/argv/headless tests, debug-line tests,
host-call tests, and compiled-C end-to-end execution tests.

## License

MIT. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).


































