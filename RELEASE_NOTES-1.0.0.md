# KR16 1.0.0 — Teaching Toolchain Release

This is the 1.0 release of the KR16 16-bit teaching computer and toolchain.

## Included

- 16-bit CPU core, memory map, console, keyboard, Timer 0, interrupts
- Container/CRC format, debug objects, line/type/variable/frame metadata
- Two-pass assembler with macros, conditionals, `.crt0`, and listings
- Linker with first-page layout, relocations, map/strip options
- VM with argv, embedded BIOS, trace/statistics, host calls, and terminal I/O
- Teaching C subset, `stdarg.h`, built-in headers, and `-I` paths
- Libc: `putchar`, `puts`, `getchar`, `printf` subset, `strlen`, `strcpy`,
  `strcat`, `strcmp`, `memset`, `memcpy`, `atoi`, `abs`, `malloc`, `calloc`,
  `realloc`, `free`, `exit`
- DAP server, live VS Code extension, locals/globals, multi-frame stack walk
- Windows and Linux packages, deterministic archives, and CI matrices

## Language Scope

KR16 1.0 is a **teaching-subset release**, not a strict ISO C99 implementation.

- `long` and `long long` are 16-bit aliases for `int` (ADR 0001).
- Structs/unions, by-value parameters/returns, aggregate initializers,
  typedefs, enums, and variadic functions are supported.
- `#line`, pragmas, function pointers, floating point, and the full `printf`
  family remain deferred.

See `docs/decisions/0001-16-bit-long.md` and `docs/deferred.md`.

## Verification

The release is gated on Windows Debug/Release, Linux Debug/Clang/GCC
sanitizer matrices, the doctest/CTest suites, DAP smoke, and VS Code host
tests.
