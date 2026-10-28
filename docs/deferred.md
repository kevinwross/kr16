# Deferred Features

This file records work that is intentionally outside the current teaching
subset so the implemented behavior is not mistaken for full C99 or a complete
release.

## Host and VM

- Host file I/O (`SYS_OPEN`/`SYS_READ`/`SYS_WRITE`/`SYS_STAT` and friends).
  `SYS_VERSION` reports the host ABI; other services return `ENOSYS`.
- Sandbox policy (`--fs-root`, `--fs-readonly`, `--fs-quota`, `--no-host-fs`) is
  parsed but file services are not enabled.
- BIOS RAM loading, embedded release BIOS selection, and linker scripts.
- `--gc-all`, relocatable `-r` links, weak/common symbols, and arm64 hosts.
- Real-time pacing accuracy and the full interactive monitor.
- DAP memory/disassembly/evaluate views, function-entry breakpoints, threaded
  console/DAP coordination, and live VSIX marketplace publishing. The minimal
  DAP TCP server, basic VS Code extension, and VSIX packaging exist.

## C language and runtime

- Full preprocessor: `#include`, object/function macros, variadic macros,
  conditionals, `#error`, `#line`, and `#pragma once`.
- Structs, unions, qualifiers, and function pointers.
- `long`/32-bit arithmetic and the associated helper routines.
- Variadic C functions and `va_list`.
- Compiler-generated `.debug_types`, `.debug_vars`, and `.debug_frames`.
- Full libc: the complete `printf` family, `malloc`/`free`, string functions,
  std headers, and `errno` plumbing. The current libc has `putchar`, `puts`,
  `getchar`, `exit`, and a small `printf` supporting `%d`, `%s`, `%c`, and
  `%%`.
- Floating point, locale, signals, `setjmp`/`longjmp`, and wide characters.

## Release engineering

- Checked-in prebuilt BIOS/runtime resources and `manifest.json`.
- `bootstrap-check`/`bootstrap-update` and resource hash gates.
- Deterministic Windows zip, Linux tar.gz, VSIX, and `SHA256SUMS` packaging.
- Marketplace publishing, code signing, installers, and arm64 releases.

These items remain tracked in [implementation.md](../implementation.md) and the
design specification [kr16-plan.md](../kr16-plan.md).
