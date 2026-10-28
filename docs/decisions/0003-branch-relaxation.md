# ADR 0003: Assembler branch relaxation

- Status: accepted
- Date: 2026-10-28
- Related: [0002 heap allocator](0002-heap-allocator.md)

## Context

KR16's conditional branch (`Bcc`) stores a signed 8-bit word displacement in
the instruction. The practical range is therefore −256…+254 bytes. `JMP` and
`CALL` already use a 16-bit word displacement and reach ±64 KiB.

`kr16 cc` emits `Bcc` for every `if`, loop condition, `switch` comparison and
short-circuit term. A large function eventually produces a branch whose target
is outside the `Bcc` range, and the assembler reports:

```text
branch displacement out of range
```

The old assembler laid out every branch as a fixed 2-byte instruction, so it
could not grow a branch after its target became known. The heap allocator work
avoided the limitation by splitting the C implementation into many small
functions; that is not a general solution.

## Decision

Perform **iterative branch relaxation in the assembler**.

The assembler runs the normal first pass to collect labels and symbols. It then
repeats a layout pass with a set of branches marked long:

1. Lay out the program using short `Bcc` instructions unless a branch is marked
   long.
2. For every short conditional branch in the same section, resolve its target
   and compute the displacement.
3. If the displacement is outside −256…+254 bytes, or is not word-aligned,
   mark that branch long and repeat.
4. When a pass makes no changes, emit the final object using the resulting
   branch set.

Promotion is monotonic: a branch is never shortened again. This guarantees
convergence (at most one promotion per branch) and avoids oscillation.

### Long branch encodings

Unconditional `BRA target` becomes a 4-byte:

```asm
JMP target
```

A long conditional `Bcc target` becomes a 6-byte sequence using the inverse
condition:

```asm
B<inverse> .Lskip
JMP target
.Lskip:
```

`B<inverse>` has a fixed 4-byte displacement to skip the `JMP`, so it can never
overflow. When the original condition is true, the inverse is false and the
`JMP` executes; when false, the inverse skips it. The inverse pairs are:

```text
BEQ <-> BNE    BCS <-> BCC    BMI <-> BPL
BVS <-> BVC    BHI <-> BLS    BGE <-> BLT
BGT <-> BLE
```

`BNV` (condition 15) uses `BRA` as its inverse; it is reserved/unused by the
compiler.

### Scope

- Applies to local branches in the same section. Cross-section branches remain
  unsupported, as before.
- External `Bcc` targets remain unsupported when they cannot be relaxed; a long
  conditional uses `JMP`, but the compiler only emits local labels.
- `--raw` and object output share the same layout logic.
- The final pass regenerates listings, `.debug_line`, and relocations at the
  relaxed addresses. Layout passes suppress listings and line rows.

## Consequences

**Benefits**

- Large C functions now assemble without splitting or hand-written thunks.
- Small programs keep their original short branches; the two-pass layout only
  expands what is actually out of range.
- Hand-written assembly benefits as well, not just `kr16 cc`.
- No ISA, VM, container-format, or linker changes.

**Costs**

- Each promoted conditional adds 4 bytes; each promoted unconditional adds
  2 bytes.
- The assembler performs up to one extra layout pass per promoted branch. In
  practice this converges in one or two iterations for normal programs.
- Cross-section conditional branches remain a separate limitation.

## Alternatives considered

- **Always emit long compiler branches.** Simpler, but inflates every branch
  and does not help hand-written assembly.
- **Expand the ISA's `Bcc` encoding.** Cleaner instruction set, but requires
  VM, assembler, disassembler, container version and test changes, and breaks
  every existing image.
- **Linker trampolines.** Conditional branches have no spare encoding bits to
  reach a trampoline outside the short range; this would add a second code
  layout problem and is much more complex.
- **Split C functions.** What the heap work did; not general.

## Verification

- `tests/unit/test_assembler.cpp`:
  - a forward `BEQ` over 200 `NOP`s expands to the inverse-`BNE` + `JMP`
    sequence and assembles;
  - a backward `BRA` over the same distance expands to `JMP`.
- `tests/unit/test_compiler.cpp`:
  - a 160-statement `if (1)` body compiles and runs;
  - the same body under `if (0)` is skipped correctly.
- Full CTest: **38/38**; doctest: **117 cases / 3186 assertions**.
