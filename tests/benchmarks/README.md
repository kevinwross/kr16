# Compiler optimization benchmark

Run the loop-heavy benchmark at both optimization levels:

```powershell
build\windows-ninja\kr16.exe build tests\benchmarks\loops.c -O0 -o build\loops-o0.kr16
build\windows-ninja\kr16.exe build tests\benchmarks\loops.c -O1 -o build\loops-o1.kr16

build\windows-ninja\kr16.exe vm build\loops-o0.kr16 --stats
build\windows-ninja\kr16.exe vm build\loops-o1.kr16 --stats
build\windows-ninja\kr16.exe size build\loops-o0.kr16
build\windows-ninja\kr16.exe size build\loops-o1.kr16
```

Recorded on 2026-11-14 with the Windows Debug build:

| Metric | `-O0` | `-O1` |
|---|---:|---:|
| VM instructions | 2,503 | 1,237 |
| VM cycles | 4,055 | 2,033 |
| text bytes | 11,882 | 11,794 |

The benchmark is a nested loop with integer multiply and array-free arithmetic.
The `-O1` result is roughly half the instruction and cycle count at the same
semantics.

The unit suite also gates this through the `optimized loop benchmark` test, so
a regression in instruction count fails CTest.

## Optimization-specific loops

`tests/benchmarks/opt_loops.c` exercises the passes added after 1.0:
`i * 3` induction strength reduction, the while-pointer `R3`/`R2` cache, and
the `same()` pointer loop.

```powershell
build\windows-ninja\kr16.exe build tests\benchmarks\opt_loops.c -O0 -o build\opt_loops-o0.kr16
build\windows-ninja\kr16.exe build tests\benchmarks\opt_loops.c -O1 -o build\opt_loops-o1.kr16
build\windows-ninja\kr16.exe vm build\opt_loops-o0.kr16 --stats
build\windows-ninja\kr16.exe vm build\opt_loops-o1.kr16 --stats
```

Recorded on 2026-11-15 with the Windows Debug build:

| Metric | `-O0` | `-O1` |
|---|---:|---:|
| VM instructions | 757 | 300 |
| VM cycles | 1,216 | 428 |
| text bytes | 9,386 | 9,238 |

The optimized build executes about 39% of the baseline instruction count.

