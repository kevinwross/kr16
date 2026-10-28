# KR16

KR16 is a 16-bit teaching computer with a single-binary toolchain:

- `kr16 as` — assembler
- `kr16 dis` — disassembler
- `kr16 cc` — teaching-subset C compiler
- `kr16 ld` — linker
- `kr16 size` — memory budget report
- `kr16 debug` — decoded debug-information dump
- `kr16 vm` — virtual machine
- `kr16 dap` — minimal Debug Adapter Protocol TCP server

The design specification is [kr16-plan.md](kr16-plan.md). The implementation
plan, milestones, test gates, and work log are in
[implementation.md](implementation.md).

## Status

The **0.1.0 early-access release candidate** is published at
<https://github.com/kevinwross/kr16/releases/tag/v0.1.0>. It includes the
Windows zip, VS Code VSIX, and SHA256SUMS.

Milestones M0–M12 are implemented. The working pipeline is:

```text
C or assembly → .o → .kex → VM
```

The C compiler currently implements a teaching subset: `int`/`char`/`_Bool`, pointers,
arrays, function/variadic prototypes, object/function macros, `#`/`##` operators, conditionals, local `#include`, built-in system headers, `stdarg.h` variadics, `-I` include paths,  local/global/static variables, qualifiers, `if`/`else`, `while`, `do`/`while`,
`for`, `switch`/`case`/`default`, `enum` constants/types, `typedef` aliases, `struct`/`union` members, by-value struct parameters/returns and aggregate initializers and inferred array sizes, nested aggregate initializers, self-referential struct pointers, compound assignments, `goto` labels,
`return`, arithmetic/comparisons/logical operators, strings, and
`main(argc, argv)`. A small `libc.s` provides `putchar`, `puts`, `getchar`,
`printf` (`%d`, `%s`, `%c`, `%%`), `strlen`, `strcpy`, `strcat`, `strcmp`, `memset`,
`memcpy`, `atoi`, `abs`, `malloc`, `calloc`, `realloc`, `free`, and `exit`. `long`, the full preprocessor
(`#line`, pragmas), and the full `printf` family are not implemented yet.

The DAP TCP server, live VS Code debug session, locals/globals scopes,
multi-frame stack walking, memory/disassembly/evaluate views, and terminal
console separation are implemented.

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

C:

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

## Tests

Tests use vendored doctest and CTest. Test labels include `unit`, `golden`,
`integration`, `regression`, `fuzz`, and `slow`. The current suite includes
assembler/disassembler tests, CPU/device/interrupt tests, container fixtures,
linker relocation tests, VM boot/argv/headless tests, debug-line tests,
host-call tests, and compiled-C end-to-end execution tests.

## License

MIT. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).































