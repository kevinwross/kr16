# AGENTS.local.md — kr16

Project-local notes for agents working in this repository. The general rules live in the
user-global `~/.dsh/AGENTS.md` and the DevTemplate baseline; this file holds only what is
true of kr16.

## Branches

- `main` is the integration branch. `adopt-krc` was merged into `main` at merge commit
  `f50f239` (2026-11-21) and is kept on the remote for history: do not build, test or
  push to it, and do not delete it.
- The ARM64 tester polls `main` only and appends to `docs/arm64-test-log.md`. That file is
  append-only; a merge or rebase that conflicts keeps both sets of entries in timestamp
  order, and no entry is ever dropped.
- Never force-push `main`.

## Toolchains

- `KR16_TOOLCHAIN` selects the producer behind `kr16 build`: `builtin` (the default) or
  `krc`. The krc path is pinned by `KR16_KRC_PIN`, located by `KR16_KRC_PREFIX`, and a
  program that includes a system header also needs `KR16_KRC_SYSROOT`.
- That mode links the dynamic CRT because the package is built with it, while the
  `builtin` mode keeps the static CRT so the shipped binary needs no redistributable.
  A package of the same configuration is required
  ([why](../krcompiler/docs/toolchain-embedding.md)): Debug builds take
  `KR16_KRC_PREFIX=C:/Users/kevin/krc-debug`, Release builds `C:/Users/kevin/krc`.
- Both toolchains must stay green. Run the suite once per toolchain, on Windows and WSL;
  the README's "Toolchain selection" section has the commands.
- C-for-U is accepted as over budget for the krc toolchain by decision 6
  (2026-11-21T06:32:35-08:00); `kr16.cforu.over-budget` records the wall and skips under
  `builtin`. Do not trim `resources/fileio.c` or relax that test to change the result.
  KRCompiler 4.17, 16-bit codegen density, is the tracked work that would reopen it.

## Stage B

- Flipping the default to `krc` and deleting `src/cc`, `src/asm` and `src/link` need the
  second approval and the one-release window from the plan. Do not start stage B from a
  working session.
