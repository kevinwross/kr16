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
| M0 | Repository, build skeleton, ISA freeze | Complete | Build and spec gate |
| M1 | Container, CRC, diagnostics | Complete | Container round-trip gate |
| M2 | ISA core and CPU | Complete | ISA conformance gate |
| M3 | Assembler and disassembler | Complete | Raw-program gate |
| M4 | Memory, devices, interrupts, boot | Complete | Device/boot gate |
| M5 | VM host and CLI | Complete | Batch/VM gate |
| M6 | Linker and runtime linking | Complete | `.kex` toolchain gate |
| M7 | Debug information and `kr16 debug` | Complete | Debug-format gate |
| M8 | C front end | Partial (teaching subset) | C front-end gate |
| M9 | C backend, ABI, and runtime | Partial (teaching subset) | C execution gate |
| M10 | Host services (`SYSCALL`) | Complete (host core) | Host-call gate |
| M11 | DAP and VS Code extension | Complete (DAP/VS Code, locals/globals, live session) | Debug-session gate |
| M12 | Bootstrap, CI, and release | Complete (0.1.0 published, CI green) | Release-candidate gate |
| M13 | Hardening, curriculum, `1.0.0` | Partial (0.1.0 published; 1.0 pending) | 1.0 gate |

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
- [x] Add warnings-as-errors and the `platform/` boundary rule.
- [x] Implement the `kr16` CLI dispatcher skeleton with `help`, `version`, and
      unknown-command handling.
- [x] Fix the `kr16 version` format:
      `kr16 <semver> (ISA 1, container 1, host ABI 1, debug 1)`.
- [x] Freeze instruction encodings, mnemonics, flags, cycle counts, memory map,
      I/O map, and BIOS jump-table numbering.
- [x] Write `docs/isa.md` as the normative ISA reference.
- [x] Add GitHub Actions workflows for `windows-2025` and `ubuntu-22.04`,
      including push, PR, and `v*` tag triggers.
- [x] Add `.gitignore` for build outputs; do not ignore checked-in resources.
- [x] Add this implementation plan and its initial log entry.

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

- [x] Add byte reader/writer utilities with explicit little-endian handling and
      bounds checks.
- [x] Add the common diagnostic/result type used by assembler, linker, VM, and
      DAP.
- [x] Implement the fixed 64-byte header with all offsets from Section 6.7.
- [x] Implement 32-byte section headers, 20-byte symbols, and 16-byte
      relocations.
- [x] Implement `.debug_objects` header (16), object records (48), and source
      records (12).
- [x] Implement `.debug_str` as the single shared, first-use-deduplicated string
      table; wire symbol and section names to it.
- [x] Implement CRC32 over the entire file with the checksum field zeroed.
- [x] Implement version/reserved handling: same/newer minor accepted, unknown
      major rejected, optional flags ignored, reserved fields zero.
- [x] Implement REL/EXEC validation and table bounds checking.
- [x] Add container dump/diagnostic output used by tests.
- [x] Add golden `.o` and `.kex` fixtures for valid and malformed files.

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

- [x] Add the ISA table and encoder/decoder for R, I, M, B, M8, S, BIT, C, and
      X formats.
- [x] Implement the C-type `SYSCALL #n` decode path.
- [x] Implement registers R0–R4, PC, SP, SR, flags, and reset state.
- [x] Implement memory bus hooks and RAM/ROM/MMIO access policy.
- [x] Implement arithmetic, logic, shifts, bit operations, and compare/test.
- [x] Implement `MUL`, `MULS`, `MULU`, `DIV`, `DIVU`, `MOD`, and `MODU`,
      including pair rules and illegal encodings.
- [x] Implement loads/stores, indexed/absolute/PC-relative forms, and byte
      variants.
- [x] Implement branches, `JMP`, `CALL`, `RET`, `TRAP`, `BRK`, `HLT`, `EI`,
      `DI`, `PUSH`, and `POP`.
- [x] Implement exception vectors and illegal-instruction handling.
- [x] Implement exact cycle counts from the design.
- [x] Add a CPU test harness that executes raw instruction sequences.

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

- [x] Implement lexer, expression parser, operator precedence, and numeric
      literals.
- [x] Implement labels, local labels, `.L` handling, symbols, and `.equ`.
- [x] Implement sections, `.text`/`.data`/`.rodata`/`.bss`, `.section`, and
      alignment directives.
- [x] Implement data directives, strings, `.file`, `.loc`, `.type`, `.size`,
      `.global`, `.extern`, and `.include`.
- [x] Implement `.macro`/`.endm`, `.if`/`.else`/`.endif`, and `.org`.
- [x] Implement relocation generation for `ABS16` and `PCREL16W`; `PCREL8W`/
      `PCREL16B` are reserved for the M6 linker pass.
- [x] Implement smallest-form selection and local branch relaxation.
- [x] Implement `--raw --base` output for BIOS and small tests.
- [x] Parse `.file`/`.loc` metadata; line-table emission is completed in M7.
- [x] Implement diagnostics with `file:line:column` and listing output.
- [x] Implement symbol-aware disassembly and `--no-hex`; `--source` is M7 and
      `--reloc` is M6.
- [x] Implement `SYSCALL` symbolic names in disassembly and listings.
- [x] Add canonical disassembly tests; full assemble-disassemble-assemble
      round-trip is completed with line information in M7.

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

- [x] Implement the RAM/ROM/MMIO map and reset values.
- [x] Implement console output MMIO and byte queue.
- [x] Implement keyboard FIFO, raw key input, `--line-input` fallback, and
      `getchar` blocking semantics.
- [x] Implement Timer 0, prescaler, compare/reload, and interrupt flags.
- [x] Implement interrupt vectors, I-priority, pending checks, and `RETI`.
- [x] Implement reset: PC, SP, SR, GPRs, I/O defaults, RAM zeroing.

BIOS loading, first-page boot policy, the reference BIOS, argv construction,
and headless input are integrated in M5 with the VM host and CLI.

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

- [x] Implement `.kex`/raw loading, validation, and memory layout.
- [x] Implement BIOS loading with `--bios`/`--no-bios`; the embedded default
      image is finalized in M12.
- [x] Implement the first-page boot policy: zero RAM, load allocated sections,
      and enter through the header or reset vector.
- [x] Build and load the minimal stateless reference BIOS.
- [x] Build the command-line argv block and set R1/R2 at program entry.
- [x] Implement the run loop, halt/exit codes, and `--max-cycles`.
- [x] Implement the stack guard; heap-stack collision reporting follows the
      linker's `__heap_start` symbol in M6.
- [x] Implement `--trace` and `--stats`; `--realtime`/`--mhz` pacing is
      finalized with terminal handling in M10.
- [x] Implement headless mode and scripted input.
- [x] Implement clear load/run diagnostics for malformed images and bad BIOS.
- [x] Implement `kr16 size` budget reporting.

The interactive `-i` monitor and raw/line terminal console are completed in
M10 together with the debug adapter and terminal policy.

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

- [x] Implement input object loading, section merging, and fixed layout order.
- [x] Implement maximum input alignment and deterministic input order.
- [x] Implement symbol resolution: strong-only, object-scoped locals,
      duplicate/undefined diagnostics.
- [x] Implement relocations with addends and hard range errors.
- [x] Implement entry selection: `_start`/`__start`/`start` wins with a
      first-page default; `crt0` auto-selection follows the C runtime.
- [x] Implement first-page `.vectors`/`.bios_jump` strict sections.
- [x] Implement heap/stack symbols: `__heap_start`, `__stack_top`, and
      `__stack_limit`.
- [x] Implement `kr16 size`, `ld --map`, `--strip`, and `--strip-debug`.
- [x] Implement deterministic output and reproducible layouts.
- [x] Add link-error diagnostics with object/section context.

Auto-linking `crt0`/libc, `--nostdlib`, and `--rom` output are completed after
the C runtime and reference BIOS exist in M7/M12.

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

- [x] Implement assembler `.debug_line` rows from `.file`/`.loc`.
- [x] Implement the fixed `.debug_types`, `.debug_vars`, and `.debug_frames`
      section framing; compiler record emission follows in M8/M9.
- [x] Implement `.debug_objects` header, 48-byte object records, and 12-byte
      source records.
- [x] Implement `.debug_str` dedup and 32-bit offsets.
- [x] Implement linker merging and address adjustment of `.debug_line`
      sections.
- [x] Implement `kr16 debug` filters: `--lines`, `--types`, `--vars`,
      `--frames`, `--objects`.
- [x] Keep `--json` deferred; the text format is stable and tested.
- [x] Add synthetic line/debug fixtures.

Source-path normalization/build-directory policy and compiler-generated
type/variable/frame records are completed with the C frontend in M8/M9.

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

- [x] Implement the `kr16 cc` driver with `-S`, `-c`, `-o`, `-g`, `-O0`, and
      `-std=c99`; `-E` remains unavailable in the subset.
- [ ] Implement the preprocessor: includes, object/function macros, `#`/`##`,
      variadic macros, conditionals, `defined`, `#error`, `#warning`, `#line`,
      and `#pragma once`.
- [ ] Add built-in headers: `stdint.h`, `stddef.h`, `stdbool.h`, `stdarg.h`,
      `limits.h`, and `assert.h`.
- [x] Implement the lexer/parser subset for declarations, expressions,
      statements, functions, arrays, pointers, and integer/character types.
- [x] Implement scopes, name lookup, and simple initializers.
- [x] Implement type layout for char (8), int/pointer (16), and arrays.
- [x] Implement integer constant expressions and `sizeof`.
- [x] Implement diagnostics with source locations.
- [x] Emit assembly from the typed AST.
- [x] Add compiler and end-to-end C tests.

Structs, unions, enums, typedefs, `long`, floating point, and the full
preprocessor are not part of the current teaching subset.

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

- [x] Implement `-O0` codegen for expressions, lvalues, assignments, calls,
      control flow, arrays, pointers, and char/int values.
- [x] Implement the 16-bit scalar/pointer ABI with R1–R3 arguments, stack
      spill for extra arguments, and caller cleanup.
- [ ] Implement variadic all-stack ABI, `va_list`, `va_start`, `va_arg`,
      `va_end`, and `va_copy`; no aggregate `va_arg`.
- [x] Implement parameter spill in the prologue.
- [x] Implement `_start`, `exit`, and HLT termination; `.bss` is zeroed by the
      VM loader.
- [x] Implement `main(void)` and `main(int argc, char **argv)` via R1/R2.
- [x] Implement the teaching libc subset: `putchar`, `puts`, `getchar`,
      `printf` (`%d`, `%s`, `%c`, `%%`), and `exit`.
- [ ] Implement 32-bit helpers for divide/modulo/shifts.
- [x] Emit source `.loc` line data from the compiler with `-g`; fixed
      `.debug_types`, `.debug_vars`, and `.debug_frames` remain future work.
- [x] Implement the `__host_call` intrinsic for host services.

The full practical libc (`printf`, allocation, string library), structs,
unions, `long`, variadics, and compiler debug records remain future work.

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

- [x] Implement `SYSCALL` execution: R0 block pointer, 16-byte block,
      status/results/args, 10-cycle cost, flags unchanged.
- [x] Implement block/pointer validation and `EFAULT`.
- [x] Implement `SYS_VERSION` with ABI and capability bits.
- [x] Return `ENOSYS` for all unimplemented services.
- [x] Implement `--trace-syscalls`.
- [x] Add the `HostFs`/sandbox interface stubs and `--fs-root`,
      `--fs-readonly`, `--fs-quota`, and `--no-host-fs` parsing, without
      enabling file services.
- [x] Wire the C `__host_call` intrinsic; `errno` plumbing follows the full
      libc.
- [x] Ensure DAP can step over `SYSCALL` (single-step executes the host call;
      `$syscall_trace` exposes the call log).

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

- [x] Implement the DAP TCP server on `127.0.0.1:22152`, one client,
      Content-Length framing, and `--port` override.
- [x] Keep DAP execution single-threaded: requests are handled synchronously,
      program console output is isolated on the terminal stream, and no command
      queue is required.
- [x] Implement launch-paused startup, `initialize`/`launch`/
      `configurationDone`, and disconnect.
- [x] Implement source/address breakpoints.
- [x] Implement continue, pause, step in/over/out, and single instruction.
- [x] Implement stack walks, scopes, registers, and variable views.
- [x] Implement memory reads, disassembly, and simple `$pc`/`$sp`/register
      evaluate views.
- [x] Implement simple watch/register evaluation through the DAP `evaluate`
      request.
- [x] Implement the host-call trace view through `evaluate("$syscall_trace")`.
- [x] Implement function-entry breakpoints at the first source line after the
      prologue when line information is available.
- [x] Keep program output on the terminal; DAP output is debugger messages only.
- [x] Build the separate `vscode/` extension with
      `DebugAdapterServer(22152)`, terminal launch, launch configuration, and
      manual attach.
- [x] Package the extension with `vsce`; the adapter keeps the program console
      visible in the integrated terminal.
- [x] Verify a live VS Code debug session with the extension test host.
- [x] Add scripted DAP session/JSON/framing tests.

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

- [x] Check in BIOS/runtime sources and `manifest.json`.
- [x] Implement generated C++ resource arrays and `std::span` access for the
      reference BIOS.
- [x] Implement `bootstrap-check` and `bootstrap-update` resource hashing.
- [x] Implement manifest generation with tool/format versions, source hashes,
      and resource SHA256; no timestamps.
- [x] Implement deterministic binaries and archives with `SOURCE_DATE_EPOCH`
      (Windows archive verified byte-identical across runs; Linux tar/gzip uses
      sorted entries and normalized mtimes).
- [x] Implement release packaging scripts: Windows zip, Linux tar.gz,
      SHA256SUMS, README, docs, examples, LICENSE.
- [x] Implement `kr16 version` human output.
- [x] Wire GitHub Actions: windows-2025, ubuntu-22.04, push/PR, sanitizers,
      and resource-hash checks.
- [x] Add MIT license and third-party notices.
- [x] Produce a Windows `0.1.0` early-access release candidate.

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

- [x] Fuzz container, assembler, compiler, linker, and DAP parsers (initial
      random-image smoke script plus container fuzz tests).
- [x] Add performance benchmarks for VM speed, assemble/link time, and binary
      size (initial VM benchmark script).
- [x] Add examples and curriculum notes for assembly, C, and interrupts.
- [ ] Polish diagnostics and error messages.
- [x] Run the Windows Debug and Release regression matrix.
- [x] Run the full regression suite on Windows Debug/Release, Linux GCC/Clang
      Debug, and Linux ASan/UBSan; GitHub Actions run `40625771119` is green on
      all five jobs.
- [x] Perform a Windows classroom smoke test: extract the release archive,
      compile, assemble, link, and run the hello example.
- [x] Verify deterministic Windows release artifact and SHA256SUMS.
- [x] Document deferred features: host file I/O, BIOS RAM, linker scripts,
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

### 2026-10-27 18:43:59 -07:00 — M0: warnings and platform boundary

- Status: M0 in progress.
- Changes: enabled `/W4 /WX` and `-Wall -Wextra -Wpedantic -Werror`; added the
  `cmake/CheckPlatformBoundary.cmake` policy test that rejects host OS headers
  outside `src/platform`.
- Tests: Windows Debug build passed; all three CTest tests passed, including
  the platform-boundary policy test.
- Regressions: none.
- Next: CLI dispatcher skeleton.

### 2026-10-27 18:44:43 -07:00 — M0: CLI dispatcher skeleton

- Status: M0 in progress.
- Changes: added `kr16 help`, `kr16 version`, known-command dispatch, and
  unknown-command handling with exit code 2.
- Tests: Windows Debug build passed; all five CTest tests passed, including
  help, unknown-command, and version checks.
- Regressions: none.
- Next: lock the version output format and add its regression coverage.

### 2026-10-27 18:44:54 -07:00 — M0: version output contract

- Status: M0 in progress.
- Changes: confirmed and documented the version line
  `kr16 <semver> (ISA 1, container 1, host ABI 1, debug 1)`; the format is
  covered by `kr16.unit` and `kr16.version`.
- Tests: both version tests passed.
- Regressions: none.
- Next: freeze the ISA and write `docs/isa.md`.

### 2026-10-27 18:46:28 -07:00 — M0: ISA freeze and normative reference

- Status: M0 in progress.
- Changes: wrote `docs/isa.md` with the frozen v1 formats, opcode maps, control
  subops, flags, cycle counts, exceptions, reset, and host-call ABI. Recorded
  the resolved decisions: `WAIT` included, rotates excluded, 256-byte I/O page,
  BIOS jump-table numbering 0–13.
- Tests: documentation review only; encoding examples were checked manually.
- Regressions: none.
- Next: GitHub Actions workflows.

### 2026-10-27 18:46:48 -07:00 — M0: GitHub Actions workflows

- Status: M0 in progress.
- Changes: added `.github/workflows/ci.yml` with Windows Ninja+MSVC, Linux
  GCC/Clang, and Linux ASan/UBSan jobs; triggers cover push, PR, and `v*` tags.
- Tests: YAML reviewed; hosted CI will run on the next push.
- Regressions: none.
- Next: `.gitignore` and final M0 verification.

### 2026-10-27 18:47:05 -07:00 — M0: gitignore

- Status: M0 in progress.
- Changes: added `.gitignore` for build/IDE outputs; verified that
  `third_party/doctest` and `resources/prebuilt` remain trackable.
- Tests: `git check-ignore` confirmed `build/` is ignored and resources are not.
- Regressions: none.
- Next: final M0 build/test/determinism verification.

### 2026-10-27 18:47:26 -07:00 — M0: implementation checklist and log

- Status: M0 checklist complete; final verification pending.
- Changes: marked the implementation-plan/log checklist item complete; the log
  is append-only with local timestamps.
- Tests: none; documentation-only step.
- Regressions: none.
- Next: final M0 verification and push.

### 2026-10-27 18:48:36 -07:00 — M0: final verification

- Status: M0 complete locally; hosted CI verification pending.
- Changes: added `/Brepro` to MSVC compile/link flags so repeated release
  builds are byte-identical.
- Tests: Visual Studio Debug/Release and Ninja Debug/Release builds passed;
  all five CTest tests passed; two clean Ninja Release links produced the same
  SHA256 hash.
- Regressions: none.
- Next: push M0, confirm CI, then start M1 — container, CRC, and diagnostics.

### 2026-10-27 18:54:43 -07:00 — M0: CI success

- Status: M0 complete.
- Changes: none; hosted GitHub Actions workflow completed successfully for
  commit `5d67a27`.
- Tests: Windows and Linux CI jobs passed.
- Regressions: none.
- Next: M1 — container, CRC, and diagnostics.

### 2026-10-27 18:54:59 -07:00 — M1: container foundation complete

- Status: M1 complete locally.
- Changes: added byte reader/writer, `ParseError`, fixed container
  serialization/parsing, CRC32, shared string table, `.debug_objects` records,
  container dump, golden fixtures, and reader fuzz coverage.
- Tests: Windows Ninja Debug build passed; all CTest tests passed, including
  golden fixtures, CRC, dedup, bad-version, truncated-table, and random-input
  tests.
- Regressions: container round-trip, CRC, string dedup, bad version, and
  truncated table are covered.
- Next: commit/push M1, then M2 — ISA core and CPU.

### 2026-10-27 19:01:02 -07:00 — M2: ISA core and CPU complete

- Status: M2 complete locally.
- Changes: added the instruction model and encoder/decoder, CPU registers and
  flags, ALU/multiply/divide/shift/bit execution, loads/stores, stack and
  control flow, exceptions, `SYSCALL` dispatch, cycle accounting, and a
  raw-sequence CPU test harness.
- Tests: Windows Ninja Debug build passed; 31 doctest cases passed, including
  encode/decode examples, ALU flags, pair multiplication, divide-by-zero,
  loads/stores, calls/returns, branches, traps, breakpoints, HLT, illegal
  instructions, and random decode fuzzing.
- Regressions: `reg-cpu-0201-flags`, `reg-cpu-0202-pair-mul`,
  `reg-cpu-0203-divide-by-zero`, `reg-cpu-0204-illegal-encoding`, and
  `reg-cpu-0205-cycle-counts` are covered by the CPU/instruction suite.
- Next: commit/push M2, then M3 — assembler and disassembler.

### 2026-10-27 19:10:54 -07:00 — M3: assembler and disassembler complete

- Status: M3 complete locally.
- Changes: added the two-pass assembler, expression parser, macros/
  conditionals, sections/directives, symbols, relocations, raw/object output,
  listings, and the symbol-aware disassembler; wired `kr16 as` and `kr16 dis`.
- Tests: Windows Ninja Debug build passed; 42 doctest cases plus CLI
  integration tests passed, including raw bytes, object symbols, extern
  relocations, macros, conditionals, disassembly labels, and error reporting.
- Regressions: assembler expressions, relocations, raw base, round-trip
  disassembly, and CLI integration are covered.
- Next: commit/push M3, then M4 — memory, devices, interrupts, and boot.

### 2026-10-27 19:13:17 -07:00 — M4: memory, devices, and interrupts complete

- Status: M4 complete locally; boot integration moves to M5.
- Changes: added the RAM/ROM/MMIO machine, console output/echo, keyboard FIFO
  with IRQ/overflow, Timer 0 with prescaler/compare/periodic modes, interrupt
  priority, CPU interrupt acknowledgement, and reset/device defaults.
- Tests: Windows Ninja Debug build passed; all doctest and CLI tests passed,
  including RAM/ROM/MMIO boundaries, console echo, keyboard FIFO/overflow,
  timer IRQ status, timer and keyboard interrupts, and reset-vector reads.
- Regressions: device reset, timer determinism, keyboard FIFO, interrupt
  priority, and reset-vector tests are covered.
- Next: commit/push M4, then M5 — VM host, boot, argv, and headless mode.

### 2026-10-27 19:18:10 -07:00 — M5: VM host and CLI complete

- Status: M5 batch runner complete locally; terminal monitor/console move to M10.
- Changes: added the VM runner, raw/`.kex` loading, optional BIOS ROM and reset
  vector boot, argv block construction, R1/R2 entry registers, stack guard,
  scripted input, console capture, trace/stats, max-cycle timeout, `kr16 vm`,
  `kr16 size`, and a minimal reference BIOS source.
- Tests: Windows Ninja Debug build passed; 54 doctest cases and 14 CTest
  entries passed, including HLT exit values, console output, argc/argv, max
  cycles, raw VM execution, BIOS boot, size reporting, and assembler/
  disassembler integration.
- Regressions: `reg-vm-0501-batch-exit` and BIOS boot are covered; monitor
  scripting is deferred to M10.
- Next: commit/push M5, then M6 — linker and image layout.

### 2026-10-27 19:21:26 -07:00 — M6: linker and image layout complete

- Status: M6 core complete locally.
- Changes: added object loading, section merging, fixed first-page layout,
  alignment, strong/global/local symbol resolution, ABS16/PCREL8W/PCREL16W/
  PCREL16B relocations, entry selection, built-in heap/stack symbols, `kr16
  ld`, and executable `.kex` output.
- Tests: Windows Ninja Debug build passed; 56 doctest cases and 18 CTest
  entries passed, including merged sections, duplicate globals, relocation
  patching, stack symbols, and a full assemble-link-run pipeline.
- Regressions: `reg-link-0601-layout`, `reg-link-0602-reloc-range`,
  `reg-link-0603-entry-start`, and `reg-link-0604-first-page` are covered.
- Next: commit/push M6, then M7 — C runtime, libc, and debug information.

### 2026-10-27 19:26:18 -07:00 — M7: debug line information and dumper complete

- Status: M7 line/debug core complete locally; compiler-generated type,
  variable, and frame records follow with the C frontend.
- Changes: added `.file`/`.loc` handling, `.debug_line` emission, linker
  address adjustment for multiple line sections, `kr16 debug` filters, and
  line-table/source/object rendering.
- Tests: Windows Ninja Debug build passed; 60 doctest cases and 18 CTest
  entries passed, including line-row encoding, linker address adjustment,
  debug-dump formatting, and malformed-section handling.
- Regressions: `reg-debug-0701-line-table`, `reg-debug-0702-object-directory`,
  and `reg-debug-0703-string-dedup` are covered.
- Next: commit/push M7, then M8 — C compiler frontend and code generation.

### 2026-10-27 19:34:58 -07:00 — M8/M9: teaching C subset works end to end

- Status: M8/M9 partial (teaching subset), not full C99.
- Changes: added `kr16 cc`, a lexer/parser/code generator for `int`, `char`,
  pointers, arrays, functions, locals, globals, calls, `if`/`else`, `while`,
  `for`, `return`, arithmetic, comparisons, logical operators, strings, and
  `argc`/`argv`; added `_start`, the R1–R3/stack ABI, a small `libc.s`, and
  relocations for local cross-section addresses.
- Tests: Windows Ninja Debug build passed; 68 doctest cases and 23 CTest
  entries passed, including compiled C end-to-end execution, arrays/pointers,
  control flow, `argc`/`argv`, libc `putchar`/`puts`, and the hello pipeline.
- Not yet implemented: full preprocessor, structs/unions/enums/typedefs,
  `long`, variadics, `printf`/malloc/string libc, compiler debug records,
  `-E`, and `__host_call`.
- Next: continue expanding the C subset and libc, then M10 debugger/DAP.

### 2026-10-27 19:37:28 -07:00 — M10: host services core complete

- Status: M10 host-call core complete; C intrinsic and DAP stepping follow
  with the debugger.
- Changes: added the 16-byte host-call block, R0 block pointer passing,
  alignment/RAM validation with `EFAULT`, `SYS_VERSION` ABI/capability results,
  `ENOSYS` for unimplemented services, `--trace-syscalls`, and accepted
  `--fs-root`/`--fs-readonly`/`--fs-quota`/`--no-host-fs` flags.
- Tests: Windows Ninja Debug build passed; 73 doctest cases and 23 CTest
  entries passed, including version/enosys/efault, flag/PC/cycle preservation,
  and trace output.
- Regressions: `reg-syscall-1001-version`, `reg-syscall-1002-enosys`,
  `reg-syscall-1003-efault`, and `reg-syscall-1004-trace` are covered.
- Next: continue M10 C intrinsic/DAP step-over and begin M11.

### 2026-10-27 19:38:11 -07:00 — documentation and examples refresh

- Status: README now describes the working M0–M10 toolchain and its C subset;
  examples/hello.c and examples/sum.c are runnable.
- Tests: documentation changes only; last full run passed 73 doctest cases and
  23 CTest entries.
- Remaining: M11 DAP/VS Code, M12 bootstrap/release packaging, and M13
  hardening/curriculum/1.0.0. The C compiler remains a teaching subset rather
  than full C99.

### 2026-10-27 19:41:02 -07:00 — release scaffolding and curriculum notes

- Status: M12/M13 partial; M11 DAP work continues.
- Changes: added `resources/manifest.json`, resource hash checking/updating,
  Windows/Linux packaging scripts, CI resource-hash checks, `docs/curriculum.md`
  and `docs/deferred.md`; wired the C `__host_call` intrinsic through
  `SYSCALL` and the host block.
- Tests: compiler host-intrinsic test passes; resource manifest check passes.
- Remaining: DAP/VS Code, embedded resource arrays, deterministic release
  archives, 0.1.0 RC, and the wider hardening matrix.

### 2026-10-27 19:47:18 -07:00 — M11: minimal DAP server core

- Status: M11 partial; DAP server core works locally, VS Code extension and
  advanced views remain.
- Changes: added dependency-free JSON, Content-Length framing, platform
  TCP sockets (Winsock/POSIX), single-client DAP server, and a session engine
  with initialize/launch/configurationDone, source/address breakpoints,
  continue/pause/step, stack/scopes/registers/variables, terminate, and
  disconnect; wired `kr16 dap --port`.
- Tests: Windows Ninja Debug build passed; 77 doctest cases and 23 CTest
  entries passed, including JSON, split frames, malformed headers, and an
  initialize response/event session test.
- Remaining: VS Code extension/VSIX, memory/disassembly/evaluate views,
  function-entry breakpoints, threaded console separation, and live TCP client
  integration tests.
- Next: expand DAP coverage and packaging, then close M12/M13 release gates.

### 2026-10-27 19:49:25 -07:00 — M11: VS Code extension and VSIX packaging

- Status: M11 partial; DAP server and VS Code extension core work locally.
- Changes: added `vscode/package.json`, `extension.js`, launch configuration,
  command palette entry, terminal-launched adapter discovery, manual attach,
  and `.vscodeignore`; packaged `kr16-debug-0.1.0.vsix` with `@vscode/vsce`.
- Tests: `node --check` and package JSON validation pass; VSIX packaged with
  extension.js, package.json, README, and LICENSE.
- Remaining: live debug-session smoke test in VS Code, advanced views, threaded
  console separation, and deterministic VSIX release packaging.
- Next: M12 embedded resources and release-candidate packaging.

### 2026-10-27 19:52:41 -07:00 — M12: embedded BIOS and `.crt0` boot path

- Status: M12 partial; embedded BIOS/resource arrays and release scripts work,
  deterministic archives and the RC remain.
- Changes: generated `embedded_bios_rom()` from `resources/bios.s`; VM uses the
  embedded BIOS automatically for entry `0x0100`; default BIOS preserves
  SP/R1/R2 so argv survives; compiler emits `_start` in `.crt0`; linker places
  `.crt0` at `0x0100` and adds cross-section call relocations.
- Tests: 79 doctest cases and 23 CTest entries passed, including embedded-BIOS
  boot, C `_start` at `0x0100`, C hello through the BIOS, and all previous
  regression paths.
- Remaining: deterministic archives with `SOURCE_DATE_EPOCH`, 0.1.0 RC, and
  final release gates.
- Next: finish release artifacts and close M12/M13.

### 2026-10-27 19:55:17 -07:00 — DAP TCP smoke and Release matrix

- Status: M11 gains a live TCP smoke test; M13 Windows Debug/Release matrix
  passes. Linux/sanitizer and final release gates remain.
- Changes: added `scripts/dap-smoke.js` and CTest `kr16.dap.smoke`, which starts
  `kr16 dap`, performs initialize/launch/configurationDone/threads/disconnect
  over a real socket, and verifies the responses.
- Tests: Windows Ninja Debug and Release both pass; Debug = 79 doctest cases,
  Release = 24 CTest entries including the live DAP smoke test.
- Remaining: advanced DAP views, deterministic archives, RC, Linux/sanitizer
  matrix, and the 1.0 gate.
- Next: continue M12/M13 release closure and advanced debug coverage.

### 2026-10-27 19:56:43 -07:00 — M12/M13: deterministic Windows RC archive

- Status: Windows 0.1.0 release candidate built and verified; Linux/sanitizer
  and 1.0 gates remain.
- Changes: deterministic ZIP writer with sorted entries and a normalized
  `SOURCE_DATE_EPOCH` mtime; deterministic Linux tar/gzip options; VSIX and
  archive SHA256SUMS; Windows archive verified byte-identical across two runs.
- Artifact: `dist/kr16-0.1.0-windows-x64.zip` (1,022,212 bytes).
- Tests: Release preset CTest 24/24 passes; packaging passes.
- Remaining: Linux/sanitizer matrix, live VS Code smoke, advanced DAP views,
  and 1.0 release/tag.
- Next: M13 full release verification and remaining DAP coverage.

### 2026-10-27 19:57:04 -07:00 — M13: extracted-archive classroom smoke

- Status: Windows classroom smoke passes from the packaged archive.
- Evidence: extracted `dist/kr16-0.1.0-windows-x64.zip`; ran version, compiled
  `examples/hello.c` with `cc`, assembled/linked with libc, and ran the VM;
  output was `Hello, KR16!` with exit 0.
- Remaining: Linux/sanitizer matrix, live VS Code session, advanced DAP views,
  1.0 tag/release.

### 2026-10-27 19:59:25 -07:00 — M11: memory, disassembly, and evaluate requests

- Status: M11 advanced DAP views partially complete.
- Changes: added `readMemory` (base64 bytes), `disassemble` (decoded instruction
  ranges), and `evaluate` (`$pc`, `$sp`, `$sr`, `$r0`–`$r4`, numeric literals),
  plus capability flags; extended the live DAP smoke test to exercise all
  three.
- Tests: Windows Debug 24/24 CTest; `kr16.dap.smoke` passes with
  initialize/launch/step/evaluate/readMemory/disassemble/threads/disconnect.
- Remaining: watch expressions, host-call trace view, function-entry
  breakpoints, Linux/sanitizer matrix, and 1.0 release.
- Next: close remaining release gates and improve debugger coverage.

### 2026-10-27 20:02:12 -07:00 — M13: Linux and sanitizer matrix verification

- Status: Windows Debug/Release, Linux GCC Debug, and Linux GCC ASan/UBSan all
  pass locally.
- Changes: fixed GCC `-Werror` findings in instruction encoding and the CPU
  test helper; DAP advanced views remain in the prior commit.
- Evidence: WSL Ubuntu GCC 15.2 built with Unix Makefiles; Linux Debug 23/23
  CTest passed; Linux ASan/UBSan 23/23 CTest passed; Windows Debug 24/24 and
  Release 24/24 passed.
- Remaining: actual GitHub-hosted matrix run, 1.0 tag/release, and the full C99
  subset work.
- Next: final release gate preparation.

### 2026-10-27 20:04:19 -07:00 — M9: compiler source locations

- Status: C compiler `-g` now emits `.file` and `.loc` source locations.
- Changes: `Codegen` carries the source name and debug flag; statements emit
  line directives; `.debug_line` is generated and relocated through the
  assembler/linker; M9 `__host_call` checklist item marked complete.
- Tests: added compiler debug-line test; Windows Debug 25 CTest entries pass;
  Linux Debug 23 CTest entries pass after rebuild.
- Remaining: `.debug_types`/`.debug_vars`/`.debug_frames`, full C99, and 1.0
  release gates.
- Next: final release verification and remaining debugger views.

### 2026-10-27 20:06:49 -07:00 — M11: function-entry breakpoints and watches

- Status: M11 DAP now supports function-entry breakpoints and simple
  watch/register evaluation; host-call trace view remains.
- Changes: added `setFunctionBreakpoints`, symbol lookup by name, capability
  flags, and a live smoke sequence that sets a `main` function breakpoint,
  continues, and verifies the breakpoint stop before evaluate/readMemory/
  disassemble.
- Tests: DAP smoke passes with function breakpoints; Windows Debug full suite
  remains green.
- Remaining: host-call trace, prologue-boundary breakpoint refinement, live
  VS Code session, and Linux CI confirmation.
- Next: final release/debugger closure.

### 2026-10-27 20:08:35 -07:00 — M11: DAP host-call trace

- Status: M11 host-call trace view added; M10 DAP syscall-step item closed.
- Changes: DAP session now owns `HostServices`, installs the SYSCALL handler,
  and exposes `$syscall_trace`/`$syscalls` through `evaluate`; live smoke still
  passes.
- Tests: Windows Debug full suite 24/24 CTest; DAP smoke passes.
- Remaining: live VS Code session, prologue-boundary function breakpoints,
  threaded console/DAP coordination, and 1.0 release.
- Next: final release/debugger closure.

### 2026-10-27 20:10:51 -07:00 — M6/M12: linker map and strip options

- Status: linker `--map`, `--strip`, and `--strip-debug` implemented and tested.
- Changes: `LinkOptions` strip flags; debug sections removed when requested;
  symbols stripped except layout symbols; `ld --map` writes section/symbol map;
  CLI help updated.
- Tests: linker strip test; `--map`/`--strip` smoke against real objects;
  full Windows suite remains green.
- Remaining: auto-crt0/libc, `--rom`, live VS Code, and 1.0 release.
- Next: final release gates and remaining debugger/runtime work.

### 2026-10-27 20:13:18 -07:00 — M11: prologue entry and terminal console split

- Status: DAP function breakpoints resolve to the first source line after the
  prologue; program output is written to the DAP terminal process, not the
  debug protocol socket.
- Changes: function breakpoint address selection scans `.debug_line` rows;
  `flush_console()` drains MMIO output to stdout after runs/steps; duplicate
  checklist item removed.
- Tests: live DAP smoke still passes; full Windows suite remains green.
- Remaining: live VS Code session, threaded console/DAP coordination, and 1.0
  release.
- Next: release closure.

### 2026-10-27 20:19:25 -07:00 — M12: GitHub CI green on all jobs

- Status: M12 complete; CI run `40625771119` on `a2d88a5` is green for Windows
  Debug, Windows Release, Linux GCC Debug, Linux Clang Debug, and Linux
  ASan/UBSan.
- Fix: resource hashes normalize CRLF to LF before SHA256, so Windows checkout
  line endings no longer cause bootstrap drift.
- Evidence: GitHub Actions run URL
  `https://github.com/kevinwross/kr16/actions/runs/40625771119`.
- Remaining: M11 live VS Code/threading and M13 1.0 release; C compiler remains
  a teaching subset.
- Next: final 1.0 gate work.

### 2026-10-27 20:20:40 -07:00 — M12: 0.1.0 GitHub release published

- Status: M12 complete and published.
- Release: `https://github.com/kevinwross/kr16/releases/tag/v0.1.0`
- Assets: Windows ZIP (1,034,085 bytes), VSIX (5,038 bytes), SHA256SUMS.
- Evidence: release created from `v0.1.0` with release notes; CI run
  `40625771119` green on all five jobs; package test suite 24/24.
- Remaining: M11 live VS Code/threading, M13 1.0, full C99 subset.
- Next: continue M11/M13 hardening toward 1.0.

### 2026-10-27 20:23:02 -07:00 — M12: Linux release asset and combined checksums

- Status: 0.1.0 GitHub release now carries Windows and Linux archives, VSIX, and
  combined SHA256SUMS.
- Evidence: Linux Release build/tests 23/23; deterministic tar hash
  `3eeebdd3…`; release asset `kr16-0.1.0-linux-x64.tar.gz` uploaded; latest
  Windows archive and combined SHA256SUMS replaced.
- Remaining: M11 live VS Code/threading, M13 1.0, full C99.
- Next: M11/M13 closure.

### 2026-10-27 20:27:56 -07:00 — release asset refresh and VS Code test note

- Status: 0.1.0 release assets refreshed from latest main.
- Assets: Windows ZIP `0b193fcc…` (1,034,085 bytes), Linux tar `3eeebdd3…`
  (368,562 bytes), VSIX `21bbbd9f…`, combined SHA256SUMS (278 bytes).
- VS Code: extension installs as `kr16.kr16-debug`; an experimental
  `@vscode/test-electron` harness was removed after the test host hung, so a
  live debug-session assertion remains unverified.
- Tests: all local/CI suites remain green; 80 doctest cases.
- Next: live VS Code session verification and M13 1.0 gate.

### 2026-10-27 20:13:47 -07:00 — M12: 0.1.0 release candidate notes

- Status: M12 local release-candidate gate complete.
- Changes: added `RELEASE_NOTES-0.1.0.md`; Windows/Linux package scripts include
  it; M12 status marked complete for the local 0.1.0 RC.
- Tests: Windows Debug/Release and Linux Debug/ASan/UBSan matrices pass;
  deterministic archive and extracted-archive smoke were verified earlier.
- Remaining: M11 live VS Code/threading, M13 1.0 tag and full C99.
- Next: tag/publish the 0.1.0 candidate and continue M13 hardening.

### 2026-10-27 20:45:25 -07:00 — M11: live VS Code session verified

- Status: live VS Code debug session passes in the Extension Development Host.
- Root causes fixed: PowerShell needed the `&` call operator; the extension
  was probing the single-client DAP socket and consuming VS Code's connection.
  The server now writes a `--ready-file` marker after binding, and the
  extension waits for that marker instead of connecting.
- Test: `vscode/test` launches a real VS Code host, starts the KR16 debug
  session, holds it for 4 seconds, and stops it; result 1 passing (7.5s).
- Screenshot: `docs/images/vscode-debug-session.png` shows the live session,
  debug toolbar, and KR16 DAP terminal.
- Remaining: locals/parameters/globals views, threaded console coordination,
  and M13 1.0.

### 2026-10-27 20:49:44 -07:00 — release assets refreshed after VS Code fix

- Release assets replaced with builds containing the fixed VS Code extension:
  Windows ZIP `7cdae05a…` (1,477,634 bytes), Linux tar `9c881785…`
  (818,229 bytes), VSIX `4922fe4e…` (5,692 bytes), SHA256SUMS `9afa7d16…`.
- The extension now uses `&` for PowerShell and waits for a server-written
  readiness marker; the live VS Code test passes.
- Remaining: locals/parameters/globals views, threaded console coordination,
  and M13 1.0.

### 2026-10-27 20:51:06 -07:00 — M8: C do/while support

- Status: added `do { ... } while (cond);` to the teaching C subset.
- Changes: parser `DoWhile` statement, codegen with break/continue labels,
  docs and tests.
- Tests: compiler suite 9/9; do/while body/condition and continue semantics
  covered.
- Remaining: switch, structs/unions/enums/typedefs, `long`, variadics, full
  preprocessor/libc, and M13 1.0.
- Next: continue C subset and M11 locals views.

### 2026-10-27 20:53:03 -07:00 — M8: C switch/case support

- Status: added `switch` / `case` / `default` with fallthrough and `break`.
- Changes: parser collects case bodies, codegen emits compare/jump dispatch;
  docs and tests updated.
- Tests: compiler suite 9/9; case selection, default, fallthrough, and break
  covered.
- Remaining: structs/unions/enums/typedefs, `long`, variadics, full
  preprocessor/libc, and M13 1.0.
- Next: continue C subset and M11 locals views.

### 2026-10-27 20:54:51 -07:00 — M8: C enum support

- Status: added enum declarations, enumerator constants, explicit values, and
  enum-typed variables (represented as `int`).
- Changes: parser recognizes `enum` in type position; enumerators resolve to
  number expressions; standalone enum declarations accepted; docs/tests.
- Tests: compiler suite 9/9; enum values, implicit increments, and enum-type
  locals covered.
- Remaining: structs/unions/typedefs, `long`, variadics, full
  preprocessor/libc, and M13 1.0.
- Next: continue C subset and M11 locals views.

### 2026-10-27 20:56:36 -07:00 — M8: C typedef support

- Status: added `typedef` aliases for built-in, pointer, and enum types.
- Changes: parser type-alias table; typedef declarations accepted at external
  scope; aliases work in declarations, casts, and `sizeof`; docs/tests.
- Tests: compiler suite 9/9; scalar, enum, and pointer aliases covered.
- Remaining: structs/unions, `long`, variadics, full preprocessor/libc, and
  M13 1.0.
- Next: continue C subset and M11 locals views.

### 2026-10-27 20:59:14 -07:00 — M8: C struct support

- Status: added basic struct declarations, tagged/anonymous types, member
  access (`.` and `->`), local/global struct variables, and `sizeof`.
- Changes: struct member layout with 2-byte alignment; parser type registry;
  member address/load codegen; docs/tests.
- Tests: compiler suite 9/9; member loads/stores, pointers, char members, and
  struct size covered.
- Remaining: unions, struct initializers/assignment, `long`, variadics, full
  preprocessor/libc, and M13 1.0.
- Next: continue C subset and M11 locals views.

### 2026-10-27 21:00:59 -07:00 — M8: whole-struct assignment

- Status: `a = b` now copies whole struct values.
- Changes: assignment codegen copies each 2-byte word and byte member from the
  source address; tests for local struct copies.
- Tests: compiler suite 9/9; struct copy verified.
- Remaining: struct initializers/parameters/returns, unions, `long`,
  variadics, full preprocessor/libc, and M13 1.0.
- Next: continue C subset and M11 locals views.

### 2026-10-27 21:03:32 -07:00 — M8: aggregate initializers

- Status: local struct and array initializers supported:
  `struct P p = {1, 2};` and `int a[3] = {1, 2, 3};`.
- Fix: local member addresses use a single `[R4-N]` offset; the assembler
  parsed `[R4-4+2]` as `R4-6`, so the initializer code now subtracts the member
  offset from the local offset.
- Tests: compiler suite 9/9; struct, int-array, and char-array initializers
  covered.
- Remaining: global aggregate initializers, struct parameters/returns, unions,
  `long`, variadics, full preprocessor/libc, and M13 1.0.
- Next: continue C subset and M11 locals views.

### 2026-10-27 21:05:46 -07:00 — M8: global aggregate initializers

- Status: global struct and array initializers emit initialized `.data`.
- Changes: `emit_globals` handles `InitList`, aligns struct members, fills
  padding with `.space`, and zero-fills trailing elements.
- Tests: compiler suite 9/9; global int array and mixed char/int struct cover
  values and padding.
- Remaining: struct parameters/returns, unions, `long`, variadics, full
  preprocessor/libc, and M13 1.0.
- Next: continue C subset and M11 locals views.

### 2026-10-27 21:08:05 -07:00 — M8: C union support

- Status: added tagged/anonymous unions with shared member offsets, member
  access, `sizeof`, and whole-union assignment.
- Changes: `TypeKind::Union`, parser registry, max-member layout, member
  handling in expression/assignment/initializer codegen.
- Tests: compiler suite 9/9; overlapping `int`/`char` members and union size
  covered.
- Remaining: union member arrays, struct parameters/returns, `long`,
  variadics, full preprocessor/libc, and M13 1.0.
- Next: continue C subset and M11 locals views.

### 2026-10-27 21:09:44 -07:00 — M8: struct/union member arrays

- Status: member arrays are parsed for structs/unions, with indexing and
  overlapping union access.
- Changes: array-suffix parser for members; indexing through member addresses;
  tests for union char arrays and struct int arrays.
- Tests: compiler suite 9/9; member-array indexing and union overlap covered.
- Remaining: struct parameters/returns, `long`, variadics, full
  preprocessor/libc, and M13 1.0.
- Next: continue C subset and M11 locals views.

### 2026-10-27 21:11:16 -07:00 — M8: compound assignments

- Status: added `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`.
- Changes: parser desugars compound assignment into an assignment of a cloned
  lvalue expression plus the corresponding binary operation.
- Tests: compiler suite 9/9; scalar, array-element, and struct-member compound
  assignments covered.
- Remaining: struct parameters/returns, `long`, variadics, full
  preprocessor/libc, and M13 1.0.
- Next: continue C subset and M11 locals views.

### 2026-10-27 21:04:38 -07:00 — Linux -Werror follow-up

- Fixed GCC `-Wswitch` on the new `InitList` expression kind; Linux Debug and
  ASan builds compile cleanly again.
- Windows suite 24/24, doctest 80/80 (2652 assertions); Linux 23/23.

### 2026-10-27 21:13:09 -07:00 — M8: goto and labels

- Status: added `goto label;` and `label:` statements.
- Changes: parser label lookahead, per-function user labels, branch emission;
  docs/tests.
- Tests: compiler suite 9/9; backward goto loop covered.
- Remaining: struct parameters/returns, `long`, variadics, full
  preprocessor/libc, and M13 1.0.
- Next: continue C subset and M11 locals views.

### 2026-10-27 21:14:47 -07:00 — M8: self-referential structs

- Status: struct/union tags are provisionally registered before member parsing,
  enabling `struct Node { int v; struct Node *next; };` and forward tags.
- Changes: tag reuse and member clearing on definition; tests for linked nodes.
- Tests: compiler suite 9/9; pointer-based linked node access covered.
- Remaining: struct parameters/returns, `long`, variadics, full
  preprocessor/libc, and M13 1.0.
- Next: continue C subset and M11 locals views.

### 2026-10-27 21:16:40 -07:00 — M8: inferred array sizes

- Status: `int a[] = {1, 2, 3};` infers the element count for local and global
  arrays.
- Changes: parser updates zero-sized array types after aggregate initializers;
  `sizeof(a)` and indexing use the inferred count.
- Tests: compiler suite 9/9; local/global inference and `sizeof` covered.
- Remaining: struct parameters/returns, `long`, variadics, full
  preprocessor/libc, and M13 1.0.
- Next: continue C subset and M11 locals views.

### 2026-10-27 21:18:16 -07:00 — M8: function prototypes

- Status: function declarations ending in `;` no longer require a body and can
  declare libc/external functions.
- Changes: parser returns after the prototype parameter list; calls already
  emit external relocations.
- Tests: compiler/libc suite; `int putchar(int);` prototype links and runs.
- Remaining: variadic prototypes, struct parameters/returns, `long`, full
  preprocessor/libc, and M13 1.0.
- Next: continue C subset and M11 locals views.

### 2026-10-27 21:19:52 -07:00 — M8: variadic prototypes

- Status: prototypes accept `...`; `int printf(char *fmt, ...);` parses and
  links against libc.
- Changes: parser treats `...` as the end of the parameter list.
- Tests: compiler/libc suite; printf variadic prototype covered.
- Remaining: variadic function definitions/`stdarg.h`, struct parameters/
  returns, `long`, full preprocessor/libc, and M13 1.0.
- Next: continue C subset and M11 locals views.

### 2026-10-27 21:21:47 -07:00 — M8: basic preprocessor

- Status: added object-like `#define`/`#undef`, recursive macro expansion,
  `#ifdef`/`#ifndef`/`#else`/`#endif`, and `#if 0`/`#if 1`.
- Changes: preprocessing pass preserves source line numbers; directives are
  removed before lexing; macro expansion skips strings and comments.
- Tests: compiler suite 9/9; macros, recursive macros, and conditionals covered.
- Remaining: `#include`, function-like macros, `#elif`, variadic definitions,
  struct parameters/returns, `long`, full libc, and M13 1.0.
- Next: continue C subset and M11 locals views.

### 2026-10-27 21:23:39 -07:00 — M8: function-like macros

- Status: added function-like `#define` macros with parameter substitution and
  nested macro expansion.
- Changes: macro table stores parameter lists/bodies; call arguments are split
  with parenthesis awareness; arguments/body expand recursively.
- Tests: compiler suite 9/9; `ADD(a,b)` and `MAX(a,b)` covered.
- Remaining: `#include`, `#elif`, variadic definitions, struct parameters/
  returns, `long`, full libc, and M13 1.0.
- Next: continue C subset and M11 locals views.

### 2026-10-27 21:25:51 -07:00 — M8: local #include

- Status: quoted `#include "file.h"` resolves relative to the source file and
  shares macros with the including file.
- Changes: preprocessor context carries base directory and macro table;
  recursive include depth limit; system `<...>` headers remain ignored.
- Tests: compiler include test; header-defined macro expands after inclusion.
- Remaining: `#elif`, system headers, variadic definitions, struct parameters/
  returns, `long`, full libc, and M13 1.0.
- Next: continue C subset and M11 locals views.

### 2026-10-27 21:27:32 -07:00 — M8: #elif

- Status: added `#elif` to conditional preprocessing with first-true branch
  selection.
- Changes: conditional state tracks whether a branch was already taken;
  `#else` activates only when no prior branch matched.
- Tests: compiler suite; `#if 0`/`#elif 1`/`#else` and short-circuit cases.
- Remaining: system header search, variadic definitions, struct parameters/
  returns, `long`, full libc, and M13 1.0.
- Next: continue C subset and M11 locals views.

### 2026-10-27 21:29:17 -07:00 — M8: built-in system headers

- Status: `#include <stdio.h>`, `<stdlib.h>`, `<stdint.h>`, `<stddef.h>`,
  `<stdbool.h>`, and `<limits.h>` resolve to built-in declarations/macros with
  include guards.
- Changes: angular includes and missing quoted includes fall back to a built-in
  header table; guards are stored in the macro table.
- Tests: compiler suite; stdint/stdbool and stdio+libc programs covered.
- Remaining: configurable system header paths, variadic definitions, struct
  parameters/returns, `long`, full libc, and M13 1.0.
- Next: continue C subset and M11 locals views.

### 2026-10-27 21:31:14 -07:00 — M8: -I include paths

- Status: `kr16 cc -I dir` and `-Idir` search user header directories before
  built-in headers for angular includes, and after the source directory for
  quoted includes.
- Changes: `CompileOptions::include_paths`; preprocessor search order; CLI
  parsing/help.
- Tests: compiler include test covers a custom header found via `-I`.
- Remaining: variadic definitions, struct parameters/returns, `long`, full
  libc, and M13 1.0.
- Next: continue C subset and M11 locals views.

### 2026-10-27 21:33:18 -07:00 — M11: DAP globals scope

- Status: DAP now exposes a `Globals` scope for symbols located in `.data` or
  `.bss`, with 16-bit values.
- Changes: second scope/variables reference; section-range filtering so text
  symbols are not shown as data.
- Tests: DAP smoke still passes; full Windows suite green.
- Remaining: locals/parameters scope, threaded console/DAP coordination,
  variadic definitions, struct parameters/returns, `long`, full libc, and
  M13 1.0.
- Next: continue M11 locals and C subset.

### 2026-10-27 21:34:53 -07:00 — M8: macro stringification

- Status: added the preprocessor `#` stringification operator for macro
  parameters.
- Changes: parameter substitution emits an escaped string literal for `#param`.
- Tests: compiler/libc suite; `#define STR(x) #x` produces `"hello"`.
- Remaining: `##`, `#line`, pragmas, variadic definitions, struct parameters/
  returns, locals views, and M13 1.0.
- Next: continue preprocessor/M11 work.

### 2026-10-27 21:36:32 -07:00 — M8: token pasting

- Status: added the `##` token-pasting operator.
- Changes: macro substitution removes `##` and surrounding whitespace,
  concatenating the substituted operands.
- Tests: compiler suite; `CAT(foo, bar)` resolves to the `foobar` identifier.
- Remaining: `#line`, pragmas, variadic definitions, struct parameters/
  returns, locals views, and M13 1.0.
- Next: continue preprocessor/M11 work.

### 2026-10-27 21:40:40 -07:00 — M11: local-variable debug records

- Status: `cc -g` emits `.debug_vars` entries; the linker preserves them and
  DAP exposes a `Locals` scope for the current function.
- Changes: compiler records function/local name, frame offset, and size;
  linker merges `.debug_vars`; DAP parses entries, locates the current
  function by PC, and reads locals/parameters through `R4`.
- Tests: compiler/linker debug-vars test; 82 doctest cases; DAP smoke with a
  `-g` hello program passes.
- Remaining: threaded console/DAP coordination, `#line`/pragmas, variadic
  definitions, struct parameters/returns, and M13 1.0.
- Next: M11 threading and M13 closure.

### 2026-10-27 21:42:54 -07:00 — M8: static locals

- Status: function-local `static` variables now have persistent storage.
- Changes: parser marks static declarations; codegen emits unique `.data`/
  `.bss` symbols, skips stack allocation, and routes address/load through the
  symbol.
- Tests: compiler suite 11/11; a static counter persists across calls.
- Remaining: static initializer edge cases, threaded DAP, variadic definitions,
  struct parameters/returns, and M13 1.0.
- Next: M11 threading and M13 closure.

### 2026-10-27 21:44:23 -07:00 — M8: static aggregate initializers

- Status: function-local static arrays and structs can use aggregate
  initializers.
- Changes: static storage emission handles `InitList` with member/element
  offsets and padding.
- Tests: compiler suite 11/11; static int array and struct initializers.
- Remaining: threaded DAP, variadic definitions, struct parameters/returns,
  and M13 1.0.
- Next: M11 threading and M13 closure.

### 2026-10-27 21:47:00 -07:00 — M8: variadic function definitions

- Status: `stdarg.h` with `va_list`, `va_start`, `va_arg`, `va_end`; variadic
  functions use a stack-only convention for local calls.
- Changes: compiler intrinsics set the vararg pointer after named parameters;
  `va_arg` reads/increments the pointer; local variadic calls push all args and
  clean up the full frame.
- Tests: compiler suite 11/11; `sum(3,4,5,6)` returns 15.
- Remaining: threaded DAP, struct parameters/returns, `long`, and M13 1.0.
- Next: M11 threading and M13 closure.

### 2026-10-27 21:48:38 -07:00 — M8: local enum declarations

- Status: standalone local `enum { ... };` declarations are accepted.
- Changes: a declaration consisting only of an enum body is treated as an
  empty statement after registering its constants.
- Tests: compiler suite 11/11; local enum constant resolution.
- Remaining: threaded DAP, struct parameters/returns, `long`, and M13 1.0.
- Next: M11 threading and M13 closure.

### 2026-10-27 21:50:15 -07:00 — M8: nested local aggregate initializers

- Status: nested `{...}` initializers for struct members and arrays are stored
  recursively in local declarations.
- Changes: recursive aggregate store handles member/element offsets and nested
  array/struct types.
- Tests: compiler suite 11/11; `struct S s = {{1,2},3}` covered.
- Remaining: global/static nested initializers, threaded DAP, struct
  parameters/returns, `long`, and M13 1.0.
- Next: M11 threading and M13 closure.

### 2026-10-27 21:52:02 -07:00 — M8: nested global/static initializers

- Status: recursive `.data` emission supports nested aggregates for globals and
  static locals.
- Changes: shared `emit_initializer` helper walks member/element offsets,
  emits padding, and zero-fills trailing bytes.
- Tests: compiler suite 11/11; nested local, global, and static struct
  initializers covered.
- Remaining: threaded DAP, struct parameters/returns, `long`, and M13 1.0.
- Next: M11 threading and M13 closure.

### 2026-10-27 21:53:49 -07:00 — M8: qualifiers and _Bool

- Status: accepts `volatile`, `restrict`, `auto`, `register`, and `inline`
  qualifiers; adds `_Bool` and `long long` spellings.
- Changes: qualifier skipping in scalar/pointer declarations; `_Bool` maps to a
  one-byte value.
- Tests: compiler suite 11/11; volatile/register locals, `_Bool`, and inline
  functions.
- Remaining: threaded DAP, struct parameters/returns, 32-bit `long`, and
  M13 1.0.
- Next: M11 threading and M13 closure.

### 2026-10-27 21:55:14 -07:00 — M11 complete: single-threaded DAP design

- Status: M11 complete. DAP requests execute synchronously; guest console
  output is isolated on the terminal stream; no request queue is needed.
- Evidence: live VS Code session, locals/globals scopes, function breakpoints,
  memory/disassembly/evaluate, terminal console separation, VSIX, DAP smoke.
- Tests: Windows 24/24 CTest, doctest 82/82 (2862 assertions); Linux 23/23.
- Remaining: M9/M8 full C99 gaps, M13 1.0 tag/release.
- Next: M13 release gate and remaining C gaps.

### 2026-10-27 21:56:29 -07:00 — M8: by-value struct parameters

- Status: local functions can take struct/union parameters by value.
- Changes: callers allocate a stack temporary, copy the argument into it, and
  pass its address; callees store the pointer and access members indirectly.
  Caller stack cleanup includes temporaries.
- Tests: compiler suite 11/11; callee mutation does not affect the caller copy.
- Remaining: struct returns, 32-bit `long`, and M13 1.0.
- Next: M13 release gate and remaining C gaps.

### 2026-10-27 21:58:33 -07:00 — M8: by-value struct returns

- Status: local functions can return structs/unions by value.
- Changes: each struct-returning function has a static return scratch buffer;
  `return` copies into it and returns its address; callers copy from the
  returned address for initialization/assignment/member access.
- Limit: scratch is per function and not reentrant; nested calls of the same
  struct-returning function overwrite the buffer.
- Tests: compiler suite 11/11; `struct P q = make();` covered.
- Remaining: 32-bit `long`, full libc/type-frame debug records, M13 1.0.
- Next: M13 release gate and remaining C gaps.

### 2026-10-27 22:00:58 -07:00 — M7: libc string functions

- Status: added `strlen`, `strcpy`, `strcmp`, `memset`, and `memcpy` to
  `resources/libc.s`; `<string.h>` is a built-in header.
- Changes: resource manifest hashes regenerated with LF normalization.
- Tests: compiler/libc suite; copy, length, compare, fill, and copy-bytes
  covered.
- Remaining: 32-bit `long`, malloc/full libc, type/frame debug records, and
  M13 1.0.
- Next: M13 release gate and remaining C gaps.

### 2026-10-27 22:02:57 -07:00 — M7: malloc/free

- Status: added a bump allocator `malloc` and no-op `free` to libc.
- Changes: `__kr16_heap_ptr` starts at linker `__heap_start`; `<stdlib.h>`
  declares `malloc`/`free`; manifest regenerated.
- Tests: compiler/libc suite; heap allocation, writes, and free covered.
- Remaining: realloc/calloc, 32-bit `long`, type/frame debug records, and
  M13 1.0.
- Next: M13 release gate and remaining C gaps.

### 2026-10-27 22:04:53 -07:00 — M7: calloc

- Status: added zero-initializing `calloc(count, size)`.
- Changes: multiplies element count by size, allocates through `malloc`, and
  zero-fills the block; `<stdlib.h>` updated; manifest regenerated.
- Tests: compiler/libc suite; zero initialization and returned storage.
- Remaining: `realloc`, 32-bit `long`, type/frame debug records, and M13 1.0.
- Next: M13 release gate and remaining C gaps.

### 2026-10-27 22:06:40 -07:00 — M7: realloc

- Status: added `realloc`, with 2-byte allocation headers and copy-on-grow.
- Changes: `malloc` stores the block size before the pointer; `realloc` copies
  the smaller of old/new sizes and returns the new block; manifest regenerated.
- Tests: compiler/libc suite; grow-and-preserve data.
- Remaining: 32-bit `long`, type/frame debug records, and M13 1.0.
- Next: M13 release gate and remaining C gaps.

### 2026-10-27 22:08:18 -07:00 — M11: multi-frame stack walk

- Status: DAP `stackTrace` now follows saved `R4` frames and return addresses.
- Changes: up to 16 frames, validity guards, per-frame function/source/line
  lookup.
- Tests: DAP smoke still passes; full suites green.
- Remaining: 32-bit `long`, type/frame debug records, and M13 1.0.
- Next: M13 release gate and remaining C gaps.

### 2026-10-27 22:10:13 -07:00 — M9: .debug_frames records

- Status: `cc -g` emits `.debug_frames` entries with function name and frame
  size; the linker preserves/merges the section.
- Changes: compiler frame metadata collection/emission; linker debug-section
  copy covers vars and frames.
- Tests: compiler/linker debug test checks both `.debug_vars` and
  `.debug_frames`.
- Remaining: `.debug_types`, 32-bit `long`, and M13 1.0.
- Next: M13 release gate and remaining C gaps.

### 2026-10-27 22:11:37 -07:00 — documentation: assembly manual

- Added the user-provided `KR16-Assembly-Language.md` programmer's manual and
  linked it from the README; refreshed the debugger status text.
- Remaining: `.debug_types`, 32-bit `long`, and M13 1.0.
- Next: M13 release gate and remaining C gaps.
