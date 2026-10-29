# Deferred Features

This file records work that is intentionally outside the current teaching
subset so the implemented behavior is not mistaken for full C99 or a complete
release.

## Host and VM

- Full POSIX file API and buffered stdio. The current sandbox implements
  `SYS_OPEN`/`SYS_CLOSE`/`SYS_READ`/`SYS_WRITE`/`SYS_SEEK`/`SYS_STAT`,
  directory and namespace services, and an unbuffered `fopen`/`fread`/
  `fwrite`/`fclose` subset.
- Remaining stdio gaps: `fprintf`, `fscanf`, buffering, `setvbuf`, `ungetc`,
  `freopen`, `tmpfile`, and standard console `FILE` streams.
- BIOS RAM loading, embedded release BIOS selection, and linker scripts.
- `--gc-all`, relocatable `-r` links, weak/common symbols, and arm64 hosts.
- Real-time pacing accuracy and the full interactive monitor.
- DAP memory/disassembly/evaluate views, function-entry breakpoints, threaded
  console/DAP coordination, and live VSIX marketplace publishing. The minimal
  DAP TCP server, basic VS Code extension, and VSIX packaging exist.

## C language and runtime

- Full preprocessor: `#error`, `#line`, pragmas, and variadic macros.
- Diagnostics for an unresolvable `#include`: the preprocessor currently drops
  the directive silently, so a missing header turns undefined names into 0
  instead of failing the compile.
- Function pointers.
- True 32-bit `long`/`long long` arithmetic and helper routines. In 1.0,
  `long` is a documented 16-bit alias (ADR 0001).
- Full libc: the complete `printf` family, `errno` plumbing, and broader
  string/stdio coverage.
- Floating point, locale, signals, `setjmp`/`longjmp`, and wide characters.

## Release engineering

- Checked-in prebuilt BIOS/runtime resources and `manifest.json`.
- `bootstrap-check`/`bootstrap-update` and resource hash gates.
- Deterministic Windows zip, Linux tar.gz, VSIX, and `SHA256SUMS` packaging.
- Marketplace publishing, code signing, installers, and arm64 releases.

These items remain tracked in [implementation.md](../implementation.md) and the
design specification [kr16-plan.md](../kr16-plan.md).



