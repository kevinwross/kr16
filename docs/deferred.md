# Deferred Features

This file records work that is intentionally outside the current teaching
subset so the implemented behavior is not mistaken for full C99 or a complete
release.

## Host and VM

- Full POSIX file API. The sandbox implements
  `SYS_OPEN`/`SYS_CLOSE`/`SYS_READ`/`SYS_WRITE`/`SYS_SEEK`/`SYS_STAT`,
  directory and namespace services, and buffered `fopen`/`fread`/`fwrite`/
  `fclose` with `fprintf`, `ungetc`, `setvbuf`, and standard streams.
- Remaining stdio gaps: `fscanf`, `freopen`, `tmpfile`, and interactive stdin
  buffering. `printf`/`fprintf` formatting, `setvbuf`, and `ungetc` are
  implemented.
- BIOS RAM loading, embedded release BIOS selection, and linker scripts.
- Relocatable `-r` links, weak/common symbols, and arm64 hosts.
- Real-time pacing accuracy and the full interactive monitor.
- DAP memory/disassembly/evaluate views, function-entry breakpoints, threaded
  console/DAP coordination, and live VSIX marketplace publishing. The minimal
  DAP TCP server, basic VS Code extension, and VSIX packaging exist.

## C language and runtime

- Preprocessor pragmas and variadic macros.
- Function pointers.
- True 32-bit `long`/`long long` arithmetic and helper routines. In 1.0,
  `long` is a documented 16-bit alias (ADR 0001).
- Full libc: the complete `printf` family, `errno` plumbing, and broader
  string/stdio coverage.
- Floating point, locale, signals, `setjmp`/`longjmp`, and wide characters.

## Release engineering

- Checked-in prebuilt BIOS/runtime resources and `manifest.json`.
- `bootstrap-check`/`bootstrap-update` and resource hash gates.
- Marketplace publishing, code signing, deterministic Windows zip/Linux tar.gz
  packaging, installers, and arm64 releases.

These items remain tracked in [implementation.md](../implementation.md) and the
design specification [kr16-plan.md](../kr16-plan.md).





