# ADR 0002: Reusing heap allocator for `libc`

- Status: accepted for the teaching-toolchain release
- Date: 2026-10-28
- Supersedes: the bump allocator described in `implementation.md` (M7,
  "malloc/free")

> **Follow-up (2026-10-28).** The linker now resolves `__heap_start`,
> `__stack_top` and `__stack_limit` during relocation, and `kr16 cc` has casts.
> The fixed 16 KiB `.bss` pool is therefore a temporary implementation choice,
> not a permanent constraint. Switching the initializers to
> `[__heap_start, __stack_limit)` is the next allocator change.

## Context

The first `libc.s` allocator was a bump pointer: `malloc` appended a 2-byte
size header and moved `__kr16_heap_ptr` forward, and `free` was a no-op. It was
small and predictable, but memory was never reused. Any program that allocates
in a loop — a linked list built and torn down, a buffer grown and released,
repeated `strcat` on a temporary — grew the heap until it failed, even though
almost all of that memory was dead.

KR16 has hard constraints that shape the design:

- 16-bit, byte-addressed memory; RAM is `0x0000`–`0xEFFF`, so code, data,
  heap, and stack all compete for 60 KiB. Every byte of metadata and every
  byte of runtime code costs capacity.
- `int`/`size_t` are 16-bit signed, so a C caller cannot request more than
  32767 bytes.
- The stack starts at `0xF000` and grows down; a 4 KiB reserve is conventional.
- Only five general registers (`R0`–`R4`) are available, and the runtime is
  hand-written assembly.

A second constraint is a toolchain bug: the linker defines `__heap_start` after
it applies relocations, so a relocation against that symbol either fails to
link or, when the assembler did not record one, silently assembles to zero.
The first bump allocator was in fact handing out addresses starting at 2. Until
that is fixed, the runtime cannot learn where the image ends.

## Decision

### A fixed pool in `.bss`

The heap is a 16 KiB array, `__kr16_heap_block`, with `__kr16_heap_limit` one
past its end. This needs no linker symbol, is settled at assemble time, and is
by construction below the stack. The size is one label in `resources/libc.s`
and can be tuned between roughly 10 and 20 KiB without touching the algorithm;
it is RAM that is always reserved, even when unused.

### Block format

One 2-byte header per block; bit 0 is the used flag, bits 1–15 are the block
size in bytes (always even). `size` includes the header, so payload is
`size - 2` and the minimum block is 4 bytes (header + free-list link).

```text
allocated              free
+----------------+     +----------------+
| size | 1       |     | size           |
+----------------+     +----------------+
|    payload     |     | next free addr |
|                |     +----------------+
|                |     | unused ...     |
+----------------+     +----------------+
```

Free blocks keep their header, so `size` is always recoverable, and the link
lives inside the block's own payload (no footer, no side table).

### Free list and policies

- **Address-ordered singly linked list** of free blocks, head in
  `__kr16_free_list`. Ordering lets `free` locate both neighbours in one scan
  and coalesce without boundary tags.
- **First-fit** allocation from the lowest address. This keeps the high end of
  the pool contiguous, which is what large requests need.
- **Immediate coalescing** on `free` with the preceding and following block,
  and the merged block keeps the predecessor's place in the list.
- **Split only when the remainder is at least 4 bytes**, so churn cannot
  manufacture unusable dust; otherwise the whole block is handed over.
- **Top release**: freeing the topmost block lowers `__kr16_heap_top`, so the
  footprint follows live data rather than the historical peak.
- **Lazy growth**: `malloc` carves from untouched pool space only when no free
  block fits.

### Semantics

- `malloc(0)` and any non-positive size return `NULL`; so do requests larger
  than the pool.
- `free(NULL)` is a no-op; a block whose used flag is already clear is ignored
  (a cheap double-free guard), not a trap.
- `calloc` rejects zero counts, a product that overflows 16 bits (verified by
  dividing the product back), and a product larger than the pool, then
  zero-fills byte by byte.
- `realloc` shrinks in place, grows by absorbing a free successor, or grows
  into untouched space when the block is topmost; otherwise it allocates,
  copies the smaller size, and frees the old block. A failed grow leaves the
  original block untouched.
- `realloc(p, 0)` frees `p` and returns `NULL`; `realloc(NULL, n)` is
  `malloc(n)`.
- Pointers are 2-byte aligned. The allocator is not reentrant and not
  interrupt-safe.

### Observability

`__kr16_heap_used()`, `__kr16_heap_peak()` and `__kr16_heap_check()` are
exports, not part of `<stdlib.h>`. The checker lives in
`tests/support/heapcheck.s`, is linked only by tests, and walks the pool
comparing every block against the free list, so it verifies tiling, sizes,
ordering, coverage and the no-adjacent-free-blocks invariant in one pass.

## Consequences

- Freed memory is reused; the steady-state footprint tracks live data instead
  of total bytes ever allocated.
- 2 bytes of overhead per block: 6 % on 32-byte blocks, 12.5 % on 16-byte
  blocks, 25 % on 8-byte blocks. The overhead is fixed; it cannot be traded
  away later without changing the block format.
- The pool is a fixed 16 KiB. A program that needs a larger heap must grow the
  label, and pays for it whether or not it allocates. Making the heap grow
  toward the stack needs `__heap_start`, which needs the linker fix.
- `malloc` and `free` are O(number of free blocks). A long-lived, badly
  fragmented heap scans more; the list is empty for allocate-only programs, so
  the common case stays O(1).
- Coalescing relies on the address-ordered scan instead of a footer, so `free`
  costs O(number of free blocks) rather than O(1). Holes only survive while
  both neighbours are live — intrinsic external fragmentation, not a leak.

## Alternatives rejected

- **Boundary tags (header + footer)**: O(1) coalescing, but 4 bytes per block —
  twice the metadata for every allocation in a 16 KiB heap. The sorted list
  gives the same coalescing for 2 bytes.
- **Buddy allocator with a bitmap**: constant-time allocation and merging, but
  rounding to power-of-two classes wastes more to internal fragmentation than a
  2-byte header costs in a heap this small.
- **Segregated size-class bins**: excellent for uniform allocation sizes and
  O(1) turnaround, but it cannot coalesce across classes and it multiplies the
  code.
- **Heap above the image instead of a fixed pool**: the intended design, and
  what `kr16-plan.md` §7.13 describes, but it depends on a `__heap_start`
  relocation that the linker cannot currently resolve.
- **Implementing it in C**: rejected for now. The compiler has no casts, so a
  byte pool cannot be viewed as words, and it emits unrelaxed short branches,
  so a function longer than about 1 KiB fails to assemble. Expressing the
  allocator in C therefore required roughly twenty single-exit micro-functions,
  which was harder to read than the assembly it replaced.

## References

- `resources/libc.s` — the allocator.
- `tests/support/heapcheck.s`, `tests/support/heap_stress.s`,
  `tests/integration/heap.c` — invariant checker, randomised churn, and
  behaviour tests.
- `kr16-plan.md` §7.11 (runtime limits) and §7.13 (memory budget).
- `KR16-Assembly-Language.md` §11 (addressing modes) and §15 (instruction set).
