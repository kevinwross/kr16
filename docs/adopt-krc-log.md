# kr16 — adopting the krc toolchain (stage A log)

Append-only. Every entry starts with the exact `iso` string from `local-datetime`,
called immediately before the entry was written. Earlier entries are never rewritten;
a correction is a new entry.

## 2026-11-20T09:50:34-08:00

**Changed:** nothing in source. Stage A is held at item 2.1. This entry records the
spike, the directive gap that blocks decision 1, and the state of the gate.

**Dependency pin.** KRCompiler `adopt-krc` @ `0f61260`
(`0f61260822d7843f0df2f94879c3db098c242e8f`). The branch tip `797dfac` is documentation
only over that commit (`docs/adopt-krc-log.md`, 25 lines), so building the tree at the
tip is building the pin. The package was built from that tree into
`build/krc-pin-0f61260/`:

```pwsh
cmake -S C:\Users\kevin\source\repos\KRCompiler -B build/krc-pin-0f61260/build -G Ninja `
      -DCMAKE_BUILD_TYPE=Debug -DKRC_BUILD_TESTS=OFF -DKRC_BUILD_TOOLS=ON `
      -DCMAKE_INSTALL_PREFIX=build/krc-pin-0f61260/install
cmake --build build/krc-pin-0f61260/build --parallel
cmake --install build/krc-pin-0f61260/build
```

Result: `krc.exe` and `krld.exe` under `build/krc-pin-0f61260/build/host/bin/Debug/`,
`krc-toolchain.lib`, `krc-debug.lib`, `include/krc/**`, and both
`krc-toolchain-config.cmake` and `krc-debug-config.cmake`. Environment note: Ninja is
unusable under the session's `workspace-write` sandbox — every invocation hangs, even
a one-command trivial build — and runs normally under full file access. `cl`/`link`
run either way.

**2.1 spike — the image runs; output parity is conditional, not unconditional.**

- `krc --version` → `krc 0.1.0`; `krc --list-targets` →
  `kr16  register  pointer=16  data=ip16  backend=yes`.
- `krc --target=kr16 -c tests/integration/hello.c -o hello.kex` → exit 0, 871 bytes.
- `kr16 vm hello.kex --headless` → exit 0. The image runs.
- Output captured through `cmd /c … > file` so no shell text layer is involved:
  - builtin `hello.kex`: 14 bytes, `48 65 6c 6c 6f 2c 20 4b 52 31 36 21 0d 0a`
    (SHA-256 `116C8E01530C6F162D7676CEBD40D3FD41DFFF376BAB24C3D5D9AFCF5B7235F0`);
  - krc CLI single image: 12 bytes, `48 65 6c 6c 6f 2c 20 4b 52 31 36 21` — **no
    trailing newline**, so the console output does not match.
    The `-c` form prepends `kKr16EntryStub` (`src/tools/krc/main.cpp:144` at the pin)
    whose `puts` writes no newline.
  - krc image linked against a runtime that defines `puts` (`targets/kr16/runtime/crt0.s`
    assembled through the facade + staged `libkrcrt.kro` + the compiled fixture,
    95856-byte `.kex`): 14 bytes, the same SHA-256 as the builtin image's output.
- The literal `krc` + `krld` two-step does not link:
  `krc --target=kr16 -c --emit-object hello.c -o hello.kro` (exit 0), then
  `krld hello.kro -o hello.kex` → exit 1, `krld: undefined symbol 'puts'`.
- `--emit-object` alone is a no-op in the pinned CLI: the driver reaches
  `if (!emit_assembly && !emit_object) continue;` (`src/tools/krc/main.cpp:483`),
  prints `krc: the kr16 front end parsed the input and built IR; code generation is not
  implemented yet` and exits 2. `-c --emit-object` together write the `.kro`.

**Blocker — decision 1 is not reachable at `0f61260`.** krc's KR16 assembler
(`src/asm/kr16_assembler.cpp`, directive table at lines 581-650) accepts exactly
`.text .data .global .weak .extern .short .byte`. `resources/libc.s` needs four more:

| file | directive | lines |
|---|---|---|
| `resources/libc.s` | `.bss` | 257, 443 |
| `resources/libc.s` | `.align` | 258, 431, 444 |
| `resources/libc.s` | `.space` | 260, 262, 264, 447, 449, 451 |
| `resources/libc.s` | `.word` | 434, 436, 439 |

Probe: `krc::toolchain::assemble(<file text>, {target=Kr16})` over every file below.
`resources/libc.s` → `KR16 assembler: unknown directive '.bss' on line 257`.
`.text`, `.data`, `.global` and `.extern` are the only directives in that file that the
assembler already accepts.

**`.bss`, `.space`, `.align` and `.word` are not the complete gap.** The other files
the krc path must assemble:

- **The entry stub.** The builtin compiler emits the section directive `.crt0` at
  `src/cc/compiler.cpp:2471` (`out << ".crt0\n";`, inside the block at lines 2471-2476
  that also emits `.global _start`, `_start:`, `CALL main`, `HLT`, `.text`).
  That directive is the source of the `.crt0` error in the spike: the stub literal at
  `build/spike-21/host/spike.cpp:54` copies those five lines, and
  `krc::toolchain::assemble` rejects the first line with
  `KR16 assembler: unknown directive '.crt0' on line 1`.
  It also appears in the checked-in generated assembly: `examples/sum.s:87`,
  `tests/kevin/smalltest.s:94`, `tests/kevin/bigtest.s:78`,
  `tests/kevin/C-for-U.s:5940`.
  A krc-dialect stub could stay in `.text`, but the section is load-order significant
  (`src/link/linker.cpp:383` places `.crt0` first) and the debug model only looks for
  functions in `.text` or `.crt0` (`src/debug/debug_session.cpp:673`), so dropping the
  section is a behavioural change, not just a spelling change.
- **`.s`/`.asm` input that `kr16 build` accepts** (only if the krc branch assembles it):
  `tests/support/heap_stress.s` → `.bss` (15), `.align` (16), `.space` (18, 20, 22),
  first error `unknown directive '.bss' on line 15`.
  `resources/bios.s` (used by `kr16 as`, not `build`) → `.org` (2, 5), `.word` (6),
  first error `unknown directive '.org' on line 2`.
- **Files whose directives are all supported but which the krc assembler still
  rejects.** A directive-only 4.11 will not make these assemblable:
  - `examples/putchar.s:5` — `unknown label ''K''`: character-literal immediates
    (`MOV R1, #'K'`) are not parsed.
  - `tests/integration/link_main.s:5` — `undefined label 'put'`: a direct
    `CALL <extern>` is rejected. krc's own codegen only calls through a register
    (`LEA R0, [puts]` then `CALL R0`), so this is an assembly-model gap, not a
    directive one.
  - `tests/support/heapcheck.s:39` — `unknown KR16 mnemonic 'BHS'`.
- **Already assemblable:** `examples/sum_loop.s`, `tests/integration/asm_min.s`,
  `tests/integration/link_lib.s`, `tests/integration/vm_hello.s`.
- **Not krc-path inputs** (checked-in compiler output, listed for completeness):
  `examples/sum.s`, `tests/kevin/smalltest.s`, `tests/kevin/bigtest.s`,
  `tests/kevin/C-for-U.s` need `.file`, `.loc`, `.section`, `.asciz`, `.rodata`,
  `.crt0`.

So the full directive gap over every file above is `.bss`, `.space`, `.align`, `.word`,
`.crt0`, `.file`, `.loc`, `.section`, `.rodata`, `.asciz`, `.org`; only the first four
sit in `resources/libc.s`, and item 4.11 as described closes decision 1 but not the
`kr16 build <file.s>` cases.

**Second gap, independent of 4.11 — MSVC runtime.** kr16 builds with the static MSVC
runtime (`CMakeLists.txt:21`). A package built with MSVC's default dynamic runtime does
not link into it: `spike.exe` at `/MTd` against the `/MDd` package fails with
`LNK2001: unresolved external symbol __imp_ceilf` (`spike.exe : fatal error LNK1120: 2
unresolved externals`). The package has to be configured with the consumer's runtime
library; not yet verified.

**Gate — the branch tip is red for a pre-existing reason.**
`cmake --preset windows-ninja` → `cmake --build --preset windows-ninja-debug` →
`ctest --preset windows-ninja-debug --output-on-failure` at `52b0969` with no change:
`98% tests passed, 1 tests failed out of 59`; `kr16.unit` fails at
`tests/unit/test_resources.cpp:41` — the embedded host-service header carries CR bytes
that the checked-in `resources/systemservices.h` does not.
Applying the one uncommitted worktree file (`src/resources/embedded_libc.cpp`, the
regeneration with CR stripped) and rerunning the same build and ctest:
`100% tests passed, 0 tests failed out of 59`.
That file is stashed here as `stash@{0}`
(`pre-adopt-krc: embedded_libc regeneration with CR bytes stripped`), neither committed
nor carried into stage A; `git stash pop` restores it.

**Decision:** the programme owner is adding KRCompiler item 4.11 to extend the KR16
assembler with `.bss`, `.space`, `.align` and `.word`. Decision 1 stands; no runtime
substitution is taken (`targets/kr16/runtime/` was used only as a probe, never as the
plan's runtime). Stage A stays at 2.1 until 4.11 is published.

**Next step:** re-pin when 4.11 lands, re-run the probe over every file above, then start
2.2. The three non-directive rejections (character literals, direct extern `CALL`,
`BHS`) and the MSVC runtime match still need an owner decision.

**Commit:** this entry only, a docs-only commit on `adopt-krc`; the tree it describes is
`52b0969` with no source change.

**Blocker:** yes — decision 1 needs KRCompiler 4.11.

## 2026-11-20T09:51:14-08:00

**Changed:** nothing; correction only.

**Correction to the `2026-11-20T09:50:34-08:00` entry:** the `examples/putchar.s:5`
probe result is `KR16 assembler: undefined label `''K''`` — "undefined", not "unknown".
The diagnosis is unchanged: the operand is a character literal (`MOV R1, #'K'`) and the
assembler's operand parser does not accept one. Every other quotation in that entry was
copied from the command output and stands.

**Commit:** this entry only, a docs-only commit on `adopt-krc`.

**Next step:** unchanged — wait for KRCompiler 4.11, then re-run the probe.

**Blocker:** none beyond the one already recorded.
## 2026-11-20T11:17:03-08:00

**Changed:** stage A implemented to the runtime: the `.kdbg` reader in
`DebugSession::load_debug_info`, the same model in the DAP session, the
`build_program` krc branch behind `KR16_TOOLCHAIN=krc`, and toolchain-aware tests.
Stage A is blocked at `resources/fileio.c`, which the pinned krc does not compile.

**Dependency pin.** KRCompiler `adopt-krc` @ `0defcce`
(`0defcce05ebc0e7cd662ca31b13bb0b6abe5190f`), which carries 4.11
(`b63d668`, the KR16 assembler directives) on top of 4.6 and 4.10. The package was
built from a `git archive` of that revision into `build/krc-pin-0defcce/`:

```pwsh
git -C C:\Users\kevin\source\repos\KRCompiler archive --format=tar 0defcce |
  tar -x -C build/krc-pin-0defcce/src
cmake -S build/krc-pin-0defcce/src -B build/krc-pin-0defcce/build -G Ninja `
      -DCMAKE_BUILD_TYPE=Debug -DKRC_BUILD_TESTS=OFF -DKRC_BUILD_TOOLS=ON `
      -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDebug `
      -DCMAKE_INSTALL_PREFIX=build/krc-pin-0defcce/install
cmake --build build/krc-pin-0defcce/build --parallel
cmake --install build/krc-pin-0defcce/build
```

`MultiThreadedDebug` is required: kr16 links the static MSVC runtime
(`CMakeLists.txt:21`) and a package built with the default dynamic runtime fails to
link with `LNK2001: unresolved external symbol __imp_ceilf` (`LNK1120: 2 unresolved
externals`). The package does not install a target sysroot at this revision, so
`KR16_KRC_SYSROOT` points at the revision's `sysroot/` for the headers `fileio.c`
names.

**4.11 does what it promised.** `resources/libc.s` assembles unchanged:
`krc::toolchain::assemble` over it reports `OK`. `.bss`, `.space` and `.align` were
added; `.word` is an alias of `.short`. `.crt0` was not added, so the krc path's entry
stub goes into `.text` as
`.text / .extern main / .global _start / _start: LEA R0, [main]; CALL R0; HLT`,
which is the same three instructions in a section krc accepts and reaches `main`
through a register, the way krc's own startup object does. The object is linked
*after* the input, so the input's section offsets stay the section's own and the
listing can place labels by address; the entry symbol sets the entry point.

**What now works, and how it was verified.**

- One-step build through the facade: compile the input, assemble it, assemble the
  stub, assemble the shipped `libc.s`, compile and assemble `fileio.c`, link. With the
  runtime directory holding `libc.s` but no `fileio.c` (the builtin path also treats
  it as optional), `kr16 build hello.c -l` exits 0, writes `.s`, `.o`, `.libc.o`,
  `.kex` and `.lst`, and the image runs: output 14 bytes,
  `48 65 6c 6c 6f 2c 20 4b 52 31 36 21 0d 0a`, SHA-256
  `116C8E01530C6F162D7676CEBD40D3FD41DFFF376BAB24C3D5D9AFCF5B7235F0` — identical to
  the builtin image's output. `.kdbg` is present with debug (612 bytes) and absent
  under `--no-debug`; `.text` stays.
- Listing shape matches the builtin file: `; generated by kr16 krc from hello.c`,
  `.file 0 "hello.c"`, `.text`, `.global main`, `main:`, then address/bytes/text rows.
  Labels come from the link symbols restricted to row starts, so libc symbols do not
  leak into it.
- 2.2: a probe drove `DebugSession` over a krc-built image of
  `int value = add(2, 3);` at line 6. `source_path`, both functions and four
  executable lines load; the line-6 breakpoint stops in `main` at pc 0x07ec; `value`
  resolves at 0xefe0 with `ok=1`; one `LineOver` step lands on line 7 with
  `value == 5`. The committed regression test is
  `a krc-built image debugs through .kdbg` in `tests/unit/test_debug_session.cpp`
  (17 assertions, passes on a krc-built image).
- 2.3: `node scripts/dap-smoke.js build/check-krc/kr16.exe <krc image>` →
  `DAP smoke passed`. A krc `.kex` carries **no symbol table** (`symbols=0`), so
  function breakpoints now resolve from `.kdbg` function ranges instead of container
  symbols; locals come from the model, line rows from the primary source.

**Finding, recorded because it cost a wrong first version:** `.kdbg` variable offsets
are already frame-pointer relative — a variable at offset N is at `R4 - N`, the same
convention as `.debug_vars`. The decoder first subtracted the function's frame size and
read the wrong address; the probe showed `value` at the saved-R4 slot. Fixed.

**Blocker — `resources/fileio.c` does not compile under krc at `0defcce`.** Exact
command and output:

```pwsh
krc --target=kr16 -c C:\Users\kevin\source\repos\kr16\resources\fileio.c `
    -I C:\Users\kevin\source\repos\kr16\include `
    -I <pin>\sysroot\include -I <pin>\sysroot\kr16\include -o fileio.kro
```

```text
C:\Users\kevin\source\repos\kr16\resources\fileio.c:576:21: warning: implicit declaration of function 'fgetc'
              value = fgetc(KR16_STDIN);
                      ^
C:\Users\kevin\source\repos\kr16\resources\fileio.c:654:5: error: redefinition of function 'fgetc'
  int fgetc(FILE *stream) {
      ^
C:\Users\kevin\source\repos\kr16\resources\fileio.c:832:31: error: integer constant is too large for its type
      if (base == 10 && (high & 32768) != 0) {
                                ^
```

Exit 1. `kr16 build hello.c` reaches the same point: `build: fileio failed`. The
builtin compiler accepts both constructs. No workaround is taken: the shipped runtime
is not edited and `fileio.c` is not dropped.

Measured, so the owner knows the size of the gap: in a throwaway copy of `fileio.c`
with a forward declaration for `fgetc` and `0x8000` for `32768`, the same command
compiles and assembles the whole translation unit to a 606722-byte object. No further
diagnostics follow, so those two constructs are the complete gap for this file.

**Gates, both from a fresh configure (`build/windows-ninja` deleted first).**

```pwsh
cmake --preset windows-ninja
cmake --build --preset windows-ninja-debug
ctest --preset windows-ninja-debug --output-on-failure
```

`-- kr16 toolchain: builtin (krc pin 0defcce); kdbg reader: OFF` →
`98% tests passed, 1 tests failed out of 59`; `kr16.unit` fails only at
`tests/unit/test_resources.cpp(41)`, the pre-existing embedded-CR failure the stashed
`embedded_libc.cpp` regeneration fixes. No failure is added by this change.

```pwsh
cmake --preset windows-ninja -DKR16_TOOLCHAIN=krc `
      -DKR16_KRC_PREFIX=build/krc-pin-0defcce/install `
      -DKR16_KRC_SYSROOT=build/krc-pin-0defcce/src/sysroot
cmake --build --preset windows-ninja-debug
ctest --preset windows-ninja-debug --output-on-failure
```

`-- kr16 toolchain: krc (krc pin 0defcce); kdbg reader: ON` →
`69% tests passed, 18 tests failed out of 59`: `kr16.unit` plus the seventeen
`build`-based integration tests and their dependents (`cforu` ×5, `build.hello`,
`vm.built_hello`, `printf_hex` ×2, `fileio` ×2, `stdio` ×2, `sysfile` ×2, `args` ×2).
Every one of them fails at `build: fileio failed`; the krc reader is not implicated.

**Decision needed:** either `resources/fileio.c` gets the two small conformance edits
(it is still the shipped libc, and the throwaway probe shows the file is otherwise
accepted), or krc's front end grows the two acceptances. Both are outside what this
stage may do unilaterally, so the work stops here.

**Next step:** on that decision, finish 2.4 and run 2.5 green on both toolchains.

**Commit:** the implementation plus this entry on `adopt-krc`; the default stays
`builtin` and `src/cc`, `src/asm`, `src/link` are untouched.

**Blocker:** yes — `resources/fileio.c` under the pinned krc.

## 2026-11-20T12:47:38-08:00

**Changed:** the shipped `resources/fileio.c` gets the two conformance fixes the owner
recorded, the embedded copies are regenerated, and both toolchains are gated on Windows
and WSL. Both `builtin` gates are now green; the `krc` gates are red for three reasons
that are not the fileio compile.

**The two edits.**

| file | line | change |
|---|---|---|
| `resources/fileio.c` | 51 | `int fgetc(FILE *stream);` added to the prototype block, before its first use at line 577, so the definition at 655 is not a redefinition |
| `resources/fileio.c` | 833 | `(high & 32768)` → `(high & 0x8000)`, a hexadecimal constant whose type is `unsigned int` on the IP16 profile, which is what a bit-15 mask needs |

`resources/embed_libc.py` regenerated `src/resources/embedded_libc.cpp`: `kLibc` 19663
bytes, `kFileio` 40154, `kSystemServices` 4636. The regeneration also carries the CR
strip that was stashed before this work started, so `test_resources.cpp` now passes and
`stash@{0}` (the pre-work regeneration) is superseded — popping it would revert the
fileio change in the embedded copy.

`resources/manifest.json` refreshed with `scripts/bootstrap-check.ps1 -Update`; the
`fileio` hash is now `9FAB3033DCA0B355896B7B4DC5C7D8875416CE462A7485354C4D18BA5BED100F`
and the check then prints `resource hashes match the manifest`.

**Both compilers accept the fixed file.**

```pwsh
kr16 cc resources/fileio.c -c -I include -o build/fileio-check/fileio-builtin.o
# exit 0, 37266-byte object
krc --target=kr16 -c --emit-object resources/fileio.c -I include `
    -I <pin>/sysroot/include -I <pin>/sysroot/kr16/include -o build/fileio-check/fileio.kro
# exit 0, 606680-byte object
```

**Gates. Four runs, each from a deleted build directory so no stale artefact can pass a
test.** Windows:

```pwsh
cmake --preset windows-ninja
cmake --build --preset windows-ninja-debug
ctest --preset windows-ninja-debug --output-on-failure
```

- `builtin`: `-- kr16 toolchain: builtin (krc pin 0defcce); kdbg reader: OFF` →
  **`100% tests passed, 0 tests failed out of 59`** (43.39 s). The pre-existing
  `test_resources` failure is gone with the regeneration.
- `krc` (`-DKR16_TOOLCHAIN=krc -DKR16_KRC_PREFIX=… -DKR16_KRC_SYSROOT=…`):
  `-- kr16 toolchain: krc (krc pin 0defcce); kdbg reader: ON` →
  **`68% tests passed, 19 tests failed out of 60`**.

WSL (`linux-gcc` preset, SDL3 fetched as CI does; the pinned package built there from a
`git archive` of `0defcce` and installed under `$HOME/krc-pin`):

- `builtin`: **`100% tests passed, 0 tests failed out of 56`** (9.15 s).
- `krc`: **`68% tests passed, 18 tests failed out of 56`**.
- WSL has no `node`, so `kr16.dap.smoke` is not registered there; Windows has the
  installer and manifest tests WSL does not. That is the 56/60 difference.

**Why the krc gates are red — three causes, all reproduced directly.** `fileio.c` now
compiles and assembles; the failures are past it:

1. **`__host_call` is a kr16-compiler intrinsic, and krc emits a call to it.**
   `kr16 build tests/integration/hello.c` under krc →
   `build: ld failed: undefined symbol '__host_call'`. The builtin compiler folds
   `__host_call(n, block)` into `SYS #n` (`src/cc/compiler.cpp:8094-8100`), and `SYS`
   carries the service number as an 8-bit immediate (`docs/isa.md:292`), so no ordinary
   function can stand in for it. Nothing in the tree defines the symbol; the builtin
   image never needs one. `fileio.c` calls it at lines 100, 120, 132, 144, 145, 162,
   163, 180, 198, 199, 209, 219 and 220.
2. **An implicit variadic declaration is rejected by krc's IR verifier.**
   `krc --target=kr16 -c --emit-object tests/integration/stdio_buffered.c …` →
   `warning: implicit declaration of function 'fprintf'` then
   `error: IR verifier: function 'main': call has the wrong argument count`, exit 1. The
   call at `tests/integration/stdio_buffered.c:12` passes four arguments to an
   implicitly declared function; the builtin compiler accepts it.
3. **`C-for-U.c` does not fit in the 16-bit address space under krc's codegen.**
   `kr16 build --no-debug tests/kevin/C-for-U.c` →
   `build: ld failed: link relocation overflows its width`. Measured with a probe over
   the facade's listing rows: `C-for-U.c` `.text` 48,972 B, `libc.s` 1,698 B + 6 B
   `.data`, `fileio.c` `.text` 35,214 B, stub 8 B, base 0x100 → about 86 KiB against
   64 KiB. The builtin image of the same set is 27,626 B of `.text`.

The krc failures are exactly these: `kr16.unit` (the `build` tests plus the `.kdbg`
smoke), the `cforu` group (cause 3), the build/run pairs for hello, printf_hex, fileio,
stdio, sysfile and args (cause 1, and cause 2 for stdio), and `kr16.dap.smoke.krc`
(cause 1: its dependency cannot produce an image).

**What the krc path does prove on an image it built.** With a runtime directory holding
`libc.s` but no `fileio.c` — the same optional-file rule the builtin path uses — the krc
branch builds and the checks pass:

- `kr16 build hello.c -l` → `build: wrote hello.kex and hello.lst`; `kr16 vm hello.kex`
  → `Hello, KR16!`, exit 0; the listing has the builtin's shape.
- `node scripts/dap-smoke.js build/windows-ninja/kr16.exe build/smoke-krc4/hello.kex` →
  `DAP smoke passed` — on a `.kex` with no symbol table, so the `main` function
  breakpoint came from `.kdbg`.
- `kr16_tests --test-case="a krc-built image debugs through .kdbg"` →
  `test cases: 1 | 1 passed`, `assertions: 17 | 17 passed`.

That is the state: the reader and the producer work on a krc-built image, and the
shipped-runtime link is what blocks 2.4/2.5.

**Follow-up, not this pass (same shape as KR32's 3.9).** KRCompiler 4.12 installs the
target sysroot and exports `KRC_SYSROOT`. When the pin moves past `0defcce`, kr16
consumes the installed package — configure, build and test with an installed KRCompiler
prefix and no build tree on the path — and `KRC_SYSROOT` replaces
`KR16_KRC_SYSROOT` pointing into `build/krc-pin-0defcce/src/sysroot`. Stage B waits on
it so the flip does not depend on a directory a clean can delete. Not started here.

**Decision needed:** cause 1 is the one that blocks everything (it is what makes
`fileio.c` unlinkable under krc). Either krc learns the `__host_call` intrinsic
(a KRCompiler item, out of scope here), or the shipped runtime grows a real
`__host_call` — a dispatcher over the constant service numbers in `libc.s` — so the
wrappers stop depending on a compiler intrinsic. Cause 2 is a one-line declaration in
`tests/integration/stdio_buffered.c` if the owner wants it. Cause 3 is a codegen-density
wall: the builtin compiler's whole image for `C-for-U.c` is 27,626 B, krc's runtime
alone is 36,912 B.

**Next step:** on that decision; then 2.5.

**Commit:** the fileio fix, the regenerated resources, the manifest and this entry on
`adopt-krc`. Default stays `builtin`; nothing deleted.

**Blocker:** yes — `__host_call` (and, behind it, the variadic declaration and the
address-space wall).

## 2026-11-20T13:14:35-08:00

**Changed:** the stdio fixture's prototype, and the C-for-U size wall measured. `-O1`
verified as in effect, because the measurement is only meaningful at the level the gate
uses.

**The prototype.** `tests/integration/stdio_buffered.c` declares `fprintf` itself now,
because the kr16 profile of `<stdio.h>` stops at `printf` while `fileio.c` also defines a
formatted stream writer:

```c
/* The kr16 profile of <stdio.h> stops at printf; fileio.c also defines a
 * formatted stream writer, so declare it here rather than call it implicitly. */
int fprintf(FILE *stream, const char *format, ...);
```

Both compilers accept the file: `kr16 cc … -c` → exit 0 (3108-byte object);
`krc --target=kr16 -c --emit-object …` → exit 0 (15777-byte object), with no
`implicit declaration` warning and no IR-verifier error. With the prototype the stdio
test now fails at the same place every other krc build does — `undefined symbol
'__host_call'` — and no longer in the front end. Windows `builtin` gate re-run from a
deleted build directory: **`100% tests passed, 0 tests failed out of 59`**.

**The size wall, measured.** The literal 2.1 recipe — the startup stub, the shipped
`resources/libc.s`, and the program, entry `_start` — cannot produce an image for
`C-for-U.c`:

```text
link failed: undefined symbol 'fputc'
```

`C-for-U.c` uses the fileio layer, so it needs the runtime object that cannot link until
4.13 lands. The budget was therefore measured with `fileio.c`'s object included — it
assembles, only the link is blocked — and a two-byte measurement-only `__host_call`
stub, which is not a product path and is not used by `build_program`:

```text
linked 37198 bytes, entry 0x100          <- stub + libc.s + hello.c + fileio.c + stub
  .text addr=0x100 size=36972
  .data addr=0x916c size=6
  .bss  addr=0x9172 size=74
address span ends at 0x91bc (37308), space is 65536
```

The runtime's address span is 37,308 B, of which 36,972 B is `.text` — the owner's
36,912 B figure for the runtime alone, plus the 48-byte hello program and the 8-byte
stub. `C-for-U.c`'s own `.text` is 48,972 B (assembler listing, exact; krc emits no
`.data` or `.bss` rows for it). The full set:

```text
C-for-U.c          48972
libc.s + fileio.c  37308   (36,972 .text, 6 .data, 74 .bss)
base 0x100           256
                   -----
                   86536   against 65536  -> over by 21,000 B, before C-for-U's data
```

Linking the full set reports `link failed: link relocation overflows its width`. **No
image is produced and nothing can run: it does not fit.** 2.1's own `hello.kex` remains
the running proof; C-for-U is the one that exceeds the space.

The cause is codegen density, not the runtime's feature set: the builtin compiler
produces ~11,904 B of `.text` for `fileio.c` (the builtin `hello.kex` is 13,632 B of
`.text` for libc+fileio+hello, and libc+hello without fileio is 1,728 B), while krc
produces 35,214 B for the same file — 2.96×. For the current codegen to fit, the runtime
would have to come down to about 16,308 B, or krc's `fileio.c` output to about a third
of its size. That is a diet task, not a `build_program` task.

**`-O1` verified, and it was the measurement's setting.** The path is plumbed end to
end: `BuildOptions::optimize` defaults true (`include/kr16/build.hpp:23`), the krc branch
maps it to `settings.optimize = 1` (`src/cli/build.cpp:334`), and krc turns
`optimize > 0` into `OptimizationLevel::O1` (`src/toolchain/toolchain.cpp:140`) and runs
its optimiser for every target, KR16 included, at `src/toolchain/toolchain.cpp:446-478`.
The probe used `optimize = 1`. Measured at both levels to prove it is not a no-op:

| file | `-O0` | `-O1` | recovered |
|---|---|---|---|
| `fileio.c` | 37,442 | 35,214 | 2,228 B (6.0%) |
| `C-for-U.c` | 49,920 | 48,972 | 948 B (1.9%) |
| `hello.c` | 50 | 48 | 2 B |

So the 86,536 B figure already has `-O1` applied; `-O0` would make the wall 2,500 B
worse. The optimiser is a modest IR pass set (constant folding, alloca promotion, copy
propagation, DCE, empty-block elimination) and does not change the density that causes
the overflow.

**Decision recorded here:** the size wall blocks `kr16.cc.cforu` (and its four dependent
checks) under krc independently of 4.13. Everything else in 2.4/2.5 can proceed once
4.13 is published; C-for-U needs the runtime diet or a codegen-density change, both
their own tasks.

**Next step:** wait for KRCompiler 4.13's SHA, re-pin, and resume 2.4/2.5 with the
shipped runtime; report C-for-U as a separate, measured blocker.

**Commit:** the fixture and this entry on `adopt-krc`.

**Blocker:** 4.13 for `__host_call`; the measured size wall for C-for-U.

## 2026-11-20T13:17:06-08:00

**Changed:** nothing. Measurement only, so the runtime diet named in the previous entry
has numbers to plan from.

**Where the runtime's bytes are.** Of the 37,308 B address span, `fileio.c` compiled by
krc is 35,214 B of `.text` (95%) and `libc.s` 1,698 B (5%); the rest is the 8-byte stub
and the 48-byte hello program. Per-function sizes come from a debug image's `.kdbg`
ranges (`end - start`), so they include the padding between functions; the 56 `fileio.c`
functions plus `main` total 33,804 B, leaving about 1,410 B of `.text` for the string data
krc emits inline.

| # | function | bytes | share | cumulative |
|---|---|---|---|---|
| 1 | `fscanf` | 5,342 | 15.8% | 15.8% |
| 2 | `format_stream` | 3,896 | 11.5% | 27.3% |
| 3 | `print_number` | 1,922 | 5.7% | 33.0% |
| 4 | `fwrite` | 1,588 | 4.7% | 37.7% |
| 5 | `print_long_words` | 1,374 | 4.1% | 41.8% |
| 6 | `freopen` | 1,282 | 3.8% | 45.6% |
| 7 | `fread` | 1,026 | 3.0% | 48.6% |
| 8 | `tmpfile` | 868 | 2.6% | 51.2% |
| 9 | `ftell` | 850 | 2.5% | 53.7% |
| 10 | `fseek` | 830 | 2.5% | 56.1% |
| 11 | `mode_flags` | 822 | 2.4% | 58.6% |
| 12 | `flush_stream` | 690 | 2.0% | 60.6% |

The tail is small: `print_char` 94, `scan_is_space` 92, `fprintf` 66, `printf` 58,
`set_errno_status` 48, `rewind` 30, `main` 28, `__errno_location` 10.

`libc.s` is hand-written assembly and carries no `.kdbg` functions; label-to-label deltas
from its listing put `realloc` at 418 B, `__long_divmod` 344, `malloc` 254,
`__kr16_block_free` 206, `calloc` 70, `free` 50, `atoi` 36.

**What the call sites say.** Nothing in the repository calls `fscanf` (0 call sites
outside its own definition), `freopen` or `tmpfile` (1 each, both inside `fileio.c`
itself). Those three are 7,492 B, 22% of `fileio.c`, and 20% of the runtime. Dropping
them would move the C-for-U budget from 86,536 B to about 79,044 B — still 13,508 B over
the 65,536 B space, so the dead entry points alone do not clear the wall. `fread` has one
caller (`C-for-U.c`); `fwrite`, `ftell` and `fseek` are used by `stdio_buffered.c`.

**Next step:** unchanged — 4.13 for `__host_call`, then the diet or a codegen-density
change for C-for-U.

**Commit:** this entry only.

**Blocker:** none new.

## 2026-11-20T14:02:03-08:00

**Changed:** re-pinned to 4.13, the krc path now builds and runs with the shipped
runtime, and the suite is gated twice on Windows and WSL. 2.4 is green for every program
that fits; 2.5 is not green — 6 tests fail under krc from two causes, both outside kr16.

**Pin.** KRCompiler `adopt-krc` @ `7d005d5`
(`7d005d5d424475f5bcc50fd7f760243bba8d26e9`), whose parent `7dc7477` teaches krc the
`__host_call` intrinsic: the KR16 backend recognises a direct call with a constant
service in 0..255 and lowers it to `SYS #n`. `KR16_KRC_PIN` in `CMakeLists.txt` is now
`7d005d5`. The package was built from a `git archive` of that revision with
`-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDebug` on Windows and with GCC in WSL, both
into scratch prefixes.

**4.13 does what it promised.** `kr16 build hello.c -l` under krc now compiles the input,
assembles the stub, assembles `resources/libc.s`, compiles and assembles
`resources/fileio.c`, links and runs — the whole recipe with the shipped runtime and no
substitution.

**Image sizes after 4.13, and whether they run.**

| image | `.text` | `.data` | `.bss` | total | runs |
|---|---|---|---|---|---|
| `hello.c` | 36,912 | 6 | 74 | 36,992 | yes — prints `Hello, KR16!`, exit 0 |
| `C-for-U.c` | 85,836 (computed) | 6 | 74 | 86,172 span | **no image is produced** |

`C-for-U.c` still reports `build: ld failed: link relocation overflows its width`. Its
budget, exact at `-O1` from the four objects: stub 8 + `libc.s` 1,698 + `fileio.c` 35,158
+ `C-for-U.c` 48,972 = 85,836 B of `.text`, plus 6 B `.data`, 74 B `.bss` and the 0x100
base — 86,172 B against 65,536 B, **over by 20,636 B**. No image, so nothing to run.

**Correction to the `2026-11-20T13:14:35-08:00` entry, and to the premise behind this
task.** That measurement did **not** use krc's runtime; it used the shipped
`resources/libc.s` and `resources/fileio.c` compiled by krc, with a two-byte placeholder
standing in for `__host_call` because 4.13 did not exist yet. The figure it reported,
86,536 B, also added the 0x100 base twice: the correct figure for the same objects is
86,280 B. With 4.13 the same set measures 86,172 B — the intrinsic lowering saved 56 B
in `fileio.c` (35,214 → 35,158) and the placeholder is gone. The number moves by about
100 B, not by kilobytes, so the wall stands and the measurement the next step depends on
is: **the shipped runtime is dense, but krc's codegen of it is 36,912 B, and the program
alone is 48,972 B, which is already 75% of the 64 KiB space before the runtime is added.**

**Two test-level findings.**

1. **krc's sysroot maps `stdout` to a symbol the shipped runtime does not define.**
   `tests/integration/stdio_buffered.c` used `fprintf(stdout, "stdio ok\n")`, and krc's
   `sysroot/include/stdio.h` has `#define stdout (__krc16_stdout())`; the link fails with
   `build: ld failed: undefined symbol '__krc16_stdout'`. The built-in compiler treats
   `stdout` as a built-in identifier, so the fixture only ever worked on that path. The
   marker is now `printf("stdio ok\n")` — `fprintf` on a real `FILE` is still exercised at
   line 12 — which is the spelling both toolchains resolve from the shipped runtime. The
   mismatch itself is recorded, not fixed: any kr16 program that uses `stdout` or
   `stderr` under krc will hit it, and the fix is either krc's sysroot mapping them to
   `(FILE *)1`/`(FILE *)2` for this target or the shipped runtime growing the two
   accessors.
2. **krc attributes an assignment to its declaration's line.** The unit case
   `debug session builds and loads a C source file` builds
   `int main(void) { int local; local = 42; return local; }` and requires the session to
   land on line 3; under krc it lands on line 2. The payload has executable lines {2, 4}
   only, with 0x106-0x10A (`local = 42`) mapped to line 2 and 0x10C-0x110
   (`return local`) to line 4. With an initializer (`int local = 42;`) the same probe maps
   2 → 3 → 4 correctly, so the row for a plain declaration is swallowing the following
   assignment. This is a krc line-table defect; the test was left asserting line 3 rather
   than being gated to accept line 2.

**Gates. Four runs, each from a deleted build directory.**

| | builtin | krc |
|---|---|---|
| Windows (`windows-ninja`) | **100% — 0 failed of 59** | **90% — 6 failed of 60** |
| WSL (`linux-gcc`) | **100% — 0 failed of 56** | **89% — 6 failed of 56** |

The krc failures are identical on both platforms: `kr16.unit` (the line-table assertion
above) and the five `cforu` checks (`kr16.cc.cforu`, `vm.cforu`, `cforu.explicit_output`,
`cforu.generated`, `cforu.cycles`) from the size wall. Everything else passes under krc,
including the items this stage is graded on: `kr16.dap.smoke.krc` (the existing DAP check
over a krc-built, symbol-less `.kex`, so the `main` function breakpoint comes from
`.kdbg`) and the `a krc-built image debugs through .kdbg` unit case.

**Verdict.** 2.4: green — `build_program` under `KR16_TOOLCHAIN=krc` compiles, assembles
the stub and `libc.s`, compiles and assembles `fileio.c`, links and runs for the fixtures
that fit. 2.5: **not green**, by 6 of 60 on Windows and 6 of 56 on WSL, for the two
causes above. The size measurement is the one that decides the next step — function
sections with GC, or a runtime diet — and it is recorded rather than acted on: no runtime
change and no GC in this pass.

**Next step:** the owner's call between function sections with GC and a runtime diet for
C-for-U, plus a KRCompiler item for the line table and a decision on `stdout`/`stderr`.

**Commit:** the pin, the stdio marker and this entry on `adopt-krc`.

**Blocker:** the 64 KiB wall for C-for-U (5 tests) and krc's line table (1 test).

## 2026-11-20T14:15:26-08:00

**Changed:** nothing. The work this pass depends on is not published.

**4.15 and 4.16 are not on `adopt-krc` yet.** `origin/adopt-krc` is `937b2f4`
(`Record the 4.14 gate`); the two items are 4.15 (KR16 line-table attribution) and 4.16
(function sections and GC). No re-pin, so the suite has not been re-run and `kr16.unit`'s
line assertion is untouched and still failing under krc as recorded at
`2026-11-20T14:02:03-08:00`.

**Two things checked while waiting, so the sectioning is not started blind.**

1. **The KR16 assembler at `937b2f4` has no `.section` directive** (`git grep '"\.section"'
   src/asm/` returns nothing), so `resources/libc.s` cannot be split into
   `.section .text.<name>` for the krc path at the current pin.
2. **GC in the pin is object-level, not section-level**: `src/linker/linker.cpp:225` keeps
   `reachable(objects.size())` and walks object-to-object through relocations. With
   `resources/libc.s` as one object, `gc_sections` can drop all of it or none of it; no
   function inside it can be collected. That is exactly what 4.16 changes.

**A third finding, which is kr16's own work and not KRCompiler's.** kr16's linker does not
merge `.text.<name>` into `.text`. Probe: `_start` in `.text` plus `extra` in
`.section .text.extra` assembles (`kr16 as`, exit 0) and links (`kr16 ld`, exit 0), but
`kr16 size` reports `.text` 8 and data 4 — `extra`'s four bytes are not in `.text`, so a
sectioned `libc.s` would misplace every libc function in a builtin image. Splitting the
file therefore needs a small change in `src/link/linker.cpp` to fold `.text.*`, `.data.*`
and `.bss.*` into their base sections, so both toolchains keep accepting the same shipped
file. Not started: it is only needed together with the split.

**Plan when the SHAs publish:** re-pin and rebuild the package; re-run 2.5 and confirm
`kr16.unit` passes with the line assertion untouched; split `libc.s` at its `.global`
boundaries and teach the builtin linker the base-section merge; build with `gc_sections`
on; re-measure C-for-U and report the size. If it is still over 64 KiB, stop — whether
C-for-U is over budget for stage A is the owner's decision, 4.17 is tracked and not
started, `fileio.c` is not trimmed and the five `cforu` tests are not relaxed.

**Next step:** wait for 4.15 and 4.16.

**Commit:** this entry only.

**Blocker:** 4.15 and 4.16 unpublished.

## 2026-11-20T23:05:23-08:00

**Changed:** re-pinned to 4.16, `resources/libc.s` split into per-function sections,
`gc_sections` on for the krc path, the built-in linker folding the named sections back
into their base sections, and the suite gated twice on Windows and WSL. Stage A is
closed except C-for-U, which is still over the address space.

**Pin.** KRCompiler `adopt-krc` @ `d2be672`
(`d2be672214a413456313448e47f942416bb4d9f5`): 4.15 (`57182f6`, the KR16 line-table
attribution) and 4.16 (`5983f90`, `.section .text.<name>`, section-level GC). The package
was built from a `git archive` of that revision on Windows and in WSL; `KR16_KRC_PIN` is
`d2be672`.

**The split.** `resources/libc.s` now carries one `.section .text.<name>` before each of
its 19 function globals — `putchar puts getchar __long_divmod abs atoi strcat strlen
strcpy strcmp memset memcpy malloc free calloc realloc __kr16_heap_used
__kr16_heap_peak exit`. The splitter tracked the base section, so the three data globals
in `.data`/`.bss` (`__kr16_heap_start`, `__kr16_heap_top`, `__kr16_free_list`) are
untouched. The file still totals 1,698 B of `.text`; the embedded `kLibc` is 20,119 B
(the 456 extra bytes are the directives) and the resource manifest was refreshed. Both
assemblers accept it: `kr16 as` exit 0, krc 4.16 `assemble` `OK`.

**The built-in linker folds the named sections back.** `src/link/linker.cpp` maps
`.text.<name>`, `.data.<name>` and `.bss.<name>` to their base output section, so the
built-in image keeps its layout and both toolchains consume the same shipped file. That
was the merge flagged on `2026-11-20T14:15:26-08:00`; it is done here.

**GC on for the krc path:** `settings.gc_sections = true` in `build_program`'s krc
branch, with the entry section as the walk's root.

**Measurements.**

| image | `.text` | result |
|---|---|---|
| `hello.c` (krc, GC) | **86 B** | runs, `Hello, KR16!`, exit 0 |
| `C-for-U.c` (krc, GC) | **no image** | `build: ld failed: link relocation overflows its width` |

`hello` dropped from 36,912 B to 86 B because GC now drops the whole `fileio` object —
nothing in it is reachable from `hello` — and the unused libc sections with it.

`C-for-U` still does not fit, and the split cannot change that: krc emits one `.text` per
translation unit, so an image that calls any `fileio` function keeps all 35,158 B of it.
The lower bound before libc, `.data`, `.bss` and the base:

```text
C-for-U.c program       48,972
fileio.c                 35,158
startup stub                  8
                        -------
                         84,138   against 65,536  ->  over by 18,602 B
```

Every libc section C-for-U references adds to that, so no amount of libc collection
closes the gap; the mass is the program's own code and `fileio`, each a single section.

**Gates. Four runs, each from a deleted build directory.**

| | builtin | krc |
|---|---|---|
| Windows (`windows-ninja`) | **100% — 0 failed of 59** | **92% — 5 failed of 60** |
| WSL (`linux-gcc`) | **100% — 0 failed of 56** | **91% — 5 failed of 56** |

Under krc the only failures are the five `cforu` checks; **`kr16.unit` passes on both
platforms with its line assertion untouched** — `test_debug_session.cpp` still requires
line 3 for `int local; local = 42;`, and 4.15 reports line 3. The krc `.kdbg` debug case
and `kr16.dap.smoke.krc` pass on both platforms.

**Decision left open, not taken here:** whether C-for-U is over budget for stage A. The
five `cforu` checks are not relaxed, `fileio.c` is not trimmed, and 4.17 (16-bit codegen
density) is tracked and not started.

**Next step:** the owner's call on C-for-U; nothing else in stage A is open.

**Commit:** the pin, the split, the linker merge, the GC flag, the regenerated resources
and this entry on `adopt-krc`.

**Blocker:** C-for-U is 18,602 B over before libc, from its own code and `fileio`.

## 2026-11-21T06:45:16-08:00

**Stage A is complete.** Decision 6 (`4533e5b`, 2026-11-21T06:32:35-08:00) accepts C-for-U
as over budget for stage A; the five failing `cforu` checks under krc are now one negative
test, both suites are green on Windows and WSL, and the limitation is recorded rather than
left red. Stage B (2.6) is not started: it needs its second approval and the one-release
window.

**The test.** `kr16.cforu.over-budget`
(`tests/cforu_over_budget.cmake`, registered under both toolchains) runs
`kr16 build --no-debug tests/kevin/C-for-U.c` and:

- under `krc`, requires a non-zero exit whose output contains
  `ld failed: link relocation overflows its width` — it passes, which is decision 6
  recorded as expected behaviour;
- under `builtin`, the build succeeds, so the script prints
  `skip: the build succeeded, so this toolchain fits C-for-U` and CTest marks the case
  skipped (`SKIP_REGULAR_EXPRESSION`). The five positive checks — `kr16.cc.cforu`,
  `kr16.vm.cforu`, `kr16.cforu.explicit_output`, `kr16.cforu.generated`,
  `kr16.cforu.cycles` — stay exactly as they were and still run under `builtin`.

The comment in the script and in `tests/CMakeLists.txt` cites decision 6 and KRCompiler
4.17 as the tracked work that would reopen it: when krc's 16-bit output shrinks enough to
fit, the build succeeds, the test fails and prints the build's real output, which is the
signal to restore the five checks. `fileio.c` is not trimmed for this.

**The limitation, in numbers.** `resources/libc.s` assembles to 1,698 B; the wall is krc's
compiled output. C-for-U's image needs 48,972 B of program code, 35,158 B of `fileio.c`
and the 8-byte startup stub — **84,138 B of `.text` against the 65,536 B address space,
over by 18,602 B before any libc section**. 4.16's function sections and GC closed the
rest of the suite (hello is 86 B and runs) but cannot shrink two single-section objects.
The lever is codegen density: KRCompiler 4.17, tracked and not started.

**Gates. Four runs, each from a deleted build directory, pinned at `d2be672`.**

| | builtin | krc |
|---|---|---|
| Windows (`windows-ninja`) | **100% — 0 failed of 60**, 1 skipped | **100% — 0 failed of 56** |
| WSL (`linux-gcc`) | **100% — 0 failed of 57**, 1 skipped | **100% — 0 failed of 52** |

The one skipped case in each builtin run is `kr16.cforu.over-budget`, listed under "The
following tests did not run". The Windows/WSL and builtin/krc count differences are the
platform-only tests: `kr16.installer` and `kr16.resources.manifest` are Windows-only, and
`kr16.dap.smoke` plus `kr16.dap.smoke.krc` need `node`, which WSL does not have.

`kr16.unit` passes on both platforms under both toolchains with its line assertion
untouched, and under krc the `.kdbg` debug case, `kr16.dap.smoke.krc` and
`kr16.cforu.over-budget` all pass.

**Docs.** `README.md` gains a "Toolchain selection" section under Build (the two values
the krc path needs, the `.kdbg` reader following the toolchain, and that the default flips
only after stage B), and its Tests section now states the per-toolchain run, the negative
test, the 84,138 B figure and the 4.17 dependency. There is no `AGENTS.local.md` in this
repository, so nothing to update there.

**Next step:** stage B (2.6) on the second approval and the one-release window. Nothing
else in stage A is open.

**Commit:** the test, its registration, the README lines and this entry on `adopt-krc`.

**Blocker:** none. C-for-U is a recorded stage A limitation, not a blocker.

## 2026-11-21T16:01:57-08:00

**`adopt-krc` is merged into `main`.** Merge commit `f50f239` with parents `6971a40`
(`origin/main`, the tester's log of `main @ d3404fa`) and `87d1c80` (`adopt-krc`, stage A
close-out). Not a fast-forward: `main` carried the tester's commit, as the request said.
`adopt-krc` stays on the remote for history and is not deleted.

**The one conflict was the tester's log.** `docs/arm64-test-log.md` was add/add — `main`
had one entry, `adopt-krc` had three. The merged file keeps all four verbatim, in timestamp
order: `13:54:00` (`adopt-krc @ a6bdc1a`), `13:59:28` (`main @ d3404fa`), `14:04:32`
(`adopt-krc @ c098d4e`), `23:08:01` (`adopt-krc @ 5ec6eb9`). Each entry was checked against
its source side and appears unchanged; nothing was rewritten or dropped.

**Gate on `main`, four runs from deleted build directories, pinned at `d2be672`.**

| | builtin | krc |
|---|---|---|
| Windows (`windows-ninja`) | **100% — 0 failed of 60**, 1 skipped | **100% — 0 failed of 56** |
| WSL (`linux-gcc`) | **100% — 0 failed of 57**, 1 skipped | **100% — 0 failed of 52** |

The skipped case in each builtin run is `kr16.cforu.over-budget`, listed under "The
following tests did not run"; under krc it passes, which is decision 6 behaving as recorded
— the link fails with `ld failed: link relocation overflows its width` and the test asserts
exactly that. The count differences are the platform-only tests (`kr16.installer`,
`kr16.resources.manifest`) and the `node`-dependent DAP checks absent from WSL.

**Docs, so the tester polls `main`.** `docs/arm64-tester.md` now watches `main` only, with a
note that `adopt-krc` is merged, kept, and not tested again; its polling, build, log and
state sections name `main` instead of a `<branch>` placeholder. `README.md`'s Status
records the merge commit and that the tester polls `main`. A new `AGENTS.local.md` records
the branch policy, the toolchain selection, the C-for-U decision and the stage B rule.

**Next step:** the ARM64 tester's next entry will be `main @ <sha>`. Stage B (2.6) still
needs its second approval and the one-release window.

**Commit:** the merge, the docs and this entry on `main`.

**Blocker:** none.
