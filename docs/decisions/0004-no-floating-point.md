# ADR 0004: No floating-point support

- Status: accepted
- Date: 2026-11-15
- Related: [0001 16-bit long](0001-16-bit-long.md), [../isa.md](../isa.md)

## Context

KR16 is a 16-bit teaching CPU with an integer ALU, five general registers, and
no floating-point registers or instructions. The C compiler implements a
teaching subset; `long` is already a documented 16-bit alias rather than a
second scalar width.

Adding floating point would require one of:

- a soft-float runtime, compiler type/literal/conversion support, ABI rules,
  varargs promotion, libc formatting/parsing, and debugger formatting
- an FPU extension touching the ISA, VM, assembler, disassembler, object
  format, and every image

Both are substantial additions outside the integer teaching model, and the
toolchain currently has no `%f`, `strtod`, `math.h`, or floating-point ABI.

## Decision

KR16 has **no floating-point support**.

- `float`, `double`, and `long double` are not part of the C subset.
- Floating-point literals are not part of the language subset.
- There is no soft-float runtime, FPU, floating-point register, or
  floating-point instruction.
- `printf` has no floating-point conversions, and there is no `strtod`,
  `atof`, or `math.h`.

Programs that need fractional values use scaled integers or a fixed-point
convention implemented by the program. This is a language/toolchain decision,
not a VM error.

## Consequences

- The compiler rejects floating-point declarations and literals.
- The ABI remains entirely 16-bit integer/pointer based.
- No VM, image-format, or handoff changes are implied by this decision.
- A future floating-point proposal requires a new ADR that supersedes this one.

## Alternatives considered

- **Soft-float `float`** — portable and VM-neutral, but adds a large runtime,
  compiler type system work, ABI/varargs rules, libc formatting, and debugger
  support for a teaching subset that deliberately stays integer-only.
- **FPU extension** — largest option; changes the ISA, VM, assembler,
  disassembler, images, and compatibility surface.
- **Fixed point** — the existing integer types already support this; it does
  not require C floating-point semantics.
