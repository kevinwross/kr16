# Handoff: reusing heap allocator (`malloc`/`free`/`calloc`/`realloc`)

- Status: **implemented and verified**; committed as `ea60403`
- Last touched: 2026-10-28
- Design record: [`docs/decisions/0002-heap-allocator.md`](docs/decisions/0002-heap-allocator.md)

## Follow-up from the next session

- Casts are now implemented (`d24c958`): `(int *)malloc(128)` and other
  scalar/pointer casts compile.
- The linker now resolves `__heap_start`, `__stack_top` and `__stack_limit`
  during relocation (`4255269`). The fixed `.bss` pool is therefore no longer
  required; switching the allocator to `[__heap_start, __stack_limit)` is the
  remaining optional step.
- Assembler branch relaxation is implemented, so large C functions no longer
  hit the `branch target out of range` limit.
- The heap work and its new tests are committed. Full CTest result after the
  cast, linker and relaxation changes: **38/38**; doctest: **117 cases /
  3186 assertions**.


This document is for whoever picks this up next. It assumes you know nothing
about the change beyond the repo itself.

## TL;DR

`resources/libc.s` no longer has a bump allocator. It now has a first-fit
allocator with an address-ordered free list, immediate coalescing, in-place
`realloc`, and a 16 KiB static pool. Tests pass: the full doctest suite, a
behaviour test, and a 400-operation randomised churn test with an invariant
checker.

Two toolchain limitations shaped the result; both are now fixed:

1. The linker can resolve relocations against `__heap_start` /
   `__stack_top` / `__stack_limit` (commit `4255269`). The fixed pool can be
   replaced by a linker-sized one.
2. `kr16 cc` has casts (`d24c958`) and the assembler now relaxes long branches,
   so large C functions compile.

The heap is still a fixed `.bss` pool; converting it to the region above the
image is the next allocator change. The allocator remains assembly for now,
although the original branch-range blocker is gone.

## First five minutes

```powershell
# 1. read the design
#    docs/decisions/0002-heap-allocator.md  (~120 lines)
#    resources/libc.s lines 333-396 (block layout + globals)

# 2. run the suite exactly as it was verified
build\windows-ninja\tests\kr16_tests.exe          # 112 cases, expect SUCCESS

# 3. run the allocator tests
$kr = "build\windows-ninja\kr16.exe"
& $kr as resources\libc.s -o build\scratch\libc.o
& $kr as tests\support\heapcheck.s -o build\scratch\heapcheck.o
& $kr as tests\support\heap_stress.s -o build\scratch\stress.o
& $kr ld build\scratch\libc.o build\scratch\heapcheck.o build\scratch\stress.o -o build\scratch\stress.kex
& $kr vm build\scratch\stress.kex                # expect: OK

& $kr cc tests\integration\heap.c -S -o build\scratch\heapint.s
& $kr as build\scratch\heapint.s -o build\scratch\heapint.o
& $kr ld build\scratch\libc.o build\scratch\heapcheck.o build\scratch\heapint.o -o build\scratch\heapint.kex
& $kr vm build\scratch\heapint.kex               # expect: heap ok peak=16002
```

`build\scratch\` is a scratch directory created during this work; it is not
tracked. The CMake equivalents are `kr16.*.heap*` in
[`tests/CMakeLists.txt`](tests/CMakeLists.txt:177).

## What changed

Uncommitted working-tree changes:

| Path | Change |
|---|---|
| `resources/libc.s` | allocator replaced (lines 333-966); `exit` now moves its argument to `R0` first |
| `resources/manifest.json` | libc sha256 refreshed (LF-normalised, uppercase) |
| `src/resources/embedded_libc.cpp` | regenerated from `libc.s` |
| `tests/integration/heap.c` | **new** behaviour test |
| `tests/support/heapcheck.s` | **new** invariant checker (test-only link) |
| `tests/support/heap_stress.s` | **new** seeded churn test |
| `tests/CMakeLists.txt` | 8 new tests, lines 175-232 |
| `docs/decisions/0002-heap-allocator.md` | **new** ADR |
| `implementation.md` | milestone entry appended |
| `README.md` | "heap allocation" → "first-fit heap allocator" |

### Symbol map in `resources/libc.s`

| Symbol | Line | Kind |
|---|---|---|
| `__kr16_heap_start` | 367 | `.data` word holding the pool base |
| `__kr16_heap_end` | 369 | `.data` word holding one past the pool |
| `__kr16_heap_top` | 372 | `.data` word, first untouched byte |
| `__kr16_heap_block` | 381 | `.bss`, `.space 16384` — **the pool size** |
| `__kr16_heap_limit` | 383 | `.bss` label, pool end |
| `__kr16_free_list` | 385 | `.bss` word, head of the free list |
| `__kr16_used_bytes` / `__kr16_peak_bytes` | 387/389 | `.bss` accounting |
| `malloc` | 397 | export |
| `free` | 532 | export |
| `__kr16_block_free` | 566 | internal: coalesce + insert + top release |
| `__kr16_unlink` | 680 | internal: remove a known-free block |
| `calloc` | 703 | export |
| `realloc` | 745 | export |
| `__kr16_heap_used` / `__kr16_heap_peak` | 950/957 | exports for tests + debugger |
| `exit` | 963 | export |

`__kr16_heap_start`, `__kr16_heap_top` and `__kr16_free_list` are `.global`
only because `tests/support/heapcheck.s` reads them; everything else is local.

## Design in one page

Block format, 2-byte header, everything 2-byte aligned:

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

- Bit 0 of the header is the used flag; sizes are even so the bit is free.
- `size` **includes** the header → payload is `size - 2`, minimum block is 4.
- Free blocks keep their header, and the link lives in their own payload: no
  footer, no side table, 2 bytes of metadata per block.
- Free list is **singly linked, sorted by address**. Ordering is what lets
  `free` find both neighbours in one scan and coalesce without boundary tags.
- `malloc`: first fit, scanning low to high; split only when the remainder can
  hold a 4-byte block; otherwise take the whole block.
- `free`: coalesce with the following block, then the preceding one, insert at
  the scan position, and if the result ends at `heap_top`, lower `heap_top` and
  unlink it (memory returns to the untouched region, so the footprint tracks
  live data rather than the historical peak).
- `realloc`: shrink in place; grow by absorbing a free successor
  (`__kr16_unlink` + merge); grow into untouched space when the block is
  topmost; otherwise allocate, copy `min(old, new)`, free the old block. On
  failure the original block is untouched.
- `calloc`: rejects zero counts, products that overflow 16 bits (verified by
  dividing the product back), and products larger than the pool; then zeroes
  byte by byte. No `memset` dependency.

Semantics (some are behaviour changes from the bump allocator):

| Call | Result |
|---|---|
| `malloc(0)`, `malloc(negative)`, `malloc(> 16380)` | `NULL` |
| `free(NULL)` | no-op |
| `free(p)` twice | ignored (used bit already clear), no corruption |
| `realloc(NULL, n)` | `malloc(n)` |
| `realloc(p, 0)` | `free(p)`, returns `NULL` |
| `calloc(0, n)`, `calloc(200, 200)` | `NULL` |
| `exit(n)` | halts with `R0 = n` (previously `R0` was untouched) |

Costs: 2 bytes per block (6 % on 32-byte payloads, 12.5 % on 16-byte, 25 % on
8-byte). `malloc`/`free` are O(number of *free* blocks); allocate-only programs
stay O(1) because the list is empty. 16 KiB pool is always reserved in `.bss`;
the heap test image measures `text 3470, data 39, bss 16392`.

### The one bug worth remembering

While coalescing, `free` must track **two** links: `slot` (points at `cur`) and
`ppred` (points at the block before `cur`). When the freed block merges with its
predecessor *and* is topmost, the release must unlink via `ppred`, not `slot`.
Getting this wrong leaves a phantom free block above `heap_top`; later
allocations hand out overlapping memory and `used_bytes` drifts. Registers in
`__kr16_block_free`: `[R4-2]` addr, `[R4-4]` size, `[R4-6]` slot,
`[R4-8]` cur, `[R4-10]` ppred.

## Verification

| Check | Command | Observed |
|---|---|---|
| Behaviour | `kr16 vm heapint.kex` | `heap ok peak=16002` |
| Churn + checker | `kr16 vm stress.kex` | `OK` (173,821 instructions) |
| Existing suite | `tests\kr16_tests.exe` | 112/112 cases, 3126 assertions |
| `malloc` from C | `kr16 build tests/kevin/smalltest.c` | prints as before |
| `printf` `%x` | `kr16 build tests/integration/printf_hex.c` | `0 1 f 10 ff fff 1234 ffff -> 255=ff hex Z` |

`heap.c` covers: reuse of a freed block, coalescing two neighbours into a block
a larger request can use, 200 iterations of churn without ratcheting the peak,
reuse of a hole between live blocks, `realloc` grow/shrink/move, `calloc`
zeroing after reuse, double free, `malloc(20000)` and the 16 KiB ceiling.

`heap_stress.s` runs 400 seeded malloc/free operations over 32 slots and calls
`__kr16_heap_check()` after **every** operation, then frees everything and
requires `__kr16_heap_used() == 0`. It prints `OK`, or `F<code>` plus the
checker code.

### `__kr16_heap_check()` codes

`0` ok, `1` header past top, `2` size below minimum, `3` block overruns top,
`4` free block missing from the list, `5` list out of order, `6` two adjacent
free blocks (coalescing missed), `7` list not exhausted at top, `8` bad
`heap_top`. The checker walks the pool and consumes the free list as it goes,
so a single pass verifies tiling, sizes, ordering, coverage and adjacency.

## Toolchain findings

### 1. Linker-provided symbols (fixed)

`4255269` makes `symbol_value()` consult the linker-provided `__heap_start`,
`__stack_top` and `__stack_limit` values before failing. `.extern` references
now relocate correctly; the recommended fix recorded below is implemented.
The allocator still uses the fixed pool until the follow-up conversion.

### 2. `kr16 cc` limits (fixed)

Casts are implemented in `d24c958`. Branch relaxation is implemented for
`Bcc` in the assembler, so long functions no longer fail to assemble. Writing
the allocator in C is now possible in principle; keeping it in assembly is a
choice, not a remaining toolchain blocker.

## Gotchas when editing this

1. **Hand-written assembly needs `.extern`** for every symbol not defined in
   the same file, including `putchar`, `malloc`, `free`. Without it the
   assembler silently encodes a call to the *next instruction* (observed:
   `CALL putchar` → `CALL 0x0758`). This cost time during this work and it will
   bite again.
2. **The linker fix is in**, so `__heap_start`/`__stack_limit` may be used
   when the pool is converted; do not use them while the fixed pool remains.
3. **Regenerate after any `libc.s` edit**:
   ```powershell
   python resources/embed_libc.py
   # sha256 of LF-normalised bytes, uppercase, into resources/manifest.json
   $raw=[System.IO.File]::ReadAllBytes('resources\libc.s')
   $norm=[System.Text.Encoding]::UTF8.GetBytes(([System.Text.Encoding]::UTF8.GetString($raw) -replace "`r`n","`n"))
   ([System.Security.Cryptography.SHA256]::Create().ComputeHash($norm) | ForEach-Object { $_.ToString('X2') }) -join ''
   ```
   Current value: `92C95DAD3C853A1F34C52F566E738142E2C28C101D877E2EDD78F1ACEFB02D6A`.
4. **`kr16 build` writes intermediates beside its input** (`x.s`, `x.o`,
   `x.libc.o`). Never run it inside `tests/integration/`; copy the source to
   `build/scratch/` first.
5. **Building the toolchain** needs the VS environment and, in this sandbox,
   full file access — MSVC's `cl.exe` deadlocks connecting to `mspdbsrv` over a
   named pipe the workspace sandbox blocks. Use
   `cmd /c build\scratch\build.bat` (calls `vcvars64.bat` then `ninja -C
   build\windows-ninja kr16`). Rebuild after any C++/assembler change.
6. `tests/support/heapcheck.s` is **test-only**; it is linked explicitly by the
   heap tests, never by `kr16 build`. Keep it out of `libc.s` so the runtime
   stays small.

## Suggested next steps

1. Switch the allocator's `__kr16_heap_start` / `__kr16_heap_end` /
   `__kr16_heap_top` initializers from the fixed `.bss` block to
   `__heap_start` / `__stack_limit`, add `.extern` declarations, and update the
   ceiling test. The linker fix is already in.
2. Add doctest cases to `tests/unit/test_compiler.cpp` for reuse, coalescing and
   `realloc` in place. They were deliberately not added because the test binary
   needs an MSVC rebuild to run them, and an unverified test is worse than none.
   The integration test already covers the same behaviour.
3. ~~Consider branch relaxation in `kr16 cc`~~ — done in the assembler; large
   C functions now assemble.
4. Optional: `--stack-reserve` exists as a `LinkOptions` field but has no CLI
   flag; once the heap derives from the linker symbols, that flag becomes the
   knob for trading heap against stack.
