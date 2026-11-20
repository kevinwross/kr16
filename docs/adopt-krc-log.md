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
