# KR16 Implementation Plan

Status: planning complete; implementation not started
Companion specification: [kr16-plan.md](kr16-plan.md)
Target release: `1.0.0` (first classroom release; development starts at `0.1.0`)
Last updated: 2026-10-27

## 1. Purpose and Rules

This document turns the design in `kr16-plan.md` into an ordered implementation
program. It is intentionally more granular than the roadmap in the design:
each milestone has a goal, a task list, required tests, regression tests, and
exit criteria.

Rules:

1. `kr16-plan.md` is the source of truth for behavior, formats, and CLI
   contracts. If implementation discovers a contract gap, update the design
   first, then implement.
2. Work one milestone at a time unless dependencies allow safe parallel work.
   Keep `main` buildable and testable.
3. Every behavior change includes tests. Every fixed bug gets a regression test
   that fails without the fix.
4. A milestone is not complete until its exit criteria and the global
   definition of done are satisfied.
5. Append to the log at the bottom of this file at the end of each work
   session. Do not rewrite prior log entries; corrections go in a new entry.
6. Do not silently skip a failing test. Mark it with a tracking issue and a
   reason, or fix it.

## 2. Current Status

| Milestone | Scope | Status | Gate |
|---|---|---|---|
| M0 | Repository, build skeleton, ISA freeze | Not started | Build and spec gate |
| M1 | Container, CRC, diagnostics | Not started | Container round-trip gate |
| M2 | ISA core and CPU | Not started | ISA conformance gate |
| M3 | Assembler and disassembler | Not started | Raw-program gate |
| M4 | Memory, devices, interrupts, boot | Not started | Device/boot gate |
| M5 | VM host and CLI | Not started | Batch/monitor gate |
| M6 | Linker and runtime linking | Not started | `.kex` toolchain gate |
| M7 | Debug information and `kr16 debug` | Not started | Debug-format gate |
| M8 | C front end | Not started | C front-end gate |
| M9 | C backend, ABI, and runtime | Not started | C execution gate |
| M10 | Host services (`SYSCALL`) | Not started | Host-call gate |
| M11 | DAP and VS Code extension | Not started | Debug-session gate |
| M12 | Bootstrap, CI, and release | Not started | Release-candidate gate |
| M13 | Hardening, curriculum, `1.0.0` | Not started | 1.0 gate |

Dependency order:

```text
M0 -> M1 -> M2 -> M3 -> M4 -> M5 -> M6 -> M7 -> M8 -> M9 -> M10 -> M11 -> M12 -> M13
```

M8 can start after M3 in parallel with M4–M7. The VS Code extension shell can
start after M7, but end-to-end DAP work waits for M10/M11.

## 3. Global Definition of Done

Every milestone must satisfy:

- Code compiles with warnings-as-errors on Windows and Linux.
- New unit, golden, integration, and regression tests pass.
- Existing regression suite remains green.
- Linux ASan/UBSan run is clean.
- No known P0/P1 defects in the milestone scope.
- Docs and CLI help reflect the new behavior.
- Design changes, if any, are already in `kr16-plan.md`.
- The log has an entry for the completed milestone.

A release gate additionally requires:

- Full CTest suite on Windows x64 and Linux x64, Debug and Release.
- Deterministic binary/resource/archive hashes where applicable.
- Headless integration suite with scripted input.
- Manual smoke test of the classroom path.
- No known data-loss, security, or reproducibility issues.

## 4. Test Strategy

### 4.1 Test layers

| Layer | Location | Purpose |
|---|---|---|
| Unit | `tests/unit/` | ISA, container, parser, linker, VM, DAP protocol |
| Golden | `tests/golden/` | Exact bytes, maps, listings, debug dumps |
| Integration | `tests/integration/` | Assemble/link/run programs headlessly |
| Regression | `tests/regression/` | Every fixed bug, named by issue |
| Conformance | `tests/conformance/` | ISA encodings, ABI calls, container layout |
| Fuzz | `tests/fuzz/` | Container, assembler, compiler, DAP parsers |
| Cross-platform | CI | Windows/Linux parity |
| Determinism | CI | Repeat builds/archives and compare hashes |

### 4.2 Rules

- Use vendored doctest for unit tests and CTest for registration.
- Test labels: `unit`, `golden`, `integration`, `regression`, `fuzz`, `slow`.
- Integration tests run `kr16 vm --headless` and compare stdout, stderr, and
  exit code. They must not require a TTY.
- Golden files are checked in and reviewed like code.
- A regression test name includes a stable issue key, for example
  `reg-container-0007-bad-crc`.
- Fuzz targets run with a time/iteration cap in CI; longer runs are manual.
- No test may depend on wall-clock timing. Timer tests use simulated cycles.
- No test may depend on the developer's CWD; use temp directories.
- Windows path tests use short paths and avoid case-only duplicate names where
  the filesystem is case-insensitive.

### 4.3 Standard commands

Exact preset names are fixed in M0. The expected forms are:

```text
cmake --preset windows-vs2026
cmake --build --preset windows-vs2026-debug
ctest --preset windows-vs2026-debug

cmake --preset linux-gcc
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug

cmake --build --preset linux-gcc-sanitize
ctest --preset linux-gcc-sanitize
```

## 5. Milestones

### M0 — Repository, Build Skeleton, and ISA Freeze

**Goal:** Create the project skeleton, build/test infrastructure, CLI entry
point, and normative ISA document so implementation can start without format
churn.

**Dependencies:** none.

**Tasks:**

- [x] Create the repository layout: `src/`, `include/`, `tests/`, `cmake/`,
      `third_party/doctest/`, `resources/prebuilt/`, `vscode/`, `docs/`,
      `examples/`.
- [x] Add `LICENSE` (MIT) and third-party notices for doctest.
- [x] Add CMake 3.25+ project with presets for `windows-vs2026`,
      `windows-ninja`, `linux-gcc`, and `linux-clang`.
- [x] Add Debug, Release, and Linux sanitizer presets.
- [x] Vendor doctest; register CTest and test labels.
- [ ] Add warnings-as-errors and the `platform/` boundary rule.
- [ ] Implement the `kr16` CLI dispatcher skeleton with `help`, `version`, and
      unknown-command handling.
- [ ] Fix the `kr16 version` format:
      `kr16 <semver> (ISA 1, container 1, host ABI 1, debug 1)`.
- [ ] Freeze instruction encodings, mnemonics, flags, cycle counts, memory map,
      I/O map, and BIOS jump-table numbering.
- [ ] Write `docs/isa.md` as the normative ISA reference.
- [ ] Add GitHub Actions workflows for `windows-2025` and `ubuntu-22.04`,
      including push, PR, and `v*` tag triggers.
- [ ] Add `.gitignore` for build outputs; do not ignore checked-in resources.
- [ ] Add this implementation plan and its initial log entry.

**Testing:**

- Configure and build with at least one Windows preset and one Linux preset.
- Run `ctest` and verify the doctest smoke test executes.
- Check `kr16 help`, `kr16 version`, and an unknown command's exit code.
- Validate CI YAML locally where possible; run the workflow on a branch.
- Build twice from clean trees and compare the `kr16` binary hash.

**Regression tests:**

- `reg-cli-0001-help-version`
- `reg-build-0002-preset-smoke`
- `reg-build-0003-deterministic-skeleton`

**Exit criteria:**

- Both platforms configure, build, and pass smoke tests.
- `docs/isa.md` is reviewed and matches the design.
- Version/help output is stable and tested.
- CI is green on a PR.

---

### M1 — Container, CRC, and Diagnostics

**Goal:** Implement the self-describing `.o`/`.kex` container reader/writer and
the shared byte-level diagnostics used everywhere.

**Dependencies:** M0.

**Tasks:**

- [ ] Add byte reader/writer utilities with explicit little-endian handling and
      bounds checks.
- [ ] Add the common diagnostic/result type used by assembler, linker, VM, and
      DAP.
- [ ] Implement the fixed 64-byte header with all offsets from Section 6.7.
- [ ] Implement 32-byte section headers, 20-byte symbols, and 16-byte
      relocations.
- [ ] Implement `.debug_objects` header (16), object records (48), and source
      records (12).
- [ ] Implement `.debug_str` as the single shared, first-use-deduplicated string
      table; wire symbol and section names to it.
- [ ] Implement CRC32 over the entire file with the checksum field zeroed.
- [ ] Implement version/reserved handling: same/newer minor accepted, unknown
      major rejected, optional flags ignored, reserved fields zero.
- [ ] Implement REL/EXEC validation and table bounds checking.
- [ ] Add container dump/diagnostic output used by tests.
- [ ] Add golden `.o` and `.kex` fixtures for valid and malformed files.

**Testing:**

- Round-trip every field and table; assert exact offsets and sizes.
- Verify CRC over loadable data, debug data, padding, and header.
- Test string dedup order and offsets.
- Test unknown major rejection, newer minor acceptance, reserved bits, and
  malformed counts/offsets.
- Fuzz random bytes; no crashes, only diagnostics.
- Verify 32-bit offsets/sizes and 16-bit guest fields.

**Regression tests:**

- `reg-container-0101-header-roundtrip`
- `reg-container-0102-crc-whole-file`
- `reg-container-0103-string-dedup`
- `reg-container-0104-bad-version`
- `reg-container-0105-truncated-table`

**Exit criteria:**

- Reader/writer round-trips all fixtures byte-for-byte.
- Malformed files fail cleanly with useful errors.
- Fuzz smoke run is clean.
- Golden files are checked in.

---

### M2 — ISA Core and CPU

**Goal:** Implement instruction encoding/decoding, CPU state, ALU/control
execution, flags, exceptions, and cycle accounting.

**Dependencies:** M1.

**Tasks:**

- [ ] Add the ISA table and encoder/decoder for R, I, M, B, M8, S, BIT, C, and
      X formats.
- [ ] Implement the C-type `SYSCALL #n` decode path.
- [ ] Implement registers R0–R4, PC, SP, SR, flags, and reset state.
- [ ] Implement memory bus hooks and RAM/ROM/MMIO access policy.
- [ ] Implement arithmetic, logic, shifts, bit operations, and compare/test.
- [ ] Implement `MUL`, `MULS`, `MULU`, `DIV`, `DIVU`, `MOD`, and `MODU`,
      including pair rules and illegal encodings.
- [ ] Implement loads/stores, indexed/absolute/PC-relative forms, and byte
      variants.
- [ ] Implement branches, `JMP`, `CALL`, `RET`, `TRAP`, `BRK`, `HLT`, `EI`,
      `DI`, `PUSH`, and `POP`.
- [ ] Implement exception vectors and illegal-instruction handling.
- [ ] Implement exact cycle counts from the design.
- [ ] Add a CPU test harness that executes raw instruction sequences.

**Testing:**

- Per-instruction unit tests with normal, boundary, and flag-sensitive values.
- Encode/decode round-trip for every mnemonic and form.
- Random 16/32-bit decode fuzzing; invalid encodings must not crash.
- Flag truth tables for C, Z, N, V; signed/unsigned edge cases.
- Pair multiply legal/illegal bases and multipliers.
- Division by zero, signed overflow, and unaligned access costs.
- Program-level execution tests for loops, calls, stack, and branches.

**Regression tests:**

- `reg-cpu-0201-flags`
- `reg-cpu-0202-pair-mul`
- `reg-cpu-0203-divide-by-zero`
- `reg-cpu-0204-illegal-encoding`
- `reg-cpu-0205-cycle-counts`

**Exit criteria:**

- ISA conformance suite passes.
- All instructions have encode/decode/execute coverage.
- No decoder crash on fuzz input.
- Cycle counts match the design.

---

### M3 — Assembler and Disassembler

**Goal:** Deliver `kr16 as` and `kr16 dis`: source to relocatable/raw object,
and readable symbol-aware disassembly.

**Dependencies:** M2.

**Tasks:**

- [ ] Implement lexer, expression parser, operator precedence, and numeric
      literals.
- [ ] Implement labels, local labels, `.L` handling, symbols, and `.equ`.
- [ ] Implement sections, `.text`/`.data`/`.rodata`/`.bss`, `.section`, and
      alignment directives.
- [ ] Implement data directives, strings, `.file`, `.loc`, `.type`, `.size`,
      `.global`, `.extern`, and `.include`.
- [ ] Implement `.macro`/`.endm`, `.if`/`.else`/`.endif`, and `.org`.
- [ ] Implement relocation generation for `ABS16`, `PCREL8W`, `PCREL16W`, and
      `PCREL16B`.
- [ ] Implement smallest-form selection and local branch relaxation.
- [ ] Implement `--raw --base` output for BIOS and small tests.
- [ ] Implement `-g` line-table generation from `.file`/`.loc`.
- [ ] Implement diagnostics with `file:line:column`, caret, and listing output.
- [ ] Implement symbol-aware disassembly, `--source`, `--reloc`, and `--no-hex`.
- [ ] Implement `SYSCALL` symbolic names in disassembly and listings.
- [ ] Implement round-trip assembly of disassembler output.

**Testing:**

- Golden assembly files with exact bytes and listings.
- Expression precedence and constant-folding tests.
- Directive and section tests, including raw/`.org` placement.
- Relocation tables for every relocation type and boundary.
- Local labels, static symbols, and `-g` retention.
- Diagnostics tests for bad operands, bad sizes, duplicate labels, and
      out-of-range values.
- Assemble → disassemble → assemble byte comparison.
- Error corpus with no crashes and stable messages.

**Regression tests:**

- `reg-asm-0301-expressions`
- `reg-asm-0302-relocations`
- `reg-asm-0303-local-labels`
- `reg-asm-0304-raw-base`
- `reg-asm-0305-roundtrip`
- `reg-dis-0306-symbol-names`

**Exit criteria:**

- Hand-written assembly programs assemble and run as raw images.
- Disassembly round-trips to identical bytes.
- Golden tests and diagnostics are stable.
- Raw BIOS-style images can be produced.

---

### M4 — Memory, Devices, Interrupts, and Boot

**Goal:** Make the VM capable of booting and running assembly programs with
console, keyboard, timer, interrupts, and the reference BIOS path.

**Dependencies:** M3.

**Tasks:**

- [ ] Implement the RAM/ROM/MMIO map and reset values.
- [ ] Implement console output MMIO and byte queue.
- [ ] Implement keyboard FIFO, raw key input, `--line-input` fallback, and
      `getchar` blocking semantics.
- [ ] Implement Timer 0, prescaler, compare/reload, and interrupt flags.
- [ ] Implement interrupt vectors, I-priority, pending checks, and `RETI`.
- [ ] Implement reset: PC, SP, SR, GPRs, I/O defaults, RAM zeroing.
- [ ] Implement BIOS loading, embedded default, `--bios`, `--no-bios`, and
      `load-bios`.
- [ ] Implement the first-page vector/jump-table initialization rules.
- [ ] Build the minimal stateless reference BIOS.
- [ ] Implement command-line argv block construction and R1/R2 entry registers.
- [ ] Add headless scripted keyboard input.

**Testing:**

- Device register reset and read/write behavior.
- Timer prescaler and deterministic cycle counting.
- Keyboard FIFO ordering and blocking behavior.
- Interrupt priority, masking, and nesting rules.
- Reset/BIOS/no-BIOS boot paths and first-page contents.
- BIOS preserves R1/R2/SP; argv reaches the program.
- Console output and keyboard input under headless mode.
- Raw assembly programs exercising timer and keyboard interrupts.

**Regression tests:**

- `reg-dev-0401-timer-deterministic`
- `reg-dev-0402-keyboard-fifo`
- `reg-dev-0403-interrupt-priority`
- `reg-dev-0404-bios-boot`
- `reg-dev-0405-argv-entry`

**Exit criteria:**

- A hand-written assembly program can use console, keyboard, timer, and
  interrupts under the VM.
- BIOS/no-BIOS boot tests pass.
- Headless input is deterministic.

---

### M5 — VM Host and CLI

**Goal:** Deliver the usable `kr16 vm` runner, monitor, tracing, timing, and
diagnostics.

**Dependencies:** M4.

**Tasks:**

- [ ] Implement `.kex`/raw loading, validation, and memory layout.
- [ ] Implement the run loop, halt/exit codes, and `--max-cycles`.
- [ ] Implement stack guard and heap-stack collision checks.
- [ ] Implement `--trace`, `--stats`, `--realtime`, and `--mhz`.
- [ ] Implement the `-i` monitor: `run`, `pause`, `step`, `regs`, `mem`,
      `reset`, `load-bios`, `quit`.
- [ ] Implement raw/line console modes and `Ctrl+C` behavior.
- [ ] Implement headless mode and scripted input.
- [ ] Implement clear load/run diagnostics for malformed images and bad BIOS.
- [ ] Implement `kr16 size` budget reporting if not already stubbed.

**Testing:**

- Batch runs of assembly integration programs with expected stdout/exit code.
- Monitor command scripts and state transitions.
- `--max-cycles` timeout and cycle counts.
- `--trace`/`--stats` output stability.
- Stack guard and heap collision tests.
- Raw/line input tests, including no-TTY fallback.
- Windows/Linux terminal behavior where available.

**Regression tests:**

- `reg-vm-0501-batch-exit`
- `reg-vm-0502-monitor-script`
- `reg-vm-0503-max-cycles`
- `reg-vm-0504-stack-guard`
- `reg-vm-0505-headless-input`

**Exit criteria:**

- Assembly `.kex`/raw programs run reliably.
- Monitor and headless modes are usable and tested.
- Diagnostics are clear and nonzero exit codes are correct.

---

### M6 — Linker and Runtime Linking

**Goal:** Deliver `kr16 ld`: relocatable objects to `.kex`, with runtime
auto-linking, map/size reporting, ROM images, and strict layout rules.

**Dependencies:** M5.

**Tasks:**

- [ ] Implement input object loading, section merging, and fixed layout order.
- [ ] Implement maximum input alignment and deterministic input order.
- [ ] Implement symbol resolution: strong-only, object-scoped locals,
      duplicate/undefined diagnostics.
- [ ] Implement relocations with addends and hard range errors.
- [ ] Implement entry selection: user `_start` wins; otherwise `crt0` when
      `main` is present; `--entry` override.
- [ ] Implement auto-linking of `crt0`/libc and `--nostdlib`.
- [ ] Implement first-page `.vectors`/`.bios_jump` strict sections.
- [ ] Implement heap/stack symbols and `--stack-reserve`.
- [ ] Implement `--map`, `kr16 size`, `--strip`, and `--strip-debug`.
- [ ] Implement `--rom` stateless BIOS output and `--raw --base`.
- [ ] Implement deterministic output and reproducible layouts.
- [ ] Add link-error diagnostics with object/section context.

**Testing:**

- Layout order, alignment, first-page reservation, and overflow errors.
- Symbol resolution and duplicate/undefined cases.
- All relocation types, boundary values, and overflow diagnostics.
- Entry rules: `_start`, `main`+`crt0`, both, `--entry`, `--nostdlib`.
- Runtime auto-link and unused runtime section behavior.
- `.vectors`/`.bios_jump` size/overlap tests.
- Map/size output and ROM output.
- Deterministic repeated links; golden `.kex`.

**Regression tests:**

- `reg-link-0601-layout`
- `reg-link-0602-reloc-range`
- `reg-link-0603-entry-start`
- `reg-link-0604-first-page`
- `reg-link-0605-strip`
- `reg-link-0606-rom-stateless`

**Exit criteria:**

- Hand-written assembly objects link and run as `.kex`.
- Runtime auto-link path works.
- Layout, map, size, and errors are stable.
- ROM output matches the expected 3,840-byte image rules.

---

### M7 — Debug Information and `kr16 debug`

**Goal:** Implement the fixed debug sections, linker merge, source table, and
the human-readable debug dumper needed by the DAP.

**Dependencies:** M6.

**Tasks:**

- [ ] Implement assembler `.debug_line` rows from `.file`/`.loc`.
- [ ] Implement fixed `.debug_types`, `.debug_vars`, and `.debug_frames`
      records.
- [ ] Implement `.debug_objects` header, 48-byte object records, and 12-byte
      source records.
- [ ] Implement `.debug_str` dedup and 32-bit offsets.
- [ ] Implement linker merging/relocation of debug sections.
- [ ] Implement source-path normalization, build-directory storage, and
      absolute fallback.
- [ ] Implement `kr16 debug` filters: `--lines`, `--types`, `--vars`,
      `--frames`, `--objects`.
- [ ] Keep `--json` deferred; ensure the text format is stable and tested.
- [ ] Add synthetic debug fixtures for types, scopes, frames, and parameters.

**Testing:**

- Golden line tables and debug dumps.
- Object directory 16/48/12 layout and source table offsets.
- Type/variable/frame record round-trip.
- String dedup and source-path normalization.
- Parameters are FP-relative after the prologue.
- `--strip`/`--strip-debug` behavior.
- Malformed debug sections fail cleanly.

**Regression tests:**

- `reg-debug-0701-line-table`
- `reg-debug-0702-object-directory`
- `reg-debug-0703-string-dedup`
- `reg-debug-0704-source-paths`
- `reg-debug-0705-strip-debug`

**Exit criteria:**

- `kr16 debug` correctly dumps fixtures.
- Linker merge is deterministic.
- The DAP-facing model is complete enough to implement M11.

---

### M8 — C Front End

**Goal:** Preprocess, parse, and semantically analyze the C99 subset, producing
a typed AST and assembly skeleton.

**Dependencies:** M3; can run in parallel with M4–M7.

**Tasks:**

- [ ] Implement `kr16 cc` driver and `-E`, `-S`, `-c`, `-o`, `-I`, `-D`, `-U`,
      `-g`, `-O0`, and `-std=c99`.
- [ ] Implement the preprocessor: includes, object/function macros, `#`/`##`,
      variadic macros, conditionals, `defined`, `#error`, `#warning`, `#line`,
      and `#pragma once`.
- [ ] Add built-in headers: `stdint.h`, `stddef.h`, `stdbool.h`, `stdarg.h`,
      `limits.h`, and `assert.h`.
- [ ] Implement lexer/parser for declarations, expressions, statements,
      functions, arrays, pointers, structs, unions, enums, and typedefs.
- [ ] Implement scopes, name lookup, typedef disambiguation, qualifiers, and
      initializers.
- [ ] Implement type layout: char 8, short/int 16, long 32, pointers 16,
      natural alignment, struct/union padding.
- [ ] Implement integer promotions, usual conversions, constant expressions,
      and `sizeof`.
- [ ] Implement diagnostics with source locations and recovery.
- [ ] Emit an assembly skeleton from the typed AST (full codegen in M9).
- [ ] Add parser/type-layout golden tests.

**Testing:**

- Preprocessor golden tests for macros, conditionals, includes, and errors.
- Parser tests for declarations and the classic C ambiguities.
- Type layout tests including nested arrays, structs, unions, and `long`.
- Constant-expression and `sizeof` tests.
- Diagnostics tests for malformed C and unsupported features.
- `-E` and `-S` golden output; compile-only corpus.
- No codegen correctness expected yet; no undefined behavior.

**Regression tests:**

- `reg-cc-0801-preprocessor`
- `reg-cc-0802-parser-ambiguity`
- `reg-cc-0803-type-layout`
- `reg-cc-0804-constant-fold`
- `reg-cc-0805-diagnostics`

**Exit criteria:**

- The C corpus parses and type-checks.
- `-E` is usable and tested.
- `-S` produces valid assembly skeletons.
- Unsupported features produce clear errors.

---

### M9 — C Backend, ABI, and Runtime

**Goal:** Compile and run C programs end-to-end with the documented ABI, libc,
`crt0`, `main(argc, argv)`, and debug info.

**Dependencies:** M8, M6, M7.

**Tasks:**

- [ ] Implement `-O0` codegen for expressions, lvalues, assignments, calls,
      control flow, arrays, structs, unions, pointers, and `long`.
- [ ] Implement the C ABI: 16-bit scalar/pointer args in R1–R3, stack-only
      `long`/aggregates, right-to-left/caller cleanup, hidden struct return.
- [ ] Implement variadic all-stack ABI, `va_list`, `va_start`, `va_arg`,
      `va_end`, and `va_copy`; no aggregate `va_arg`.
- [ ] Implement parameter spill before the first `.loc` and function-entry
      breakpoint semantics.
- [ ] Implement `crt0`, `_start`, `.bss` zeroing, vectors, `exit`, and halt.
- [ ] Implement `main(void)` and `main(int argc, char **argv)` via R1/R2.
- [ ] Implement the practical libc: printf family, puts/putchar/getchar,
      malloc/free/calloc/realloc, string functions, and std headers.
- [ ] Implement 32-bit helpers for divide/modulo/shifts.
- [ ] Emit fixed `.debug_types`, `.debug_vars`, `.debug_frames`, and `.loc`
      data.
- [ ] Implement `__host_call` intrinsic for later host services.

**Testing:**

- ABI conformance tests for argument registers, stack layout, return values,
  structs, `long`, and varargs.
- C feature programs: hello, loops, arrays, structs, unions, enums, pointers,
  function pointers, recursion, malloc, printf, and `argc`/`argv`.
- Compare expected stdout and exit codes under headless VM.
- Golden assembly for representative C programs.
- Debug-location tests for parameters and locals.
- 32-bit arithmetic edge cases and helper functions.
- ASan/UBSan on host-side compiler code.

**Regression tests:**

- `reg-abi-0901-arg-regs`
- `reg-abi-0902-stack-order`
- `reg-abi-0903-struct-return`
- `reg-abi-0904-varargs`
- `reg-abi-0905-argc-argv`
- `reg-c-0906-printf`
- `reg-c-0907-malloc`

**Exit criteria:**

- The C integration corpus runs correctly.
- ABI tests pass.
- `main(argc, argv)` works.
- Debug locations are correct at `-O0`.
- The classroom hello-world and data-structure examples work.

---

### M10 — Host Services (`SYSCALL`)

**Goal:** Implement the VM host-call instruction path, version query, trace,
and the stable ABI envelope. File services remain deferred to the post-v1 file
phase.

**Dependencies:** M9.

**Tasks:**

- [ ] Implement `SYSCALL` execution: R0 block pointer, 16-byte block,
      status/results/args, 10-cycle cost, flags unchanged.
- [ ] Implement block/pointer validation and `EFAULT`.
- [ ] Implement `SYS_VERSION` with ABI and capability bits.
- [ ] Return `ENOSYS` for all unimplemented services.
- [ ] Implement `--trace-syscalls`.
- [ ] Add the `HostFs`/sandbox interface stubs and `--fs-root`,
      `--fs-readonly`, `--fs-quota`, and `--no-host-fs` parsing, without
      enabling file services.
- [ ] Wire the C `__host_call` intrinsic and `errno` plumbing.
- [ ] Ensure DAP can step over `SYSCALL`.

**Testing:**

- Valid `SYS_VERSION` call and capability bits.
- Unimplemented service returns `ENOSYS`.
- Invalid block pointer, alignment, and bounds return `EFAULT`.
- Flags, PC, and cycle count after `SYSCALL`.
- `--trace-syscalls` output.
- C intrinsic round-trip.
- Regression under DAP stepping later.

**Regression tests:**

- `reg-syscall-1001-version`
- `reg-syscall-1002-enosys`
- `reg-syscall-1003-efault`
- `reg-syscall-1004-trace`

**Exit criteria:**

- Assembly and C can query `SYS_VERSION`.
- Unimplemented services fail cleanly.
- File-service flags are accepted but file calls are documented as deferred.

---

### M11 — DAP and VS Code Extension

**Goal:** Deliver a working debug session in VS Code while the program keeps
its console I/O.

**Dependencies:** M7, M10.

**Tasks:**

- [ ] Implement the DAP TCP server on `127.0.0.1:22152`, one client,
      Content-Length framing, and `--port` override.
- [ ] Implement VM/DAP/console threads and the command queue.
- [ ] Implement launch-paused startup, `initialize`/`launch`/
      `configurationDone`, and disconnect/re-listen.
- [ ] Implement source/address/function breakpoints.
- [ ] Implement continue, pause, step in/over/out, and single instruction.
- [ ] Implement stack walks, scopes, locals/params/globals, registers, memory,
      disassembly, evaluate/watch, and host-call trace.
- [ ] Implement function-entry breakpoints after the prologue.
- [ ] Keep program output on the terminal; DAP output is debugger messages only.
- [ ] Build the separate `vscode/` TypeScript project with
      `DebugAdapterServer(22152)`, terminal launch, launch configuration, and
      manual attach.
- [ ] Package with `vsce`; do not launch the host hidden for interactive
      programs.
- [ ] Add scripted DAP client tests.

**Testing:**

- Protocol tests: initialize, launch, breakpoints, stepping, stack, variables,
  registers, memory, disassembly, evaluate, disconnect.
- Launch/attach and port-conflict tests.
- Console separation: program output on terminal, not DAP `output`.
- Function-entry parameter visibility.
- Windows/Linux integrated-terminal smoke tests.
- VSIX install and manual debug session.

**Regression tests:**

- `reg-dap-1101-protocol`
- `reg-dap-1102-breakpoints`
- `reg-dap-1103-stack-vars`
- `reg-dap-1104-console-split`
- `reg-dap-1105-disconnect`

**Exit criteria:**

- A student can debug a C program in VS Code with breakpoints, stepping, stack,
  locals, and registers.
- Program I/O remains in the terminal.
- Disconnect/reconnect works.
- VSIX is packaged and installable.

---

### M12 — Bootstrap, CI, and Release

**Goal:** Make the project reproducible, bootstrappable, and shippable as a
release candidate.

**Dependencies:** M11.

**Tasks:**

- [ ] Check in prebuilt BIOS/runtime resources and `manifest.json`.
- [ ] Implement generated C++ resource arrays and `std::span` access.
- [ ] Implement `bootstrap-check` (diff) and `bootstrap-update` (write).
- [ ] Implement manifest generation with tool/format versions, source hash,
      and resource SHA256; no timestamps.
- [ ] Implement deterministic binaries and archives with
      `SOURCE_DATE_EPOCH`.
- [ ] Implement release packaging: Windows zip, Linux tar.gz, VSIX,
      SHA256SUMS, README, docs, examples, LICENSE.
- [ ] Implement `kr16 version` human output and dev git hash.
- [ ] Wire GitHub Actions: windows-2025, ubuntu-22.04, push/PR/tags, release on
      `v*` tags, sanitizers, bootstrap/resource/archive hash checks.
- [ ] Add MIT license and third-party notices.
- [ ] Produce a `0.1.0` early-access release candidate.

**Testing:**

- `bootstrap-check` byte-identical in CI.
- Two clean builds produce identical binaries and resources.
- Deterministic zip/tar/VSIX hashes.
- Release archive installs/run on a clean Windows/Linux environment.
- `kr16 version` output.
- CI matrix Debug/Release and sanitizers.
- SHA256SUMS verification.

**Regression tests:**

- `reg-release-1201-bootstrap-drift`
- `reg-release-1202-archive-hash`
- `reg-release-1203-version-output`
- `reg-release-1204-manifest`

**Exit criteria:**

- Release candidate artifacts are built and verified.
- CI is green on Windows/Linux.
- Bootstrap drift fails CI.
- A clean-machine smoke test passes.

---

### M13 — Hardening, Curriculum, and `1.0.0`

**Goal:** Harden the toolchain, finish examples/docs, run the full matrix, and
ship `1.0.0`.

**Dependencies:** M12.

**Tasks:**

- [ ] Fuzz container, assembler, compiler, linker, and DAP parsers.
- [ ] Add performance benchmarks for VM speed, assemble/link time, and binary
      size.
- [ ] Add examples and curriculum notes for assembly, C, interrupts, and
      debugging.
- [ ] Polish diagnostics and error messages.
- [ ] Run the full regression suite on Windows/Linux, Debug/Release,
      sanitizers.
- [ ] Perform a classroom smoke test: install, compile, run, debug.
- [ ] Verify deterministic release artifacts and SHA256SUMS.
- [ ] Document deferred features: host file I/O, BIOS RAM, linker scripts,
      `--gc-all`, `-r`, weak/common, arm64, installers, marketplace, signing.
- [ ] Tag and publish `1.0.0`.
- [ ] Append the final log entry.

**Testing:**

- Full test matrix.
- Fuzz smoke runs with no crashes.
- Performance sanity thresholds.
- Manual classroom workflow on a clean machine.
- Release artifact install/run.
- Regression suite across all milestones.

**Regression tests:**

- Add a final `reg-1.0-*` set for the release smoke path.
- Re-run every prior regression test.

**Exit criteria:**

- `1.0.0` artifacts published.
- All release gates pass.
- No known P0/P1 defects.
- Documentation and examples are complete enough for a classroom.

## 6. Release Gates

| Gate | After | Requirement |
|---|---|---|
| A | M3 | CPU, assembler, disassembler, and raw images work |
| B | M6 | Assembly toolchain produces and runs `.kex` files |
| C | M9 | C programs compile, link, run, and pass ABI tests |
| D | M11 | VS Code debug session works with console I/O separated |
| E | M12 | Reproducible release candidate is packaged |
| F | M13 | `1.0.0` passes the full matrix and classroom smoke test |

At each gate:

- Run the full CTest suite and the accumulated regression suite.
- Run Linux ASan/UBSan.
- Run Windows Release and Debug.
- Re-run the deterministic/bootstrap checks where applicable.
- Record the result in the log.

## 7. Risk Register

| Risk | Impact | Mitigation |
|---|---|---|
| Contract drift during implementation | Rework across tools | Update `kr16-plan.md` first; golden conformance tests |
| C ABI/codegen complexity | Late C failures | ABI tests early in M9; small C corpus grows continuously |
| TTY/console behavior differs | Debug workflow breaks | Integrated-terminal tests; `--line-input` fallback; manual smoke |
| Bootstrap resources drift | Non-reproducible releases | `bootstrap-check`; manifest hashes; CI hash comparison |
| Windows/Linux path differences | Debugger/source failures | Relative source paths; case-insensitive dedup; cross-platform tests |
| Scope creep into deferred features | 1.0 slips | Deferred list in M13; no file I/O, installers, arm64, marketplace in v1 |

## 8. Post-v1 Backlog

Tracked but intentionally outside `1.0.0`:

- Host filesystem services, `FILE*`, and the `./sandbox` quota implementation.
- BIOS RAM/`--bios-ram` and stateful ROM data.
- Linker scripts, `--gc-all`, partial linking `-r`, weak/common symbols.
- JSON map and `kr16 debug --json`/`kr16 version --json`.
- VS Code marketplace publishing, code signing, MSI/deb/rpm installers.
- Linux arm64 and Windows arm64 builds.
- `-O1`, location lists, additional debugger features.
- More devices and more C library coverage.

## 9. Log

Append-only. Add a new entry at the bottom at the end of each work session.
Each entry starts with the local date and time, including the UTC offset.
Do not rewrite previous entries; corrections belong in a new entry.

Entry format:

```text
### YYYY-MM-DD HH:MM:SS ±HH:MM — <milestone or summary>
- Status:
- Changes:
- Tests:
- Regressions:
- Next:
```

### 2026-10-27 18:32:56 -07:00 — Implementation plan created

- Status: planning complete; M0 not started.
- Changes: created `implementation.md` with milestones, task lists, testing,
  regression expectations, release gates, and a log.
- Tests: none; planning only.
- Regressions: none.
- Next: start M0 — repository/build skeleton and ISA freeze.

### 2026-10-27 18:37:42 -07:00 — Log timestamp format updated

- Status: documentation-only change.
- Changes: Section 9 now requires a local date, time, and UTC offset on every
  entry; the initial entry was updated to the same format.
- Tests: none; Markdown-only.
- Regressions: none.
- Next: start M0 — repository/build skeleton and ISA freeze.

### 2026-10-27 18:40:58 -07:00 — M0: repository layout

- Status: M0 in progress.
- Changes: created the source/test/docs/examples/resources/vscode/cmake
  directory skeleton and placeholder READMEs.
- Tests: none; documentation-only step.
- Regressions: none.
- Next: license and third-party notices.

### 2026-10-27 18:41:14 -07:00 — M0: license and third-party notices

- Status: M0 in progress.
- Changes: added the MIT `LICENSE`, `THIRD_PARTY_NOTICES.md`, and the vendored
  doctest license notice.
- Tests: none; documentation-only step.
- Regressions: none.
- Next: CMake project and presets.

### 2026-10-27 18:42:10 -07:00 — M0: CMake project and presets

- Status: M0 in progress.
- Changes: added CMake 3.25+ project, version library, minimal `kr16`
  executable, test registration, and Windows/Linux configure/build/test
  presets.
- Tests: configured and built `windows-vs2026`; `ctest` smoke test passed;
  `kr16` printed the expected version line.
- Regressions: none.
- Next: Debug/Release/sanitizer preset verification.

### 2026-10-27 18:42:31 -07:00 — M0: Debug/Release/sanitizer presets

- Status: M0 in progress.
- Changes: verified the Debug and Release configure/build/test presets; the
  Linux GCC/Clang and sanitizer presets are defined for CI and Linux hosts.
- Tests: Windows Release build and `ctest` passed.
- Regressions: none.
- Next: vendor doctest and add CTest labels.

### 2026-10-27 18:43:16 -07:00 — M0: vendored doctest and CTest labels

- Status: M0 in progress.
- Changes: vendored doctest 2.4.11, added the doctest main and version tests,
  and registered `kr16.unit`/`kr16.version` with `unit` and `smoke` labels.
- Tests: Windows Debug build passed; both CTest tests passed.
- Regressions: none.
- Next: warnings-as-errors and platform boundary.
