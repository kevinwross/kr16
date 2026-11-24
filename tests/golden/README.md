# Golden Container Fixtures

These files are stable byte-level fixtures for the KR16 container reader:

- `exec_min.kr16` — minimal EXEC container with `.text`, `.bss`, `.debug_str`,
  and a `main` symbol.
- `rel_min.o` — minimal REL container with an `ABS16` relocation.

Regenerate them from the repository root with a configured build:

```text
cmake --build --preset windows-ninja-debug --target kr16_golden_gen
build/windows-ninja/tests/kr16_golden_gen.exe
```

On Linux the executable path is
`build/linux-gcc/tests/kr16_golden_gen`.

The fixtures are read by `tests/unit/test_container_fixtures.cpp`. Review any
regeneration diff carefully; changing a fixture means the container layout
changed.
