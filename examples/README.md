# Examples

Runnable examples for the current toolchain:

- `hello.c` — libc `puts` and the C/link/VM pipeline.
- `sum.c` — functions, loops, and `for`.
- `sum_loop.s` — hand-written 1..10 loop; exits with 55.
- `putchar.s` — writes `KR16` through the `0xF000` console port.

Build and run the assembly examples from the repository root:

```text
kr16 build examples/sum_loop.s -o build/sum_loop.kr16
kr16 vm build/sum_loop.kr16        # exit status 55

kr16 build examples/putchar.s -o build/putchar.kr16
kr16 vm build/putchar.kr16         # prints KR16
```

Build and run the C hello example from the repository root:

```text
kr16 cc examples/hello.c -S -o build/hello.s
kr16 as build/hello.s -o build/hello.o
kr16 as resources/libc.s -o build/libc.o
kr16 ld build/libc.o build/hello.o -o build/hello.kr16
kr16 vm build/hello.kr16
```

