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

Every tool has detailed option help: `kr16 <command> --help` (or
`kr16 help <command>`).

The design specification is [kr16-plan.md](kr16-plan.md); the planned
[system services C interface](systemservices.md) defines the `SYS` ABI and
service structures. The implementation plan, milestones, test gates, and work
log are in [implementation.md](implementation.md).

## Status

The **1.0.0 teaching-toolchain release** is published at
<https://github.com/kevinwross/kr16/releases/tag/v1.0.0>. It includes the
Windows zip, VS Code VSIX, and SHA256SUMS.

Milestones M0–M12 are implemented. The working pipeline is:

```text
C or assembly → .o → .kex → VM
```

## Dead-code collection

`kr16 ld --gc-all` drops whole input objects that are unreachable from the
`_start`/`__start`/`start` entry symbol. It is object-level only: an unused
function in an otherwise-referenced object stays, because the object format and
compiler currently emit one `.text` per translation unit. The linker map lists
dropped global symbols as `dropped-symbol <name>`.

The compiler also omits a `static` function whose name occurs only at its
definition, and marks used static functions local instead of global. This
removes unused file-local helpers without linker GC; it does not remove an
unused external function from a partially used object.

## Host filesystem sandbox

`kr16 vm` exposes host file services inside `--fs-root` (default `./sandbox`).
`--fs-quota` limits the total bytes of all files under that root, not just the
files the guest writes. The default quota is 5 MiB, so pointing `--fs-root` at a
directory that already contains build outputs can make every write fail with
`SYS_ENOSPC`. Use a dedicated empty directory, or raise `--fs-quota` above the
size of the tree.

The C compiler currently implements a teaching subset: `int`/`char`/`_Bool`, pointers,
arrays, functions and prototypes, function pointers, object/function macros, `#`/`##`, conditionals,
quoted and built-in system headers, `-I` paths, `stdarg.h` variadics,
local/global/static variables, qualifiers, `if`/`else`, `while`, `do`/`while`,
`for`, `switch`, `enum`, `typedef`, `struct`/`union` (including by-value
parameters/returns and aggregate initializers), compound assignments, casts,
and `goto` labels. The assembler relaxes branches that leave the 8-bit `Bcc`
range, so large C functions assemble without splitting. A small `libc.s`
provides `putchar`, `puts`, `getchar`, buffered `fopen`/`fread`/`fwrite`/
`fclose`, `fprintf`, and `printf` with `%d`, `%i`, `%u`, `%x`, `%X`, `%o`,
`%p`, `%c`, `%s`, `%%`, width and padding, plus string functions, `atoi`,
`abs`, a first-fit heap allocator (`malloc`, `calloc`, `realloc`, `free`),
and `exit`.

`long` and `long long` are 32-bit types with 4-byte storage, `sizeof`,
add/subtract/multiply/divide, shifts, bitwise operations, comparisons, assignment, compound assignment, `++`/`--`, and constant
initializers. Two-word long parameters/returns and `printf` `%ld`/`%lu`/`%lx`
formatting and long `scanf` conversions are supported. See
[`docs/decisions/0005-32-bit-long.md`](docs/decisions/0005-32-bit-long.md).
`kr16 cc` uses `-O1` by default (constant folding, simple-expression codegen,
and a scalar IR/graph-coloring backend where supported); `-O0` keeps the
straightforward stack-based codegen for stepping and teaching. Debug builds
(`-g`) use the simpler backend so frame-based debug variables stay valid.
Floating point is explicitly out of scope (ADR 0004).
Strict C99 conformance, preprocessor pragma semantics, and
`fscanf`/`freopen`/`tmpfile` remain deferred beyond 1.0.

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

The settings dialog includes a **Command line arguments** field. It is parsed
with Windows-style quoting (backslashes are literal except before a quote), and
the resulting strings become `argv[1..argc-1]`; `argv[0]` is the generated image
name. The field supports selection, copy/cut/paste, and normal line editing.
Arguments are applied on Load or Restart. Guest file paths still use `/` inside
the sandbox; the argument parser preserves Windows paths but does not translate
host paths.

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
Windows exe, both AppImages, the VS Code VSIX, and `SHA256SUMS`.

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
host-call tests, compiled-C end-to-end execution tests, and the
[C-for-U compiler test](tests/kevin/C-for-U.c), which emits assembler and has
its generated program assembled, linked, and executed by CTest.

## License

MIT. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

















































