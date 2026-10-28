# Examples

Runnable examples for the current toolchain:

- `hello.c` — libc `puts` and the C/link/VM pipeline.
- `sum.c` — functions, loops, and `for`.

Build and run the C hello example from the repository root:

```text
kr16 cc examples/hello.c -S -o build/hello.s
kr16 as build/hello.s -o build/hello.o
kr16 as resources/libc.s -o build/libc.o
kr16 ld build/libc.o build/hello.o -o build/hello.kex
kr16 vm build/hello.kex
```

Hand-written assembly examples will be added with the curriculum pass.
