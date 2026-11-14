# KR16 Assembly Language

## A User's Guide and Programmer's Reference

This manual teaches you to write, assemble, link, run, and inspect KR16 assembly
language programs. It is written for someone who is new to assembly language but
who is comfortable using a command line and editing a text file.

The reference half of the manual documents the KR16 instruction set, the
registers, the addressing modes, the stack, the frame pointer, the program
counter, and the flags register.

The examples were assembled and run with the 0.1.0 toolchain:

```text
kr16 0.1.0 (ISA 1, container 1, host ABI 1, debug 1)
```

The commands below assume that `kr16` is on your `PATH`. If you are using the
Windows release zip from the repository, the executable is
`dist/kr16-0.1.0-windows-x64/bin/kr16.exe`; substitute that path for `kr16`.

A note on notation: `Rd` means the destination register, `Rs` the source
register, `imm` an immediate value, and `label` a symbol. A leading `#` marks an
immediate operand. Hexadecimal values are written with a `0x` prefix. Flags are
written `C`, `Z`, `N`, `V`, and `I`.

---

## Contents

1. [The KR16 machine](#1-the-kr16-machine)
2. [Your first program](#2-your-first-program)
3. [Hello, World!](#3-hello-world)
4. [Arithmetic, flags, and branches](#4-arithmetic-flags-and-branches)
5. [Functions, the stack, and frames](#5-functions-the-stack-and-frames)
6. [Writing assembly source](#6-writing-assembly-source)
7. [Assembler directives](#7-assembler-directives)
8. [From source to runnable `.kex`](#8-from-source-to-runnable-kex)
9. [The disassembler](#9-the-disassembler)
10. [Programmer's reference: the machine](#10-programmers-reference-the-machine)
11. [Programmer's reference: addressing modes](#11-programmers-reference-addressing-modes)
12. [Programmer's reference: the stack and calling convention](#12-programmers-reference-the-stack-and-calling-convention)
13. [Programmer's reference: the flags register](#13-programmers-reference-the-flags-register)
14. [Programmer's reference: instruction formats and opcodes](#14-programmers-reference-instruction-formats-and-opcodes)
15. [Instruction set reference](#15-instruction-set-reference)
16. [Interrupts, exceptions, and traps](#16-interrupts-exceptions-and-traps)
17. [I/O and system services](#17-io-and-system-services)
18. [Instruction timing](#18-instruction-timing)
19. [Encoding examples](#19-encoding-examples)
20. [Implementation notes for this release](#20-implementation-notes-for-this-release)
21. [Appendix A: complete example programs](#appendix-a-complete-example-programs)
22. [Appendix B: the KR16 BIOS](#appendix-b-the-kr16-bios)
23. [Appendix C: quick reference](#appendix-c-quick-reference)
24. [Appendix D: troubleshooting and glossary](#appendix-d-troubleshooting-and-glossary)

---

## 1. The KR16 machine

KR16 is a small, 16-bit teaching computer. It has a single flat address space
and a simple, regular instruction set. There are no privilege levels, caches,
pipelines, or branch delay slots. That makes it a good machine on which to learn
what assembly language is doing.

The important numbers:

| Property | Value |
|---|---|
| Word size | 16 bits (2 bytes) |
| Address space | 64 KiB, byte-addressable |
| Byte order | Little-endian |
| Instructions | 16 bits, with 32-bit forms when a full immediate, address, or displacement is needed |
| General registers | `R0`–`R4`, plus `SP`, `SR`, and `PC` |
| Program entry | `0x0100` by convention |
| Stack | Full-descending, starts at `0xF000`, default limit `0xE000` |

"Little-endian" means that a 16-bit word is stored low byte first. The word
`0x1234` appears in memory as the bytes `34 12`. This matters when you load or
store bytes, and when you inspect a memory dump.

### 1.1 Memory map

```text
  0x0000 +----------------------------+
         | Hardware vectors           |  8 x 16-bit addresses (RAM)
  0x000F +----------------------------+
  0x0010 | BIOS jump table            |  120 x 16-bit addresses (RAM)
  0x00FF +----------------------------+
  0x0100 | General RAM                |  program, data, heap, stack
         |                            |
  0xEFFF +----------------------------+
  0xF000 | Memory-mapped I/O          |  256 bytes
  0xF0FF +----------------------------+
  0xF100 | BIOS ROM code/data         |  3,840 bytes including reset vector
  0xFFFD +----------------------------+
  0xFFFE | Reset vector               |  16-bit address
  0xFFFF +----------------------------+
```

- RAM is `0x0000`–`0xEFFF`, 60 KiB.
- The first page is `0x0000`–`0x00FF`: 16 bytes of hardware vectors and 240
  bytes of BIOS jump table.
- Normal programs begin at `0x0100`.
- Memory-mapped I/O is `0xF000`–`0xF0FF`.
- ROM is `0xF100`–`0xFFFF`, write-protected while the CPU runs.
- The stack starts at `0xF000` and grows downward. The linker's default stack
  limit is `0xE000`, giving a 4 KiB stack reserve.

The VM zeroes RAM at startup for deterministic behavior. The ROM is loaded
before execution. The reset vector at `0xFFFE` is the address at which the CPU
starts after reset.

### 1.2 The registers at a glance

| Register | Code | Conventional use |
|---|---|---|
| `R0` | 0 | Return value, scratch, low word of a pair |
| `R1` | 1 | First argument, return high word |
| `R2` | 2 | Second argument, pair base |
| `R3` | 3 | Third argument |
| `R4` / `FP` | 4 | Frame pointer; callee-saved |
| `SP` | 5 | Stack pointer; full-descending |
| `SR` | 6 | Status register: flags and interrupt enable |
| `PC` | 7 | Program counter |

`R0`–`R4` are general 16-bit registers. `SP`, `SR`, and `PC` are special but
can be read and written like registers in many instructions. `FP` is an alias
for `R4`; the assembler accepts either spelling.

The roles above are conventions, not hardwired behavior. The CPU does not
reserve `R0` for return values; the calling convention does that. The one
exception is the `MULS`/`MULU` instruction, which requires a pair base of `R0`
or `R2`.

### 1.3 What happens when a program runs

A normal `.kex` program is loaded by the VM at the addresses recorded in the
file. The VM starts the CPU at the reset vector (`0xF100` in the reference
BIOS), and the BIOS jumps to `0x0100`. The instructions at `0x0100` are your
program's entry point.

The program keeps running until it executes `HLT`, until it hits an unhandled
exception, or until the VM's cycle limit is reached. On `HLT`, the VM exits
with the low 8 bits of `R0` as the process exit status:

```text
exit status = R0 & 0xFF
```

That is why the examples in this manual often end with `MOV R0, #0` before
`HLT`, and why a program that returns 55 makes `kr16 vm` exit with status 55.

### 1.4 A first mental model

Think of the machine as three things:

1. **Registers** — eight 16-bit slots inside the CPU. Arithmetic happens on
   values in registers.
2. **Memory** — 65,536 bytes addressed by 16-bit addresses. Instructions,
   strings, variables, and the stack all live here.
3. **The program counter** — the address of the next instruction. The CPU
   fetches the instruction there, executes it, and normally moves to the next
   one.

Assembly language is a human-readable spelling of the bit patterns that tell
the CPU what to do. A *directive* tells the assembler to do something at
assembly time, such as place bytes in a section or define a constant. An
*instruction* becomes machine code that the CPU executes.

---

## 2. Your first program

We will start with the smallest useful program: print one character. This uses
only three ideas: put a value in a register, store a byte to a memory-mapped I/O
address, and halt.

### 2.1 The source file

Create a file named `first.s`:

```asm
; first.s - print one character and stop.
.text
.global _start
_start:
    MOV   R0, #'H'          ; character to print
    MOV   R1, #0xF000       ; console data register
    STB   [R1], R0          ; write one byte
    MOV   R0, #0            ; exit status 0
    HLT
```

### 2.2 Assemble, link, and run

```text
kr16 as first.s -o first.o
kr16 ld first.o -o first.kex
kr16 vm first.kex
```

Output:

```text
H
```

There is no newline after the `H`, because the program never prints one. The
process exit status is 0.

### 2.3 What each line does

| Line | Meaning |
|---|---|
| `.text` | Switch to the `.text` section, which holds executable code. |
| `.global _start` | Export `_start` so the linker can use it as the entry point. |
| `_start:` | Define a label at the current address. |
| `MOV R0, #'H'` | Put the character code for `H` (72) into `R0`. |
| `MOV R1, #0xF000` | Put the console data register address into `R1`. |
| `STB [R1], R0` | Store the low byte of `R0` at the address in `R1`. |
| `MOV R0, #0` | Set the value that `HLT` will return. |
| `HLT` | Stop the CPU. The VM exits with `R0 & 0xFF`. |

Two details are worth noticing.

First, `#` means "immediate". `MOV R0, #'H'` moves the number 72 into `R0`; it
does not load from an address called `'H'`. The `'H'` form is a character
literal, and the assembler converts it to its byte value.

Second, the console is memory-mapped at address `0xF000`. Writing a byte to
that address sends it to the VM's standard output. `STB` writes one byte;
`ST` would write a 16-bit word.

### 2.4 Exit status

The VM turns `HLT` into a process exit. `R0 & 0xFF` becomes the exit status. In
this program `R0` is 0 at the end, so the exit status is 0. If you remove the
`MOV R0, #0`, the exit status is 72 (`'H' & 0xFF`), which is legal but probably
not what you intended.

On a Unix-like shell you can inspect the status with `$?`; in PowerShell, use
`$LASTEXITCODE`.

---

## 3. Hello, World!

Printing one character is a good start, but a string needs a loop. The program
below walks a NUL-terminated string in `.rodata` and writes each character to
the console.

### 3.1 The program

```asm
; hello.s - write "Hello, World!\n" to the console.
.text
.global _start
_start:
    MOV   R1, #0xF000       ; console data register
    MOV   R2, #message      ; R2 points at the string
.next:
    LDB   R0, [R2]          ; load one character
    CMP   R0, #0            ; NUL terminator?
    BEQ   .done
    STB   [R1], R0          ; send it to the console
    INC   R2
    BRA   .next
.done:
    MOV   R0, #0
    HLT

.rodata
message:
    .asciz "Hello, World!\n"
```

### 3.2 Build and run

```text
kr16 as hello.s -o hello.o
kr16 ld hello.o -o hello.kex
kr16 vm hello.kex
```

Output:

```text
Hello, World!
```

### 3.3 How it works

The string lives in the read-only data section:

```asm
.rodata
message:
    .asciz "Hello, World!\n"
```

`.asciz` emits the characters followed by a NUL byte. The label `message` is
the address of the first character.

The code uses three pointers:

- `R1` always holds `0xF000`, the console data register.
- `R2` starts at the address of `message` and advances one byte per loop
  iteration.
- `R0` holds the current character.

The loop is:

```asm
.next:
    LDB   R0, [R2]          ; load the byte at [R2]
    CMP   R0, #0            ; compare it with zero
    BEQ   .done             ; if equal, leave the loop
    STB   [R1], R0          ; otherwise write it
    INC   R2                ; advance the pointer
    BRA   .next             ; repeat
```

`LDB` loads one byte and zero-extends it to 16 bits. `CMP R0, #0` sets the
flags for `R0 - 0`. If the result is zero, `Z=1`, and `BEQ` is taken. `BRA` is
the unconditional branch.

`INC R2` adds one to the pointer. Because this is a byte string, the pointer
advances by one byte, not one word.

### 3.4 What the assembler and linker do with `#message`

The line

```asm
MOV   R2, #message
```

does not know the final address of `message` when the assembler runs. The
assembler emits a 16-bit immediate field and a relocation record that says
"fill this in with the address of `message`". The linker decides where
`.rodata` goes, resolves the symbol, and patches the immediate.

For this program the link map is:

```text
KR16 map
entry=0x100
section .text addr=0x100 size=24
section .rodata addr=0x118 size=15
symbol _start value=0x100
symbol .next value=0x108
symbol .done value=0x114
symbol message value=0x118
symbol __heap_start value=0x128
symbol __stack_top value=0xf000
symbol __stack_limit value=0xe000
```

So `message` ends up at `0x0118`, and the linked instruction becomes:

```text
0x0104  0xF040 0x0118  MOV R2, #0x118
```

The object file, before linking, shows the placeholder `0x0000` in that field.
This is normal. It is why you should disassemble the `.kex` when you want to see
final addresses, or inspect the map file.

### 3.5 The complete disassembly

After linking, `kr16 dis hello.kex --no-hex` prints:

```text
.text:
  0x0100  MOV R1, #0xF000
  0x0104  MOV R2, #0x118
  0x0108  LDB R0, [R2]
  0x010A  CMP R0, #0x0
  0x010C  BEQ 0x0114
  0x010E  STB [R1], R0
  0x0110  INC R2
  0x0112  BRA 0x0108
  0x0114  MOV R0, #0x0
  0x0116  HLT
```

Notice that the assembler chose the shortest encoding for each instruction.
`MOV R1, #0xF000` needs the 32-bit extended form because `0xF000` does not fit
in a byte. `MOV R0, #0` fits in the 6-bit immediate form, so it is a single
16-bit instruction. `CMP R0, #0` is also a 16-bit immediate-form instruction.
`INC R2` is a single 16-bit register instruction.

---

## 4. Arithmetic, flags, and branches

Assembly arithmetic is built from a small set of operations plus the flags
register. This section introduces the pattern with a loop that sums the numbers
1 through 10.

### 4.1 The program

```asm
; loop_sum.s - sum 1..10 into R0, then halt.
.text
.global _start
_start:
    MOV   R0, #0            ; total = 0
    MOV   R1, #1            ; i = 1
    MOV   R2, #10           ; limit
.loop:
    CMP   R1, R2
    BGT   .done
    ADD   R0, R1            ; total += i
    INC   R1
    BRA   .loop
.done:
    HLT                     ; exit status = R0 & 0xFF = 55
```

Build and run:

```text
kr16 as loop_sum.s -o loop_sum.o
kr16 ld loop_sum.o -o loop_sum.kex
kr16 vm loop_sum.kex
```

There is no console output. The exit status is 55:

```text
55
```

### 4.2 How the loop works

`CMP R1, R2` computes `R1 - R2` and sets the flags, but does not store the
result. `BGT` means "branch if greater than" in the signed sense. It reads the
`Z`, `N`, and `V` flags; it does not compare registers itself.

`ADD R0, R1` adds `R1` to `R0` and stores the result in `R0`. `INC R1` adds one
to `R1`. `BRA` jumps back unconditionally.

The loop executes with `i` = 1 through 10. When `i` becomes 11, `CMP` sets the
flags so that `BGT` is taken and the loop exits. `R0` then contains 55.

### 4.3 Signed and unsigned branches

The same comparison can be interpreted two ways:

- `BGT`/`BGE`/`BLT`/`BLE` treat the operands as signed 16-bit numbers.
- `BHI`/`BCS`/`BCC`/`BLS` treat the operands as unsigned 16-bit numbers.

For the positive values in this example either interpretation gives the same
answer. It matters when values have bit 15 set: `0xFFFF` is `-1` signed and
`65535` unsigned.

The condition codes are covered in detail in
[Section 13](#13-programmers-reference-the-flags-register).

### 4.4 Why flags matter

Most arithmetic and comparison instructions set four flags:

- `Z` — the result was zero.
- `N` — bit 15 of the result was set (negative if interpreted as signed).
- `C` — carry out of an addition, or "no borrow" for a subtraction.
- `V` — signed overflow.

`MOV`, loads, stores, and `LEA` do **not** change the flags. A common beginner
mistake is:

```asm
MOV   R0, #0
BEQ   somewhere          ; WRONG: MOV did not set Z
```

`MOV` leaves the flags alone. To test a value, use `CMP` or `TEST`:

```asm
MOV   R0, #0
CMP   R0, #0
BEQ   somewhere          ; correct: CMP set Z
```

---

## 5. Functions, the stack, and frames

A function call needs a return address. KR16 uses the stack for that, and the
standard calling convention uses `R1`–`R3` for the first three arguments and
`R0` for the return value.

### 5.1 A function with locals

This program computes `1 + 2 + ... + n` with a real function, a frame pointer,
and local variables:

```asm
; sum_to.s - a complete function with a frame and locals.
; int sum_to(int n) {
;     int total = 0;
;     for (int i = 1; i <= n; i++) total += i;
;     return total;
; }
.text
.global _start
_start:
    MOV   R1, #10           ; argument n
    CALL  sum_to
    HLT                     ; exit status = 55

sum_to:
    PUSH  R4                ; save caller's FP
    MOV   R4, SP            ; establish our frame
    SUB   SP, #6            ; three local words: n, total, i
    ST    [R4-2], R1        ; n     = argument
    MOV   R0, #0
    ST    [R4-4], R0        ; total = 0
    MOV   R0, #1
    ST    [R4-6], R0        ; i     = 1
.loop:
    LD    R0, [R4-6]        ; R0 = i
    LD    R1, [R4-2]        ; R1 = n
    CMP   R0, R1
    BGT   .done             ; while (i <= n)
    LD    R1, [R4-4]        ; R1 = total
    ADD   R1, R0            ; total += i
    ST    [R4-4], R1
    INC   R0                ; i++
    ST    [R4-6], R0
    BRA   .loop
.done:
    LD    R0, [R4-4]        ; return total
    MOV   SP, R4            ; discard locals
    POP   R4                ; restore caller's FP
    RET
```

Build and run:

```text
kr16 as sum_to.s -o sum_to.o
kr16 ld sum_to.o -o sum_to.kex
kr16 vm sum_to.kex
```

The exit status is 55.

### 5.2 The call sequence

At the call site:

```asm
MOV   R1, #10
CALL  sum_to
HLT
```

`CALL sum_to` pushes the address of the instruction after the call — here the
`HLT` — onto the stack, then sets `PC` to `sum_to`. `RET` pops that address back
into `PC`.

The callee starts with:

```asm
PUSH  R4
MOV   R4, SP
SUB   SP, #6
```

`PUSH R4` saves the caller's frame pointer. `MOV R4, SP` makes `R4` point at
that saved value. `SUB SP, #6` reserves three words for locals. The stack now
looks like this:

```text
higher addresses
+----------------------+
| caller's frame       |
+----------------------+
| return address       |  [R4+2]
+----------------------+
| saved caller FP      |  [R4+0]  <- R4 / FP
+----------------------+
| n                    |  [R4-2]
| total                |  [R4-4]
| i                    |  [R4-6]
+----------------------+
| ...                  |  <- SP
lower addresses
```

The epilogue reverses the setup:

```asm
MOV   SP, R4            ; drop all locals and temporaries
POP   R4                ; restore caller's FP
RET                     ; return to caller
```

Using `MOV SP, R4` rather than `ADD SP, #6` is deliberate: it also discards any
temporary values the function may have pushed.

### 5.3 Why `R4` is useful

Every local is at a fixed offset from `R4`, even while the function pushes and
pops temporaries. Without a frame pointer you would have to track the stack
pointer's current offset by hand. With `R4`, `n` is always `[R4-2]`, `total` is
always `[R4-4]`, and so on.

The frame pointer is also what the debugger uses to walk the call stack:
`[R4]` is the previous `R4`, and `[R4+2]` is the return address. The outermost
frame conventionally has `R4 = 0` to terminate the chain.

### 5.4 Arguments beyond the first three

The first three arguments go in `R1`, `R2`, and `R3`. Additional arguments are
pushed on the stack from right to left. The first stack argument is at
`[FP+4]`, after the saved FP at `[FP]` and the return address at `[FP+2]`.

```asm
; int f(int a, int b, int c, int d, int e)
; a=R1, b=R2, c=R3, d=[FP+4], e=[FP+6]
.text
.global _start
_start:
    MOV   R1, #1            ; a
    MOV   R2, #2            ; b
    MOV   R3, #3            ; c
    MOV   R0, #5            ; e (rightmost argument first)
    PUSH  R0
    MOV   R0, #4            ; d
    PUSH  R0
    CALL  f
    ADD   SP, #4            ; caller removes d and e
    HLT

f:
    PUSH  R4
    MOV   R4, SP
    LD    R0, [R4+4]        ; d
    LD    R2, [R4+6]        ; e
    ADD   R0, R2
    MOV   SP, R4
    POP   R4
    RET
```

The caller cleans up stack arguments with `ADD SP, #4`. This program's exit
status is 9 (`4 + 5`).

### 5.5 The rules to remember

- **Arguments:** `R1`, `R2`, `R3`; more on the stack, right to left.
- **Return value:** `R0` (with `R1` holding the high word for a 32-bit value).
- **Caller-saved:** `R0`–`R3`, flags, `SR`.
- **Callee-saved:** `R4` (`FP`) and `SP`.
- **Stack alignment:** keep `SP` even. All pushes and pops move by two bytes.
- **Cleanup:** the caller removes stack arguments; the callee restores `SP` and
  `R4`.
- **Interrupt handlers:** the CPU saves `SR` and `PC`; the handler must save and
  restore any other registers it uses, and return with `RETI`.

The full stack and frame reference is in
[Section 12](#12-programmers-reference-the-stack-and-calling-convention).

---

## 6. Writing assembly source

This section describes the lexical rules and the common operand forms. Once you
know these, the instruction reference is mostly a matter of looking up names and
encodings.

### 6.1 Line layout

A source line has this general shape:

```text
[label:] [mnemonic [operands]] [; comment]
```

Examples:

```asm
start:                      ; a label on its own line
    MOV   R0, #1            ; an instruction
    .word 0x1234            ; a directive
```

- A label must end with a colon.
- A label can share a line with an instruction: `loop: DEC R1`.
- Instructions and directives may be indented; indentation is cosmetic.
- One instruction or directive per line.
- Blank lines are ignored.

### 6.2 Comments

Two comment forms are accepted:

```asm
; This is a comment.
MOV R0, #1      // This is also a comment.
```

`#` is **not** a comment character. It introduces an immediate operand. A `;`
or `//` inside a quoted string does not start a comment.

### 6.3 Labels and symbols

A label names an address. Symbol names may contain letters, digits, `_`, and
`.`, and may not begin with a digit. Labels and symbols are **case-sensitive**:

```asm
Loop:
    BRA Loop        ; correct
    BRA loop        ; a different symbol: undefined
```

Mnemonics and register names are case-insensitive:

```asm
mov r0, #1
MOV R0, #1
```

Both are the same instruction. `R0` and `r0` are the same register. `FP` is an
alias for `R4`.

A label beginning with `.L` is still an ordinary label in this implementation.
It is not scoped to a function and it is not automatically hidden from the
linked image. Use unique names.

### 6.4 Operand order

KR16 uses Intel-style operand order: **destination first**.

```asm
ADD   R0, R1            ; R0 = R0 + R1
LD    R0, [R1+4]        ; load into R0 from memory
ST    [R1+4], R0        ; store R0 to memory
```

The destination is the register that is written. For stores, the memory
operand is written, so it comes first.

### 6.5 Register names

| Spelling | Register |
|---|---|
| `R0`, `R1`, `R2`, `R3`, `R4` | General registers |
| `FP` | Alias for `R4` |
| `SP` | Stack pointer |
| `SR` | Status register |
| `PC` | Program counter |

Register names are case-insensitive. There are no byte or 32-bit register
names; all registers are 16 bits.

### 6.6 Immediates and memory operands

An immediate is written with `#`:

```asm
MOV   R0, #42
ADD   R1, #-1
CMP   R2, #0x1000
```

A memory operand is written in brackets:

```asm
LD    R0, [R1]          ; address = R1
LD    R0, [R1+4]        ; address = R1 + 4
LD    R0, [R1-2]        ; address = R1 - 2
LD    R0, [0xF004]      ; absolute address
```

There is no auto-increment or auto-decrement addressing. To walk an array, use
an explicit `INC`, `DEC`, `ADD`, or `SUB` on the pointer register.

### 6.7 Numbers and character literals

Numeric literals may be written in several bases:

```asm
42              ; decimal
0x2A            ; hexadecimal
0b101010        ; binary
0o52            ; octal
'A'             ; character literal: 65
'\n'            ; newline: 10
```

Character literals support the escapes `\n`, `\t`, `\r`, `\\`, `\'`, `\"`, and
`\0`. A character literal is an integer value, not a string.

String literals are used with `.ascii` and `.asciz`:

```asm
.asciz "Hello, World!\n"
```

String escapes are the same family: `\n`, `\t`, `\r`, `\\`, and `\"`.

### 6.8 Expressions

Expressions may use:

| Operators | Meaning |
|---|---|
| `+`, `-`, `*`, `/`, `%` | Arithmetic; division is integer division |
| `&` | Bitwise AND |
| `^` | Bitwise XOR |
| `<<`, `>>` | Shifts |
| `~` | Bitwise complement |
| unary `+`, `-` | Sign |
| `(...)` | Grouping |

The bitwise operators are `&` (AND), `|` (OR), and `^` (XOR).

Precedence, from highest to lowest, is: unary `+`, `-`, `~`; then `*`, `/`,
`%`; then `+`, `-`; then `<<`, `>>`; then `&`; then `^`; then `|`. Parentheses
override the default precedence.

There are no relational or logical operators (`==`, `<`, `&&`, `||`). For
conditional assembly, use arithmetic: zero is false, nonzero is true.

The current location is written `.`:

```asm
here:
    BRA .               ; branch to itself: an infinite loop
```

`low(expr)` returns the low 16 bits of a constant, and `high(expr)` returns
bits 16–31:

```asm
.equ ADDR, 0x12345678
.word low(ADDR)         ; 0x5678
.word high(ADDR)        ; 0x1234
```

Symbolic expressions are deliberately limited. You may add or subtract a
constant from a symbol:

```asm
.word message+2
MOV   R0, #table+4
```

You may not add two symbols, subtract two symbols, or negate a symbol. Those
forms produce an "expression is too complex" diagnostic.

The `.equ` constants are resolved at assembly time. They are not link-time
symbols and do not appear in the object's symbol table.

### 6.9 Sections

Code and data live in named sections. The standard ones are:

| Section | Purpose |
|---|---|
| `.text` | Executable code |
| `.rodata` | Read-only constants and strings |
| `.data` | Initialized read/write data |
| `.bss` | Zero-initialized data (occupies no file bytes) |
| `.crt0` | Optional startup code placed before `.text` |

A custom section can be created with `.section name`. Sections have flags and
alignment, and the linker combines sections with the same name.

The default section is `.text`, so a file that starts with instructions without
a `.text` directive still assembles. It is clearer to write `.text` explicitly.

### 6.10 Shortest-form encoding

The assembler is a two-pass assembler. It chooses the smallest legal encoding
for each instruction:

- An ALU immediate uses the 6-bit form if the value is in `-32..31`; otherwise
  it uses a 32-bit extended form.
- A `MOV` immediate uses the 6-bit form, then the 8-bit form, then the 32-bit
  form, depending on the value.
- A load or store displacement uses the 6-bit indexed form if it is in
  `-32..31`; otherwise it uses a 32-bit indexed form, if one exists.
- A `Bcc` branch is always a 16-bit conditional branch.
- `JMP label` and `CALL label` are always 32-bit relative forms.

This is why the disassembly of a simple program shows a mix of 2-byte and
4-byte instructions. It is also why a value such as `MOV R0, #0x12` assembles
to the 6-bit immediate form, not the 8-bit form: the 6-bit form is smaller.

### 6.11 Relocations and symbols

When an instruction refers to a symbol whose final address is not known yet, the
assembler emits a relocation. The linker fills it in.

Common cases:

- `MOV Rd, #symbol` — absolute 16-bit address.
- `.word symbol` — absolute 16-bit address in data.
- `LD Rd, [symbol]` — absolute 16-bit address.
- `JMP symbol` / `CALL symbol` — PC-relative 16-bit word displacement.

A symbol defined in another object must be declared with `.extern`:

```asm
.extern putchar
```

A symbol you want other objects to see must be exported with `.global`:

```asm
.global putchar
putchar:
    ...
```

**Important:** the assembler rejects an undeclared symbol with
`undefined symbol 'name'`. Always write `.extern` for every symbol that is
defined in another file; a declared symbol still becomes a clear linker error
if no object defines it.

### 6.12 Labels and the entry point

The linker chooses an entry point in this order:

1. A global symbol named `_start`.
2. A global symbol named `__start`.
3. A global symbol named `start`.
4. Otherwise, `0x0100`.

For a normal single-file program, put `.global _start` and place `_start` at the
beginning of `.text`. The linker will then place `.text` at `0x0100` and record
`0x0100` as the entry.

### 6.13 What is not in the language

The following are not part of this implementation. Knowing them saves time:

- `.skip` — use `.space` or `.zero`.
- `$` as the current-location symbol — use `.`.
- Local label scopes (`.L` labels are ordinary global-to-the-file labels).
- Automatic branch relaxation for `Bcc` — an out-of-range branch is an error.
- Cross-section `Bcc` targets — use `JMP` if the target is in another section.
- Scaled indexing such as `[R1+R2*2]`, auto-increment, and double indirection.
- Memory-to-memory operations.
- `ROL`/`ROR`.
- `LEA Rd, [symbol]` for an absolute symbol — use `MOV Rd, #symbol`.
- `.-symbol` in `.size` — use a constant size.

These restrictions are also listed with workarounds in
[Appendix D](#appendix-d-troubleshooting-and-glossary).

---

## 7. Assembler directives

Directives begin with a dot. They do not become machine instructions; they tell
the assembler how to build the object file. Directive names are
case-insensitive.

### 7.1 Directive summary

| Directive | What it does |
|---|---|
| `.text` | Switch to the executable code section |
| `.crt0` | Switch to the startup code section |
| `.rodata` | Switch to the read-only data section |
| `.data` | Switch to the initialized read/write data section |
| `.bss` | Switch to the zero-initialized data section |
| `.section name` | Switch to or create a named section |
| `.byte expr, ...` | Emit 8-bit values |
| `.word expr, ...` | Emit 16-bit little-endian values |
| `.long expr, ...` | Emit 32-bit little-endian values |
| `.ascii "text", ...` | Emit string bytes without a terminator |
| `.asciz "text", ...` | Emit string bytes followed by a NUL |
| `.space n` | Reserve `n` zero bytes |
| `.zero n` | Same as `.space n` |
| `.align n` | Align the current location to an `n`-byte boundary |
| `.balign n` | Alias for `.align n` |
| `.p2align n` | Align to a `2^n`-byte boundary |
| `.equ name, expr` | Define an assembly-time constant |
| `.set name, expr` | Define or redefine an assembly-time constant |
| `.global name, ...` | Export symbols |
| `.globl name, ...` | Alias for `.global` |
| `.extern name, ...` | Declare symbols defined in another file |
| `.type symbol, @function` | Set symbol type metadata |
| `.type symbol, @object` | Set symbol type metadata |
| `.size symbol, expr` | Set symbol size metadata |
| `.file "path"` | Add a source file to the debug file table |
| `.file index "path"` | Add a source file at a specific index |
| `.loc file, line, [column]` | Map following instructions to a source position |
| `.include "path"` | Textually include another source file |
| `.macro name params` | Begin a parameter-substitution macro |
| `.endm` | End a macro |
| `.if expr` | Begin conditional assembly |
| `.else` | Alternative branch of `.if` |
| `.endif` | End conditional assembly |
| `.org expr` | Set the absolute output address (raw mode only) |

### 7.2 Section directives

```asm
.text                   ; executable code
.rodata                 ; read-only constants
.data                   ; initialized read/write data
.bss                    ; zero-initialized data
.crt0                   ; optional startup code
.section .mydata        ; custom section
```

`.text`, `.crt0`, `.rodata`, `.data`, and `.bss` are the standard sections. The
linker gives them their usual flags:

- `.text` and `.crt0`: allocated, executable.
- `.rodata`: allocated, read-only.
- `.data`: allocated, writable, initialized.
- `.bss`: allocated, writable, no file data (NOBITS).

A custom section created with `.section` is allocated by default. A name
beginning with `.debug` is treated as debug information.

The `.crt0` section is placed before `.text` by the linker. It is available for
startup code, but most small programs do not need it.

### 7.3 Data directives

```asm
.byte   0x12, 0x34, 255         ; three bytes
.word   0x1234, 0xABCD          ; two 16-bit words
.long   0x12345678              ; one 32-bit value
.ascii  "KR16"                  ; four bytes, no terminator
.asciz  "ready\n"               ; bytes plus a NUL
```

The values are emitted in little-endian order:

```asm
.word 0x1234            ; bytes: 34 12
.long 0x12345678        ; bytes: 78 56 34 12
```

`.word` may hold a symbol, in which case the assembler emits an absolute 16-bit
relocation:

```asm
.word message
.word table+4
```

`.byte` and `.long` cannot hold a symbol. `.byte` truncates the value to its low
8 bits. `.long` emits a full 32-bit value.

Strings may contain the usual escapes:

```asm
.asciz "line one\nline two\n"
.ascii "no terminator"
```

Multiple strings can be given to `.ascii` and `.asciz`:

```asm
.asciz "one", "two"     ; "one\0two\0"
```

### 7.4 Reserving space

```asm
.space 32               ; reserve 32 zero bytes
.zero  8                ; same as .space 8
```

In `.text`, `.rodata`, `.data`, and custom PROGBITS sections, the bytes are
present in the file and are zero. In `.bss`, the reservation increases the
section's NOBITS size and does not add bytes to the file.

Use `.space` or `.zero` for `.bss`, not `.byte` or `.word`:

```asm
.bss
buffer:
    .space 64           ; correct: 64 bytes of zero-initialized storage
```

Emitting data into a NOBITS section is an error.

### 7.5 Alignment

```asm
.align 2                ; next location is a multiple of 2
.align 4                ; next location is a multiple of 4
.balign 8               ; same as .align 8
.p2align 5              ; next location is a multiple of 2^5 = 32
```

`.align n` means an `n`-byte boundary, not a power-of-two exponent. `.p2align n`
means `2^n`. The assembler fills the gap with zero bytes. In `.bss`, it increases
the NOBITS size.

Alignment is relative to the start of the current section. The section itself
is placed by the linker with at least its recorded alignment (normally 2), so a
`.align 4` inside a section does not necessarily make the final absolute address
a multiple of 4. For instruction addresses, the important requirement is that
they are even; the assembler and linker maintain that.

### 7.6 Constants: `.equ` and `.set`

```asm
.equ CONSOLE, 0xF000
.equ NEWLINE, 10
.equ MASK, 0x00FF

.set RETRIES, 3
.set RETRIES, 4         ; .set may be redefined
```

The expression must be a constant at the point of definition. Forward
references are not resolved for `.equ`:

```asm
.equ A, B+1             ; error if B is not defined yet
.equ B, 5
```

Constants are substituted into expressions; they are not symbols and do not
appear in the object file. Use `.equ` for readability:

```asm
.equ CONSOLE, 0xF000
...
MOV R1, #CONSOLE
STB [R1], R0
```

### 7.7 Exporting and importing symbols

```asm
.global _start
.global putchar, puts

.extern getchar
.extern memcpy
```

`.global` (or `.globl`) marks symbols as globally visible. It can appear before
or after the label. `.extern` declares a symbol that is defined in another
object.

```asm
.text
.global _start
.extern putchar

_start:
    MOV   R1, #'A'
    CALL  putchar
    MOV   R0, #0
    HLT
```

Do not use `.extern` for a symbol that is defined in the same file; the
assembler will see a duplicate definition.

**Remember:** use `.extern` for every cross-file reference. The assembler now
reports an undeclared symbol as `undefined symbol 'name'` instead of encoding a
zero displacement.

### 7.8 Symbol metadata: `.type` and `.size`

```asm
.global sum_to
.type sum_to, @function
sum_to:
    ...
.size sum_to, 20
```

`.type` accepts `@function`, `@object`, or the same names without the `@`.
`.size` sets the symbol's size metadata. Both are optional and are mainly for
debuggers and tools.

The `.size` expression must be a constant. The common `.-symbol` idiom is not
accepted in this release:

```asm
.size sum_to, .-sum_to  ; error: symbolic expression is too complex
```

Use a literal or a constant expression instead.

### 7.9 Debug information: `.file` and `.loc`

```asm
.file 0 "hello.s"
.text
_start:
.loc 0, 7, 5
    MOV   R0, #1
.loc 0, 8, 5
    ADD   R0, #2
.loc 0, 9, 5
    HLT
```

`.file` adds a path to the debug file table. An optional numeric index selects
the slot:

```asm
.file 0 "hello.s"
.file 1 "helpers.s"
```

`.loc` maps the instructions that follow it to a file index, line, and optional
column. The assembler emits a `.debug_line` section. `kr16 debug --lines` prints
the mapping:

```text
line table (.debug_line)
  0x0100  file 0:7:5
  0x0102  file 0:8:5
  0x0104  file 0:9:5
```

The `-g` option is accepted by `kr16 as`. In this release, the line table is
generated from `.loc` directives; you do not need `-g` for `.loc` to work.

### 7.10 Conditional assembly

```asm
.equ DEBUG, 1

.if DEBUG
    MOV R0, #1
.else
    MOV R0, #0
.endif
```

The condition is an assembly-time constant expression. Zero is false; nonzero
is true. `.if` blocks can be nested. There is one `.else` per `.if`; there is no
`.elseif`. The skipped lines are not assembled, but they are still scanned for
macro expansion.

### 7.11 Macros

```asm
.macro emit_char value
    MOV   R0, #\value
    MOV   R3, #0xF000
    STB   [R3], R0
.endm

.text
_start:
    emit_char 'O'
    emit_char 'K'
```

A macro is a simple parameter-substitution text macro. The body is copied at
each invocation, and each `\parameter` token is replaced by the corresponding
argument. Macro names are case-insensitive; the parameters are matched by name.

Rules and limitations:

- Define the macro before you use it.
- Parameters are separated by commas in the `.macro` line and in the call.
- `.endm` ends the macro.
- The number of arguments must match the number of parameters.
- Macro expansion is recursive up to a depth of 32.
- There is no local-label scope inside a macro. If a macro contains a label and
  you invoke it twice, the label is duplicated. Use a parameter to build unique
  names, or avoid labels in macros.

### 7.12 Include files

```asm
.include "macros.inc"
.include "constants.inc"
```

`.include` is a textual include processed before assembly. A relative path is
resolved relative to the including file's directory. If the file is not found
there, the assembler searches the directories given with `-I`:

```text
kr16 as main.s -o main.o -I include
```

Includes can be nested. There are no include guards; guard repeated includes
with `.if`/`.endif` if necessary.

### 7.13 `.org`

```asm
.org 0xF100
bios_start:
    JMP #0x0100

.org 0xFFFE
    .word bios_start
```

`.org` sets the absolute output address. It is allowed only in raw mode
(`kr16 as ... --raw --base addr`). The target must be at or after the base and
at or before `0xFFFF`. It cannot move backwards. The gap between the current
offset and the target is filled with zeros.

`.org` is primarily for ROM images and absolute layouts. Relocatable code uses
sections and the linker instead.

### 7.14 A complete directives example

```asm
; directives.s - exercises the data and metadata directives.
.equ CONSOLE, 0xF000
.equ NEWLINE, 10
.equ MASK, 0x00FF

.text
.global _start
_start:
    MOV  R1, #CONSOLE
    MOV  R0, #'H'
    STB  [R1], R0
    MOV  R0, #NEWLINE
    STB  [R1], R0
    MOV  R0, #0
    HLT

.rodata
.align 2
banner:
    .ascii "KR16"           ; four bytes, no terminator
    .byte  0                ; explicit terminator
    .asciz "ready\n"
    .word  CONSOLE          ; 16-bit word
    .long  0x12345678       ; 32-bit long
    .space 4                ; four zero bytes

.data
.align 4
counter:
    .word 0

.bss
.align 2
scratch:
    .space 32
```

This program prints `H` followed by a newline. The map shows the section
layout and the final addresses:

```text
section .text   addr=0x100 size=16
section .rodata addr=0x110 size=22
section .data   addr=0x126 size=2
section .bss    addr=0x128 size=32
```

The `.rodata` size is 22: four bytes of `KR16`, one NUL, six bytes of
`ready\n`, two bytes of `.word`, four bytes of `.long`, and four bytes of
`.space`. The `.data` word is separate, and `.bss` contributes no file bytes.

---

## 8. From source to runnable `.kex`

The KR16 toolchain is deliberately split into small steps. The important
pipeline is:

```text
hello.s  --kr16 as-->  hello.o  --kr16 ld-->  hello.kex  --kr16 vm-->  output
```

- `.s` is assembly source.
- `.o` is a relocatable object: code, data, symbols, and relocations whose final
  addresses are not yet known.
- `.kex` is an executable container: sections with final addresses, an entry
  point, symbols, and no relocations.
- `kr16 vm` loads the `.kex` and runs it.

You cannot normally run a `.o` directly. You can inspect it with `kr16 dis`, but
it is not a finished program.

### 8.1 Step 1: assemble

```text
kr16 as hello.s -o hello.o
```

If you omit `-o`, the default output is `a.o`.

The assembler reads the source, expands includes and macros, evaluates
constants, lays out sections, defines symbols, encodes instructions, and writes
a relocatable KR16 container. If there are errors, it prints diagnostics in the
form:

```text
file:line:column: message
```

For example:

```text
hello.s:6:1: expected [memory] operand
```

The assembler stops at the first error. The exit status is 1 on error, 0 on
success.

Useful assembler options:

| Option | Meaning |
|---|---|
| `-o path` | Output file |
| `--raw` | Emit a flat binary instead of an object |
| `--base addr` | Base address for raw output and `.org` |
| `-I dir` | Add an include search directory |
| `-g` | Accepted for toolchain compatibility; debug lines come from `.loc` |
| `--help` | Print usage |

### 8.2 Step 2: inspect the object (optional)

```text
kr16 dis hello.o --no-hex
```

This shows the code with section-relative addresses and labels. Addresses are
usually offsets from zero because the linker has not placed the section yet.
Relocation fields are shown as zero. This is a useful way to check that the
assembler chose the encodings you expected, but it is not the final program.

### 8.3 Step 3: link

```text
kr16 ld hello.o -o hello.kex
```

If you omit `-o`, the default output is `a.kex`.

The linker:

1. Merges sections with the same name.
2. Lays out `.crt0` (if present), `.text`, `.rodata`, `.data`, `.bss`, and any
   custom sections.
3. Resolves symbols and applies relocations.
4. Records the entry point.
5. Writes an executable container with final addresses.

Useful linker options:

| Option | Meaning |
|---|---|
| `-o path` | Output file |
| `--map path` | Write a map of sections and symbols |
| `--strip-debug` | Remove debug sections |
| `--strip` | Remove debug sections and most symbols |
| `--help` | Print usage |

The map is the easiest way to see final addresses:

```text
kr16 ld hello.o -o hello.kex --map hello.map
```

```text
KR16 map
entry=0x100
section .text addr=0x100 size=24
section .rodata addr=0x118 size=15
symbol _start value=0x100
symbol .next value=0x108
symbol .done value=0x114
symbol message value=0x118
symbol __heap_start value=0x128
symbol __stack_top value=0xf000
symbol __stack_limit value=0xe000
```

The linker defines three convenience symbols:

- `__heap_start` — the first address after the loaded image, aligned to 2.
- `__stack_top` — `0xF000`.
- `__stack_limit` — `0xE000` by default.

The default stack reserve is 4 KiB. The linker does not currently expose a
command-line option to change it.

### 8.4 Step 4: run

```text
kr16 vm hello.kex
```

The VM loads the allocatable sections, zeroes RAM, sets up the stack, loads the
BIOS, and starts the CPU. Console output goes to standard output. Diagnostic
output such as `--trace` or `--stats` goes to standard error.

On `HLT`, the process exit status is `R0 & 0xFF`.

Useful VM options:

| Option | Meaning |
|---|---|
| `--raw` | Load a flat binary instead of a container |
| `--base addr` | Load address for a raw binary |
| `--bios path` | Load a custom 3.75 KiB BIOS ROM |
| `--no-bios` | Start directly at the image entry point |
| `--max-cycles n` | Stop after `n` simulated cycles |
| `--input path` | Feed scripted bytes to the keyboard |
| `--trace` | Print a line for each executed instruction |
| `--trace-syscalls` | Print host system calls |
| `--stats` | Print cycle and instruction counts |
| `--realtime` | Accepted; real-time pacing is not implemented in this release |
| `--mhz n` | Accepted with `--realtime`; currently no effect |
| `-- args...` | Program arguments after a literal `--` |

A complete run:

```text
kr16 as hello.s -o hello.o
kr16 ld hello.o -o hello.kex
kr16 vm hello.kex
```

Output:

```text
Hello, World!
```

### 8.5 Step 5: inspect size and debug information

```text
kr16 size hello.kex
```

```text
   text    data     bss     dec     hex  file
     24      15       0      39      27  hello.kex
```

`text` counts executable allocated bytes. `data` counts initialized
non-executable bytes, including `.rodata` and `.data`. `bss` counts
zero-initialized storage, which is not stored in the file.

If the source has `.file` and `.loc` directives, you can inspect the line table:

```text
kr16 debug hello.kex --lines
```

Without `.loc`, the output is `no line table`.

### 8.6 Layout and entry point

For a normal image the linker places:

1. `.crt0`, if present, at `0x0100`.
2. `.text` immediately after, or at `0x0100` if there is no `.crt0`.
3. `.rodata`, then `.data`, then `.bss`, each aligned.
4. Custom allocated sections after the standard sections.
5. `__heap_start` after the image.

The entry point is the first global symbol named `_start`, `__start`, or
`start`; otherwise it is `0x0100`. In a single-file program, the usual setup is:

```asm
.text
.global _start
_start:
    ...
```

If you do not define one of those symbols, the first instruction at `0x0100`
still runs, but the entry is implicit. Being explicit is clearer and survives
moving the start label.

### 8.7 Raw binaries

For a BIOS, ROM, or a simple loader, you may want a flat binary instead of a
container:

```text
kr16 as program.s -o program.bin --raw --base 0x0100
kr16 vm program.bin --raw --base 0x0100
```

In raw mode:

- No object file is written; the bytes are emitted in order.
- Section boundaries, symbols, and relocations are not available.
- `.org` is allowed.
- The base address is used for `.org`, labels, and branch calculations.
- External symbols cannot be used; there is no linker to resolve them.

A raw image is loaded at `--base`. It has no header and no entry field. For a
normal program, use `--base 0x0100`. For a BIOS ROM, use `--base 0xF100`, as
described in [Appendix B](#appendix-b-the-kr16-bios).

### 8.8 Common build problems

| Symptom | Likely cause and fix |
|---|---|
| `cannot open hello.s` | Wrong path or file name. |
| `unknown instruction` | Typo, or the instruction is not in the ISA. |
| `expected [memory] operand` | Use brackets: `LD R0, [R1]`, not `LD R0, R1`. |
| `LEA has no absolute form` | `LEA` needs `[Rs+disp]`; use `MOV Rd, #symbol` for an absolute address. |
| `byte indexed form requires a 6-bit displacement` | `LDB`/`LDSB`/`STB` with an index outside `-32..31`; load the displacement into a register or use an absolute byte access. |
| `branch target out of range` | `Bcc` reaches only `-256..+254` bytes; use `JMP` or restructure. |
| `external branch target is not supported` | Use `JMP`/`CALL` for external targets; `Bcc` must be local and in the same section. |
| Branch to a label in another section | `Bcc` has no relocation; use `JMP` instead. |
| `undefined symbol foo` | Add `.extern foo`, check spelling, or link the object that defines it. |
| `duplicate global symbol foo` | Two objects define the same global. |
| `no .text section` | Add a `.text` section with code. |
| `.org is only allowed in raw mode` | Use `--raw`, or use sections for relocatable code. |
| `cannot emit data in a NOBITS section` | Use `.space`/`.zero` in `.bss`, not `.byte`/`.word`. |
| `symbolic expression is too complex` | Only `symbol + constant` and `symbol - constant` are supported. |
| `raw assembly cannot use external symbols` | Define the symbol in the same raw file, or use object/link mode. |
| VM: `unhandled exception 0` | Illegal instruction, often a fall-through into data; add `HLT`. |
| VM: `unhandled trap` | `TRAP #n` read a zero jump-table entry. |
| VM: `maximum cycle count reached` | The program did not halt; check loops and `HLT`. |
| `no line table` | Add `.file` and `.loc` directives. |

---

## 9. The disassembler

`kr16 dis` decodes KR16 instructions from an object, an executable, or a raw
binary. It is the quickest way to see what the assembler actually emitted and
what the linker placed in the final image.

### 9.1 Usage

```text
kr16 dis file.o|file.kex [options]
```

Options:

| Option | Meaning |
|---|---|
| `--raw` | Treat the input as a flat binary |
| `--base addr` | Address to use for a raw image (and for sections with address 0) |
| `--no-hex` | Omit the encoded machine words |
| `--source` | Accepted for compatibility; currently no effect |
| `--symbols` | Accepted for compatibility; currently no effect |
| `--help` | Print usage |

The disassembler reads only executable allocated sections. It skips data and
debug sections and does not decode `.bss`.

### 9.2 Output format

A normal line looks like this:

```text
  0x0100  0xF020 0xF000  MOV R1, #0xF000
  ^       ^      ^       ^
  |       |      |       instruction
  |       |      second word (only for a 32-bit instruction)
  |       first machine word
  address
```

- The address is four hexadecimal digits.
- A 16-bit instruction shows one machine word and spaces where the second word
  would be.
- A 32-bit instruction shows two machine words.
- An invalid encoding is printed as `.word 0xXXXX`.
- A section header such as `.text:` is printed before that section's code.

With `--no-hex`, the machine words are omitted and only the address and
instruction remain.

### 9.3 Disassembling an object

```text
kr16 dis hello.o
```

```text
.text:
_start:
  0x0000  0xF020 0xF000  MOV R1, #0xF000
  0x0004  0xF040 0x0000  MOV R2, #0x0
.next:
  0x0008  0x4080       LDB R0, [R2]
  0x000A  0x1080       CMP R0, #0x0
  0x000C  0x7003       BEQ .done
  0x000E  0x6040       STB [R1], R0
  0x0010  0x048C       INC R2
  0x0012  0x7EFA       BRA .next
.done:
  0x0014  0x1180       MOV R0, #0x0
  0x0016  0x8800       HLT
```

This is useful for checking encodings and labels. Two things are not final:

- Addresses are section-relative offsets, starting at 0.
- The `MOV R2, #0x0` field is a relocation placeholder. The linker will replace
  it with the address of `message`.

The branch targets and labels are resolved within the object, so `BEQ .done` and
`BRA .next` are readable.

### 9.4 Disassembling a linked `.kex`

```text
kr16 dis hello.kex --no-hex
```

```text
.text:
  0x0100  MOV R1, #0xF000
  0x0104  MOV R2, #0x118
  0x0108  LDB R0, [R2]
  0x010A  CMP R0, #0x0
  0x010C  BEQ 0x0114
  0x010E  STB [R1], R0
  0x0110  INC R2
  0x0112  BRA 0x0108
  0x0114  MOV R0, #0x0
  0x0116  HLT
```

Now the relocation is resolved: `message` is `0x0118`, and the branch targets
are absolute addresses.

In this release, labels are printed when disassembling a relocatable `.o` but
not when disassembling a linked `.kex`. Linked images are printed numerically.
Use the linker map to correlate addresses with symbol names:

```text
kr16 ld hello.o -o hello.kex --map hello.map
```

Then read `hello.map`. `kr16 debug --lines` can also show source lines if the
program was assembled with `.file`/`.loc`.

### 9.5 Disassembling a raw binary

A raw binary has no section headers or symbols, so you must tell the
disassembler where it will be loaded:

```text
kr16 dis hello.bin --raw --base 0x0100
```

```text
  0x0100  0xF020 0xF000  MOV R1, #0xF000
  0x0104  0xF040 0x0118  MOV R2, #0x118
  ...
```

The same binary at the wrong base gives plausible but wrong addresses. Always
pass the load address that matches the image.

The disassembler does not know where code ends and data begins in a raw image.
It decodes every word until the end of the file. For a ROM with zero padding,
the padding appears as instructions:

```text
  0xF100  MOV PC, #0x100
  0xF104  ADD R0, R0
  0xF106  ADD R0, R0
```

`0x0000` decodes as `ADD R0, R0`; it is padding, not code. Disassemble the known
code range, or use a linked `.kex` with section boundaries when possible.

### 9.6 Reading the output

- **Addresses** are absolute in a `.kex`, section offsets in a `.o`, and
  `base + offset` in a raw image.
- **Branch targets** are printed as absolute addresses. To go back to a label,
  use the map or the object disassembly.
- **Jump and call targets** are absolute addresses as well. `JMP` and `CALL`
  with a label are 32-bit relative forms and print as a single target address.
- **`JMP #addr`** is a pseudo-instruction that the assembler turns into
  `MOV PC, #imm16`; the disassembler prints `MOV PC, #addr`.
- **16-bit displacements** are encoded as an unsigned 16-bit word and added
  modulo 64 KiB. The disassembler prints them unsigned. For example,
  `[R1+64536]` means `[R1-1000]`.
- **`SYS #0`** is printed with the known host-service name:
  `SYS #SYS_VERSION`.
- **Invalid encodings** print as `.word 0xXXXX`.

### 9.7 What the disassembler will not tell you

- It does not print source code interleaved with instructions. `--source` is
  accepted but currently has no effect.
- It does not print a symbol table for a linked image. Use `--map`.
- It does not decode data sections.
- It does not validate that a raw image's code starts at the `--base` you gave.
- It does not know the difference between code and data in a raw image.

---

## 10. Programmer's reference: the machine

This part is the reference for writing serious assembly. It covers the CPU
state, addressing modes, stack, frame pointer, program counter, flags, and each
instruction.

### 10.1 CPU state

The CPU has:

- Five general registers, `R0`–`R4`.
- A stack pointer, `SP`.
- A status register, `SR`.
- A program counter, `PC`.
- A halted flag and an internal cycle counter.

There are no hidden general registers. There are no condition-code registers
other than `SR`.

### 10.2 Registers

| Register | Code | Width | Role and rules |
|---|---|---|---|
| `R0` | 0 | 16 | Return value, scratch, low half of a pair; pair base for `MULS`/`MULU` |
| `R1` | 1 | 16 | First argument; return high word; pair high for `R0` pair |
| `R2` | 2 | 16 | Second argument; pair base for `MULS`/`MULU` |
| `R3` | 3 | 16 | Third argument; pair high for `R2` pair |
| `R4` / `FP` | 4 | 16 | Frame pointer; callee-saved |
| `SP` | 5 | 16 | Stack pointer; full-descending; callee-saved |
| `SR` | 6 | 16 | Flags and interrupt enable; only bits 0–4 are implemented |
| `PC` | 7 | 16 | Program counter |

Register use is a convention, not an enforced hardware rule, with these
exceptions:

- `MULS` and `MULU` require `Rd` to be `R0` or `R2`, and `Rs` to be `R0`–`R4`.
- Loads and `LEA` cannot write `PC` or `SR`.
- `PC` and `SR` have special meanings when written by other instructions.

### 10.3 Register save conventions

For calls:

- `R0`–`R3` are caller-saved. A function may clobber them.
- `R4` (`FP`) and `SP` are callee-saved. A function must preserve them.
- Flags and `SR` are caller-saved. A function that changes the interrupt enable
  state must restore it if its caller expects it preserved.

At program entry, the VM builds an argument block on the stack for normal
`.kex` images, even with no user arguments. `R1` is `argc`, `R2` points to
`argv`, and `argv[0]` is the image name. The strings and the null-terminated
pointer array are above `SP`; a custom `_start` receives those registers. The
windowed debugger's **Command line arguments** setting supplies `argv[1..]`,
parsed with Windows-style quoting (backslashes are literal except before a
quote).

### 10.4 Reset state

On reset:

- `PC` is loaded from the little-endian word at `0xFFFE`.
- `SP = 0xF000`.
- `R0`–`R4 = 0`.
- `SR = 0` (all flags clear, interrupts disabled).
- I/O registers return to their reset values.
- RAM is zeroed by the VM.
- ROM is loaded and write-protected.

Normal `.kex` programs start at `0x0100`. The reference BIOS at `0xF100` jumps
there.

### 10.5 Instruction fetch and alignment

The CPU fetches a 16-bit word at `PC`.

- If the top nibble is `0xF`, the instruction is 32 bits and a second word is
  fetched at `PC + 2`.
- If the top nibble is `0x8` and bits 11–8 are 0 or 1, the instruction is 32
  bits.
- Otherwise it is 16 bits.

An odd instruction-fetch address raises a bus-fault exception (vector 5).
Instructions should be word-aligned; the assembler and linker maintain this for
normal code.

---

## 11. Programmer's reference: addressing modes

KR16 has a small set of addressing modes. The operand syntax tells the
assembler which one to use.

### 11.1 Summary

| Mode | Syntax | Typical encoding | Example |
|---|---|---|---|
| Register | `Rd, Rs` | R-type | `ADD R0, R1` |
| Immediate, 6-bit | `#expr` | I-type | `ADD R0, #1` |
| Immediate, 8-bit | `#expr` | M8 | `MOV R0, #100` |
| Immediate, 16-bit | `#expr` | X-type | `MOV R0, #0xF000` |
| Register indirect | `[Rs]` | M-type with disp 0 | `LD R0, [R1]` |
| Indexed, 6-bit | `[Rs+disp]` | M-type | `LD R0, [R1+4]` |
| Indexed, 16-bit | `[Rs+disp]` | X-type | `LD R0, [R1+1000]` |
| PC-relative | `[PC+disp]` | M- or X-type with `Rs=PC` | `LD R0, [PC+4]` |
| Absolute | `[addr16]` | X-type | `LD R0, [0xF004]` |
| Branch | `Bcc label` | B-type | `BEQ done` |
| Jump/call label | `JMP label`, `CALL label` | C-type 32-bit | `CALL sum` |
| Jump/call register | `JMP Rs`, `CALL Rs` | C-type 16-bit | `CALL R1` |
| Address computation | `LEA Rd, [Rs+disp]` | M- or X-type | `LEA R0, [R1+6]` |

There is no auto-increment, auto-decrement, scaled index, or memory-to-memory
mode. At most one operand is a memory operand.

### 11.2 Register mode

```asm
ADD   R0, R1
MOV   R2, R3
CMP   R4, R0
```

Both operands are registers. The destination is also the first source for
two-operand ALU instructions: `ADD R0, R1` means `R0 = R0 + R1`.

### 11.3 Immediate mode

```asm
MOV   R0, #42
ADD   R1, #-1
CMP   R2, #0x1000
```

An immediate is a constant value encoded in the instruction. There are three
sizes:

- 6-bit signed immediate, range `-32..31`, in an I-type instruction.
- 8-bit immediate, range `0..255` zero-extended or `-128..-1` sign-extended, in
  an M8 `MOV`.
- 16-bit immediate, range `-32768..65535`, in a 32-bit X-type instruction.

The assembler picks the smallest form that fits. For `MOV`, the order is I-type,
M8, X-type. For the other ALU immediate instructions, the order is I-type,
X-type; there is no 8-bit ALU form.

### 11.4 Register indirect

```asm
LD    R0, [R1]          ; address = R1
ST    [R1], R0          ; address = R1
LDB   R0, [R2]          ; byte load
STB   [R2], R0          ; byte store
```

`[Rs]` is the same as `[Rs+0]`. It is encoded as an M-type instruction with a
6-bit displacement of zero.

### 11.5 Indexed mode

```asm
LD    R0, [R1+4]        ; 6-bit displacement
LD    R0, [R1-2]        ; negative 6-bit displacement
LD    R0, [R1+1000]     ; 16-bit displacement
ST    [R2+6], R3
LEA   R0, [R1+6]        ; address computation, no load
```

The effective address is:

```text
address = Rs + sign_extend(displacement)   (modulo 64 KiB)
```

For a 6-bit displacement, the range is `-32..31`. For a 16-bit displacement,
the range is `-32768..32767`.

`LDB`, `LDSB`, and `STB` have no 32-bit indexed form in this implementation. If
the displacement does not fit in 6 bits, the assembler reports:

```text
byte indexed form requires a 6-bit displacement
```

To access a byte at a large offset, load the offset into a register first:

```asm
MOV   R3, #1000
ADD   R3, R1
LDB   R0, [R3]
```

### 11.6 PC-relative mode

```asm
LD    R0, [PC+4]
LEA   R0, [PC+2]
```

When the base register is `PC`, the base is the address of the **next**
instruction, not the address of the current instruction.

- For a 16-bit M-type instruction, the next instruction is at `PC+2`.
- For a 32-bit X-type instruction, the next instruction is at `PC+4`.

This makes PC-relative data access position-independent. The assembler accepts
a constant displacement here. A symbolic displacement is encoded as an
absolute relocation and is not a true PC-relative reference in this release;
use `MOV Rd, #symbol` for a symbol address.

### 11.7 Absolute mode

```asm
LD    R0, [0xF004]
ST    [0xF000], R0
LDB   R0, [0xF004]
LDSB  R0, [0xF004]
STB   [0xF000], R0
```

An absolute operand is a 16-bit address encoded in the second word of an X-type
instruction. It is not a register-plus-displacement form.

`LEA` has no absolute form. To get the address of a symbol, use:

```asm
MOV   R2, #message
```

### 11.8 Branch mode

```asm
BEQ   done
BNE   loop
BGT   greater
BRA   next
```

`Bcc` is a 16-bit instruction with an 8-bit signed displacement in **words**.
The target is:

```text
target = address_of_branch + 2 + displacement * 2
```

The range is `-256..+254` bytes (`-128..+127` words). The assembler does not
relax an out-of-range `Bcc`; it reports an error. For longer distances, use
`JMP`. A `Bcc` target must also be in the same section as the branch; use `JMP`
for a cross-section target.

### 11.9 Jump and call modes

```asm
JMP   done              ; 32-bit relative jump
CALL  sum_to            ; 32-bit relative call
JMP   R1                ; register jump
CALL  R1                ; register call
JMP   #0x0100           ; pseudo-instruction: MOV PC, #0x0100
```

`JMP label` and `CALL label` use a 16-bit signed word displacement:

```text
target = address_of_instruction + 4 + displacement * 2
```

`JMP Rs` and `CALL Rs` use the register value directly.

`JMP #addr` is accepted by the assembler and encoded as `MOV PC, #imm16`. It
does not push a return address. `CALL #addr` is accepted with the same encoding,
but it is **not** a real call because it does not push a return address; use
`CALL label` or `CALL Rs` when you need a return.

### 11.10 LEA mode

```asm
LEA   R0, [R1]          ; R0 = R1
LEA   R0, [R1+6]        ; R0 = R1 + 6
LEA   R0, [R1+1000]     ; R0 = R1 + 1000
```

`LEA` computes an address but does not access memory. It is useful for array
indexing and for taking the address of a stack local:

```asm
LEA   R0, [R4-6]        ; address of the local at FP-6
```

`LEA` cannot write `PC` or `SR`, and it has no absolute form.

---

## 12. Programmer's reference: the stack and calling convention

### 12.1 The stack

The stack is a region of RAM that grows toward lower addresses. It is
full-descending:

- `SP` points to the last word pushed.
- A push decrements `SP` by 2, then writes the word at `[SP]`.
- A pop reads the word at `[SP]`, then increments `SP` by 2.

Example:

```text
initial:       SP = 0xF000
PUSH R0:       SP = 0xEFFE, [0xEFFE] = R0
PUSH R1:       SP = 0xEFFC, [0xEFFC] = R1
POP  R1:       R1 = [0xEFFC], SP = 0xEFFE
POP  R0:       R0 = [0xEFFE], SP = 0xF000
```

The default stack top is `0xF000`, which is one past the last usable RAM byte.
The first pushed word goes at `0xEFFE`. The default stack limit is `0xE000`.
The VM checks `SP` after each instruction and raises a bus fault (vector 5) if
`SP` leaves `[__stack_limit, 0xF000]`.

Keep `SP` even. All pushes, pops, calls, returns, and traps move it by two
bytes. A 16-bit word access at an odd address is allowed on RAM but costs an
extra cycle; keeping the stack aligned avoids that and matches the ABI.

### 12.2 What uses the stack

| Operation | Stack effect |
|---|---|
| `PUSH Rs` | `SP -= 2`, write `Rs` |
| `POP Rd` | read `[SP]` into `Rd`, `SP += 2` |
| `CALL label` / `CALL Rs` | push the next `PC` |
| `RET` | pop `PC` |
| `RETI` | pop `PC`, then pop `SR` |
| `TRAP #n` | push the next `PC` only |
| `BRK` | push `SR`, then push the next `PC`; clear `I` |
| Hardware interrupt | push `SR`, then push `PC`; clear `I` |

`PUSH` and `POP` can use any register code, including `PC` and `SR`. `PUSH PC`
pushes the address of the next instruction. `POP PC` is equivalent to a return.
`POP SR` changes the flags and interrupt enable. These are occasionally useful
in low-level code, but they are easy to misuse; prefer the named instructions
(`RET`, `RETI`, `EI`, `DI`) when they express the intent.

### 12.3 The call frame

The standard frame layout is:

```text
higher addresses
+----------------------+
| caller's frame       |
+----------------------+
| stack arguments      |  [FP+4], [FP+6], ...
+----------------------+
| return address       |  [FP+2]
+----------------------+
| saved caller FP      |  [FP+0]  <- R4 / FP
+----------------------+
| locals               |  [FP-2], [FP-4], ...
+----------------------+
| temporaries          |  <- SP
lower addresses
```

- `[FP]` is the caller's frame pointer.
- `[FP+2]` is the return address.
- `[FP+4]` is the first stack argument.
- `[FP-2]`, `[FP-4]`, ... are locals.
- `SP` points at the current top of stack, below the locals and any temporaries.

A function that uses a frame follows this pattern:

```asm
my_function:
    PUSH  R4                ; save caller's FP
    MOV   R4, SP            ; establish our frame
    SUB   SP, #frame_size   ; allocate locals (even number of bytes)

    ; ... body ...

    MOV   SP, R4            ; discard locals and temporaries
    POP   R4                ; restore caller's FP
    RET
```

A leaf function that uses no stack space can omit the frame entirely:

```asm
add3:
    MOV   R0, R1
    ADD   R0, R2
    ADD   R0, R3
    RET
```

A function that calls other functions or uses locals should normally establish
a frame. The frame pointer makes stack unwinding and debugging possible.

### 12.4 Arguments and return values

- First three arguments: `R1`, `R2`, `R3`.
- Additional arguments: pushed right-to-left before the call.
- First stack argument: `[FP+4]`.
- Return value: `R0`.
- 32-bit return: `R0` low word, `R1` high word.
- The caller removes stack arguments after the call with `ADD SP, #n`.
- Variadic functions receive all arguments on the stack, including named ones.

The callee should not assume that `R0`–`R3` survive a call to another function.
If a value is needed across a call, save it in a callee-saved register (`R4`) or
on the stack.

### 12.5 Frame pointer chain

The frame pointer forms a linked list:

```text
[FP]     = caller's FP
[FP+2]   = return address
```

The outermost frame conventionally has `R4 = 0`. A debugger can walk the chain
until it reaches zero. A hand-written `_start` can set `R4 = 0` explicitly:

```asm
_start:
    MOV   R4, #0
    ...
```

This is not required for execution, but it makes stack traces cleaner.

### 12.6 Interrupt handler frames

Hardware interrupts and `BRK` push `SR` first, then `PC`:

```text
higher addresses
+----------------------+
| saved SR             |  [SP+2]
+----------------------+
| saved PC             |  [SP]    <- SP
+----------------------+
lower addresses
```

`RETI` pops `PC` first, then `SR`. The handler must preserve any other registers
it uses:

```asm
timer_handler:
    PUSH  R0
    PUSH  R1
    ; ... handle interrupt ...
    POP   R1
    POP   R0
    RETI
```

`TRAP` is different: it pushes only the return address and does not change
`SR` or `I`. A trap handler returns with `RET`, not `RETI`.

---

## 13. Programmer's reference: the flags register

`SR` is the status register. It holds the condition flags and the interrupt
enable bit.

### 13.1 Layout

| Bit | Name | Meaning |
|---|---|---|
| 0 | `C` | Carry out of addition, or "no borrow" for subtraction |
| 1 | `Z` | Zero result |
| 2 | `N` | Negative: bit 15 of the result |
| 3 | `V` | Signed overflow |
| 4 | `I` | Interrupt enable |
| 15–5 | — | Reserved; read as zero |

`SR` is 16 bits wide, but only bits 0–4 are implemented. Writing `SR` with
`MOV` or `POP` masks off the reserved bits. `RETI` restores the saved `SR`.

`EI` sets `I`; `DI` clears `I`. Neither affects `C`, `Z`, `N`, or `V`.

### 13.2 Carry and borrow

For addition, `C` is the carry out of bit 15:

```text
0xFFFF + 1 = 0x0000 with C=1
```

For subtraction, `C` is **no borrow**:

```text
5 - 3 = 2 with C=1       (no borrow; unsigned 5 >= 3)
3 - 5 = 0xFFFE with C=0  (borrow; unsigned 3 < 5)
```

This is the ARM-style convention, not the x86 borrow convention. It makes
multiword subtraction straightforward.

### 13.3 Signed overflow

`V` is set when the signed result does not fit in 16 bits. For example:

```text
0x7FFF + 1 = 0x8000   ; V=1 (32767 + 1 overflows into negative)
0x8000 - 1 = 0x7FFF   ; V=1 (-32768 - 1 overflows into positive)
```

`N` is simply bit 15 of the result; it is not the same as `V`. Signed
comparisons use combinations of `N` and `V`.

### 13.4 Which instructions set which flags

| Instruction group | `C` | `Z` | `N` | `V` |
|---|---|---|---|---|
| `ADD`, `ADC` | carry out | result == 0 | bit 15 | signed overflow |
| `SUB`, `SBC`, `CMP` | no borrow | result == 0 | bit 15 | signed overflow |
| `AND`, `OR`, `XOR`, `TEST` | 0 | result == 0 | bit 15 | 0 |
| `NOT` | 0 | result == 0 | bit 15 | 0 |
| `NEG`, `INC`, `DEC` | as add/sub | result == 0 | bit 15 | signed overflow |
| `MUL` | 0 | low 16 == 0 | bit 15 of low 16 | 0 |
| `MULS`, `MULU` | 0 | full 32-bit product == 0 | bit 31 | 0 |
| `DIV`, `DIVU`, `MOD`, `MODU` | 0 | result == 0 | bit 15 | 0 |
| `SHL`, `SHR`, `SAR` | last bit out | result == 0 | bit 15 | 0 |
| `BTST` | unchanged | `Z = NOT bit` | unchanged | unchanged |
| `BSET`, `BCLR`, `BTOG` | unchanged | unchanged | unchanged | unchanged |
| `MOV`, loads, stores, `LEA`, `PUSH`, `POP`, `SEXT`, `ZXTB` | unchanged | unchanged | unchanged | unchanged |
| `SYS` | unchanged | unchanged | unchanged | unchanged |
| `EI`, `DI` | unchanged | unchanged | unchanged | unchanged |

A shift count of zero leaves `C`, `Z`, `N`, and `V` unchanged.

The table assumes `MOV` and `POP` use an ordinary register. `MOV SR, Rs` and
`POP SR` write the status register and therefore change flags and interrupt
enable. `BRK` clears `I`; `RETI` restores the saved `SR`.

### 13.5 Condition codes

Branches test the flags, not the operands. Use `CMP` or `TEST` first.

| Code | Mnemonic | Taken when | Meaning |
|---|---|---|---|
| 0 | `BEQ` | `Z=1` | Equal |
| 1 | `BNE` | `Z=0` | Not equal |
| 2 | `BCS` / `BHS` | `C=1` | Unsigned greater than or equal |
| 3 | `BCC` / `BLO` | `C=0` | Unsigned less than |
| 4 | `BMI` | `N=1` | Negative |
| 5 | `BPL` | `N=0` | Positive or zero |
| 6 | `BVS` | `V=1` | Signed overflow |
| 7 | `BVC` | `V=0` | No signed overflow |
| 8 | `BHI` | `C=1` and `Z=0` | Unsigned greater than |
| 9 | `BLS` | `C=0` or `Z=1` | Unsigned less than or equal |
| 10 | `BGE` | `N = V` | Signed greater than or equal |
| 11 | `BLT` | `N != V` | Signed less than |
| 12 | `BGT` | `Z=0` and `N = V` | Signed greater than |
| 13 | `BLE` | `Z=1` or `N != V` | Signed less than or equal |
| 14 | `BRA` / `BAL` | always | Unconditional |
| 15 | `BNV` | never | Never |

`BHS` is an alias for `BCS`, and `BLO` is an alias for `BCC`. `BAL` is an alias
for `BRA`.

### 13.6 Multiword arithmetic

Addition with carry:

```asm
; (R1:R0) = (R1:R0) + (R3:R2)
ADD   R0, R2
ADC   R1, R3
```

Subtraction with borrow:

```asm
; (R1:R0) = (R1:R0) - (R3:R2)
SUB   R0, R2
SBC   R1, R3
```

`ADC` adds the incoming `C`; `SBC` subtracts `1 - C`, which is the borrow from
the previous subtraction. Do not use `ADD`/`SUB` for the high word of a
multiword operation, because they ignore the incoming carry/borrow.

### 13.7 Useful flag idioms

```asm
CMP   R0, #0
BEQ   is_zero

TEST  R0, #1
BNE   is_odd            ; TEST sets Z based on R0 & 1

BTST  R0, #7
BNE   bit7_set          ; BTST sets Z=0 when the bit is 1
```

`TEST` clears `C` and `V`; `BTST` changes only `Z`.

---

## 14. Programmer's reference: instruction formats and opcodes

All instructions are multiples of 16 bits. A 32-bit instruction is two
consecutive 16-bit words, low word first.

### 14.1 Notation

| Symbol | Meaning |
|---|---|
| `ddd` | Destination register code, 3 bits |
| `sss` | Source register code, 3 bits |
| `fff` | 3-bit function code (I-type, S-type, BIT-type) |
| `ffffff` | 6-bit function code (R-type) |
| `iiiiii` | 6-bit immediate, signed |
| `iiiiiiii` | 8-bit immediate (M8) |
| `S` | 1-bit sign selector (M8) |
| `tttt` | 4-bit major opcode |
| `cccc` | 4-bit condition code |
| `dddddd` | 6-bit displacement, signed |
| `dddddddd` | 8-bit displacement, signed, in words |
| `cccccc` | 6-bit shift count |
| `bbbbbb` | 6-bit bit number |
| `eeee` | 4-bit extended operation |
| `aaaaa` | 5-bit auxiliary field |
| `rrrrr` | 5-bit "rest" field |
| `nnnnnnnn` | 8-bit trap or system-call number |

Register codes are 0–7: `R0`=0, `R1`=1, `R2`=2, `R3`=3, `R4`=4, `SP`=5,
`SR`=6, `PC`=7.

### 14.2 R-type: register ALU

```text
  15..12  11..9  8..6  5..0
  0000    ddd    sss   ffffff
```

### 14.3 I-type: immediate ALU

```text
  15..12  11..9  8..6  5..0
  0001    ddd    fff   iiiiii
```

`imm6` is signed, `-32..31`.

### 14.4 M-type: load, store, LEA (16-bit)

```text
  15..12  11..9  8..6  5..0
  tttt    ddd    sss   dddddd
```

`disp6` is signed, `-32..31`. The address is `Rs + sign_extend(disp6)`,
modulo 64 KiB. When `Rs = PC`, the base is the next instruction.

### 14.5 M8-type: `MOV` immediate 8-bit

```text
  15..12  11..9  8     7..0
  1001    ddd    S     iiiiiiii
```

`S=0` zero-extends the 8-bit value; `S=1` sign-extends it.

### 14.6 B-type: conditional branch

```text
  15..12  11..8  7..0
  0111    cccc   dddddddd
```

`disp8` is signed and measured in words. Target = `PC + 2 + disp8 * 2`.

### 14.7 S-type: shift by immediate

```text
  15..12  11..9  8..6  5..0
  1011    ddd    fff   cccccc
```

`func` 0 = `SHL`, 1 = `SHR`, 2 = `SAR`. Count is 0–15.

### 14.8 BIT-type: bit operation by immediate

```text
  15..12  11..9  8..6  5..0
  1100    ddd    fff   bbbbbb
```

`func` 0 = `BTST`, 1 = `BSET`, 2 = `BCLR`, 3 = `BTOG`. Bit number is 0–15.

### 14.9 C-type: control and special

```text
  15..12  11..8  7..5  4..0
  1000    ssss   aaa   rrrrr
```

Subops 0 and 1 are 32-bit; the rest are 16-bit. Some instructions use the low
8 bits as a number (`TRAP`, `SYS`); see the subop table.

### 14.10 X-type: extended 32-bit

```text
  Word 1:
  15..12  11..8  7..5  4..0
  1111    eeee   ddd   aaaaa

  Word 2:
  15..0
  imm16 / address / displacement (little-endian)
```

For `ext` 0–12, `aux` must be zero. For `ext` 13–15, `aux` is `Rs`.

### 14.11 Major opcode map

| Top nibble | Format | Meaning |
|---|---|---|
| `0x0` | R | ALU register-register |
| `0x1` | I | ALU immediate (6-bit) |
| `0x2` | M | `LD` word |
| `0x3` | M | `ST` word |
| `0x4` | M | `LDB` byte, zero-extend |
| `0x5` | M | `LDSB` byte, sign-extend |
| `0x6` | M | `STB` byte |
| `0x7` | B | Conditional branch |
| `0x8` | C | Control and special |
| `0x9` | M8 | `MOV Rd, #imm8` |
| `0xA` | M | `LEA` |
| `0xB` | S | Shift by immediate |
| `0xC` | BIT | Bit operation by immediate |
| `0xD` | — | Reserved |
| `0xE` | — | Reserved |
| `0xF` | X | Extended 32-bit |

### 14.12 R-type function codes

| Code | Mnemonic | Operation |
|---|---|---|
| 0 | `ADD` | `Rd = Rd + Rs` |
| 1 | `ADC` | `Rd = Rd + Rs + C` |
| 2 | `SUB` | `Rd = Rd - Rs` |
| 3 | `SBC` | `Rd = Rd - Rs - (1 - C)` |
| 4 | `CMP` | flags of `Rd - Rs` |
| 5 | `AND` | `Rd = Rd & Rs` |
| 6 | `OR` | `Rd = Rd OR Rs` |
| 7 | `XOR` | `Rd = Rd XOR Rs` |
| 8 | `MOV` | `Rd = Rs` |
| 9 | `TEST` | flags of `Rd & Rs` |
| 10 | `NEG` | `Rd = -Rd` |
| 11 | `NOT` | `Rd = ~Rd` |
| 12 | `INC` | `Rd = Rd + 1` |
| 13 | `DEC` | `Rd = Rd - 1` |
| 14 | `SHL` | `Rd = Rd << (Rs & 15)` |
| 15 | `SHR` | `Rd = Rd >> (Rs & 15)` |
| 16 | `SAR` | arithmetic right shift |
| 17 | `MUL` | low 16 bits of `Rd * Rs` |
| 18 | `DIV` | signed quotient |
| 19 | `DIVU` | unsigned quotient |
| 20 | `MOD` | signed remainder |
| 21 | `MODU` | unsigned remainder |
| 22 | `BTST` | `Z = NOT bit` |
| 23 | `BSET` | set bit |
| 24 | `BCLR` | clear bit |
| 25 | `BTOG` | toggle bit |
| 26 | `SEXT` | sign-extend low byte |
| 27 | `ZXTB` | zero-extend low byte |
| 28 | `MULS` | signed product into `Rd:Rd+1` |
| 29 | `MULU` | unsigned product into `Rd:Rd+1` |
| 30–63 | — | Reserved |

Unary functions ignore `Rs`; the assembler encodes `Rs = Rd`.

### 14.13 I-type function codes

| Code | Mnemonic |
|---|---|
| 0 | `ADD` |
| 1 | `SUB` |
| 2 | `CMP` |
| 3 | `AND` |
| 4 | `OR` |
| 5 | `XOR` |
| 6 | `MOV` |
| 7 | `TEST` |

### 14.14 X-type extension codes

| `ext` | Mnemonic | Operation |
|---|---|---|
| 0 | `MOV Rd, #imm16` | `Rd = imm16` |
| 1 | `ADD Rd, #imm16` | `Rd = Rd + imm16` |
| 2 | `SUB Rd, #imm16` | `Rd = Rd - imm16` |
| 3 | `CMP Rd, #imm16` | flags of `Rd - imm16` |
| 4 | `AND Rd, #imm16` | `Rd = Rd & imm16` |
| 5 | `OR Rd, #imm16` | `Rd = Rd OR imm16` |
| 6 | `XOR Rd, #imm16` | `Rd = Rd XOR imm16` |
| 7 | `TEST Rd, #imm16` | flags of `Rd & imm16` |
| 8 | `LD Rd, [addr16]` | word load |
| 9 | `ST [addr16], Rd` | word store |
| 10 | `LDB Rd, [addr16]` | byte load, zero-extend |
| 11 | `LDSB Rd, [addr16]` | byte load, sign-extend |
| 12 | `STB [addr16], Rd` | byte store |
| 13 | `LD Rd, [Rs+disp16]` | word load, indexed |
| 14 | `ST [Rs+disp16], Rd` | word store, indexed |
| 15 | `LEA Rd, [Rs+disp16]` | address computation |

### 14.15 C-type subops

| Subop | Mnemonic | Size | Operation |
|---|---|---|---|
| 0 | `JMP rel16` | 32 | `PC = next PC + rel16 * 2` |
| 1 | `CALL rel16` | 32 | push next PC; `PC = next PC + rel16 * 2` |
| 2 | `JMP Rs` | 16 | `PC = Rs` |
| 3 | `CALL Rs` | 16 | push next PC; `PC = Rs` |
| 4 | `RET` | 16 | `PC = pop` |
| 5 | `RETI` | 16 | `PC = pop`; `SR = pop` |
| 6 | `TRAP #n` | 16 | push next PC; `PC = [0x0010 + 2n]` |
| 7 | `BRK` | 16 | breakpoint exception (vector 1) |
| 8 | `HLT` | 16 | stop CPU |
| 9 | `NOP` | 16 | no operation |
| 10 | `EI` | 16 | set `I` |
| 11 | `DI` | 16 | clear `I` |
| 12 | `WAIT` | 16 | wait for interrupt |
| 13 | `PUSH Rs` | 16 | push register |
| 14 | `POP Rd` | 16 | pop register |
| 15 | `SYS #n` | 16 | host service call |

For `TRAP #n`, the instruction is encoded as `0x8600 | n`; for `SYS #n`,
it is `0x8F00 | n`. The number occupies the low 8 bits.

### 14.16 Reserved and illegal encodings

The following raise the illegal-instruction exception (vector 0):

- Top nibbles `0xD` or `0xE`.
- R-type function codes 30–63.
- Nonzero reserved fields, or nonzero `aux` where it must be zero.
- `MULS`/`MULU` with a pair base other than `R0`/`R2`, or a multiplier outside
  `R0`–`R4`.
- `LD`, `LDB`, `LDSB`, or `LEA` with `Rd = PC` or `Rd = SR`.
- `TRAP` with `n > 119`.
- A nonzero reserved bit in a control encoding.

An odd instruction-fetch address raises the bus-fault exception (vector 5).
A `SYS` with an invalid block is an `EFAULT` result, not an illegal
instruction.

---

## 15. Instruction set reference

This section documents every instruction. The encoding fields refer to the
notation in [Section 14](#14-programmers-reference-instruction-formats-and-opcodes).

For each instruction:

- **Syntax** shows the assembly forms.
- **Operation** describes the effect.
- **Encoding** gives the bit pattern and function/extension code.
- **Flags** lists the flags that change.
- **Cycles** gives the simulated cost of the common 16-bit form.
- **Notes** gives restrictions and useful details.

Cycle counts are for the current VM. A 32-bit form usually costs one extra
cycle over the equivalent 16-bit form.

### 15.1 Data movement

#### MOV — Move

**Syntax**

```asm
MOV   Rd, Rs
MOV   Rd, #imm
```

**Operation:** `Rd = Rs` or `Rd = immediate`.

**Encoding**

- Register: R-type, `0000 ddd sss 001000` (func 8).
- 6-bit immediate: I-type, `0001 ddd 110 iiiiii` (func 6).
- 8-bit immediate: M8, `1001 ddd S iiiiiiii`.
- 16-bit immediate: X-type, `1111 0000 ddd 00000` + `imm16`.

**Flags:** unchanged. Writing `SR` with `MOV` does change the flags, because
that is a write to the status register.

**Cycles:** 1 for a 16-bit form; 2 for a 32-bit form.

**Notes:** The assembler chooses the shortest form. `MOV R0, #0x12` uses the
6-bit form, `MOV R0, #100` uses the 8-bit form, and `MOV R0, #0xF000` uses the
32-bit form. `MOV PC, Rs` and `MOV PC, #imm` are jumps. `MOV Rd, SR` reads the
flags; `MOV SR, Rd` writes them (reserved bits are ignored). Use `EI`/`DI` for
the interrupt bit when you do not intend to change other flags.

#### LD — Load word

**Syntax**

```asm
LD    Rd, [Rs]
LD    Rd, [Rs+disp]
LD    Rd, [addr16]
```

**Operation:** `Rd = memory16[address]`.

**Encoding**

- 6-bit indexed: M-type, top nibble `0x2`.
- Absolute: X-type, `ext=8`, `1111 1000 ddd 00000` + `addr16`.
- 16-bit indexed: X-type, `ext=13`, `1111 1101 ddd sss00` + `disp16`.

**Flags:** unchanged.

**Cycles:** 2, plus 1 if the word address is odd.

**Notes:** The address is `Rs + sign_extend(disp)`, modulo 64 KiB. With
`Rs = PC`, the base is the next instruction. `Rd` may not be `PC` or `SR`
(illegal instruction). Unaligned word access is allowed but costs an extra
cycle. `LD` is little-endian.

#### LDB — Load byte, zero-extend

**Syntax**

```asm
LDB   Rd, [Rs]
LDB   Rd, [Rs+disp6]
LDB   Rd, [addr16]
```

**Operation:** `Rd = zero_extend(memory8[address])`.

**Encoding**

- 6-bit indexed: M-type, top nibble `0x4`.
- Absolute: X-type, `ext=10`, `1111 1010 ddd 00000` + `addr16`.

**Flags:** unchanged.

**Cycles:** 2.

**Notes:** There is no 32-bit indexed byte form. A displacement outside
`-32..31` is an assembler error. `Rd` may not be `PC` or `SR`.

#### LDSB — Load byte, sign-extend

**Syntax**

```asm
LDSB  Rd, [Rs]
LDSB  Rd, [Rs+disp6]
LDSB  Rd, [addr16]
```

**Operation:** `Rd = sign_extend(memory8[address])`.

**Encoding**

- 6-bit indexed: M-type, top nibble `0x5`.
- Absolute: X-type, `ext=11`, `1111 1011 ddd 00000` + `addr16`.

**Flags:** unchanged.

**Cycles:** 2.

**Notes:** A byte of `0xFF` becomes `0xFFFF`; a byte of `0x80` becomes
`0xFF80`. As with `LDB`, there is no 32-bit indexed byte form, and `Rd` may not
be `PC` or `SR`.

#### ST — Store word

**Syntax**

```asm
ST    [Rs], Rd
ST    [Rs+disp], Rd
ST    [addr16], Rd
```

**Operation:** `memory16[address] = Rd`.

**Encoding**

- 6-bit indexed: M-type, top nibble `0x3`.
- Absolute: X-type, `ext=9`, `1111 1001 ddd 00000` + `addr16`.
- 16-bit indexed: X-type, `ext=14`, `1111 1110 ddd sss00` + `disp16`.

**Flags:** unchanged.

**Cycles:** 2, plus 1 if the word address is odd.

**Notes:** The memory operand is written first in assembly. The value is the
low 16 bits of the source register. There is no memory-to-memory store.

#### STB — Store byte

**Syntax**

```asm
STB   [Rs], Rd
STB   [Rs+disp6], Rd
STB   [addr16], Rd
```

**Operation:** `memory8[address] = Rd & 0xFF`.

**Encoding**

- 6-bit indexed: M-type, top nibble `0x6`.
- Absolute: X-type, `ext=12`, `1111 1100 ddd 00000` + `addr16`.

**Flags:** unchanged.

**Cycles:** 2.

**Notes:** There is no 32-bit indexed byte form. Use a register for a large
offset. The console at `0xF000` is written with `STB`.

#### LEA — Load effective address

**Syntax**

```asm
LEA   Rd, [Rs]
LEA   Rd, [Rs+disp]
```

**Operation:** `Rd = address`, without reading memory.

**Encoding**

- 6-bit indexed: M-type, top nibble `0xA`.
- 16-bit indexed: X-type, `ext=15`, `1111 1111 ddd sss00` + `disp16`.

**Flags:** unchanged.

**Cycles:** 2.

**Notes:** `LEA` has no absolute form. To get the address of a symbol, use
`MOV Rd, #symbol`; the linker resolves it. `LEA` may not write `PC` or `SR`.
A common use is taking the address of a local: `LEA R0, [R4-6]`.

#### PUSH — Push register

**Syntax**

```asm
PUSH  Rs
```

**Operation:** `SP = SP - 2`; `memory16[SP] = Rs`.

**Encoding:** C-type, subop 13, `1000 1101 aaa 00000` (regA = `Rs`).

**Flags:** unchanged.

**Cycles:** 2.

**Notes:** The stack is full-descending. Pushing a word can trigger the stack
guard if `SP` leaves `[stack_limit, 0xF000]`. `PUSH PC` pushes the address of
the next instruction; `PUSH SR` pushes the flags. There is no byte push and no
push-immediate form; load a value into a register first.

#### POP — Pop register

**Syntax**

```asm
POP   Rd
```

**Operation:** `Rd = memory16[SP]`; `SP = SP + 2`.

**Encoding:** C-type, subop 14, `1000 1110 aaa 00000` (regA = `Rd`).

**Flags:** unchanged, unless `Rd` is `SR`.

**Cycles:** 2.

**Notes:** `POP PC` is a return. `POP SR` restores the flags and interrupt
enable. `RET` and `RETI` are the preferred spellings for returns.

### 15.2 Arithmetic

#### ADD — Add

**Syntax**

```asm
ADD   Rd, Rs
ADD   Rd, #imm
```

**Operation:** `Rd = Rd + Rs` or `Rd = Rd + imm`.

**Encoding**

- Register: R-type, func 0, `0000 ddd sss 000000`.
- 6-bit immediate: I-type, func 0, `0001 ddd 000 iiiiii`.
- 16-bit immediate: X-type, `ext=1`, `1111 0001 ddd 00000` + `imm16`.

**Flags:** `C`, `Z`, `N`, `V`.

**Cycles:** 1 for the 16-bit forms; 2 for the 32-bit immediate form.

**Notes:** The 6-bit immediate is signed. `C` is the carry out. `ADD SP, #n`
is a valid way to clean stack arguments.

#### ADC — Add with carry

**Syntax**

```asm
ADC   Rd, Rs
```

**Operation:** `Rd = Rd + Rs + C`.

**Encoding:** R-type, func 1, `0000 ddd sss 000001`.

**Flags:** `C`, `Z`, `N`, `V`.

**Cycles:** 1.

**Notes:** There is no immediate form. Use it after `ADD` for multiword
addition. The carry input is the current `C` flag.

#### SUB — Subtract

**Syntax**

```asm
SUB   Rd, Rs
SUB   Rd, #imm
```

**Operation:** `Rd = Rd - Rs` or `Rd = Rd - imm`.

**Encoding**

- Register: R-type, func 2, `0000 ddd sss 000010`.
- 6-bit immediate: I-type, func 1, `0001 ddd 001 iiiiii`.
- 16-bit immediate: X-type, `ext=2`, `1111 0010 ddd 00000` + `imm16`.

**Flags:** `C` (no borrow), `Z`, `N`, `V`.

**Cycles:** 1 for the 16-bit forms; 2 for the 32-bit immediate form.

**Notes:** `C=1` means no borrow. For multiword subtraction use `SUB` on the low
word and `SBC` on the high word, not `SUB` on both.

#### SBC — Subtract with borrow

**Syntax**

```asm
SBC   Rd, Rs
```

**Operation:** `Rd = Rd - Rs - (1 - C)`.

**Encoding:** R-type, func 3, `0000 ddd sss 000011`.

**Flags:** `C` (no borrow), `Z`, `N`, `V`.

**Cycles:** 1.

**Notes:** The borrow is `1 - C`, matching the "C is no borrow" convention.
There is no immediate form.

#### CMP — Compare

**Syntax**

```asm
CMP   Rd, Rs
CMP   Rd, #imm
```

**Operation:** set the flags from `Rd - Rs` or `Rd - imm`; do not write `Rd`.

**Encoding**

- Register: R-type, func 4, `0000 ddd sss 000100`.
- 6-bit immediate: I-type, func 2, `0001 ddd 010 iiiiii`.
- 16-bit immediate: X-type, `ext=3`, `1111 0011 ddd 00000` + `imm16`.

**Flags:** `C`, `Z`, `N`, `V`.

**Cycles:** 1 for the 16-bit forms; 2 for the 32-bit immediate form.

**Notes:** `CMP` is the normal way to prepare for a conditional branch. It does
not change `Rd`.

#### NEG — Negate

**Syntax**

```asm
NEG   Rd
```

**Operation:** `Rd = 0 - Rd` (two's complement).

**Encoding:** R-type, func 10, `0000 ddd ddd 001010` (the assembler sets
`Rs = Rd`; the CPU ignores `Rs`).

**Flags:** `C`, `Z`, `N`, `V`, computed as for `0 - Rd`.

**Cycles:** 1.

**Notes:** Negating `0x8000` produces `0x8000` and sets `V`. `NEG` is not the
same as `NOT`; `NOT` is bitwise complement, `NEG` is arithmetic negation.

#### INC — Increment

**Syntax**

```asm
INC   Rd
```

**Operation:** `Rd = Rd + 1`.

**Encoding:** R-type, func 12, `0000 ddd ddd 001100`.

**Flags:** `C`, `Z`, `N`, `V`, as for addition.

**Cycles:** 1.

**Notes:** `INC` wraps from `0xFFFF` to `0x0000` and sets `C=1` on that wrap.
It sets `V` on `0x7FFF -> 0x8000`.

#### DEC — Decrement

**Syntax**

```asm
DEC   Rd
```

**Operation:** `Rd = Rd - 1`.

**Encoding:** R-type, func 13, `0000 ddd ddd 001101`.

**Flags:** `C`, `Z`, `N`, `V`, as for subtraction.

**Cycles:** 1.

**Notes:** `DEC` wraps from `0x0000` to `0xFFFF` and sets `C=0` (borrow). It
sets `V` on `0x8000 -> 0x7FFF`.

#### MUL — Multiply (low 16 bits)

**Syntax**

```asm
MUL   Rd, Rs
```

**Operation:** `Rd = low16(Rd * Rs)`.

**Encoding:** R-type, func 17, `0000 ddd sss 010001`.

**Flags:** `C=0`, `V=0`; `Z` and `N` from the low 16-bit result.

**Cycles:** 1.

**Notes:** This is an unsigned 16x16 multiplication truncated to 16 bits. For a
full 32-bit product, use `MULU` (or `MULS` for signed).

#### MULS — Signed multiply to register pair

**Syntax**

```asm
MULS  Rd, Rs
MULS  Rd          ; one-operand shorthand, Rs = Rd+1
```

**Operation:** `Rd:Rd+1 = signed(Rd) * signed(Rs)`; `Rd` gets the low word,
`Rd+1` the high word.

**Encoding:** R-type, func 28, `0000 ddd sss 011100`.

**Flags:** `C=0`, `V=0`; `Z` is set if the full 32-bit product is zero; `N` is
bit 31 of the product.

**Cycles:** 1.

**Notes:** `Rd` must be `R0` or `R2`, and `Rs` must be `R0`–`R4`. Any other
combination raises the illegal-instruction exception. Both source values are
read before either destination is written, so `MULS R0, R1` is safe even though
the high half overwrites `R1`. The one-operand form uses `R1` for `R0` and `R3`
for `R2`. The assembler does not reject an invalid pair base; the CPU faults at
run time.

#### MULU — Unsigned multiply to register pair

**Syntax**

```asm
MULU  Rd, Rs
MULU  Rd          ; one-operand shorthand, Rs = Rd+1
```

**Operation:** `Rd:Rd+1 = unsigned(Rd) * unsigned(Rs)`.

**Encoding:** R-type, func 29, `0000 ddd sss 011101`.

**Flags:** `C=0`, `V=0`; `Z` from the full 32-bit product; `N` from bit 31.

**Cycles:** 1.

**Notes:** Same pair restrictions as `MULS`: `Rd` is `R0` or `R2`, `Rs` is
`R0`–`R4`. Use this when both operands are unsigned.

#### DIV — Signed divide

**Syntax**

```asm
DIV   Rd, Rs
```

**Operation:** `Rd = signed(Rd) / signed(Rs)`.

**Encoding:** R-type, func 18, `0000 ddd sss 010010`.

**Flags:** `C=0`, `V=0`; `Z` and `N` from the quotient.

**Cycles:** 1.

**Notes:** Division by zero raises the divide-by-zero exception (vector 2)
before writing the destination. Signed division truncates toward zero.

#### DIVU — Unsigned divide

**Syntax**

```asm
DIVU  Rd, Rs
```

**Operation:** `Rd = unsigned(Rd) / unsigned(Rs)`.

**Encoding:** R-type, func 19, `0000 ddd sss 010011`.

**Flags:** `C=0`, `V=0`; `Z` and `N` from the quotient.

**Cycles:** 1.

**Notes:** Division by zero raises vector 2. Use this for unsigned values.

#### MOD — Signed remainder

**Syntax**

```asm
MOD   Rd, Rs
```

**Operation:** `Rd = signed(Rd) % signed(Rs)`.

**Encoding:** R-type, func 20, `0000 ddd sss 010100`.

**Flags:** `C=0`, `V=0`; `Z` and `N` from the remainder.

**Cycles:** 1.

**Notes:** Division by zero raises vector 2. The remainder has the sign of the
dividend, matching C's truncation-toward-zero rule.

#### MODU — Unsigned remainder

**Syntax**

```asm
MODU  Rd, Rs
```

**Operation:** `Rd = unsigned(Rd) % unsigned(Rs)`.

**Encoding:** R-type, func 21, `0000 ddd sss 010101`.

**Flags:** `C=0`, `V=0`; `Z` and `N` from the remainder.

**Cycles:** 1.

**Notes:** Division by zero raises vector 2.

### 15.3 Logic and shifts

#### AND — Bitwise AND

**Syntax**

```asm
AND   Rd, Rs
AND   Rd, #imm
```

**Operation:** `Rd = Rd & Rs` or `Rd = Rd & imm`.

**Encoding**

- Register: R-type, func 5, `0000 ddd sss 000101`.
- 6-bit immediate: I-type, func 3, `0001 ddd 011 iiiiii`.
- 16-bit immediate: X-type, `ext=4`, `1111 0100 ddd 00000` + `imm16`.

**Flags:** `C=0`, `V=0`; `Z` and `N` from the result.

**Cycles:** 1 for the 16-bit forms; 2 for the 32-bit immediate form.

**Notes:** Use `AND` to mask bits. Use `TEST` if you only want the flags and do
not want to modify the register.

#### OR — Bitwise OR

**Syntax**

```asm
OR    Rd, Rs
OR    Rd, #imm
```

**Operation:** `Rd = Rd OR Rs` or `Rd = Rd OR imm`.

**Encoding**

- Register: R-type, func 6, `0000 ddd sss 000110`.
- 6-bit immediate: I-type, func 4, `0001 ddd 100 iiiiii`.
- 16-bit immediate: X-type, `ext=5`, `1111 0101 ddd 00000` + `imm16`.

**Flags:** `C=0`, `V=0`; `Z` and `N` from the result.

**Cycles:** 1 for the 16-bit forms; 2 for the 32-bit immediate form.

**Notes:** Use `OR` to set bits.

#### XOR — Bitwise exclusive OR

**Syntax**

```asm
XOR   Rd, Rs
XOR   Rd, #imm
```

**Operation:** `Rd = Rd XOR Rs` or `Rd = Rd XOR imm`.

**Encoding**

- Register: R-type, func 7, `0000 ddd sss 000111`.
- 6-bit immediate: I-type, func 5, `0001 ddd 101 iiiiii`.
- 16-bit immediate: X-type, `ext=6`, `1111 0110 ddd 00000` + `imm16`.

**Flags:** `C=0`, `V=0`; `Z` and `N` from the result.

**Cycles:** 1 for the 16-bit forms; 2 for the 32-bit immediate form.

**Notes:** Use `XOR Rd, Rd` to clear a register, or `XOR Rd, #0xFFFF` to
complement it. `XOR Rd, Rd` sets `Z=1` and clears `C` and `V`.

#### TEST — Test bits

**Syntax**

```asm
TEST  Rd, Rs
TEST  Rd, #imm
```

**Operation:** set the flags from `Rd & Rs` or `Rd & imm`; do not write `Rd`.

**Encoding**

- Register: R-type, func 9, `0000 ddd sss 001001`.
- 6-bit immediate: I-type, func 7, `0001 ddd 111 iiiiii`.
- 16-bit immediate: X-type, `ext=7`, `1111 0111 ddd 00000` + `imm16`.

**Flags:** `C=0`, `V=0`; `Z` and `N` from the result.

**Cycles:** 1 for the 16-bit forms; 2 for the 32-bit immediate form.

**Notes:** `TEST` is to `AND` as `CMP` is to `SUB`: flags only. `TEST Rd, #1`
sets `Z=0` if the low bit is set.

#### NOT — Bitwise complement

**Syntax**

```asm
NOT   Rd
```

**Operation:** `Rd = ~Rd`.

**Encoding:** R-type, func 11, `0000 ddd ddd 001011`.

**Flags:** `C=0`, `V=0`; `Z` and `N` from the result.

**Cycles:** 1.

**Notes:** `NOT` is bitwise complement, not arithmetic negation. Use `NEG` for
`-Rd`.

#### SHL — Shift left logical

**Syntax**

```asm
SHL   Rd, Rs
SHL   Rd, #count
```

**Operation:** `Rd = Rd << count`; bits shifted out are discarded.

**Encoding**

- Register count: R-type, func 14, `0000 ddd sss 001110`.
- Immediate count: S-type, func 0, `1011 ddd 000 cccccc`.

**Flags:** `C` is the last bit shifted out; `V=0`; `Z` and `N` from the result.
A count of zero leaves all flags unchanged.

**Cycles:** 1.

**Notes:** The immediate count is 0–15. A register count uses the low 4 bits of
`Rs`. `SHL` is a logical left shift; the result is the same for signed and
unsigned values, but the overflow interpretation differs.

#### SHR — Shift right logical

**Syntax**

```asm
SHR   Rd, Rs
SHR   Rd, #count
```

**Operation:** `Rd = Rd >> count`, filling the high bits with zero.

**Encoding**

- Register count: R-type, func 15, `0000 ddd sss 001111`.
- Immediate count: S-type, func 1, `1011 ddd 001 cccccc`.

**Flags:** `C` is the last bit shifted out; `V=0`; `Z` and `N` from the result.
A count of zero leaves all flags unchanged.

**Cycles:** 1.

**Notes:** Use `SHR` for unsigned division by powers of two. For signed values,
use `SAR` to preserve the sign.

#### SAR — Shift right arithmetic

**Syntax**

```asm
SAR   Rd, Rs
SAR   Rd, #count
```

**Operation:** `Rd = signed(Rd) >> count`, filling the high bits with the sign.

**Encoding**

- Register count: R-type, func 16, `0000 ddd sss 010000`.
- Immediate count: S-type, func 2, `1011 ddd 010 cccccc`.

**Flags:** `C` is the last bit shifted out; `V=0`; `Z` and `N` from the result.
A count of zero leaves all flags unchanged.

**Cycles:** 1.

**Notes:** `SAR` rounds toward negative infinity for negative values, which is
not the same as signed division truncating toward zero. For example, `-3 SAR 1`
is `-2`, while `-3 / 2` is `-1`.

### 15.4 Bit operations

#### BTST — Test bit

**Syntax**

```asm
BTST  Rd, Rs
BTST  Rd, #bit
```

**Operation:** `Z = NOT ((Rd >> bit) & 1)`; other flags unchanged.

**Encoding**

- Register bit number: R-type, func 22, `0000 ddd sss 010110`.
- Immediate bit number: BIT-type, func 0, `1100 ddd 000 bbbbbb`.

**Flags:** `Z` only. `C`, `N`, and `V` are unchanged.

**Cycles:** 1.

**Notes:** The bit number is 0–15 for the immediate form; the register form
uses the low 4 bits. `BTST` sets `Z=0` when the bit is 1, so use `BNE` to branch
on a set bit and `BEQ` to branch on a clear bit.

#### BSET — Set bit

**Syntax**

```asm
BSET  Rd, Rs
BSET  Rd, #bit
```

**Operation:** set bit `bit` of `Rd`.

**Encoding**

- Register bit number: R-type, func 23, `0000 ddd sss 010111`.
- Immediate bit number: BIT-type, func 1, `1100 ddd 001 bbbbbb`.

**Flags:** unchanged.

**Cycles:** 1.

**Notes:** Bit number 0–15; register form uses the low 4 bits.

#### BCLR — Clear bit

**Syntax**

```asm
BCLR  Rd, Rs
BCLR  Rd, #bit
```

**Operation:** clear bit `bit` of `Rd`.

**Encoding**

- Register bit number: R-type, func 24, `0000 ddd sss 011000`.
- Immediate bit number: BIT-type, func 2, `1100 ddd 010 bbbbbb`.

**Flags:** unchanged.

**Cycles:** 1.

**Notes:** Bit number 0–15.

#### BTOG — Toggle bit

**Syntax**

```asm
BTOG  Rd, Rs
BTOG  Rd, #bit
```

**Operation:** complement bit `bit` of `Rd`.

**Encoding**

- Register bit number: R-type, func 25, `0000 ddd sss 011001`.
- Immediate bit number: BIT-type, func 3, `1100 ddd 011 bbbbbb`.

**Flags:** unchanged.

**Cycles:** 1.

**Notes:** Bit number 0–15.

### 15.5 Byte extension

#### SEXT — Sign-extend byte

**Syntax**

```asm
SEXT  Rd
```

**Operation:** `Rd = sign_extend(Rd & 0xFF)`.

**Encoding:** R-type, func 26, `0000 ddd ddd 011010`.

**Flags:** unchanged.

**Cycles:** 1.

**Notes:** Useful after loading a byte with a plain `LDB` or after arithmetic
that leaves a signed 8-bit value in the low byte. `0x80` becomes `0xFF80`.

#### ZXTB — Zero-extend byte

**Syntax**

```asm
ZXTB  Rd
```

**Operation:** `Rd = Rd & 0xFF`.

**Encoding:** R-type, func 27, `0000 ddd ddd 011011`.

**Flags:** unchanged.

**Cycles:** 1.

**Notes:** Clears the high byte of the register.

### 15.6 Branches

#### Bcc — Conditional branch

**Syntax**

```asm
BEQ   label
BNE   label
BCS   label        ; alias: BHS
BCC   label        ; alias: BLO
BMI   label
BPL   label
BVS   label
BVC   label
BHI   label
BLS   label
BGE   label
BLT   label
BGT   label
BLE   label
BRA   label        ; alias: BAL
BNV   label
```

**Operation:** if the condition is true, `PC = PC + 2 + disp8 * 2`; otherwise
continue with the next instruction.

**Encoding:** B-type, `0111 cccc dddddddd`.

**Flags:** unchanged.

**Cycles:** 1 if not taken, 2 if taken.

**Notes:** The displacement is an 8-bit signed number of words, so the range is
`-128..+127` words (`-256..+254` bytes). The assembler does not automatically
relax an out-of-range branch; it reports `branch target out of range`. Use a
`JMP` for longer distances. A `Bcc` target must also be in the same section as
the branch. The assembler emits no relocation for `Bcc`; a branch to another
section is silently wrong. Use `JMP` for cross-section or external targets.

`BRA` is condition 14 (always). `BNV` is condition 15 (never), useful rarely.
`BHS` is `BCS`; `BLO` is `BCC`; `BAL` is `BRA`.

### 15.7 Jumps, calls, and returns

#### JMP — Jump

**Syntax**

```asm
JMP   label
JMP   Rs
JMP   #addr
```

**Operation**

- `JMP label`: `PC = address + 4 + rel16 * 2`.
- `JMP Rs`: `PC = Rs`.
- `JMP #addr`: assembler emits `MOV PC, #addr`.

**Encoding**

- Relative: C-type, subop 0, `1000 0000 000 00000` + `rel16`.
- Register: C-type, subop 2, `1000 0010 aaa 00000` (regA = `Rs`).
- `#addr`: X-type `MOV PC, #imm16`, `1111 0000 111 00000` + `addr16`.

**Flags:** unchanged.

**Cycles:** relative 3; register 2; `MOV PC` 2.

**Notes:** `JMP label` is a 32-bit instruction and does not push a return
address. `JMP Rs` is a 16-bit instruction. `JMP .` is an infinite loop.

#### CALL — Call subroutine

**Syntax**

```asm
CALL  label
CALL  Rs
```

**Operation**

- `CALL label`: push next `PC`; `PC = address + 4 + rel16 * 2`.
- `CALL Rs`: push next `PC`; `PC = Rs`.

**Encoding**

- Relative: C-type, subop 1, `1000 0001 000 00000` + `rel16`.
- Register: C-type, subop 3, `1000 0011 aaa 00000` (regA = `Rs`).

**Flags:** unchanged.

**Cycles:** relative 4; register 3.

**Notes:** The return address is the address of the instruction after the call.
Use `RET` to return. The assembler also accepts `CALL #addr`, but it encodes it
as `MOV PC, #addr` without pushing a return address; that is not a real call.

#### RET — Return from subroutine

**Syntax**

```asm
RET
```

**Operation:** `PC = pop`.

**Encoding:** C-type, subop 4, `1000 0100 000 00000`.

**Flags:** unchanged.

**Cycles:** 3.

**Notes:** `RET` is equivalent to `POP PC` but has a shorter, clearer encoding.
The stack pointer must point at the saved return address.

#### RETI — Return from interrupt

**Syntax**

```asm
RETI
```

**Operation:** `PC = pop`; `SR = pop`.

**Encoding:** C-type, subop 5, `1000 0101 000 00000`.

**Flags:** restored from the popped `SR`.

**Cycles:** 5.

**Notes:** Hardware interrupts and `BRK` push `SR` and `PC`; `RETI` restores
both. `TRAP` pushes only `PC`, so a trap handler returns with `RET`, not `RETI`.

### 15.8 Traps, breakpoints, and processor control

#### TRAP — Software trap

**Syntax**

```asm
TRAP  #n
```

**Operation:** push next `PC`; `PC = memory16[0x0010 + 2n]`.

**Encoding:** C-type, subop 6, `1000 0110 nnnnnnnn` (`0x8600 | n`).

**Flags:** unchanged. `TRAP` does not push `SR` and does not change `I`.

**Cycles:** 4.

**Notes:** `n` is 0–119. The table entry is a 16-bit handler address in RAM at
`0x0010 + 2n`. A zero entry halts the VM with `unhandled trap`. A trap handler
returns with `RET`.

#### BRK — Breakpoint

**Syntax**

```asm
BRK
```

**Operation:** push `SR`; push next `PC`; clear `I`; `PC = vector[1]`.

**Encoding:** C-type, subop 7, `1000 0111 000 00000`.

**Flags:** `I` is cleared; the saved `SR` is on the stack.

**Cycles:** 7 in the current VM (exception entry). The ISA summary table lists
4; the VM charges the exception-entry cost.

**Notes:** A debugger may intercept `BRK` before delivery. A normal handler
returns with `RETI`. If vector 1 is zero, the VM halts with an unhandled
exception.

#### HLT — Halt

**Syntax**

```asm
HLT
```

**Operation:** stop the CPU.

**Encoding:** C-type, subop 8, `1000 1000 000 00000`.

**Flags:** unchanged.

**Cycles:** 1.

**Notes:** In the VM, `HLT` ends execution and the process exits with
`R0 & 0xFF`. In a design without an operating system, `HLT` simply stops the
CPU.

#### NOP — No operation

**Syntax**

```asm
NOP
```

**Operation:** do nothing.

**Encoding:** C-type, subop 9, `1000 1001 000 00000`.

**Flags:** unchanged.

**Cycles:** 1.

**Notes:** Useful for alignment or as a placeholder.

#### EI — Enable interrupts

**Syntax**

```asm
EI
```

**Operation:** set `I`.

**Encoding:** C-type, subop 10, `1000 1010 000 00000`.

**Flags:** `I` set; other flags unchanged.

**Cycles:** 1.

**Notes:** Interrupts are checked between instructions when `I=1`. Enabling
interrupts does not take a pending interrupt until the next instruction
boundary.

#### DI — Disable interrupts

**Syntax**

```asm
DI
```

**Operation:** clear `I`.

**Encoding:** C-type, subop 11, `1000 1011 000 00000`.

**Flags:** `I` cleared; other flags unchanged.

**Cycles:** 1.

**Notes:** Use `DI` around critical sections that must not be interrupted.

#### WAIT — Wait for interrupt

**Syntax**

```asm
WAIT
```

**Operation:** suspend instruction execution until an interrupt source is
pending.

**Encoding:** C-type, subop 12, `1000 1100 000 00000`.

**Flags:** unchanged.

**Cycles:** 1 to enter.

**Notes:** Architectural behavior is to wait. The current VM treats `WAIT` as a
one-cycle no-op and continues to the next instruction; see
[Section 20](#20-implementation-notes-for-this-release). If you need a real
wait in the VM today, use a loop or rely on `--max-cycles`.

#### SYS — Host system call

**Syntax**

```asm
SYS #n
```

**Operation:** call host service `n`; `R0` points to a 16-byte, 2-byte-aligned
parameter block.

**Encoding:** C-type, subop 15, `1000 1111 nnnnnnnn` (`0x8F00 | n`).

**Flags:** unchanged.

**Cycles:** 10.

**Notes:** This is a VM host call, not a guest exception and not a BIOS trap.
The block layout is:

| Word offset | Field |
|---|---|
| 0 | status |
| 2 | result0 |
| 4 | result1 / high word |
| 6 | result2 |
| 8 | arg0 |
| 10 | arg1 |
| 12 | arg2 |
| 14 | arg3 |

The VM validates the block and guest pointers, performs the service, writes
status/results, leaves `R0` unchanged, and advances `PC`. An invalid block
returns `EFAULT` (status 5); an unimplemented service returns `ENOSYS`
(status 8). Version 1 implements `SYS_VERSION` (`#0`):

- `result0` = host ABI version (1)
- `result1` = console file-descriptor bitmask (16)
- `result2` = 0

The disassembler prints known service names, such as
`SYS #SYS_VERSION`. The assembler accepts a numeric `#n`; there are no
predefined `SYS_*` constants in this release.

---

## 16. Interrupts, exceptions, and traps

KR16 has a simple exception model. There are no privilege modes, and interrupts
do not nest automatically.

### 16.1 Hardware vectors

The hardware vector table is at `0x0000`, one 16-bit handler address per
vector:

| Address | Vector | Source |
|---|---|---|
| `0x0000` | 0 | Illegal instruction |
| `0x0002` | 1 | Breakpoint (`BRK`) |
| `0x0004` | 2 | Divide by zero |
| `0x0006` | 3 | Timer 0 |
| `0x0008` | 4 | Keyboard |
| `0x000A` | 5 | Bus fault |
| `0x000C–0x000F` | 6–7 | Reserved |

A zero vector entry halts the VM with an unhandled-exception diagnostic.

### 16.2 Exception entry

When an exception or hardware interrupt is taken:

1. `SP -= 2`; `[SP] = SR`.
2. `SP -= 2`; `[SP] = PC`.
3. Clear `I`.
4. `PC = vector[vector_number]`.
5. 7 cycles.

The saved `PC` depends on the exception:

| Exception | Saved PC |
|---|---|
| Illegal instruction | Faulting instruction |
| Breakpoint | Next instruction |
| Divide by zero | Faulting instruction |
| Bus fault | Faulting instruction |

`RETI` pops `PC` and then `SR`, restoring the interrupted state.

### 16.3 Interrupt priority

Hardware interrupts are checked between instructions when `I=1`. The keyboard
has priority over the timer. Hardware interrupts do not nest automatically
because entry clears `I`; a handler that wants nesting must re-enable
interrupts explicitly.

### 16.4 Traps

A trap is a software request for a service:

```asm
MOV   R1, #'A'
TRAP  #0                 ; putchar
```

`TRAP #n` pushes only the return address and jumps through the BIOS jump table
at `0x0010 + 2n`. It does not push `SR` and does not change `I`. A trap handler
returns with `RET`.

The jump table has 120 entries, `n = 0..119`. A zero entry is an unhandled trap
in the VM.

### 16.5 `BRK` versus `TRAP`

| Feature | `TRAP #n` | `BRK` |
|---|---|---|
| Purpose | Request a BIOS service | Breakpoint / debugger |
| Pushes | Return address | `SR` and return address |
| Changes `I` | No | Clears `I` |
| Handler returns with | `RET` | `RETI` |
| Table | BIOS jump table at `0x0010` | Hardware vector 1 at `0x0002` |

### 16.6 `SYS` versus `TRAP`

`SYS #n` is a VM host service. It does not use the guest vector or BIOS
jump table. It passes a 16-byte block in `R0` and returns status/results in that
block. Use it for host services such as version information. Use `TRAP` for
guest BIOS services.

---

## 17. I/O and system services

### 17.1 Memory-mapped I/O

The console, keyboard, and timer are memory-mapped in `0xF000–0xF0FF`.

| Address | Register | Access | Meaning |
|---|---|---|---|
| `0xF000` | Console data | Write | Write a byte to standard output |
| `0xF001` | TX ready | Read | Always 1 in the current VM |
| `0xF002` | Console echo | Read/write | Bit 0 enables keyboard echo |
| `0xF004` | Keyboard data | Read | Pop the next keyboard byte (0 if empty) |
| `0xF005` | Keyboard status | Read/write | Bit 0 = data available; bit 1 = overflow; write bit 1 to clear overflow |
| `0xF006` | Keyboard IRQ enable | Read/write | Bit 0 enables the keyboard interrupt |
| `0xF008` | Timer control | Read/write | Bit 0 enable; bit 1 periodic; bit 2 IRQ enable |
| `0xF00A` | Timer count low | Read/write | Low byte of the counter |
| `0xF00B` | Timer count high | Read/write | High byte of the counter |
| `0xF00C` | Timer compare low | Read/write | Low byte of the compare value |
| `0xF00D` | Timer compare high | Read/write | High byte of the compare value |
| `0xF00E` | Timer match | Read/write | Read 1 if matched; write 1 to clear |
| `0xF010` | Timer prescale low | Read/write | Low byte of the prescaler |
| `0xF011` | Timer prescale high | Read/write | High byte of the prescaler |
| `0xF012` | IRQ status | Read | Bit 0 timer IRQ pending; bit 1 keyboard IRQ pending |

Writing a byte to `0xF000` appends it to the VM's console output. The VM
collects console output and writes it to standard output.

A minimal character output routine:

```asm
; void putchar(char c) -- argument in R1
putchar:
    MOV   R0, #0xF000
    STB   [R0], R1
    RET
```

A minimal character input routine:

```asm
; char getchar(void)
getchar:
    MOV   R0, #0xF004
    LDB   R0, [R0]
    RET
```

### 17.2 BIOS services

The BIOS jump table is at `0x0010`, with entry `n` at `0x0010 + 2n`. A service
BIOS can install handlers for the following conventional services:

| `n` | Service |
|---|---|
| 0 | `putchar` |
| 1 | `getchar` |
| 2 | `puts` |
| 3 | `exit` |
| 4 | `timer_set` |
| 5 | `timer_read` |
| 6 | `kbd_read` |
| 7 | `kbd_status` |
| 8 | `random` |
| 9 | `memcpy` |
| 10 | `memset` |
| 11 | `strlen` |
| 12 | `version` |
| 13 | `delay` |
| 14–119 | Reserved |

The minimal reference BIOS shipped with the toolchain does not install these
services; it only jumps to the program at `0x0100`. The extended example in
[Appendix B](#appendix-b-the-kr16-bios) shows one way to install a small service
table.

### 17.3 Host calls

`SYS #0` (`SYS_VERSION`) and the sandboxed file, time, random, and
filesystem-information services are implemented. The complete C interface is
in [systemservices.md](systemservices.md). The block layout and status codes
are documented with the `SYS` instruction in
[Section 15](#15-instruction-set-reference).

A complete example:

```asm
; syscall_demo.s - ask the VM host for its ABI version.
.data
.align 2
host_block:
    .space 16               ; status, 3 results, 4 arguments

.text
.global _start
_start:
    MOV   R0, #host_block
    SYS #0              ; SYS_VERSION
    LD    R0, [R0+2]        ; result0 = host ABI version (1)
    HLT                     ; exit status 1
```

With `kr16 vm syscall_demo.kex --trace-syscalls`, the VM prints:

```text
SYS #0 block=0x10a status=0 result0=1 result1=16
```

---

## 18. Instruction timing

The VM counts simulated cycles for the timer and for deterministic traces. The
counts below are the 16-bit forms unless noted.

| Class | Cycles |
|---|---|
| 16-bit ALU register or immediate | 1 |
| 16-bit load, store, or `LEA` | 2 |
| 16-bit conditional branch | 1 not taken, 2 taken |
| `JMP Rs` | 2 |
| `CALL Rs` | 3 |
| `RET` | 3 |
| `RETI` | 5 |
| `PUSH` / `POP` | 2 |
| `TRAP` | 4 |
| `BRK` | 7 in the current VM (exception entry) |
| `SYS` | 10 |
| `HLT`, `NOP`, `EI`, `DI` | 1 |
| 32-bit instruction | +1 over the equivalent 16-bit form |
| Hardware interrupt entry | 7 |
| Unaligned RAM word access | +1 |
| `WAIT` | 1 to enter; cycles continue in the current VM |

Examples:

- `MOV R0, #1` — 1 cycle.
- `MOV R0, #0x1234` — 2 cycles (32-bit).
- `LD R0, [R1+4]` — 2 cycles.
- `LD R0, [0xF004]` — 3 cycles (32-bit load).
- `BEQ` not taken — 1 cycle; taken — 2 cycles.
- `JMP label` — 3 cycles.
- `CALL label` — 4 cycles.
- `RET` — 3 cycles.
- `SYS #0` — 10 simulated cycles; host wall time does not advance the
  simulated timer.

The timer advances with simulated cycles, not wall-clock time. Real-time pacing
is not implemented in this release; `--realtime` and `--mhz` are accepted but
currently have no effect.

---

## 19. Encoding examples

This section shows complete encodings you can check against the disassembler.
The hex values are what the assembler emits.

### 19.1 Bit-level examples

```text
ADD R0, R1       0x0040      0000 0000 0100 0000
                             R-type: Rd=0, Rs=1, func=0

ADD R0, #1       0x1001      0001 0000 0000 0001
                             I-type: Rd=0, func=0, imm6=1

ADD R0, #100     0xF100      1111 0001 0000 0000
                 0x0064      X-type: ext=1, Rd=0, word2=100

MOV R0, #0x12    0x1192      0001 0001 1001 0010
                             I-type: Rd=0, func=6, imm6=0x12

MOV R0, #100     0x9064      1001 0000 0110 0100
                             M8: Rd=0, sign=0, imm8=100

MOV R0, #0x1234  0xF000      1111 0000 0000 0000
                 0x1234      X-type: ext=0, Rd=0, word2=0x1234

LD R0, [R1+4]    0x2044      0010 0000 0100 0100
                             M-type: LD, Rd=0, Rs=1, disp6=4

ST [R1+4], R0    0x3044      0011 0000 0100 0100
                             M-type: ST, Rd=0, Rs=1, disp6=4

LD R0, [0xF004]  0xF800      1111 1000 0000 0000
                 0xF004      X-type: ext=8, Rd=0, addr16=0xF004

LD R0, [R1+100]  0xFD01      1111 1101 0000 0001
                 0x0064      X-type: ext=13, Rd=0, Rs=1, disp16=100

BEQ label        0x7003      0111 0000 0000 0011
                             B-type: cond=0, disp8=3 words

BRA label        0x7EFF      0111 1110 1111 1111
                             B-type: cond=14, disp8=-1 word

JMP label        0x8000      1000 0000 0000 0000
                 0x0006      C-type: subop=0, rel16=6 words

CALL R1          0x8320      1000 0011 0010 0000
                             C-type: subop=3, regA=1

RET              0x8400      1000 0100 0000 0000
                             C-type: subop=4

TRAP #2          0x8602      1000 0110 0000 0010
                             C-type: subop=6, n=2

SYS #0       0x8F00      1000 1111 0000 0000
                             C-type: subop=15, n=0

PUSH R0          0x8D00      1000 1101 0000 0000
                             C-type: subop=13, regA=0

POP R0           0x8E00      1000 1110 0000 0000
                             C-type: subop=14, regA=0

HLT              0x8800      1000 1000 0000 0000
                             C-type: subop=8

MULS R0, R1      0x005C      0000 0000 0101 1100
                             R-type: Rd=0, Rs=1, func=28

SHL R0, #1       0xB001      1011 0000 0000 0001
                             S-type: Rd=0, func=0, count=1

BTST R0, #1      0xC001      1100 0000 0000 0001
                             BIT-type: Rd=0, func=0, bit=1
```

### 19.2 Encoding worksheet

To decode a 16-bit instruction:

1. Look at the top nibble.
2. If it is `0x0`, it is R-type: split into `Rd` (bits 11–9), `Rs` (8–6), and
   function (5–0).
3. If it is `0x1`, it is I-type: `Rd`, function, and a signed 6-bit immediate.
4. If it is `0x2`–`0x6` or `0xA`, it is M-type: `Rd`, `Rs`, and a signed 6-bit
   displacement.
5. If it is `0x7`, it is a branch: condition (11–8) and signed 8-bit word
   displacement (7–0).
6. If it is `0x8`, it is a control instruction: subop (11–8), `regA` (7–5),
   rest (4–0). Subops 0 and 1 have a second word.
7. If it is `0x9`, it is `MOV` immediate 8-bit: `Rd`, sign bit, immediate.
8. If it is `0xB`, it is a shift immediate: `Rd`, function, count.
9. If it is `0xC`, it is a bit immediate: `Rd`, function, bit.
10. If it is `0xF`, it is 32-bit X-type: ext (11–8), `Rd` (7–5), aux (4–0), and
    a second word.

---

## 20. Implementation notes for this release

The ISA reference and this manual describe the architecture. A few details of
the 0.1.0 implementation are worth knowing because they affect day-to-day use.

### 20.1 Branch relaxation is not implemented

`Bcc` is always a 16-bit instruction with an 8-bit signed word displacement.
The assembler does not replace an out-of-range branch with an inverted branch
around a `JMP`. It reports:

```text
branch target out of range
```

Use `JMP` for long distances, or restructure the code.

### 20.2 `.L` labels are ordinary labels

A label beginning with `.L` is not scoped to a function and is not hidden from
the linked image. Use unique names; do not rely on `.L` for local scope.

### 20.3 External symbols need `.extern`

A reference to an undeclared symbol is rejected with `undefined symbol`. Always
declare external symbols:

```asm
.extern putchar
```

### 20.4 Linked-image disassembly is numeric

The disassembler prints labels for relocatable `.o` files. For a linked `.kex`,
it prints numeric addresses. Use `kr16 ld --map` to correlate addresses and
symbols.

### 20.5 16-bit displacements print unsigned

A `disp16` field is a 16-bit two's-complement value added modulo 64 KiB. The
disassembler prints it as an unsigned number. For example:

```text
ST [R1+64536], R0       ; actually [R1-1000]
```

Subtract 65536 from values above 32767 to read them as negative.

### 20.6 `WAIT` does not suspend the VM

The ISA describes `WAIT` as waiting for an interrupt. The current VM consumes
one cycle and continues to the next instruction. Do not rely on `WAIT` to block
execution in this release.

### 20.7 `BRK` timing

`BRK` enters through the shared exception path, which the VM charges 7 cycles.
The ISA summary table lists 4 cycles for `BRK`; the difference is an
implementation detail of the current exception path.

### 20.8 `.size` needs a constant

The common `.-symbol` size expression is not supported. Use a literal or a
constant expression:

```asm
.size sum_to, 20
```

### 20.9 `.skip` is not implemented

Use `.space` or `.zero`.

### 20.10 Branch targets and external symbols

`Bcc` targets must be in the same section as the branch. The assembler emits no
relocation for `Bcc`, so a cross-section branch is silently wrong. Use `JMP` or
`CALL` for cross-section and external targets. The assembler reports
`external branch target is not supported` when a branch cannot be resolved.

### 20.11 `SYS` service names

The disassembler knows the names of the host services (`SYS_VERSION`,
`SYS_OPEN`, and so on) and prints them. The assembler does not define `SYS_*`
constants; write the numeric service number, for example `SYS #0`.

### 20.12 Real-time pacing

`--realtime` and `--mhz` are accepted by `kr16 vm` but real-time pacing is not
implemented in this release. The VM runs as fast as it can, and the simulated
timer advances only with simulated cycles.

---

## Appendix A: complete example programs

The listings in this appendix are complete and were assembled and run with the
0.1.0 toolchain. They are a good starting point for experiments.

### A.1 Print one character

```asm
; first.s - print one character and stop.
.text
.global _start
_start:
    MOV   R0, #'H'          ; character to print
    MOV   R1, #0xF000       ; console data register
    STB   [R1], R0          ; write one byte
    MOV   R0, #0            ; exit status 0
    HLT
```

Build and run:

```text
kr16 as first.s -o first.o
kr16 ld first.o -o first.kex
kr16 vm first.kex
```

Output: `H` (no newline). Exit status: 0.

### A.2 Hello, World!

```asm
; hello.s - write "Hello, World!\n" to the console.
.text
.global _start
_start:
    MOV   R1, #0xF000       ; console data register
    MOV   R2, #message      ; R2 points at the string
.next:
    LDB   R0, [R2]          ; load one character
    CMP   R0, #0            ; NUL terminator?
    BEQ   .done
    STB   [R1], R0          ; send it to the console
    INC   R2
    BRA   .next
.done:
    MOV   R0, #0
    HLT

.rodata
message:
    .asciz "Hello, World!\n"
```

Output:

```text
Hello, World!
```

### A.3 Sum 1 through 10

```asm
; loop_sum.s - sum 1..10 into R0, then halt.
.text
.global _start
_start:
    MOV   R0, #0            ; total = 0
    MOV   R1, #1            ; i = 1
    MOV   R2, #10           ; limit
.loop:
    CMP   R1, R2
    BGT   .done
    ADD   R0, R1            ; total += i
    INC   R1
    BRA   .loop
.done:
    HLT                     ; exit status = 55
```

Exit status: 55.

### A.4 A function with a frame

```asm
; sum_to.s - a complete function with a frame and locals.
; int sum_to(int n) {
;     int total = 0;
;     for (int i = 1; i <= n; i++) total += i;
;     return total;
; }
.text
.global _start
_start:
    MOV   R1, #10           ; argument n
    CALL  sum_to
    HLT                     ; exit status = 55

sum_to:
    PUSH  R4                ; save caller's FP
    MOV   R4, SP            ; establish our frame
    SUB   SP, #6            ; three local words: n, total, i
    ST    [R4-2], R1        ; n     = argument
    MOV   R0, #0
    ST    [R4-4], R0        ; total = 0
    MOV   R0, #1
    ST    [R4-6], R0        ; i     = 1
.loop:
    LD    R0, [R4-6]        ; R0 = i
    LD    R1, [R4-2]        ; R1 = n
    CMP   R0, R1
    BGT   .done             ; while (i <= n)
    LD    R1, [R4-4]        ; R1 = total
    ADD   R1, R0            ; total += i
    ST    [R4-4], R1
    INC   R0                ; i++
    ST    [R4-6], R0
    BRA   .loop
.done:
    LD    R0, [R4-4]        ; return total
    MOV   SP, R4            ; discard locals
    POP   R4                ; restore caller's FP
    RET
```

Exit status: 55.

### A.5 Stack arguments

```asm
; stack_args.s - pass two arguments on the stack.
.text
.global _start
_start:
    MOV   R1, #1            ; a
    MOV   R2, #2            ; b
    MOV   R3, #3            ; c
    MOV   R0, #5            ; e (rightmost argument first)
    PUSH  R0
    MOV   R0, #4            ; d
    PUSH  R0
    CALL  f
    ADD   SP, #4            ; caller removes d and e
    HLT                     ; R0 = 9

; int f(int a, int b, int c, int d, int e)
f:
    PUSH  R4
    MOV   R4, SP
    LD    R0, [R4+4]        ; d
    LD    R2, [R4+6]        ; e
    ADD   R0, R2            ; d + e
    MOV   SP, R4
    POP   R4
    RET
```

Exit status: 9.

### A.6 Directives

```asm
; directives.s - exercises the data and metadata directives.
.equ CONSOLE, 0xF000
.equ NEWLINE, 10
.equ MASK, 0x00FF

.text
.global _start
_start:
    MOV  R1, #CONSOLE
    MOV  R0, #'H'
    STB  [R1], R0
    MOV  R0, #NEWLINE
    STB  [R1], R0
    MOV  R0, #0
    HLT

.rodata
.align 2
banner:
    .ascii "KR16"           ; four bytes, no terminator
    .byte  0                ; explicit terminator
    .asciz "ready\n"
    .word  CONSOLE          ; 16-bit word
    .long  0x12345678       ; 32-bit long
    .space 4                ; four zero bytes

.data
.align 4
counter:
    .word 0

.bss
.align 2
scratch:
    .space 32
```

Output: `H` followed by a newline. Exit status: 0.

### A.7 Include and macro

`macros.inc`:

```asm
; macros.inc - a tiny include file with a macro.
.macro emit_char value
    MOV   R0, #\value
    MOV   R3, #0xF000
    STB   [R3], R0
.endm
```

`macro_demo.s`:

```asm
; macro_demo.s - include a file, expand a macro, use .if/.else.
.include "macros.inc"
.equ USE_UPPER, 1

.text
.global _start
_start:
.if USE_UPPER
    emit_char 'O'
    emit_char 'K'
.else
    emit_char 'n'
    emit_char 'o'
.endif
    emit_char '\n'
    MOV   R0, #0
    HLT
```

Build:

```text
kr16 as macro_demo.s -o macro_demo.o
kr16 ld macro_demo.o -o macro_demo.kex
kr16 vm macro_demo.kex
```

Output: `OK` followed by a newline. Exit status: 0.

### A.8 Host system call

```asm
; syscall_demo.s - ask the VM host for its ABI version.
.data
.align 2
host_block:
    .space 16               ; status, 3 results, 4 arguments

.text
.global _start
_start:
    MOV   R0, #host_block
    SYS #0              ; SYS_VERSION
    LD    R0, [R0+2]        ; result0 = host ABI version (1)
    HLT                     ; exit status 1
```

Exit status: 1. With `--trace-syscalls`, the host call is printed.

---

## Appendix B: the KR16 BIOS

### B.1 The minimal reference BIOS

The BIOS source shipped with the repository is deliberately tiny. It is a
stateless ROM that jumps to the normal program entry:

```asm
; Minimal stateless reference BIOS for KR16.
.org 0xF100
  JMP #0x0100

.org 0xFFFE
  .word 0xF100
```

Build it as a raw 3.75 KiB ROM:

```text
kr16 as resources/bios.s -o bios.bin --raw --base 0xF100
```

The output is 3,840 bytes. The code at `0xF100` is:

```text
0xF100  MOV PC, #0x100
```

`JMP #0x0100` is a pseudo-instruction for `MOV PC, #0x100`. It does not push a
return address; it simply transfers control.

The reset vector at `0xFFFE` contains `0xF100`, so after reset the CPU starts at
`0xF100`, executes the jump, and arrives at `0x0100`.

The rest of the ROM is zero padding up to `0xFFFE`. A raw disassembly will
decode that padding as instructions (`0x0000` is `ADD R0, R0`); do not mistake
padding for code.

### B.2 How the VM boots a `.kex`

1. The VM loads the BIOS ROM at `0xF100`.
2. It reads the reset vector at `0xFFFE` and sets `PC` to that value.
3. The BIOS executes and jumps to `0x0100`.
4. The VM has already loaded the `.kex` sections at their recorded addresses.
5. Your program runs from `0x0100`.

With `--no-bios`, the VM starts directly at the image entry point. With
`--bios path`, it loads a custom ROM instead of the embedded one.

### B.3 An extended service BIOS

The minimal BIOS does not install trap services. The following stateless BIOS
installs a small jump table and a few handlers. It is a complete, working
example of direct MMIO and BIOS table setup.

```asm
; bios_demo.s - a slightly larger stateless BIOS for the KR16 ROM window.
; Build: kr16 as bios_demo.s -o bios_demo.bin --raw --base 0xF100
; Install jump-table entries, then jump to the program at 0x0100.

.equ CONSOLE, 0xF000
.equ KEYBOARD, 0xF004
.equ TRAP_TABLE, 0x0010
.equ PROGRAM_ENTRY, 0x0100

.org 0xF100
bios_start:
    DI
    MOV   SP, #0xF000

    ; BIOS jump table: service 0 = putchar, 1 = getchar, 2 = puts,
    ; 3 = exit, 12 = version. All other entries point at unimplemented.
    MOV   R2, #TRAP_TABLE
    MOV   R0, #bios_putchar
    ST    [R2], R0
    MOV   R0, #bios_getchar
    ST    [R2+2], R0
    MOV   R0, #bios_puts
    ST    [R2+4], R0
    MOV   R0, #bios_exit
    ST    [R2+6], R0

    ; Entries 4..119 default to unimplemented.
    ADD   R2, #8
    MOV   R0, #bios_unimplemented
    MOV   R1, #116
.fill:
    ST    [R2], R0
    ADD   R2, #2
    DEC   R1
    BNE   .fill

    ; Entry 12 is version.
    MOV   R0, #bios_version
    MOV   R2, #TRAP_TABLE
    ST    [R2+24], R0

    JMP   #PROGRAM_ENTRY

; void putchar(char c) -- argument in R1
bios_putchar:
    MOV   R0, #CONSOLE
    STB   [R0], R1
    MOV   R0, #0
    RET

; char getchar(void) -- returns the next keyboard byte, or 0 if none
bios_getchar:
    MOV   R0, #KEYBOARD
    LDB   R0, [R0]
    RET

; void puts(const char *s) -- argument in R1, NUL-terminated
bios_puts:
    MOV   R2, #CONSOLE
.puts_loop:
    LDB   R0, [R1]
    CMP   R0, #0
    BEQ   .puts_done
    STB   [R2], R0
    INC   R1
    BRA   .puts_loop
.puts_done:
    MOV   R0, #0
    RET

; void exit(int status) -- argument in R1
bios_exit:
    MOV   R0, R1
    HLT

; void version(void) -- returns BIOS version in R0
bios_version:
    MOV   R0, #1
    RET

; Unimplemented trap: return an error marker.
bios_unimplemented:
    MOV   R0, #0xFFFF
    RET

.org 0xFFFE
    .word bios_start
```

Build:

```text
kr16 as bios_demo.s -o bios_demo.bin --raw --base 0xF100
```

The result is exactly 3,840 bytes. The handlers are placed after the startup
code in the same ROM; the jump table contains their absolute addresses. The
`.fill` loop sets entries 4 through 119 to `bios_unimplemented`, then entry 12
is overwritten with `bios_version`.

A program can use the services through `TRAP`:

```asm
; hello_bios.s - use BIOS trap services instead of direct MMIO.
.text
.global _start
_start:
    MOV   R1, #message
    TRAP  #2                ; puts(message)
    MOV   R1, #0
    TRAP  #3                ; exit(0)

.rodata
message:
    .asciz "Hello from the BIOS!\n"
```

Build and run with the extended BIOS:

```text
kr16 as hello_bios.s -o hello_bios.o
kr16 ld hello_bios.o -o hello_bios.kex
kr16 vm hello_bios.kex --bios bios_demo.bin
```

Output:

```text
Hello from the BIOS!
```

The default embedded BIOS is the minimal one, so a trap program will report
`unhandled trap` unless you pass `--bios bios_demo.bin`.

### B.4 BIOS rules

A v1 BIOS is stateless: `.text` and `.rodata` only, no `.data` or `.bss`. It
must fit in the 3,840-byte ROM window and place its reset vector at `0xFFFE`.
Use `.org` to place the startup code at `0xF100` and the reset vector at
`0xFFFE`:

```asm
.org 0xF100
bios_start:
    ...
.org 0xFFFE
    .word bios_start
```

The `.org` gaps are zero-filled. Keep the total at or below 3,840 bytes.

---

## Appendix C: quick reference

### C.1 Register quick reference

| Register | Code | Notes |
|---|---|---|
| `R0` | 0 | Return/scratch; pair base with `R1` |
| `R1` | 1 | Arg 1; return high; pair high |
| `R2` | 2 | Arg 2; pair base with `R3` |
| `R3` | 3 | Arg 3; pair high |
| `R4`/`FP` | 4 | Frame pointer; callee-saved |
| `SP` | 5 | Stack pointer; full-descending |
| `SR` | 6 | Flags and interrupt enable |
| `PC` | 7 | Program counter |

### C.2 Flag quick reference

```text
SR:  15..5 reserved (read 0) | 4 I | 3 V | 2 N | 1 Z | 0 C
```

- `C` — carry / no borrow.
- `Z` — zero.
- `N` — negative (bit 15).
- `V` — signed overflow.
- `I` — interrupt enable.

### C.3 Condition quick reference

| Signed | Unsigned | Meaning |
|---|---|---|
| `BEQ` | `BEQ` | equal |
| `BNE` | `BNE` | not equal |
| `BGE` | `BCS`/`BHS` | greater/equal or higher/equal |
| `BLT` | `BCC`/`BLO` | less or lower |
| `BGT` | `BHI` | greater or higher |
| `BLE` | `BLS` | less/equal or lower/equal |

### C.4 Directive quick reference

| Directive | Use |
|---|---|
| `label:` | Define a symbol at the current address |
| `.text` | Code section |
| `.crt0` | Optional startup code section |
| `.rodata` | Read-only data |
| `.data` | Initialized writable data |
| `.bss` | Zero-initialized data |
| `.section name` | Custom section |
| `.byte`, `.word`, `.long` | Emit 1, 2, or 4 bytes |
| `.ascii`, `.asciz` | Emit strings |
| `.space`, `.zero` | Reserve bytes |
| `.align`, `.balign`, `.p2align` | Align the location |
| `.equ`, `.set` | Define constants |
| `.global`, `.globl` | Export symbols |
| `.extern` | Import symbols |
| `.type`, `.size` | Symbol metadata |
| `.file`, `.loc` | Debug line information |
| `.include` | Include a file |
| `.macro`/`.endm` | Define a macro |
| `.if`/`.else`/`.endif` | Conditional assembly |
| `.org` | Absolute placement in raw mode |

### C.5 Common MMIO addresses

| Address | Use |
|---|---|
| `0xF000` | Console data (write a byte) |
| `0xF004` | Keyboard data (read a byte) |
| `0xF005` | Keyboard status |
| `0xF006` | Keyboard IRQ enable |
| `0xF008` | Timer control |
| `0xF00A`/`0xF00B` | Timer count |
| `0xF00C`/`0xF00D` | Timer compare |
| `0xF00E` | Timer match |
| `0xF012` | IRQ status |

### C.6 Syscall block

| Offset | Field |
|---|---|
| 0 | status |
| 2 | result0 |
| 4 | result1 / high word |
| 6 | result2 |
| 8 | arg0 |
| 10 | arg1 |
| 12 | arg2 |
| 14 | arg3 |

### C.7 Pipeline

```text
program.s --kr16 as--> program.o --kr16 ld--> program.kex --kr16 vm--> output
```

Inspect:

```text
kr16 dis program.o
kr16 dis program.kex --no-hex
kr16 ld program.o -o program.kex --map program.map
kr16 size program.kex
kr16 debug program.kex --lines
```

---

## Appendix D: troubleshooting and glossary

### D.1 Diagnostics: assembler

Assembler diagnostics have the form:

```text
file:line:column: message
```

The assembler stops at the first error. Common messages:

| Message | Meaning / fix |
|---|---|
| `unknown instruction` | The mnemonic is not recognized. Check spelling; mnemonics are case-insensitive but must be in the ISA. |
| `expected [memory] operand` | `LD`/`ST`/`LDB`/`LDSB`/`STB`/`LEA` need brackets around the memory operand. |
| `LEA has no absolute form` | Use `LEA Rd, [Rs+disp]`, or `MOV Rd, #symbol` for an absolute symbol. |
| `byte indexed form requires a 6-bit displacement` | A byte load/store displacement is outside `-32..31`; use a register for the offset. |
| `branch target out of range` | `Bcc` is limited to `-256..+254` bytes; use `JMP`. |
| `external branch target is not supported` | `Bcc` cannot target another object; use `JMP`/`CALL`. |
| `duplicate label` | Two labels with the same case-sensitive name. |
| `symbolic expression is too complex` | Only symbol ± constant is supported; simplify the expression. |
| `raw assembly cannot use external symbols` | Raw mode has no linker; define the symbol locally. |
| `.org is only allowed in raw mode` | Add `--raw`, or use sections for relocatable code. |
| `cannot emit data in a NOBITS section` | Use `.space`/`.zero` in `.bss`. |
| `division by zero` | A constant expression divided by zero. |
| `missing .endm` | A `.macro` has no matching `.endm`. |
| `macro ... expects ... arguments` | The invocation's argument count does not match the definition. |
| `cannot find include` | The `.include` path is wrong; add `-I`. |

### D.2 Diagnostics: linker

| Message | Meaning / fix |
|---|---|
| `undefined symbol foo` | A referenced symbol is not defined. Add `.extern`, link the defining object, or fix the spelling. |
| `duplicate global symbol foo` | Two objects export the same name. |
| `no .text section` | The link inputs contain no `.text`; add code. |
| `ABS16 relocation out of range` | A 16-bit absolute address does not fit. |
| `PCREL16W relocation out of range` | A `JMP`/`CALL` target is too far away. |
| `image exceeds memory` | Sections do not fit in 64 KiB. |
| `inconsistent section type` | The same section name is PROGBITS in one object and NOBITS in another. |

### D.3 Diagnostics: VM

| Message | Meaning / fix |
|---|---|
| `unhandled exception 0` | Illegal instruction. Often the program fell through into data; add `HLT` or a branch. |
| `unhandled exception 2` | Divide by zero with vector 2 unset. |
| `unhandled exception 5` | Bus fault: odd instruction address or stack guard. |
| `unhandled trap` | `TRAP #n` read a zero BIOS jump-table entry. |
| `maximum cycle count reached` | The program did not halt before `--max-cycles`. |
| `program entry is below the first page` | A raw image's entry is below `0x0100`; use a valid `--base`. |
| `cannot open ...` | The program path is wrong. |

### D.4 Glossary

**Addressing mode** — The rule for computing an operand's address or value,
such as register, immediate, indirect, indexed, or absolute.

**Assembler** — The tool that turns `.s` source into a relocatable `.o` object.

**Bit** — A binary digit, 0 or 1.

**Byte** — Eight bits. KR16 memory is byte-addressable.

**Directive** — An assembler command beginning with `.`, such as `.text` or
`.word`; it does not become a machine instruction.

**Entry point** — The address at which execution begins. Usually `_start` at
`0x0100`.

**Flag** — A single-bit condition in `SR`, such as `Z` or `C`.

**Frame pointer** — `R4`/`FP`, pointing at the saved caller frame pointer in a
stack frame.

**Immediate** — A constant encoded in the instruction, written with `#`.

**Instruction** — A machine operation, such as `ADD` or `LD`.

**Kex** — A KR16 executable container, produced by the linker.

**Label** — A name for an address, written with a trailing colon.

**Linker** — The tool that combines `.o` files, resolves symbols, lays out
sections, and writes a `.kex`.

**Little-endian** — Low byte first in memory.

**MMIO** — Memory-mapped I/O; device registers appear at memory addresses.

**Object** — A relocatable `.o` file containing code, data, symbols, and
relocations, but not final addresses.

**Opcode** — The part of an instruction that selects the operation.

**PC** — Program counter, the address of the next instruction.

**Raw binary** — A flat image with no container header, used for ROMs and
simple loaders.

**Register** — A small, fast storage location inside the CPU.

**Relocation** — A placeholder in an object file that the linker fills with a
final address or displacement.

**Section** — A named region of an object or executable, such as `.text` or
`.data`.

**SP** — Stack pointer.

**SR** — Status register, holding flags and the interrupt enable.

**Symbol** — A name for an address, such as a label or an external function.

**Trap** — A software request for a BIOS service, using `TRAP #n`.

**Vector** — An entry in the exception or interrupt table, holding a handler
address.

**Word** — 16 bits (2 bytes) on KR16.

---

*End of manual.*
