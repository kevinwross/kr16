# ADR 0001: `long` is a 16-bit alias

- Status: accepted for the 1.0 teaching-toolchain release
- Date: 2026-10-27

## Context

KR16 is a 16-bit teaching CPU. The C compiler intentionally implements a
teaching subset of C99, not a complete ISO C implementation. True 32-bit
`long` support would require multiword arithmetic, comparisons, shifts, casts,
function arguments/returns, varargs, `printf`, and debug-info changes.

A partial 32-bit type would be more confusing for students than a clearly
documented 16-bit type.

## Decision

`long` and `long long` are accepted spellings but are aliases for the 16-bit
`int` type. They have size 2 and the same range as `int`.

This is a deliberate teaching-subset limitation. The 1.0 release is a
teaching-toolchain release, not a claim of strict C99 conformance.

## Consequences

- Existing sources using `long` continue to compile.
- Programs requiring 32-bit ranges or overflow behavior must use software
  multiword routines or a future compiler extension.
- Strict C99 conformance remains deferred beyond 1.0.
- Documentation and release notes must state the alias explicitly.
