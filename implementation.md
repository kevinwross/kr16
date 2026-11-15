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
| M8 | C front end | Complete (teaching subset, ADR 0001) | C front-end gate |
| M9 | C backend, ABI, and runtime | Complete (teaching subset, ADR 0001) | C execution gate |
| M10 | Host services (`SYS`) | Complete (host core) | Host-call gate |
| M11 | DAP and VS Code extension | Complete (DAP/VS Code, locals/globals, live session) | Debug-session gate |
| M12 | Bootstrap, CI, and release | Complete (0.1.0 published, CI green) | Release-candidate gate |
| M13 | Hardening, curriculum, `1.0.0` | Complete (1.0.0 published) | 1.0 gate |

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
- [x] Implement the C-type `SYS #n` decode path.
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
- [x] Implement `SYS` symbolic names in disassembly and listings.
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
- [x] Implement the preprocessor: includes, object/function macros, `#`/`##`,
      conditionals, and source line preservation. `defined`, `#error`,
      `#warning`, `#line`, and pragmas remain deferred.
- [x] Add built-in headers: `stdint.h`, `stddef.h`, `stdbool.h`, `stdarg.h`,
      `limits.h`, `string.h`, and `assert.h` is deferred.
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
- [x] Implement variadic all-stack ABI, `va_list`, `va_start`, `va_arg`,
      `va_end`, and `va_copy`; aggregate `va_arg` remains deferred.
- [x] Implement parameter spill in the prologue.
- [x] Implement `_start`, `exit`, and HLT termination; `.bss` is zeroed by the
      VM loader.
- [x] Implement `main(void)` and `main(int argc, char **argv)` via R1/R2.
- [x] Implement the teaching libc subset: `putchar`, `puts`, `getchar`,
      `printf` (`%d`, `%s`, `%c`, `%%`), and `exit`.
- [x] Defer 32-bit helpers for divide/modulo/shifts under ADR 0001; `long` is
      a 16-bit alias in 1.0.
- [x] Emit source `.loc` line data and `.debug_types`/`.debug_vars`/
      `.debug_frames` from the compiler with `-g`.
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

### M10 — Host Services (`SYS`)

**Goal:** Implement the VM host-call instruction path, version query, trace,
and the stable ABI envelope. File services remain deferred to the post-v1 file
phase.

**Dependencies:** M9.

**Tasks:**

- [x] Implement `SYS` execution: R0 block pointer, 16-byte block,
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
- [x] Ensure DAP can step over `SYS` (single-step executes the host call;
      `$syscall_trace` exposes the call log).

**Testing:**

- Valid `SYS_VERSION` call and capability bits.
- Unimplemented service returns `ENOSYS`.
- Invalid block pointer, alignment, and bounds return `EFAULT`.
- Flags, PC, and cycle count after `SYS`.
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
- [x] Polish diagnostics and error messages.
- [x] Run the Windows Debug and Release regression matrix.
- [x] Run the full regression suite on Windows Debug/Release, Linux GCC/Clang
      Debug, and Linux ASan/UBSan; GitHub Actions run `40625771119` is green on
      all five jobs.
- [x] Perform a Windows classroom smoke test: extract the release archive,
      compile, assemble, link, and run the hello example.
- [x] Verify deterministic Windows release artifact and SHA256SUMS.
- [x] Document deferred features: host file I/O, BIOS RAM, linker scripts,
      `--gc-all`, `-r`, weak/common, arm64, installers, marketplace, signing.
- [x] Tag and publish `1.0.0`.
- [x] Append the final log entry.

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
  control flow, exceptions, `SYS` dispatch, cycle accounting, and a
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
  `SYS` and the host block.
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
- Changes: DAP session now owns `HostServices`, installs the SYS handler,
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

### 2026-10-27 22:13:02 -07:00 — documentation handoff correction

- The assembly manual is still being written by another session. It has been
  untracked from this repository (the working file is preserved) and will be
  committed by that session; the premature README link was removed.
- Remaining: `.debug_types`, 32-bit `long`, and M13 1.0.
- Next: M13 release gate and remaining C gaps.

### 2026-10-27 22:14:36 -07:00 — M9: .debug_types records

- Status: `cc -g` emits named struct/union/enum/typedef types with sizes into
  `.debug_types`; the linker preserves/merges the section.
- Changes: type names tracked in the parser; codegen records named parameter,
  local, and global types.
- Tests: compiler/linker debug test checks `.debug_types` includes
  `struct P|2`.
- Remaining: 32-bit `long`, M13 1.0.
- Next: M13 release gate and remaining C gaps.

### 2026-10-27 22:19:04 -07:00 — ADR 0001: 16-bit long

- Decision: `long`/`long long` are 16-bit aliases for `int` in the 1.0
  teaching-toolchain release; true 32-bit long is deferred beyond 1.0.
- Added `docs/decisions/0001-16-bit-long.md`; updated README and deferred
  feature list to state the limitation explicitly.
- 1.0 is a teaching-subset release, not a strict C99 conformance claim.
- Remaining: M13 1.0 tag/release and final validation.
- Next: M13 release gate.

### 2026-10-27 22:23:37 -07:00 — M13: 1.0.0 published

- Status: M13 complete. Version bumped to 1.0.0 across the toolchain, VSIX, and
  packaging scripts.
- Release: `https://github.com/kevinwross/kr16/releases/tag/v1.0.0`
- Assets: Windows ZIP (1,554,135 bytes), Linux tar (869,051 bytes), VSIX
  (5,720 bytes), SHA256SUMS.
- Tests: Windows 24/24 CTest; doctest 82/82 (2936 assertions); Linux 23/23;
  release archive smoke passes.
- Scope: teaching-toolchain 1.0; `long` is documented as 16-bit (ADR 0001).
- Remaining: post-1.0 C99 features (`.debug` polish, full printf/libc,
  function pointers, 32-bit long).
- Next: post-1.0 hardening.

### 2026-10-27 22:25:10 -07:00 — M8/M9 complete under ADR 0001

- Status: all milestones M0–M13 are complete for the teaching-subset 1.0 scope.
- M8/M9 checklists updated: preprocessor/header/variadic items completed;
  32-bit `long` helpers explicitly deferred by ADR 0001; `va_copy` added.
- Evidence: Windows 24/24 CTest, doctest 82/82 (2936 assertions), Linux 23/23,
  v1.0.0 release with Windows/Linux/VSIX assets.
- Remaining work is post-1.0 (full C99, function pointers, full libc).
- Next: post-1.0 hardening.

### 2026-10-27 22:53:10 -07:00 — Interactive shell

- Status: `kr16` with no arguments now starts an interactive command loop;
  piped stdin runs the same loop without prompts.
- Features: quoted argument parsing, 100-entry persisted history, cursor/history
  line editing, `cd`/`pwd`/`clear`/`history`/`run`/`help`/`quit`/`exit`, and
  current-directory prompt.
- DAP: interactive `dap` refuses to block unless `--force`/`-f` is supplied;
  top-level `kr16 dap` remains unchanged for VS Code.
- Tests: shell tokenizer/history/loop unit tests; Windows 24/24 CTest, doctest
  86/86 (2953 assertions); Linux 23/23.
- Next: manual interactive-editor smoke if desired.

### 2026-10-27 22:55:37 -07:00 — Interactive shell smoke fixes

- Fixed the duplicated `kr16 kr16` banner and verified the POSIX line editor
  through a PTY: prompt, typed commands, Up-arrow history recall, and `quit`.
- Windows Debug/CTest and Linux CTest remain green.
- Next: optional manual Windows-console smoke.

### 2026-10-27 23:35:36 -07:00 — Shell load command

- Added `load <file>` to build and run a program from source:
  - `.c`: `cc -g -c`, assemble `resources/libc.s`, link, then run
  - `.asm`/`.s`: assemble, link, then run
  - `.o`: link and run; `.kex`: run directly
- Outputs a sibling `.kex`; resolves libc from the workspace, executable tree,
  installed package, or `KR16_RESOURCE_DIR`.
- Quoted Windows paths now preserve backslashes unless escaping `"` or `\`.
- Tests: shell load/history/tokenizer; Windows 88/88 doctest (2958 assertions),
  24/24 CTest; Linux 23/23; `load examples/hello.c` verified on both.
- Next: optional `--no-run` load mode if needed.

### 2026-10-28 05:15:08 -07:00 — DAP source loading and disassembly

- Root cause: `.debug_line` carried file indices but the assembler discarded
  `.file` names, so the DAP fell back to opening the `.kex` binary.
- Fix: emit `.debug_sources`, merge it in the linker, resolve the source path
  against the program/workspace, and never fall back to the binary path.
- Fixed assembler string unescaping so single Windows backslashes in `.file`
  paths survive.
- Stack frames at entry report line 1 when no line row exists, so VS Code opens
  the source file.
- Disassembly requests now clamp large instruction counts instead of rejecting
  them; DAP smoke requests 100 instructions.
- Tests: DAP frame source path `hello.c`; 89/89 doctest (2970 assertions),
  Windows 24/24 CTest, Linux 23/23; DAP source/disassembly smoke green.
- Next: verify VS Code's Open Disassembly View context action manually.

### 2026-10-28 05:34:27 -07:00 — VS Code disassembly command

- Added **KR16: Show Disassembly** (`kr16.disassembly`) to the extension:
  requests `stackTrace`, then `disassemble` around the current PC, and opens a
  read-only disassembly document beside the source.
- Added a debug-toolbar menu entry and activation event; bumped the extension
  to 1.1.0 and packaged/installed `kr16-debug-1.1.0.vsix`.
- Clarified that the Disassembly/Registers/Memory/Peripherals sidebar views in
  the screenshot belong to PlatformIO, not KR16; the native VS Code
  disassembly view is opened from the stack-frame context menu.
- Next: manual VS Code smoke of the new command.

### 2026-10-28 05:51:30 -07:00 — live disassembly refresh

- The C source is the correct debug source: the stopped PC maps to
  `bigtest.c` line 14. The custom disassembly document was a snapshot, which is
  why it did not change while stepping.
- DAP now emits a `kr16.stopped` custom event carrying the current PC.
- Extension 1.2.0 listens for it and replaces the open disassembly document
  around the new PC on every stop/step.
- Bumped/packaged/installed `kr16-debug-1.2.0.vsix`; no generated `.S` file is
  needed for disassembly.
- Tests: DAP smoke asserts `kr16.stopped`; 89/89 doctest (2970 assertions);
  Linux 23/23; extension JS syntax check passes.
- Next: rebuild the adapter after stopping the active debug session, then
  reload VS Code.

### 2026-10-28 06:08:51 -07:00 — source breakpoint relocation fix

- Root cause: `.debug_line` relocation added only the output `.text` section
  base, not the object's placement offset. With libc first, bigtest line 9
  mapped to `0x010C` inside libc, so the source breakpoint never hit.
- Fix: relocate debug-line rows by the object `.text` placement address.
  Bigtest line 9 now maps to `0x0388`; continuing from line 14 stops at the
  line-9 breakpoint.
- Added a multi-object `.debug_line` placement test.
- Tests: 90/90 doctest (2976 assertions), Windows 24/24 CTest, Linux 23/23;
  source breakpoint smoke returned `reason: breakpoint`, `hitBreakpointIds:[1]`.
- Rebuilt `build\windows-ninja\kr16.exe` and `tests\kevin\bigtest.kex`.

### 2026-10-28 06:45:07 -07:00 — assembler `-l` listing file

- Gap: the assembler already built `AsmResult::listing` on every pass, but no
  CLI path wrote it, so the listing was dead code even though plan §6.10/§6.11
  specifies `kr16 as ... -l listing`.
- Wired `-l FILE` in `run_as`: same writer as the object, `-l` with no path is a
  usage error (exit 2), and the flag composes with `--raw`/`--base`.
- Format is `0xADDR  bytes  source` per line; object mode prints
  section-relative addresses, `--raw` prints `--base`-relative ones.
- Tests: `as -l` writes the file and its address/byte columns match the
  disassembler; missing path is a usage error; new `kr16.as.listing` CTest case.
  92/92 doctest (2985 assertions), Windows 25/25 CTest.
- Next: `-D name=value` is the remaining documented `as` flag that is still
  unimplemented.

### 2026-10-28 06:59:17 -07:00 — one-step `kr16 build`

- Added `kr16 build <file>`: `cc -S` → `as` → `ld` with `resources/libc.s`,
  writing `<name>.kex` next to the input. `.s`, `.asm`, and `.o` inputs are
  accepted too; `--libc` links the runtime for those (implicit for `.c`).
- Artifacts stay beside the source (`name.s`, `name.o`, `name.libc.o`), so
  there is no temp directory and nothing to clean up.
- Flags: `-o`, `-l`/`-L` (listing beside the source), `--listing=FILE`, `-I`,
  `-g` (on by default), `--no-debug`, `--libc`, `--run`. Option letters are
  matched case-insensitively.
- Moved the shell's `load` onto the same `build_program` pipeline, so it no
  longer leaves a stray `<name>.o` behind; `build` also works in the shell.
- Tests: `test_build.cpp` (C to runnable `.kex`, listing, debug sections,
  `--run`, `-I`, assembly/object input, `--libc`, usage errors) plus three
  CTest cases. 100/100 doctest (3018 assertions), Windows 28/28 CTest in both
  Debug and Release.
- Found while testing, not fixed here: an unresolvable `#include` is silently
  dropped, so `ANSWER` folded to 0 instead of erroring. Logged in
  `docs/deferred.md`.

### 2026-10-28 07:05:50 -07:00 — `%x` in the libc printf

- Added `%x` to `resources/libc.s`: unsigned hexadecimal, lowercase, no
  leading zeros and no `0x` prefix, so `printf("%x", 15)` prints `f` and
  `printf("%x", -1)` prints `ffff` (the value is 16-bit unsigned).
- Implementation: `.Lpf_print_hex` shifts right four bits at a time and
  recurses, so digits emit most-significant first; at most four frames deep.
  It reuses `.Lpf_getarg`, so `%x` mixes with `%d`/`%s`/`%c` and stack
  arguments behave the same way. Dispatch is one `CMP R0, #'x'` in the format
  loop; existing branches stay well inside the disp8 range.
- Refreshed the libc hash in `resources/manifest.json`; the CI bootstrap gate
  (`scripts/bootstrap-check.ps1`) passes.
- Tests: new `tests/integration/printf_hex.c` driven by `kr16.build.printf_hex`
  and `kr16.vm.printf_hex`, asserting
  `0 1 f 10 ff fff 1234 ffff -> 255=ff hex Z`. 100/100 doctest (3018
  assertions), Windows 30/30 CTest in both Debug and Release.
- Not added: `%X`, `%u`, `%o`, `%p`, and width/zero padding; those stay in
  `docs/deferred.md` with the rest of the printf family.

### 2026-10-28 07:13:15 -07:00 — `kr16 dis` label offset on linked images

- Root cause: the linker stores **absolute** symbol addresses in a `.kex`
  (`placement.address + symbol.value`), but `disassemble_container` added the
  output section address again when building its label map. Every label was
  therefore shifted by the section base (`+0x0106` for `.text`), so `main:` and
  the libc entry points vanished from the listing and surviving labels landed
  on unrelated instructions (`CALL .Lpd_positive` inside `atoi`).
- Fix: use `symbol.value` as-is for `ContainerKind::Exec` images and keep the
  section-relative path for objects. Objects are unaffected because their
  section addresses are zero.
- Tests: new doctest links a one-object image and asserts `_start:` and `loop:`
  sit on `0x0100`/`0x0102` and that a branch prints `BRA loop`. It fails on the
  old code (labels dropped, `BRA 0x0102`).
- 101/101 doctest (3024 assertions), Windows 30/30 CTest in Debug and Release.





### 2026-10-28 15:48:30 -07:00 — Reusing heap allocator

- Status: replaced the bump allocator in `resources/libc.s` with a first-fit
  allocator that reuses freed memory. `free` coalesces with both neighbours,
  `realloc` grows and shrinks in place when it can, and `calloc` checks for
  overflow before zeroing.
- Design: 2-byte header (size | used, minimum block 4 bytes); an
  address-ordered free list whose links live in the free blocks themselves;
  split only when the remainder can hold a 4-byte block; the topmost block is
  handed back so the footprint follows live data. The pool is a 16 KiB `.bss`
  array rather than the region above the image, because a `__heap_start`
  relocation cannot be resolved yet; that also keeps the heap clear of the
  stack at 0xF000.
- Accounting: `__kr16_heap_used()` and `__kr16_heap_peak()` exist for tests
  and the debugger. `exit` now returns its argument in R0.
- Toolchain finding: the linker defines `__heap_start`, `__stack_top` and
  `__stack_limit` *after* applying relocations, so a relocation against them
  either fails to link or silently assembles to zero. The old bump allocator
  was handing out addresses starting at 2 as a result. The allocator now avoids
  those symbols; the linker fix is still outstanding.
- Tests: new `tests/support/heapcheck.s` (invariant walker, linked by tests
  only), `tests/support/heap_stress.s` (400 seeded malloc/free operations with
  the checker after each) and `tests/integration/heap.c` (reuse, coalescing,
  churn, hole reuse, realloc grow/shrink/move, calloc zeroing and overflow,
  double free, 16 KiB ceiling), wired to CTest as `kr16.*.heap*`.
- 112/112 doctest (3126 assertions) with the existing malloc/free/calloc/realloc
  cases still passing; stress run 173,821 instructions, `heap ok peak=16002`.
- Next: fix `__heap_start` in the linker, then size the pool from the linker
  instead of a constant.

### 2026-10-28 — linker-provided heap symbols and casts

- Fixed `symbol_value()` in `src/link/linker.cpp` to consult `__heap_start`,
  `__stack_top` and `__stack_limit` while applying relocations, instead of
  defining them only after relocation. `.extern` references now resolve.
- Added a linker relocation test that checks the emitted `.data` words against
  the image's provided symbols.
- `kr16 cc` now parses C casts: integer/char/pointer casts, including the
  `int *p = (int *)malloc(...)` form, with pointer/index/dereference typing.
- CTest: 38/38 (`kr16.*.heap*` included); doctest: 114 cases / 3165 assertions.
- The allocator still uses its fixed 16 KiB `.bss` pool; switching to
  `[__heap_start, __stack_limit)` is the remaining heap change.

### 2026-10-28 — assembler branch relaxation

- Added iterative `Bcc` relaxation to `src/asm/assembler.cpp`. First pass
  collects symbols; subsequent layout passes promote out-of-range branches and
  recompute labels until stable; the final pass emits objects, relocations,
  listings and debug lines at the relaxed addresses.
- Long conditional branches use an inverse short branch over a 4-byte `JMP`;
  long unconditional branches use `JMP` directly.
- Added `docs/decisions/0003-branch-relaxation.md` and tests for forward and
  backward long branches plus large C functions (taken and skipped bodies).
- CTest: 38/38; doctest: 118 cases / 3228 assertions.
- `kr16 cc` is no longer limited to roughly 1 KiB functions by branch range.

### 2026-10-28 — detailed command help

- Added a centralized `command_help()` in `src/cli/commands.cpp` with complete
  usage, positional arguments, options, defaults and examples for `as`, `dis`,
  `cc`, `build`, `ld`, `size`, `debug`, `vm`, `dap`, `version`, `shell` and
  `help`.
- Every command now accepts `--help` and `-h`; `kr16 help <command>` uses the
  same text. The general help points at both forms.
- Documented accepted-but-inactive compatibility flags (`-D`/`-U`, `-O0`,
  `-std=...`, deferred file-service flags) and unavailable options (`-E`,
  `--json`, `-i`) rather than omitting them.
- Added a unit test that runs every command with `--help`, checks exit status
  and usage text, plus checks for the assembler's key options.
- CTest: 38/38; doctest: 118 cases / 3228 assertions.

### 2026-10-28 — compiler optimization M1/M2

- Added `docs/compiler-opt.md` with milestones M0–M5, test plans and notes.
- M1: AST constant folding, constant `if`/`while`/`for` control flow, algebraic
  identities, and unreachable-statement trimming.
- M2: target-directed codegen for simple expressions (`R0`/`R1`), direct local
  loads/stores, direct declaration stores, and simple assignment lowering.
- Added `CompileOptions.optimize` (`-O1` default, `-O0` fallback) and updated
  `kr16 cc --help`.
- Benchmark on a 1000-iteration loop: 35,033 → 16,025 instructions, 56,055 →
  25,041 cycles, 122 → 74 text bytes (`-O0` → `-O1`).
- Doctest: 121 cases / 3307 assertions; CTest 38/38 on Windows and 37/37 on
  Linux.
- M3–M5 (IR/CFG, graph coloring, peephole) remain planned in
  `docs/compiler-opt.md`.

### 2026-10-28 — scalar IR, CFG liveness, and graph coloring

- Added a three-address IR backend for scalar, call-free functions.
- Added basic blocks, CFG successors, liveness, interference graphs, and
  five-register graph coloring (`R0`–`R4`; `R4` is saved/restored).
- Unsupported functions (calls, aggregates, pointers, globals, static locals,
  more than three parameters) and graphs needing more than five registers fall
  back to the M1/M2 emitter.
- Debug builds (`-g`) keep the existing backend so frame-based debug variables
  and line mapping remain valid.
- Added `-O1` IR tests for loops, `if`/`else`, `do`/`while`, `break`,
  `continue`, `for`, fallback pressure, and function calls.
- Added M5 IR peepholes: adjacent copy coalescing, comparison/branch fusion,
  immediate-operand folding, dead-code elimination, strength reduction for
  power-of-two multiplies, constant-branch folding, and fallthrough-jump
  removal.
- Optimized simple one-to-three-argument call marshalling to load argument
  registers directly and avoid stack shuffling for `printf`-style calls,
  including simple binary argument expressions like `n - 1`.
- Fallback `if`/`while`/`for`/`do` conditions now branch directly on simple
  comparisons instead of materializing a 0/1 value.
- Fallback simple expressions now use immediate forms (`SUB R0, #1`,
  `AND R0, #0xFF`, power-of-two shifts) and target a caller-selected register
  for direct call arguments.
- Added liveness-based IR dead-store elimination for overwritten assignments.
- Added a target-directed pure-expression allocator for the fallback emitter;
  nested arithmetic uses `R0`–`R3` and spills only for deeper trees.
- Complex pure call arguments now evaluate directly into `R1`–`R3`.
- Scalar loop benchmark: `-O1` now 38 text bytes, 5,014 instructions, 7,026
  cycles (from 122 / 35,033 / 56,055 at `-O0`).
- Doctest: 131 cases / 3385 assertions; CTest 38/38 Windows and 37/37 Linux.
- Remaining work recorded in `docs/compiler-opt.md`: stack spilling, full copy
  coalescing, and broader IR coverage.

### 2026-10-28 — C-for-U test compiler

- Added `tests/kevin/C-for-U.c`, a compact C-to-KR16-assembly compiler
  written in the KR16 C subset.
- It embeds a fixed 1024-byte `source` array, streams assembler output through
  `putchar`, and uses `malloc`/`free` for its token buffers.
- Target output follows the existing ABI (R1-R3 arguments, R0 return, R4 frame)
  and can be linked with `resources/libc.s`.
- The embedded test program exercises recursion, calls, a local array, a loop,
  arithmetic, string data, and `printf`; generated output prints
  `sum=30 fact=120`.
- Compiler image: 18,428 text bytes, 1,992 data bytes, 21,854 bss bytes
  (including libc's 16 KiB heap); code-only budget is under 40 KiB.
- Added `kr16.cc.cforu`, `kr16.vm.cforu`, and `kr16.cforu.generated` tests. The
  last test captures the emitted assembly, assembles/links it, runs it, and
  checks the output.
- Full suite: 41/41 Windows CTest and 40/40 Linux CTest.

### 2026-10-28 — scrollable debugger panels

- Variables, call stack, and console panels now scroll independently with the
  mouse wheel and draw a scrollbar when content overflows.
- Variables panel scrolls through a combined locals/globals row list; global
  symbols are shown before internal `__kr16_*` runtime symbols.
- Console defaults to the bottom and scrolls upward when requested.
- C-for-U `main` has no locals, so its `(none)` locals list is expected; its
  compiler state (`source`, `tkind`, `ttext`, etc.) is global and now appears
  before the libc heap symbols.
- C-for-U's embedded test program also exercises `do`/`while` and `times++`.

### 2026-10-28 — load/compile/link settings

- Added a gear button and modal settings popup with optimization (`-O0`/`-O1`),
  debug info, libc linkage, and assembler-listing controls.
- Settings apply immediately: changing one stops a running program, saves the
  setting, and rebuilds/reloads the current source.
- Settings persist in `%APPDATA%/KR16/settings.ini` or the platform equivalent
  config directory.
- Added the debug/IR warning when `-g` and `-O1` are both enabled.
- Added `BuildOptions::optimize`, passed `-O0`/`-O1` through `build_program`,
  and made libc linkage a real build option for all input kinds.
- Added `kr16 build -O0/-O1` command-line flags.

### 2026-10-28 — source gutter and memory focus

- Restored a visible 18 px breakpoint gutter at the left of the C source pane,
  with a separator and shifted line numbers; breakpoint dots still occupy the
  gutter column.
- Clicking a variable now points the memory panel at that variable's address.
  This is especially useful for arrays such as `source`, whose displayed value
  is its first two bytes, not its address.

### 2026-10-28 — gutter and variable annotation tweaks

- Moved the source breakpoint marker toward the left edge of its gutter.
- Added matching 18 px gutter, separator, and 6x6 breakpoint marker to the
  disassembly pane.
- Variable rows now include their address, for example `source (&0x90CA)`.

### 2026-10-28 — source gutter column alignment

- Widened the source gutter to match the disassembly address column (90 px)
  and right-aligned line numbers at the same position as disassembly addresses.
- Source code now starts after the shared gutter width, with the breakpoint
  marker still in the left 18 px slice.

### 2026-10-28 — independent code scrolling and step follow

- Source and disassembly panes no longer snap back to the current line on every
  repaint. Wheel scrolling remains where the user left it so breakpoints can be
  set elsewhere.
- When execution advances (step, run-to-breakpoint, restart, reload), both panes
  scroll only if needed to bring the current line/instruction back into view.

### 2026-10-28 — variable address/value columns

- Centered the red breakpoint marker in the 18 px source and disassembly gutters.
- Removed the address suffix from variable names and added separate `Addr` and
  `Value` columns, with headers level with the `Variables` title.
- Variable addresses are shown as `&0x....` in their own column.

### 2026-10-28 — variable header typography

- `Addr` and `Value` now use the same accent bold font as the `Variables`
  title.
- Headers are centered over fixed-width `Addr` and `Value` columns.

### 2026-10-28 — follow edited PC and decode outside text

- Auto-follow now triggers on a PC change as well as instruction progress, so
  editing PC scrolls the source/disassembly to the new location.
- If PC is moved outside the image's normal `.text` disassembly, the session
  decodes a 128-byte window at PC and inserts it into the disassembly list.
- Disassembly highlighting treats a PC inside a multi-byte instruction as that
  instruction's current row.

### 2026-10-28 — draggable vertical scrollbars

- Added vertical scrollbars to the C source and disassembly panes.
- Replaced the bottom-panel indicator-only scrollbars with shared draggable
  scrollbars for Variables, Call stack, and Console.
- Thumb dragging and track paging update the corresponding scroll offset
  immediately; wheel scrolling and step-follow behavior are unchanged.

### 2026-10-28 — typed variables and pointer preview

- Debug metadata now records local variable types and emits a `.debug_globals`
  name/type table; the linker preserves it.
- Variable tooltips show the type.
- Hovering a `char *` displays up to 16 characters of the pointed-to string,
  with `...` when longer or `<null>` for a null pointer.
- Clicking a pointer variable's value routes that value into the memory pane.

### 2026-10-28 — disassembly hover index fix

- Hover detection stored the visible-row index, then the tooltip looked up that
  index in the full disassembly vector. After scrolling, this showed help for a
  completely different instruction.
- Rows now carry their full disassembly index; hover highlighting and help use
  that index. Scrolling also clears the stale hover.

### 2026-10-28 — watch pane, pointer following, and memory views

- Added a Watches section to the Variables pane. Right-click a variable to pin
  or unpin it; watches re-resolve on every paint and survive stepping.
- Watch rows show `+`/`-` expanders for arrays, pointers, and structs. Array
  elements and struct fields become child rows; struct field metadata is emitted
  in `.debug_types`.
- Pointer values now support dereference previews for scalar pointees as well
  as `char *` strings.
- Clicking a pointer value selects it for the Memory pane and enables `PTR`
  follow mode; `PTR` toggles tracking while the pointer value changes.
- Memory now cycles through `HEX`, `ASC`, and `STR` views.
- Array and struct values get type-aware summaries in the Variables list.

### 2026-10-28 — gutter context menu and character values

- Right-clicking the source or disassembly gutter opens a context menu with
  **Toggle breakpoint** and **Run to line/address**.
- Run-to uses a temporary breakpoint and removes it when execution stops.
- One-byte variables display a printable character alongside the numeric value;
  space is shown as `' '` and non-printable bytes are omitted.

### 2026-10-28 — pane height adjustment

- Memory now shows four 16-byte rows (hex and alternate views).
- The bottom Variables/Call stack/Console area gained four text rows.
- C source and disassembly panes shrink by the same amount to make room.

### 2026-10-28 — system-services C interface specification

- Added `systemservices.md` defining the per-service C argument/result
  structures, `SYS` block mapping, service numbers, errors, limits, sandbox
  policy, and examples.
- Added the reference `include/kr16/systemservices.h` header. Its `sys_*`
  wrappers will ship as the corresponding host services are implemented.

### 2026-10-29 — sandboxed file I/O and stdio

- Implemented `SYS_OPEN`/`CLOSE`/`READ`/`WRITE`/`SEEK`/`STAT`/`UNLINK`/
  `RENAME`/`MKDIR`/`READDIR`/`TIME`/`RANDOM`/`FSINFO` with a portable
  sandbox/filesystem backend.
- Added `resources/fileio.c` with the `sys_*` wrappers and an unbuffered stdio
  subset: `fopen`, `fclose`, `fread`, `fwrite`, `fgetc`, `fputc`, `fgets`,
  `fputs`, `fseek`, `ftell`, `rewind`, `fflush`, `feof`, `ferror`, `clearerr`,
  `remove`, and `rename`.
- Added runtime compile/link integration for `fileio.c`, embedded runtime
  sources, `--fs-root`/`--fs-quota`/`--fs-readonly`/`--no-host-fs` plumbing,
  and file-I/O CTest coverage.
- The host ABI now reports the implemented filesystem, directory, time,
  random, console, and fsinfo capability bits.

### 2026-10-29 — command-line arguments in the debugger

- Added a Windows-compatible command-line parser that preserves backslashes in
  Windows paths and supports quoted arguments and empty quoted strings.
- Extracted argv stack construction into `initialize_process_stack` and used it
  from the VM runner, DebugSession (including restart), and DAP launch.
- Added `SessionBuildOptions::command_line`, a portable single-line `TextField`
  with selection/copy/cut/paste, and a settings-dialog field. Arguments are
  applied on Load or Restart; the generated image basename is `argv[0]`.
- Added parser/stack tests and a debug-session argc/argv test.

### 2026-10-29 — C-for-U file output

- C-for-U derives `<input-base>.s` from the input path or uses the `-o` path.
- Assembly and argv comments are written to the output file through the
  sandboxed file API; stdout reports `; wrote <path>`.
- Added default-output and explicit-output CTest coverage.

### 2026-11-14 08:47:16 -08:00 — Post-1.0 heap from linker symbols

- `resources/libc.s` now initializes the heap from `.extern __heap_start` and
  `.extern __stack_limit`; the fixed 16 KiB `.bss` pool and
  `__kr16_heap_block`/`__kr16_heap_limit` are gone.
- Heap metadata is eight bytes of `.bss`; growing the heap consumes only the
  linker-defined region above the image.
- Updated `tests/integration/heap.c` to allocate and touch 20,000 bytes and
  verify the peak, proving the old 16 KiB ceiling is gone.
- Regenerated `src/resources/embedded_libc.cpp` and `resources/manifest.json`.
- Updated ADR 0002, the allocator handoff, and the final-plan next-work list.
- Tests: Windows 48/48 CTest; Linux 48/48 CTest with UI off; heap test reports
  `heap ok peak=20002`, BSS is 8 bytes.
- Next: undefined-symbol diagnostics.

### 2026-11-14 08:50:50 -08:00 — Undefined assembly symbols

- The assembler now records unresolved symbol references and rejects any that
  are neither defined nor declared with `.extern`:
  `undefined symbol 'missing' (declare it with .extern for another object)`.
- Forward references within a file still resolve after the final pass; `.extern`
  declarations still produce linker relocations.
- Added regression tests for both the error and the allowed `.extern` case.
- Updated the assembly manual and final plan to remove the silent-zero warning.
- Tests: Windows 48/48 CTest; Linux 48/48 CTest; new assembler tests pass.
- Next: buffered stdio and the remaining `printf` family.

### 2026-11-14 09:03:50 -08:00 — Buffered stdio and fprintf

- Extended `resources/fileio.c` with per-stream buffers, flush/refill helpers,
  line/full/unbuffered modes, `ungetc`, `setvbuf`, standard `stdin`/`stdout`/
  `stderr` streams, and a varargs `fprintf`.
- `fprintf` supports `%d`/`%i`, `%u`, `%x`/`%X`, `%o`, `%p`, `%c`, `%s`, `%%`,
  width, `-`, and `0` padding.
- Fixed codegen for byte locals beyond the 6-bit displacement range by emitting
  `LEA` before `LDB`/`STB`.
- Fixed the compiler's all-stack call path for external `fprintf` so fixed
  parameters and varargs use the same convention as the variadic callee.
- Remaining gaps are `fscanf`, `freopen`, `tmpfile`, and `printf`/`fprintf`
  format parity; the assembly `printf` still has the older format set.
- Tests: new `stdio_buffered.c` write/read/seek/`fprintf`/`ungetc` test;
  Windows 50/50 CTest, Linux 50/50 CTest.
- Next: finish stdio parity or start compiler optimization M3–M5.

### 2026-11-14 09:16:21 -08:00 — printf parity and stdio follow-up

- Moved formatted output to the buffered C stdio layer: `printf` now calls the
  same `format_stream` helper as `fprintf`, so both support `%d`/`%i`/`%u`/
  `%x`/`%X`/`%o`/`%p`/`%c`/`%s`/`%%`, width, `-`, and `0` padding.
- Removed the older assembly `printf`; `libc.s` now provides console primitives
  and the C runtime owns formatting.
- Compiler calls to `printf`/`fprintf` use the all-stack variadic convention.
- Updated C-for-U’s generated calls to `printf` to use that convention.
- Updated the heap and printf CTest links to include the runtime fileio object.
- Tests: Windows 51/51 CTest; Linux 51/51 CTest; `printf_hex`, C-for-U,
  heap, fileio, and stdio all pass.
- Remaining stdio gaps: `fscanf`, `freopen`, `tmpfile`, interactive stdin
  buffering.

### 2026-11-14 09:20:43 -08:00 — Compiler M5 address folding

- Constant indexes into local arrays now fold directly into the frame
  displacement: `values[2]` becomes one `LEA R0, [R4-...]` instead of
  push/scale/pop/add.
- Added a pattern and differential execution test for constant array indexes.
- Marked the M5 address-calculation item complete in `docs/compiler-opt.md`.
- Tests: Windows 51/51 CTest; Linux 51/51 CTest.
- Next: remaining M3/M4 spilling and loop benchmarks.

### 2026-11-14 09:24:27 -08:00 — Compiler M5 member address folding

- Constant member offsets of local structs now fold directly into the frame
  displacement: `p.y` becomes one `LEA R0, [R4-...]` instead of `LEA` plus
  `ADD`.
- Added a pattern and differential execution test for the struct member case.
- Extended the M5 address-folding note in `docs/compiler-opt.md`.
- Tests: Windows 51/51 CTest; Linux 51/51 CTest.
- Next: remaining M3/M4 spilling and loop benchmarks.

### 2026-11-14 09:27:00 -08:00 — Compiler M5 loop benchmark

- Added a loop-heavy differential test: `-O1` must emit fewer instructions
  than `-O0` and produce the same result.
- Marked the M5 loop benchmark suite item complete in `docs/compiler-opt.md`.
- Tests: Windows 51/51 CTest; Linux 51/51 CTest.
- Next: remaining M3/M4 spill/select coloring and copy propagation.

### 2026-11-14 09:28:39 -08:00 — Compiler benchmark corpus

- Added `tests/benchmarks/loops.c` and a README with `-O0`/`-O1` VM stats.
- Recorded: 2,503 instructions / 4,055 cycles at `-O0` versus 1,237 / 2,033 at
  `-O1`; text 11,882 versus 11,794 bytes.
- Fixed `kr16 build -O0/-O1`: option matching lowercased the argument but
  compared against uppercase `-O`, so both flags were rejected.
- Tests: full Windows and Linux suites remain green; benchmark commands now
  work.
- Next: remaining M3/M4 spill/select coloring and copy propagation.

### 2026-11-14 09:35:03 -08:00 — CLI optimization-level regression test

- Added a unit test that builds the same C source with `-O0` and `-O1` and
  requires both commands to succeed.
- This locks in the `kr16 build` option-parsing fix.
- Tests: Windows 51/51 CTest; Linux 51/51 CTest.
- Next: remaining M3/M4 spill/select coloring and copy propagation.

### 2026-11-14 09:40:09 -08:00 — DAP restart

- Added `supportsRestartRequest` and a `restart` handler that reloads the
  program, resets the VM, preserves breakpoints, and re-emits the entry stop.
- Added a DAP session test covering the restart response and stopped event.
- Updated the final plan and VS Code README; restart is no longer a debugger
  gap.
- Tests: Windows 51/51 CTest; Linux 51/51 CTest.
- Next: conditional breakpoints/setVariable, or the remaining compiler spills.

### 2026-11-14 09:47:28 -08:00 — DAP loaded sources and source content

- Added `supportsLoadedSourcesRequest`, `loadedSources`, and `source` request
  handling. The adapter reports the resolved debug source path and returns the
  file contents.
- Added DAP tests for the loaded-source path and source content.
- Updated the final plan and VS Code README capability tables.
- Tests: Windows 51/51 CTest; Linux 51/51 CTest.
- Next: conditional breakpoints and setVariable.

### 2026-11-14 09:52:53 -08:00 — DAP conditional breakpoints

- Added simple conditional breakpoints: `$r0`–`$r4`, `$pc`, `$sp`, and `$sr`
  compared with `==`, `!=`, `<`, `<=`, `>`, or `>=` against decimal or `0x`
  literals.
- Breakpoint conditions are stored with line/address breakpoints and checked
  before reporting a hit; `supportsConditionalBreakpoints` is now true.
- Added a DAP test that sets an entry breakpoint with `$pc == 0x0100` and
  verifies it stops with reason `breakpoint`.
- Tests: Windows 51/51 CTest; Linux 51/51 after one flaky DAP-smoke rerun.
- Next: `setVariable`, delayed stack traces, or the remaining compiler spills.

### 2026-11-14 10:01:15 -08:00 — DAP setVariable

- Added `supportsSetVariable` and a `setVariable` handler for registers
  (`R0`–`R4`, `SP`, `SR`, `PC`), globals in `.data`/`.bss`, and locals from
  `.debug_vars`.
- Added a DAP test that writes `R0`, reads it back through `evaluate`, and
  verifies `0x1234`.
- Updated the final plan and VS Code README; remaining debugger gaps are
  hit-count breakpoints, step back, and delayed stack traces.
- Tests: Windows 51/51 CTest; Linux 51/51 CTest.
- Paused after this gate at the user's request.

### 2026-11-14 17:24:22 -08:00 — DAP hit-count breakpoints

- Added numeric `hitCondition` support: a breakpoint stops on its Nth hit.
- `supportsHitConditionalBreakpoints` is now true.
- Added a DAP test that sets hit count 2 at the entry address and verifies the
  first restart reports `entry` while the second reports `breakpoint`.
- Updated the final plan and VS Code README.
- Tests: Windows 51/51 CTest; Linux 51/51 CTest.
- Next: step back and delayed stack traces, or remaining compiler spills.

### 2026-11-14 17:29:04 -08:00 — DAP delayed stack traces

- `stackTrace` now honors `startFrame` and `levels`, returns a paged
  `stackFrames` array, and reports the full `totalFrames`.
- `supportsDelayedStackTraceLoading` is now true.
- Added a DAP test requesting one frame and checking the page size and total.
- Updated the final plan and VS Code README; only step back remains in the
  debugger roadmap.
- Tests: Windows 51/51 CTest; Linux 51/51 CTest.
- Next: compiler spills or step back, then release pipeline parity.

### 2026-11-14 17:32:38 -08:00 — Release pipeline parity

- Added a `vscode` release job that packages `kr16-debug.vsix` with
  `@vscode/vsce`.
- The publish job now waits for the VSIX, writes `SHA256SUMS` for the exe,
  both AppImages, and the VSIX, and attaches all five assets to the release.
- Added `kr16.release.workflow` as a policy test that guards the workflow
  contents.
- Updated the final plan, README, and deferred list.
- Tests: Windows 52/52 CTest; Linux 52/52 CTest.
- Next: curriculum examples and the remaining compiler spills.

### 2026-11-14 17:35:46 -08:00 — Curriculum assembly examples

- Added `examples/sum_loop.s`: hand-written 1..10 loop, exits with 55.
- Added `examples/putchar.s`: writes `KR16` through the `0xF000` console
  port.
- Added CTest build/run coverage for both examples, including an
  expected-exit checker for the 55 result.
- Updated `examples/README.md` and linked the worked exercises from
  `docs/curriculum.md`.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.
- Remaining roadmap: compiler M3–M5 spill/copy/loop work and DAP step back.

### 2026-11-14 17:41:18 -08:00 — DAP step back

- Added `supportsStepBack` and a 64-entry snapshot history of machine + CPU
  state before step/next/stepOut.
- `stepBack` restores the last snapshot and emits a `step` stop; `continue`
  clears the history.
- Added a DAP test that steps one instruction, verifies PC moved, steps back,
  and verifies PC returned to the original address.
- Updated the final plan and VS Code README; debugger depth is complete.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.
- Remaining: compiler M3–M5 spill/copy/loop work.

### 2026-11-14 17:45:03 -08:00 — Compiler high-pressure fallback test

- Added a differential test for an eight-parameter scalar function, exercising
  the M2 fallback when the IR graph exceeds R0–R4 pressure.
- Documented the current spill policy in `docs/compiler-opt.md`: pure
  expression trees spill in the fallback; IR coloring still declines graphs
  above five registers.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.
- Remaining: true IR spill/select coloring and loop-invariant optimization.

### 2026-11-14 17:46:24 -08:00 — Compiler M3–M5 scope closed

- Closed M3–M5 for the scalar teaching backend: IR/CFG/liveness, R0–R4 graph
  coloring, address folding, peepholes, loop benchmark, and high-pressure
  coverage are in place.
- Documented the pressure policy explicitly: graphs above five registers use
  the M2 fallback, which spills pure expression trees; IR spill slots, broader
  copy propagation, and a dedicated loop-invariant pass remain future polish.
- Tests already green: Windows 56/56 CTest; Linux 56/56 CTest.
- Roadmap items 1–7 are now implemented for the documented 1.x teaching scope.

### 2026-11-14 18:47:00 -08:00 — IR spill/select allocation

- Added a spill path to the scalar IR backend. It first attempts the existing
  five-color allocation; if that fails it reruns selection with R0–R1 as
  candidates, R2–R3 as scratch, and stack slots for the remaining values.
- The spilled emitter sets `R4` as a frame pointer, reserves the spill frame,
  loads/stores virtual values, and restores SP/R4 on every return.
- Unsupported functions still use the M2 fallback; `-g` remains on M2.
- Added an eight-live-local differential test returning 91 and exercised the
  spill frame (`SUB SP, #20` in the generated function).
- Verification: Windows 56/56 CTest; Linux 56/56 CTest; loop benchmark
  unchanged at 1,237 instructions / 2,033 cycles for `-O1`.
- Remaining: basic-block copy propagation and induction-variable strength
  reduction.

### 2026-11-14 18:58:49 -08:00 — Spill-path review follow-up

- Added spill stress tests for branch comparisons, char truncation, and a
  loop that keeps more than five values live across spill stores.
- Reviewed the 240-byte spill-frame cap and documented why larger frames fall
  back to M2: the prologue uses one 8-bit `SUB SP` immediate.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.
- Commits `6ee82b4` and `0a79b35` are on `origin/main`; this follow-up is
  ready to commit.

### 2026-11-14 19:16:12 -08:00 — Basic-block copy propagation

- Added `ir_copy_propagate`: aliases `Move`/`Assign` destinations within a basic
  block, rewrites later uses, kills aliases when either value is redefined, and
  removes alias assignments whose destination is not live out.
- Runs after dead-store elimination and before coloring; refreshes liveness when
  it changes the IR.
- Added a pattern test: `a = x; b = a; c = b; return c` now emits the alias
  chain as one `MOV R0, R1`.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.
- Benchmark remains 1,237 instructions / 2,033 cycles at `-O1`.
- Next: induction-variable strength reduction.

### 2026-11-14 19:25:01 -08:00 — Basic-block constant reuse

- `ir_const` now returns an existing constant value when the same immediate has
  already been defined in the current basic block.
- Added a pattern test: two `2` literals in one function now share one
  `MOV ..., #2`; the function still returns 5.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.
- Benchmark remains 1,237 instructions / 2,033 cycles at `-O1`.
- Next: induction-variable strength reduction.

### 2026-11-14 19:36:34 -08:00 — Induction-variable strength reduction

- Added `ir_strength_reduce_loops`: finds a counted-loop latch self-increment,
  replaces `i * C` in the body with a loop-carried accumulator initialized to
  zero, and increments it by `C * step` in the latch.
- Skips loops that modify the induction variable inside the body.
- Runs after copy propagation, then re-runs dead-code elimination and
  fallthrough cleanup before coloring.
- Test: `i * 3` over four iterations returns 18 and the function contains no
  `MUL`.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.
- Loop benchmark remains 1,237 instructions / 2,033 cycles at `-O1`.

### 2026-11-14 20:05:40 -08:00 — Pointer and logical conditions

- Extended the fallback simple-leaf emitter to handle `*p` and constant-index
  array/pointer accesses (`p[0]`) with the correct byte/word load.
- `emit_simple_condition` now emits direct branches for those leaves and
  short-circuits `&&`/`||` in both true and false branch directions.
- Added an `is_simple_condition` precheck so unsupported conditions fall back
  without emitting a partial branch and leaving an undefined label.
- Added a pointer-condition test; regenerated `C-for-U.S` with the same
  function; `same` now branches directly in both loop tests and the mismatch
  check.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.
- `resources/fileio.c` compiles and assembles again; it now exercises the
  `mode == 0 || mode[0] == '\0'` form.

### 2026-11-14 20:22:45 -08:00 — Indexed and arithmetic conditions

- Extended condition leaves to variable indexes: `s[i]` now loads the base and
  index into separate registers instead of push/pop.
- Index address generation uses `MOV R1, R0` plus an index register for simple
  indexes, removing a push/pop from each `d[i] = s[i]`-style access.
- Comparison conditions now accept arithmetic right operands such as
  `i < max - 1`; they evaluate the right operand into R1 and branch directly.
- Removed the stale `is_simple_binary` guard in the condition emitter that was
  causing a silent fallback after validation.
- `C-for-U.S` regenerated: `same` and `i < max - 1 && s[i] != 0` now branch
  directly; `PUSH R0` count dropped from 286 to 145.
- Added a differential pattern test for the arithmetic/indexed condition.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-14 20:31:03 -08:00 — Simple indexed stores

- Assignments whose value is a simple leaf now keep the destination address in
  R2 and store directly, instead of `PUSH R0` / evaluate / `POP R0`.
- `d[i] = 0` becomes address calculation, `MOV R2, R0`, `MOV R0, #0`,
  `STB [R2], R0`.
- Added a pattern and differential test for `d[2] = 0` / `return d[2]`.
- Regenerated `C-for-U.S`; the `PUSH R0` count dropped from 145 to 112.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-14 20:36:20 -08:00 — Direct indexed base loads

- Indexed address generation now loads a simple identifier base directly into
  R1 instead of loading into R0 and copying with `MOV R1, R0`.
- In `C-for-U`, `d[i] = 0` now starts:
  `LD R1, [R4-2]`, `LD R0, [R4-8]`, `ADD R0, R1`.
- Added a pattern/differential test for a pointer base with a variable index.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-14 21:02:43 -08:00 — Multi-argument call registers

- Calls with more than three arguments now keep the first three in R1–R3
  when they are register-safe simple expressions, instead of pushing all
  arguments and popping the first three back.
- Also handles the common form where argument 0 is a complex expression but
  arguments 1–2 are simple: stack arguments are pushed, argument 0 is
  evaluated to R1, and arguments 1–2 go to R2/R3.
- Restricted the simple-argument check to operations the leaf emitter
  supports, preventing address-of and other unsupported unary forms from
  entering the optimized path.
- `C-for-U.S` `PUSH R0` count dropped from 112 to 52.
- Added a differential test for a four-argument call.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-14 21:23:34 -08:00 — Store value pairing and remaining call pushes

- Simple indexed stores now evaluate the value directly into R1 and keep the
  destination address in R0; `d[i] = 0` emits `MOV R1, #0; STB [R0], R1`
  rather than preserving a copy of the address in R2.
- Extended multi-argument call setup to cover four-or-more argument calls
  whose first three expressions are register-safe, and the common form where
  argument 0 is complex while arguments 1–2 are register-safe.
- Restricted the simple-argument predicate to supported unary operations,
  fixing a temporary fileio/stdout regression from address-of arguments.
- `C-for-U.S` `PUSH R0` count is 52.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.
- Remaining priority items: loop-local pointer/induction registers, broader
  redundant load/store peepholes, and direct boolean returns.


### 2026-11-14 21:29:33 -08:00 — Unused assignment results

- Expression statements now suppress the assignment result copy when the value
  is unused.
- `d[i] = 0;` now ends at `MOV R1, #0; STB [R0], R1` without the trailing
  `MOV R0, R1`.
- This is the first part of the redundant copy/store peephole priority item.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.
- Remaining: loop-local pointer/induction registers and direct boolean returns.

### 2026-11-14 21:52:09 -08:00 — Loop induction cache

- Added a constrained loop-local cache for counted `for` loops: a scalar
  induction variable is loaded into R3, read from R3 through the condition and
  body, incremented directly in the latch, and stored back at loop exit.
- The cache is only enabled when the body has no calls, returns, nested loops,
  address-taking, or other writes to the induction variable.
- Added a differential/pattern test; `C-for-U.S` now contains 21 loop-cache
  load/update sites.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.
- Remaining priority items: broader redundant load/store peepholes and direct
  boolean returns.

### 2026-11-14 21:57:19 -08:00 — Adjacent local access peepholes

- Added a conservative adjacent-line peephole for local accesses:
  - remove `MOV Rx, Rx`;
  - remove a duplicate `LD Rx, [R4-slot]`;
  - fold `LD Rx, [R4-slot]` immediately followed by
    `ST [R4-slot], Rx`;
  - fold `ST [R4-slot], Rx` immediately followed by
    `LD Rx, [R4-slot]`.
- Only adjacent lines with the same register and slot are touched, avoiding the
  cross-basic-block cache bug from the earlier attempt.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.
- Remaining: direct boolean returns.

### 2026-11-14 22:06:36 -08:00 — Direct boolean returns

- `return <comparison>` and `return <logical expression>` now branch directly to
  two return-value paths instead of materializing a 0/1 temporary through
  `purecmp`/`fastcmp`.
- Restricted the optimization to comparison/logical expressions so ordinary
  values such as `return x;` are unchanged.
- Added a fallback-backend test covering `return a == b`; it checks that no
  `purecmp`/`fastcmp` label remains and that the result is 1.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.
- Remaining priority item: extend loop-local caching to while-loop pointers.

### 2026-11-14 22:26:04 -08:00 — Single-update while pointer cache

- Added a narrow pointer-loop cache: for `while` bodies that contain exactly one
  update of a local pointer/int/char, the variable is loaded into R3, the
  condition/update use R3, and the value is written back at loop exit.
- The generated loop is first built in a temporary stream and scanned for any
  unexpected R3 destination; if one is present, the cache is discarded and the
  normal loop is generated.
- This is intentionally narrower than the failed general cache and avoids the
  `printf_hex` regression.
- Added a differential/pattern test for `while (*p != 0) { p = p + 1; }`.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 03:19:58 -08:00 — Multi-statement while pointer cache

- Generalized the while cache from a single update to any body with one pointer
  update plus other statements: cache the pointer in R3, run the whole body
  with assignment/++ interception, and write R3 back at loop exit.
- The candidate is found by scanning for a pointer assignment/increment; the
  body generator writes into a temporary stream and is rejected if it clobbers
  R3 in a way the cache cannot tolerate.
- Verified with `while (*p) { n = n + 1; p = p + 1; }`; `p` stays in R3 while
  `n` remains frame-backed.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.
- Remaining priority-list work: parameter/local traffic, remaining call pushes,
  and benchmark coverage for the new loop passes.

### 2026-11-15 03:20:48 -08:00 — Optimization-specific benchmark

- Added `tests/benchmarks/opt_loops.c`, covering `i * 3` induction
  strength reduction and the while-pointer R3 cache.
- Recorded Windows Debug results: 757 -> 300 instructions, 1,216 -> 428
  cycles, 9,386 -> 9,238 text bytes from `-O0` to `-O1`.
- Added the commands and table to `tests/benchmarks/README.md`.
- No compiler behavior changed in this step.

### 2026-11-15 03:29:36 -08:00 — Read-only parameter cache

- Added a fallback-backend parameter cache: the first used, read-only scalar or
  pointer parameter is copied to R3 and read from there for the function body.
- Cached only for functions with no calls, loops, address-taking, or writes to
  that parameter.
- The body is generated into a temporary stream; if it writes R3 unexpectedly,
  the cache is discarded and the body is generated normally.
- Added a differential/pattern test for a read-only `int` parameter.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 03:33:42 -08:00 — Cached parameters skip frame stores

- Parameter-cache selection now runs before the prologue; a cached read-only
  parameter is not stored into its frame slot.
- Example: `int f(char *p, int x) { return x + 1; }` now emits `MOV R3, R2`
  and no `ST [R4-4], R2`.
- Parameter caching is disabled for debug builds.
- Added a pattern/differential test asserting the store is absent.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 03:41:12 -08:00 — Return-aware pointer loop cache

- The while-pointer cache now permits returns inside the loop: before returning,
  the cached pointer is written back to its frame slot.
- This lets `same(char *a, char *b)` keep `a` in R3 across the loop and its
  early mismatch return, while `b` stays frame-backed.
- Added an assertion that the `same` function contains an R3 cache load; the
  differential check still returns 1 for matching strings.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 03:48:12 -08:00 — R2 staging for complex stores

- Call-free complex assignment values now keep the target address in R2 while
  the value is evaluated, instead of `PUSH R0` / `POP R0` with an R1 shuffle.
- Applies only when the right-hand side has no calls; call-bearing stores retain
  the old push/pop path.
- `C-for-U.S`: `PUSH R0` count 52 -> 51, text 89,804 -> 89,785 bytes.
- Added a differential/pattern test.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 03:55:08 -08:00 — Two-register loop cache

- Extended the while-pointer cache to two loop-carried pointers: the first is
  kept in R3 and the second in R2. `same(char *a, char *b)` now keeps both `a`
  and `b` in registers across the loop.
- Assignment and `++`/`--` interception handle both cached names; returns write
  both registers back before leaving.
- The generated body is scanned for unexpected R3 writes and, via an R2->R3
  substitution scan, unexpected R2 writes.
- Added an assertion that `same` loads both R3 and R2.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 04:03:11 -08:00 — Cached parameters shrink the frame

- When the cached read-only parameter occupies the top frame slot, that slot is
  removed from the frame size. `f(char *p, int x)` now uses `SUB SP, #2`
  instead of `#4`.
- Parameter-cache returns no longer write the cached value back to its (now
  removed) frame slot.
- Added a pattern assertion for the reduced frame.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 04:09:54 -08:00 — Read-only local cache

- Added a text-level local cache pass: after a read-only local's initializer
  store, emit `LD R3, [R4-slot]` and replace later loads from that slot with
  `MOV Rt, R3`.
- Only enabled when the local is read-only and the function has no calls, loops,
  or address-taking. The transformed body is rejected if it clobbers R3.
- Added a pattern/differential test for `int x = 5; return x;`.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 04:24:13 -08:00 — Post-loop cache retention

- When a pointer-loop cache is followed immediately by a call-free return, the
  cache is kept live instead of being discarded at loop exit.
- `same(char *a, char *b)` no longer reloads both pointers before its final
  comparison; it reuses R3/R2 and skips the duplicate return writeback because
  the loop exit already stored them.
- Caches are cleared at function end so retained state cannot leak into the next
  function.
- Added an assertion that `same` has no `LD R0, [R4-2]` reload.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 04:41:02 -08:00 — Updated-local cache and refreshed benchmarks

- Local R3 caching now covers updated locals, not just read-only ones: stores
  after initialization become `MOV R3, R0`, and later loads use R3.
- Allowed straight-line code, `if`, and `switch`; loops, calls, address-taking,
  and indirect stores (for example `i++` via `LEA`) reject the cache.
- Added tests for updated locals across `if` and `switch`; fixed a switch-case
  recursion hole in the safety scanner that could cache loop locals.
- Extended `tests/benchmarks/opt_loops.c` with a `same()` pointer loop and
  refreshed the Windows Debug results: 1,232 -> 478 instructions,
  1,957 -> 671 cycles, 9,358 -> 9,122 text bytes.
- Documented the remaining fourth-argument `PUSH R0` instructions as
  ABI-required in `docs/compiler-opt.md`.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 04:47:47 -08:00 — Call-aware parameter cache

- Read-only parameter caching now permits calls when every call uses at most
  three arguments.
- The parameter keeps a frame slot; each `CALL` is surrounded by a spill
  (`ST [R4-slot], R3`) and reload (`LD R3, [R4-slot]`).
- Added call detection for declaration initializers and switch cases, plus a
  call-arity check so `R3` is not used for a fourth argument before the spill.
- Added a test where `x` stays cached across `inc(x)` and the result is 9.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 06:56:40 -08:00 — Call-aware local cache

- Cached locals now survive calls: every call inside the cached region spills R3
  to the local slot and reloads it afterward.
- Calls are permitted only when every call uses at most three arguments, so R3
  is not consumed as a fourth argument before the spill.
- Declaration initializers and switch cases are included in call discovery.
- Local candidate selection is deterministic: the lowest frame offset wins
  instead of unordered_map iteration order.
- Added a test where `x` survives `inc(x)` across an update and returns 4.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 07:08:36 -08:00 — Two-argument call-spill limit

- Cached parameters and locals may spill across calls only when every call uses
  at most two arguments, keeping R3 out of argument setup.
- Removed the unused loop-call spill helper after C-for-U exposed that
  loop-carried spill/reload was not safe; loop caches continue to reject calls.
- Updated the local-cache documentation and test expectation for the cached
  call argument (`MOV R2, R3`).
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 07:12:52 -08:00 — Two live locals in R3/R2

- The local cache now holds up to two locals: the lowest frame offset goes to
  R3, the next to R2.
- Both locals get load/store redirection and per-call spill/reload. Two-local
  caching requires every call to use at most one argument so R2 is not consumed
  as an argument.
- The second transform is applied only if the first succeeded; the combined
  body is rejected if either register is unexpectedly clobbered.
- Added a differential/pattern test where `x` and `y` both stay live.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 07:13:22 -08:00 — Fourth-argument stack evaluation

- Evaluated storing a fourth argument directly into a reserved stack slot
  instead of `PUSH R0`.
- The direct form needs `SUB SP, #2` plus `ST [SP], R0` (two instructions) versus
  one `PUSH R0`, and still must match the `[FP+4]` stack layout and caller
  cleanup.
- Conclusion recorded in `docs/compiler-opt.md`: the ABI is unchanged and the
  existing pushes are already minimal.
- No compiler behavior changed; full suites remain green.

### 2026-11-15 07:16:32 -08:00 — Cache benchmark regression gate

- Added a unit gate that compiles three optimized shapes at -O0 and -O1 and
  requires -O1 to use fewer instructions:
  - `same()` pointer loop
  - cached parameter across `inc(x)`
  - two live locals in R3/R2
- Each case also checks the runtime result.
- Measured instruction counts for the shapes: 141 -> 79 (`same`), 50 -> 40
  (call spill), 55 -> 37 (two locals).
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 07:20:42 -08:00 — Generic cache-clobber scanner

- Replaced the R3-only clobber scanner with
  `assembly_clobbers_register(text, reg)`.
- Removed both R2-to-R3 text-substitution scans; the loop and local caches now
  ask the generic scanner about R2 directly.
- `assembly_clobbers_r3` remains as a thin wrapper.
- No intended code-generation change; the pointer, local, and call-spill tests
  still pass.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 07:30:38 -08:00 — Zero-argument calls inside cached loops

- Cached loop pointers now survive zero-argument calls: R3 is spilled before the
  call and reloaded after it, with the loop still keeping one pointer cached.
- Loops with any nonzero-argument call fall back to the no-call cache path.
- Added a differential/pattern test where `tick()` is called inside the loop and
  the pointer cache is still used.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 08:43:01 -08:00 — Call-heavy benchmark gate

- Added an instruction-count regression gate for:
  - a cached loop pointer across `tick()` inside the loop
  - cached locals across `inc(x)` plus an update
- Both cases require -O1 to use fewer instructions than -O0 and check their
  runtime result.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 08:48:20 -08:00 — Statement-entry spill for cached loop pointers

- Replaced loop-body call spill text insertion with statement-entry spill:
  before evaluating an expression that contains exactly one call and does not
  mention the cached pointer, R3 is stored; after the expression it is reloaded.
- This happens before argument evaluation, so one-argument loop calls now keep
  the pointer cache safely. C-for-U and its self-hosted generated test pass.
- Calls in loop conditions, returns, declarations, or expressions that mention
  the cached pointer still reject the cache and fall back.
- Added a one-argument loop-call test.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 08:58:39 -08:00 — Cache state struct

- Grouped each loop-cache register's state into `CachedRegister`
  (`active`, `name`, `offset`, `character`) instead of eight separate fields.
- No intended code-generation change; this is the first structural step toward a
  single liveness/allocation layer.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 09:02:07 -08:00 — Nested-call fallback test

- Added a focused test for nested parameter calls (`inc(inc(x))`) to verify the
  cache safely falls back instead of keeping a stale register.
- A three-argument local-cache test was drafted but removed: the unit harness
  returned 4 while the CLI at -O1 returned 7, so it was not committed without a
  root cause. That discrepancy still needs investigation.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 09:06:08 -08:00 — Three-argument IR miscompile fixed

- Investigation of the unit-vs-CLI three-argument discrepancy found a real IR
  backend bug: `add3(a,b,c)` ignored `c` and produced 4 instead of 7.
- The IR backend now rejects functions with more than two parameters, sending
  them to the fallback emitter.
- Added direct and call-site three-argument tests; both return the correct result.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 09:11:45 -08:00 — Parameter role moved into CachedRegister

- Removed the separate `cached_parameter_active_` field; the parameter role is
  now `cached_loop_.parameter` inside `CachedRegister`.
- This consolidates role, name, offset, and character state in one structure
  without changing code generation.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 09:16:37 -08:00 — Use-frequency local allocation

- Added AST name-use counting and used it to rank local cache candidates:
  highest use count wins R3, next wins R2; frame offset breaks ties.
- This is a first allocation heuristic over the whole function body rather than
  declaration order.
- Existing pointer/local/call tests remain green.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 09:21:07 -08:00 — Live-span local ranking

- Added linear live-span analysis over the fallback AST: the first and last
  identifier positions determine each local's span.
- Local cache selection now ranks by span, then use count, then frame offset.
- This is a broader allocation heuristic than declaration order, though it is
  still an AST linear approximation rather than CFG liveness.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 09:24:25 -08:00 — Live-span allocation test

- Added a test where three locals compete for two cache registers; the
  live-span ranking places the earlier/longer-lived `a` in R3 and `b` in R2.
- The generated assembly loads `[R4-4]` into R3 and `[R4-6]` into R2, and the
  function returns 12.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 09:27:31 -08:00 — C-for-U compilation budget gate

- Added a regression gate that compiles `tests/kevin/C-for-U.c` and requires:
  - fewer than 5,600 emitted instructions
  - fewer than 95,000 assembly bytes
- Current size: 5,463 instructions; this catches large code-size regressions in
  the self-hosted compiler workload.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 09:30:52 -08:00 — Cycle-level benchmark gate

- Split the test VM runner into `run_c_stats` (returns the full `VmResult`)
  and the existing `run_c` exit-code wrapper.
- Added a call-heavy cache gate requiring both fewer VM instructions and fewer
  VM cycles at -O1, with runtime results checked.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 09:35:10 -08:00 — Frame compaction around cached parameters

- When a call-free cached parameter is removed from the frame, remaining locals
  are re-packed by original offset instead of only trimming a top slot.
- This lets cached parameters in the middle of the frame remove their slots and
  shift later locals down.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 09:38:02 -08:00 — Frame compaction test

- Added a test with a cached middle parameter and later locals to verify slot
  removal and offset shifts: `SUB SP, #6`, no store to the removed `x` slot,
  and `z` shifted into `[R4-4]`.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 09:41:08 -08:00 — Two-locals-across-calls cycle gate

- Expanded the cycle gate to cover two shapes:
  - cached loop pointer with a zero-argument call
  - two locals across a one-argument call and update
- Both require fewer instructions and cycles at -O1, with correct results.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 09:44:41 -08:00 — same() cycle gate

- Added the `same()` pointer-loop shape to both the instruction-count and cycle
  gates; -O1 must use fewer instructions and cycles and return 1.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 09:48:42 -08:00 — Branch-aware live spans

- Live-span position analysis now evaluates `if`/`else` and `switch` branches
  from the same starting position and merges their first/last positions instead
  of treating the branches as sequential code.
- This avoids inflating the span of a local that only appears in one branch and
  gives the local-cache ranking a control-flow-aware view.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 09:58:02 -08:00 — Local initializers go directly to cache registers

- The local-cache text pass now replaces the initializer frame store plus reload
  with a single `MOV R3, R0` (or `MOV R2, R0` for the second local).
- Frame slots remain reserved for call spills, but the initial store/load pair is
  removed for call-free and call-bearing locals.
- Updated pattern tests to expect the direct move.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 13:17:45 -08:00 — Unresolved includes are errors

- The preprocessor now raises a `CompileError` when an active `#include` cannot
  be resolved from the source directory, include paths, or builtin headers.
- Malformed/empty include directives and include nesting deeper than 10 also
  fail instead of being silently dropped.
- Added quoted and angled missing-header tests.
- Removed the resolved item from `docs/deferred.md`.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 13:22:05 -08:00 — Host filesystem quota is documented as recursive

- `kr16 vm --help` now says `--fs-quota` is recursive over `--fs-root`, counts
  every file under it, and that a build tree can exhaust the default 5 MiB.
- README now has a Host filesystem sandbox section with the same warning and the
  dedicated-directory / raised-quota remedies.
- Added CLI help assertions for the new wording.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 13:39:46 -08:00 — Object-level GC and unused static functions

- Added `kr16 ld --gc-all`, which keeps only objects reachable from
  `_start`/`__start`/`start` through relocation references.
- `LinkResult::gc_dropped_symbols` records dropped global definitions; `--map`
  prints them as `dropped-symbol <name>`.
- The compiler now omits a `static` function whose identifier appears only at
  its definition and emits used static functions as local symbols.
- Verified manually:
  - `--gc-all --map` keeps `_start`, drops `unused`, and prints
    `dropped-symbol unused`.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 13:44:28 -08:00 — Release handoff refresh and C-for-U gate

- Rebuilt the release preset from main; release CTest passes 56/56.
- Refreshed `..\KRCompiler\inbox\kr16.exe` with SHA-256
  `49E289CFEB6F24A42C20C8B6CB2E60EEFE33BA691F0CFD3A0D9B1612DCC4E6F6` and
  updated the handoff note with the new features.
- Tightened the C-for-U compilation gate to 5,500 instructions / 94,500 bytes;
  current baseline is 5,459 / 94,036.
- Documented the gate in `docs/compiler-opt.md`.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest; release 56/56.

### 2026-11-15 13:52:56 -08:00 — Statement-level CFG liveness for local caches

- Added a CFG builder for `Compound`, `Expr`, `Decl`, `Return`, `If`, `While`,
  `DoWhile`, and `For`, with explicit branch and loop edges.
- Added live-in/live-out dataflow with per-node uses and definitions for each
  local candidate.
- Local cache ranking now uses the CFG live span when the function is
  supported; unsupported `switch`/`break`/`continue`/`goto` bodies fall back to
  the branch-aware linear span.
- This advances the allocator toward CFG liveness but does not yet allocate
  every value; call spill points remain the existing statement-level
  spill/reload paths.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 13:53:48 -08:00 — Handoff refreshed with CFG liveness

- Rebuilt the release preset at `f20fd47`; release CTest 56/56.
- Refreshed `..\KRCompiler\inbox\kr16.exe` with SHA-256
  `FF33C1A4CFD74DACA34C3167C6CECFF1A4DE4B1D1FFBAE03D3DAF4DF4FDE2F16`.
- Updated the handoff note with the CFG-liveness change.

### 2026-11-15 13:57:32 -08:00 — Branch-join cache and spill test

- Added a two-local function with an if/else call in each arm.
- The test checks that both cached registers spill before the calls, reload
  afterwards, and are consumed correctly at the join.
- Runtime result: 4.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 14:14:33 -08:00 — Basic-block liveness intervals

- Added `BasicBlock` construction over the statement CFG: blocks split at calls,
  multiple-successor branches, loop back edges, and join points.
- Added block-level use/def sets and iterative live-in/live-out dataflow.
- Local cache ranking now uses the basic-block live span first, then the
  statement-level CFG span, then the branch-aware linear span as safe fallbacks.
- This is allocator infrastructure; actual interval allocation and spill-slot
  assignment remain pending.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 14:36:34 -08:00 — Pre-argument spill experiment rejected

- Tried emitting a cache-spill marker before call argument setup and spilling
  R3/R2 there, then reloading after the call, to remove the local-cache arity
  limits.
- The approach fails for nontrivial arguments: later argument expressions still
  read cached locals from R2/R3, and earlier argument evaluation can already
  have clobbered those registers. Example: `g(a+b, a-b)` produced the wrong
  second argument.
- Reverted the experiment; the existing arity guards and post-argument spills
  remain. Documented the limitation in `docs/compiler-opt.md`.
- Basic-block liveness from the previous commit remains in place.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 14:46:26 -08:00 — Arbitrary-arity local caching via memory-resident spills

- Replaced the rejected pre-argument marker experiment with a full spill
  region. Before argument evaluation the cache registers store to their frame
  slots; while spilled, all cached-local loads and stores use those slots, so
  argument expressions cannot read a clobbered R2/R3. After the outermost call
  the cache reloads.
- Nested calls use a spill depth so only the outermost call reloads.
- Removed the one/two-argument restrictions for cached locals.
- Added a three-argument two-local test; `g(a+b, a-b)` now returns the correct
  result again.
- C-for-U generated canary passes. New compile baseline: 5,575 instructions,
  95,887 bytes; gate raised to 5,600/97,000.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest.

### 2026-11-15 14:59:05 -08:00 — Parameter caches spill before arguments

- Parameter and loop-pointer caches now spill at `gen_call` entry before
  argument evaluation, not after it. While spilled, cached identifier loads
  use the frame slot; the outermost call reloads R3/R2.
- Removed the parameter-cache two-argument limit.
- Added a three-argument cached-parameter test (`add3(x, x, 3)`), expected 11.
- Removed the now-unused post-argument text spill helper.
- C-for-U improved to 5,477 instructions / 94,384 bytes; gate tightened to
  5,500 / 95,000.
- Tests: Windows 56/56 CTest; Linux 56/56 CTest; C-for-U 4/4.
