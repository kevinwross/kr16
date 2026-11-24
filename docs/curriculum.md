# KR16 Curriculum Notes

## 1. Machine model

- 64 KiB address space, little-endian words.
- `0x0000–0xEFFF` RAM, `0xF000–0xF0FF` MMIO, `0xF100–0xFFFF` ROM.
- Program entry is normally `0x0100`; the first page holds vectors and the BIOS
  jump table.
- Registers: R0–R4, PC, SP, SR; flags C, Z, N, V, I.

## 2. First assembly program

```asm
.text
.global _start
_start:
  MOV R0, #7
  HLT
```

Build and run:

```text
kr16 as program.s -o program.o -l program.lst
kr16 ld program.o -o program.kr16
kr16 vm program.kr16
```

The VM exits with `R0 & 0xFF` after `HLT`.

`-l` writes a listing with the address, the encoded bytes, and the source line
for every statement, which makes the assembly-to-memory mapping visible:

```text
0x0000   .text
0x0000   .global _start
0x0000   _start:
0x0000  87 11    MOV R0, #7
0x0002  00 88    HLT
```

Addresses are section-relative for a `.o`. With `--raw --base 0x100` the same
program is listed from `0x0100`.

## 3. Calling convention

- Arguments 0–2: R1, R2, R3.
- Later arguments: pushed right-to-left; first stack argument is at `[FP+4]`.
- Caller cleans stack arguments.
- Return value: R0.
- Callee saves R4 (frame pointer). At `-O0`, locals are FP-relative.

The teaching C compiler in this repository follows that layout.

## 4. C subset

Supported today: `int`/`char`, pointers, arrays, functions, locals/globals,
`if`/`else`, `while`, `do`/`while`, `for`, `switch`/`case`/`default`, `enum`,
`typedef`, `struct`/`union`, `return`, arithmetic/comparison/logical operators,
strings, and `main(argc, argv)`.

Example:

```c
int sum_to(int n) {
  int i;
  int total = 0;
  for (i = 1; i <= n; i++) total = total + i;
  return total;
}

int main(void) {
  return sum_to(10);
}
```

Compile and run, one step:

```text
kr16 build examples/sum.c
kr16 vm examples/sum.kr16
```

`build` leaves `sum.s`, `sum.o`, and `sum.libc.o` beside the source, records
debug information, and links the runtime automatically. Add `-l` for a
listing of the generated assembly. The same steps by hand:

```text
kr16 cc examples/sum.c -S -o build/sum.s
kr16 as build/sum.s -o build/sum.o
kr16 ld build/sum.o -o build/sum.kr16
kr16 vm build/sum.kr16
```

## 5. Devices and interrupts

- Console output: `0xF000`.
- Keyboard data/status/control: `0xF004`/`0xF005`/`0xF006`.
- Timer 0: `0xF008`–`0xF012`.
- Interrupt vectors 3 (timer) and 4 (keyboard) use the first-page vector table.
- `RETI` restores PC and SR.

## 6. Debug information

`.file` and `.loc` produce `.debug_line`; `kr16 debug --lines` renders it.
The assembler records one row per instruction with file, line, and column.

## 7. Exercises

1. Write an assembly loop that sums 1..10 and returns the result in R0.
   Worked solution: [`examples/sum_loop.s`](../examples/sum_loop.s).
2. Rewrite the loop in C and compare the generated assembly.
   Start from [`examples/sum.c`](../examples/sum.c).
3. Enable Timer 0 interrupts and toggle a memory flag in the handler.
4. Add `putchar` output to the C example. The assembly equivalent is
   [`examples/putchar.s`](../examples/putchar.s).
5. Use `kr16 dis` and `kr16 debug` to inspect the resulting image.




