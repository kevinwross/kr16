# KR16 0.1.0 Early-Access Release Candidate

This is the first runnable teaching-toolchain candidate.

## Included

- 16-bit CPU core, memory map, console, keyboard, Timer 0, interrupts
- Container/CRC format and debug-object records
- Assembler and symbol-aware disassembler
- Linker with `.crt0`, first-page layout, map/strip options, and relocations
- VM with argv, scripted input, embedded reference BIOS, trace/stats, host calls
- `SYS_VERSION` and `ENOSYS` host services
- Teaching C subset with `-g` source lines
- Small libc: `putchar`, `puts`, `getchar`, `printf` subset, `exit`
- Minimal DAP TCP server and VS Code extension/VSIX
- Windows deterministic ZIP and Linux deterministic tar scripts
- Windows Debug/Release and Linux Debug/ASan/UBSan test matrix

## Known Limitations

- C is a teaching subset: no structs/unions/enums/typedefs, `long`,
  variadics, full preprocessor, or full libc.
- File host services are deferred.
- VS Code session is minimally tested; watch/advanced views are limited.
- The 1.0 release gate is not yet reached.
