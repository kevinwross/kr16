# ADR 0005: 32-bit `long` and `long long`

- Status: accepted; implementation in progress
- Date: 2026-11-15
- Supersedes: [0001 16-bit long](0001-16-bit-long.md)

## Decision

`long` and `long long` are true 32-bit signed integer types:

- `sizeof(long) == 4`
- `sizeof(long long) == 4`
- storage is two little-endian 16-bit words
- expressions use a low-word/high-word pair
- arithmetic wraps modulo 2^32

This supersedes ADR 0001, which made `long` a 16-bit alias for the 1.0
teaching release.

## Implementation status

Implemented:
- type kinds and `sizeof`
- 4-byte local/global storage
- parser spelling for `long`, `long long`, and `long int`
- 32-bit `+`, `-`, `*`, `/`, `%`, unary `-`, `~`, `&`, `|`, `^`, `<<`, `>>`
- 32-bit equality and relational comparisons
- assignment, `++`/`--`, and compound assignment
- two-word stack ABI for long parameters and returns
- `printf` `%ld`, `%lu`, `%lx`, and `%lo` formatting

Not yet implemented (explicit diagnostics, no silent truncation):

- long `scanf` conversions (`%ld`, `%lu`, `%lx`)

Unsupported operations raise a diagnostic rather than silently producing a
16-bit result.

## Consequences

- `long` and `long long` are now distinct from `int` in the type system.
- Existing 16-bit-alias assumptions must not be used for new code.
- Long parameters and returns use a two-word stack convention.
- Debug information reports a size of 4 for these types.

## Alternatives considered

- Keep the 16-bit alias: simpler, but contradicts the requested true 32-bit
  semantics.
- Make `long long` 64-bit: more faithful to ISO C, but substantially more
  arithmetic and ABI work; KR16 targets a 32-bit long pair first.







