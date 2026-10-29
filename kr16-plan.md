# KR16 Virtual Computer — Design Plan

**Status:** Draft — CPU architecture proposed; assembler, compiler, linker, VM, and DAP pending
**Last updated:** 2026-10-27
**Implementation language:** C++20 (host toolchain)
**Targets:** Windows 10/11 (Visual Studio 2026) and Linux (GCC/Clang)
**Distribution:** one `kr16` executable

> This is a living design document. Section 5 defines the CPU in detail.
> Sections 6–10 are placeholders for the next design rounds. Nothing here
> has been implemented yet. Exact opcode assignments in Section 5.6 are
> provisional until the ISA is frozen.

---

## 1. Vision

KR16 is a small, understandable 16-bit computer for teaching computer
architecture. A student should be able to:

1. Write assembly, assemble it, and run it on a simulated machine.
2. See registers, flags, memory, and I/O change as the program runs.
3. Write a tiny BIOS and install it into the ROM area.
4. Write C, compile it to KR16 assembly, assemble it, link it, and run it.
5. Debug the result in VS Code with breakpoints, stepping, a call stack,
   locals, globals, and source-level types.

The whole toolchain — assembler, disassembler, C compiler, linker, virtual
machine, and debug adapter — ships as a single `kr16` binary.

## 2. Goals and Non-Goals

### 2.1 Goals

- **Small enough to explain.** A motivated high-school student can learn the
  complete instruction set in a few sittings.
- **C-friendly.** The ISA must support a straightforward C compiler without
  heroic code generation: byte and word accesses, indexed addressing,
  PC-relative branches, hardware multiply/divide, and a clean stack frame.
- **Debuggable first.** Symbols, source lines, types, and stack frames survive
  the toolchain so the DAP can present a rich VS Code experience.
- **One binary.** `kr16 as`, `kr16 dis`, `kr16 cc`, `kr16 ld`, `kr16 vm`, and
  `kr16 dap` are subcommands of a single executable.
- **Cross-platform.** Windows (MSVC / Visual Studio 2026) and Linux
  (GCC/Clang), with no platform-specific behavior leaking into the ISA.
- **Deterministic simulation.** Instruction timing, interrupts, and the timer
  are simulated, not wall-clock-driven, so tests and lessons are repeatable.
- **Relocatable toolchain.** The assembler and compiler emit relocatable
  objects; the linker resolves symbols and lays out the final image.

### 2.2 Non-Goals for v1

- Floating point in hardware. (Software floating point may come later.)
- Virtual memory, an MMU, or privilege rings.
- Pipelining, caches, branch prediction, or delay slots.
- Multiple CPU cores or threads.
- A graphical display. Console I/O only.
- Full ISO C. A practical C99 subset is the target.
- Self-hosting the C compiler. The compiler is a host tool; self-hosting is
  out of scope.

## 3. Design Principles

1. **One source of truth for the ISA.** The CPU, assembler, disassembler,
   compiler, and DAP all consume the same instruction/format tables. A single
   `isa` module defines encodings, mnemonics, operand kinds, and flag effects.
2. **Simple rules, few exceptions.** Prefer orthogonal addressing modes and
   uniform flag behavior. Document every exception.
3. **Text is the compiler/assembler boundary.** The compiler emits assembly
   text; the assembler is the only component that turns instructions into
   machine code. This keeps `-S` output honest and gives students a readable
   artifact.
4. **Debug information is a first-class output.** The compiler emits symbols,
   types, and line mappings; the assembler and linker preserve them; the DAP
   consumes them.
5. **Everything is testable headlessly.** The VM can run without a terminal or
   debugger, with scripted input and a cycle limit, so the toolchain can be
   tested in CI.
6. **Keep the host portable.** All platform-specific code (sockets, raw
   console input, timing) lives behind small interfaces in `platform/`.

## 4. System Overview

### 4.1 Toolchain Flow

```text
  hello.c
     |
     |  kr16 cc hello.c -o hello.o        (compiler emits assembly, assembler
     |                                      runs in-process to make the object)
     v
  hello.o  (relocatable object: sections, symbols, relocations, debug info)
     |
     |  kr16 ld hello.o -o hello.kex      (resolve symbols, lay out memory)
     v
  hello.kex (loadable image: segments, entry point, symbols, types, lines)
     |
     |  kr16 vm hello.kex                  (simulate CPU, RAM, ROM, I/O)
     |  kr16 dap hello.kex                 (same VM + DAP server on TCP)
     v
  console output / VS Code debug session
```

`kr16 cc -S hello.c -o hello.s` stops after emitting assembly.
`kr16 as hello.s -o hello.o` assembles by hand.
`kr16 dis hello.o` or `kr16 dis hello.kex` produces a readable listing.

### 4.2 Single-Binary CLI

```text
kr16 <command> [options]

Commands:
  as       Assemble a .s file into a relocatable .o
  dis      Disassemble a .o or .kex
  cc       Compile C to assembly, then assemble to .o
  ld       Link .o files into a .kex
  size     Report code/data/bss and free heap/stack
  debug    Dump decoded symbols, lines, types, variables, frames, objects
  vm       Run a .kex (or raw binary) on the virtual machine
  dap      Run the VM with the debug adapter listening on TCP
  version  Print tool, ISA, container, host-service ABI, and debug versions
  help     Print help (also: kr16 <command> --help)
```

Examples:

```text
kr16 cc hello.c -o hello.o
kr16 cc hello.c -S -o hello.s
kr16 as hello.s -o hello.o
kr16 ld hello.o -o hello.kex
kr16 size hello.kex
kr16 debug hello.kex
kr16 vm hello.kex
kr16 dap --port 22152 hello.kex
```

`kr16 version` prints
`kr16 <semver> (ISA <n>, container <n>, host ABI <n>, debug <n>)`.
Development builds may append a git hash; `--json` is a later addition.

The CLI is a thin dispatcher. Each command parses its own flags and calls the
corresponding library. The compiler calls the assembler **in-process** rather
than spawning `kr16 as`; this avoids path and quoting problems and keeps the
single-binary story clean.

### 4.3 Repository Shape (proposed)

```text
  CMakeLists.txt
  cmake/
  include/kr16/...
  src/
    cli/          command dispatch and option parsing
    common/       diagnostics, files, strings, JSON, sockets
    isa/          encodings, mnemonics, operand metadata, flag rules
    cpu/          register file, decode/execute, memory bus
    asm/          lexer, parser, encoder, object writer
    disasm/       decoder + printer
    cc/           preprocessor, parser, semantic analysis, codegen
    ld/           object reader, symbol resolution, relocation, layout
    container/    KR16 object/executable reader and writer
    vm/           machine state, loader, devices, execution loop
    dap/          DAP server, JSON protocol, debug state
    platform/     Windows/POSIX sockets, console, timing
  tests/
  examples/
  docs/
  third_party/    (only if a header-only dependency is approved)
  kr16-plan.md
```

Internal **static libraries** (`kr16_isa`, `kr16_cpu`, `kr16_asm`, etc.) keep
modules independently testable. The final `kr16` target links all of them into
one executable.

## 5. CPU Architecture

### 5.1 Summary

| Feature | Choice |
|---|---|
| Word size | 16 bits |
| Address space | 64 KiB, byte-addressable |
| Endianness | Little-endian |
| General registers | R0–R4 (all 16-bit) |
| Special registers | PC, SP, SR (all 16-bit) |
| Instruction size | Variable: 16-bit base, 32-bit when a full immediate/address is needed |
| Addressing | Register, immediate, `[Rn]`, `[Rn+disp]`, PC-relative, absolute |
| Flags | C, Z, N, V, I |
| Interrupts | Maskable, vectored, no automatic nesting |
| Interrupt sources | Timer 0, keyboard |
| Host calls | `SYS #n` to VM host services; separate from BIOS `TRAP` |
| I/O | Memory-mapped 256-byte page at `0xF000` |
| RAM | 60 KiB at `0x0000–0xEFFF`; the first 256 bytes hold the vectors and BIOS jump table |
| ROM | 3.75 KiB at `0xF100–0xFFFF`, write-protected during execution |
| Entry | Program entry at `0x0100`, immediately after the first page |
| Reset | PC <- `[0xFFFE]`, SP = `0xF000` (or below the argv block), SR = 0 |
| Debug hooks | `BRK`, `HLT`, frame-pointer chain, rich debug tables |

### 5.2 Registers

All registers are 16 bits. The 3-bit register field in instructions encodes
eight register codes:

| Code | Name | ABI role | Saved by |
|---|---|---|---|
| 0 | R0 | Return value, scratch | Caller |
| 1 | R1 | First argument, scratch | Caller |
| 2 | R2 | Second argument, scratch | Caller |
| 3 | R3 | Third argument, scratch | Caller |
| 4 | R4 | Frame pointer (FP) | Callee |
| 5 | SP | Stack pointer | Callee |
| 6 | SR | Status register | Special |
| 7 | PC | Program counter | Special |

Notes:

- R0–R3 are caller-saved. R4 and SP are callee-saved.
- Hand-written assembly may use R4 as a general register if it does not call
  C code, but the ABI reserves it.
- Reading PC as a source operand yields the address of the **next**
  instruction (the current instruction's address plus its length). This makes
  `LEA Rd, [PC+disp]`, `LD Rd, [PC+disp]`, and `MOV Rd, PC` useful for
  PC-relative data and position-independent code.
- Writing PC is a jump. Only `MOV PC, ...`, `JMP`, `CALL`, `RET`, `RETI`,
  `TRAP`, `BRK`, interrupts, and reset may write PC. Other instructions that
  try to write PC are illegal instructions.
- SP is writable by ALU and MOV instructions (useful for stack cleanup).
- SR is writable only by `MOV SR, ...`, `RETI`, and the interrupt/exception
  entry sequence. Other writes to SR are illegal.
- `PUSH`/`POP` accept any of the eight register codes, including SR and PC.

### 5.3 Status Register (SR)

| Bit | Name | Meaning |
|---|---|---|
| 0 | C | Carry / no-borrow |
| 1 | Z | Zero |
| 2 | N | Negative (bit 15 of the result) |
| 3 | V | Signed overflow |
| 4 | I | Interrupt enable |
| 5–15 | — | Reserved, read as 0, writes ignored |

Reset value: `SR = 0x0000` (interrupts disabled).

Semantics:

- **Addition:** C is the carry out of bit 15.
- **Subtraction:** C is the **complement of the borrow**. After `CMP a, b`
  (`a - b`), `C = 1` means unsigned `a >= b`; `C = 0` means `a < b`. This
  matches the `CS`/`CC` and `HI`/`LS` condition names below.
- **V:** signed overflow. For addition, set when both operands have the same
  sign and the result has a different sign. For subtraction, analogous.
- `EI` sets I; `DI` clears I.
- `MOV SR, Rs` writes all implemented bits. `MOV Rd, SR` reads them.
- A hardware interrupt pushes SR, clears I, and runs the handler. `RETI`
  restores SR from the stack.

### 5.4 Memory Map

```text
  0x0000 +----------------------------+
         | Hardware vectors           |  8 x 16-bit addresses (RAM)
  0x000F +----------------------------+
  0x0010 | BIOS jump table            |  120 x 16-bit addresses (RAM)
  0x00FF +----------------------------+
  0x0100 |                            |
         | General RAM                |  program, data, heap, stack
         |                            |
  0xEFFF +----------------------------+
  0xF000 | Memory-mapped I/O          |  256 bytes
  0xF0FF +----------------------------+
  0xF100 | BIOS ROM code/data         |  3838 bytes
  0xFFFD +----------------------------+
  0xFFFE | Reset vector               |  16-bit address
  0xFFFF +----------------------------+
```

Sizes:

- RAM region: `0x0000–0xEFFF` = 61,440 bytes = 60 KiB exactly.
- First page: 256 bytes, split into 16 bytes of hardware vectors and 240
  bytes of BIOS jump table.
- General RAM: `0x0100–0xEFFF` = 61,184 bytes = 59.75 KiB.
- MMIO: 256 bytes at `0xF000–0xF0FF`.
- ROM: `0xF100–0xFFFF` = 3,840 bytes = 3.75 KiB, including the reset vector.
- Top 4 KiB region: 256 bytes of I/O plus 3,840 bytes of ROM.

The map is a clean partition of the 64 KiB space: 60 KiB RAM, a 256-byte I/O
page, and a 3.75 KiB ROM. The first page of RAM is reserved for the hardware
vectors and the hookable BIOS jump table, so the program entry is `0x0100`.
If a full 4 KiB ROM is required, shrink the I/O page to 64 bytes
(`0xF000–0xF03F`), which still covers every device defined here; ROM then
starts at `0xF040` and has 4,032 bytes. The 256-byte page is roomier for
future devices and keeps the address arithmetic simple.

#### 5.4.1 Interrupt Vector Table

Each entry is a 16-bit handler address, little-endian, read from RAM:

| Address | Vector | Source | Saved PC points to |
|---|---|---|---|
| `0x0000` | 0 | Illegal instruction | Faulting instruction |
| `0x0002` | 1 | Breakpoint (`BRK`) | Next instruction |
| `0x0004` | 2 | Divide by zero | Faulting instruction |
| `0x0006` | 3 | Timer 0 | Next instruction |
| `0x0008` | 4 | Keyboard | Next instruction |
| `0x000A` | 5 | Bus fault | Faulting instruction |
| `0x000C` | 6 | Reserved | — |
| `0x000E` | 7 | Reserved | — |

The first eight entries are the hardware vectors, and the table is ordinary
RAM so a program or BIOS can install handlers directly. The BIOS installs
default ISRs at boot; a student can replace an ISR by writing a new handler
address into the corresponding vector. If an entry is zero, the VM halts with
an "unhandled exception" diagnostic rather than jumping to address 0.

The BIOS jump table immediately follows at `0x0010–0x00FF`; see Section
5.4.2.

#### 5.4.2 BIOS Jump Table

The rest of the first page, `0x0010–0x00FF`, is a 120-entry table of 16-bit
BIOS function addresses. Entry n lives at `0x0010 + 2*n`, for n = 0..119.
This is the page-zero BIOS jump table.

- `TRAP #n` pushes the return address and jumps to the address in entry n.
- The BIOS copies a default table from ROM into RAM at boot; by default, the
  entries point into the BIOS.
- The table is ordinary RAM, so a student can overwrite an entry to hook,
  replace, or enhance a BIOS routine without rewriting the BIOS.
- The hardware vectors at `0x0000–0x000F` are separate: replacing an ISR means
  writing a new handler address into the corresponding vector.
- The hardware vector numbers (0–7) and BIOS function numbers (0–119) are
  separate namespaces; use symbolic names in assembly to avoid confusion.
- BIOS functions use the normal C ABI: arguments in R1–R3, result in R0,
  R4/SP preserved, and return with `RET`.
- If an entry is zero, the VM treats `TRAP` as an unhandled trap and halts
  with a diagnostic.

Proposed initial BIOS function numbers (the final contract is settled with
libc in the compiler/runtime round):

| Index | Function | Arguments / result |
|---|---|---|
| 0 | `putchar` | R0 = byte |
| 1 | `getchar` | returns byte or -1 |
| 2 | `puts` | R0 = NUL-terminated string |
| 3 | `exit` | R0 = status; does not return |
| 4 | `timer_set` | R0 = control, R1 = compare, R2 = prescale |
| 5 | `timer_read` | returns count in R0 |
| 6 | `kbd_read` | returns key or -1 |
| 7 | `kbd_status` | returns status in R0 |
| 8 | `random` | returns 16-bit value in R0 |
| 9 | `memcpy` | R0 = dest, R1 = src, R2 = count |
| 10 | `memset` | R0 = dest, R1 = value, R2 = count |
| 11 | `strlen` | R0 = string, returns length |
| 12 | `version` | returns BIOS version |
| 13 | `delay` | R0 = cycles to wait |
| 14–119 | reserved | available for BIOS or student extensions |

#### 5.4.3 ROM Layout

```text
  0xF100–0xFFFD  BIOS code, data, strings, default vector/jump-table
                 templates, and trap handlers (3838 bytes)
  0xFFFE–0xFFFF  Reset vector (16-bit address, little-endian)
```

The BIOS entry point is `0xF100`; the reset vector points there. The ROM is
loaded before execution and write-protected while the CPU runs. Writes to ROM
are ignored (the VM may log them). In `--no-bios` mode the VM sets PC directly
to the `.kex` header entry (or the raw `--base`).

### 5.5 Data Representation

- **Little-endian.** A 16-bit value at address `A` has its low byte at `A` and
  its high byte at `A+1`. A 32-bit instruction's second word follows at `A+2`.
- **Byte-addressable.** All addresses are 16-bit and wrap modulo 64 KiB.
- **Instruction alignment.** PC must be even. An odd PC causes a bus fault
  (vector 5). The second word of a 32-bit instruction is at `PC+2`.
- **Data alignment.** 16-bit loads/stores may be unaligned in RAM; the VM
  performs two byte accesses (and may charge one extra cycle). In the MMIO
  page, 16-bit accesses **must** be 2-byte aligned; unaligned MMIO accesses
  cause a bus fault.
- **Two's complement** for signed values.
- **Stack alignment.** SP is always even. PUSH/POP move two bytes.

### 5.6 Instruction Encoding

#### 5.6.1 Length Rule

Fetch the 16-bit word at PC:

- If the top nibble is `0xF`, the instruction is **32-bit**; fetch the second
  word at `PC+2`.
- If the top nibble is `0x8` and the sub-opcode (bits 11–8) is `0` or `1`
  (`JMP rel16` / `CALL rel16`), the instruction is **32-bit**.
- Otherwise the instruction is **16-bit**.

This rule is deliberately simple: one branch in the decoder, no length table.

#### 5.6.2 Instruction Formats

Notation: `[15:12]` means bits 15 through 12 inclusive. `Rd` is a 3-bit
destination register code; `Rs` is a 3-bit source register code. All
multi-bit fields are unsigned unless marked signed.

**R-type — ALU register-register (16-bit)**

```text
  15..12  11..9  8..6  5..0
  opcode  Rd     Rs    function
  0000
```

**I-type — ALU immediate (16-bit)**

```text
  15..12  11..9  8..6  5..0
  opcode  Rd     func  imm6 (signed, -32..31)
  0001
```

**M-type — load / store / LEA (16-bit)**

```text
  15..12  11..9  8..6  5..0
  opcode  Rd     Rs    disp6 (signed, -32..31)
```

Address = `Rs + sign_extend(disp6)`, modulo 64 KiB. When `Rs = PC`, the base
is the address of the next instruction.

**B-type — conditional branch (16-bit)**

```text
  15..12  11..8  7..0
  opcode  cond   disp8 (signed, in words)
  0111
```

Target = `PC + 2 + (disp8 * 2)`. Range: -256 to +254 bytes.

**M8-type — MOV immediate 8-bit (16-bit)**

```text
  15..12  11..9  8     7..0
  opcode  Rd     sign  imm8
  1001
```

`sign = 0`: zero-extend imm8 to 16 bits.
`sign = 1`: sign-extend imm8 to 16 bits.
This lets the assembler encode both `0..255` and `-128..-1` compactly.

**S-type — shift by immediate (16-bit)**

```text
  15..12  11..9  8..6  5..0
  opcode  Rd     func  count (0..15)
  1011
```

`func`: 0 = SHL, 1 = SHR, 2 = SAR. Counts are 0–15; the hardware uses the low
4 bits, and the assembler rejects immediate counts outside 0–15.

**BIT-type — bit operation by immediate (16-bit)**

```text
  15..12  11..9  8..6  5..0
  opcode  Rd     func  bit (0..15)
  1100
```

`func`: 0 = BTST, 1 = BSET, 2 = BCLR, 3 = BTOG.

**C-type — control / special (16-bit, except sub-ops 0 and 1)**

```text
  15..12  11..8  7..5     4..0
  opcode  subop  regA     rest
  1000
```

The interpretation of `regA` and `rest` depends on `subop`; see the table in
Section 5.7.4.

**X-type — extended (32-bit)**

```text
  Word 1:
  15..12  11..8  7..5  4..0
  opcode  ext_op Rd    aux
  1111

  Word 2:
  15..0
  imm16 / address / displacement (little-endian)
```

For `ext_op` 0–12, `aux` is reserved (0). For 13–15, `aux` is `Rs` (0–7).

#### 5.6.3 Sixteen-Bit Major Opcode Map

| Top nibble | Format | Meaning |
|---|---|---|
| `0x0` | R-type | ALU register-register |
| `0x1` | I-type | ALU immediate (6-bit signed) |
| `0x2` | M-type | `LD` word |
| `0x3` | M-type | `ST` word |
| `0x4` | M-type | `LDB` byte, zero-extend |
| `0x5` | M-type | `LDSB` byte, sign-extend |
| `0x6` | M-type | `STB` byte |
| `0x7` | B-type | Conditional branch |
| `0x8` | C-type | Control and special instructions |
| `0x9` | M8-type | `MOV Rd, #imm8` |
| `0xA` | M-type | `LEA` |
| `0xB` | S-type | Shift by immediate |
| `0xC` | BIT-type | Bit operation by immediate |
| `0xD` | — | Reserved for future 16-bit instructions |
| `0xE` | — | Reserved for future 16-bit instructions |
| `0xF` | X-type | Extended 32-bit instruction |

#### 5.6.4 Register Encoding

| Code | Register |
|---|---|
| 0 | R0 |
| 1 | R1 |
| 2 | R2 |
| 3 | R3 |
| 4 | R4 (FP) |
| 5 | SP |
| 6 | SR |
| 7 | PC |

### 5.7 Instruction Set

The tables below are the v1 instruction set. The normative encodings and
reserved-bit rules are frozen in `docs/isa.md`.

#### 5.7.1 Data Movement

| Mnemonic | Forms | Size | Flags | Notes |
|---|---|---|---|---|
| `MOV` | `Rd, Rs` | 16 | none | Register-to-register; special registers allowed |
| `MOV` | `Rd, #imm6` | 16 | none | Sign-extended 6-bit immediate |
| `MOV` | `Rd, #u8` / `#s8` | 16 | none | Zero- or sign-extended 8-bit immediate |
| `MOV` | `Rd, #imm16` | 32 | none | Full 16-bit immediate |
| `LD` | `Rd, [Rs+disp]` | 16 / 32 | none | Word load; disp6 or disp16 |
| `LD` | `Rd, [addr16]` | 32 | none | Absolute word load |
| `LDB` | `Rd, [Rs+disp]` | 16 | none | Byte load, zero-extend (disp6) |
| `LDB` | `Rd, [addr16]` | 32 | none | Byte load, zero-extend |
| `LDSB` | `Rd, [Rs+disp]` | 16 | none | Byte load, sign-extend (disp6) |
| `LDSB` | `Rd, [addr16]` | 32 | none | Byte load, sign-extend |
| `ST` | `[Rs+disp], Rd` | 16 / 32 | none | Word store |
| `ST` | `[addr16], Rd` | 32 | none | Absolute word store |
| `STB` | `[Rs+disp], Rd` | 16 | none | Byte store (disp6) |
| `STB` | `[addr16], Rd` | 32 | none | Absolute byte store |
| `LEA` | `Rd, [Rs+disp]` | 16 / 32 | none | Address computation, no memory access |
| `PUSH` | `Rs` | 16 | none | Push any register (including SR/PC) |
| `POP` | `Rd` | 16 | none | Pop any register (including SR/PC) |
| `SEXT` | `Rd` | 16 | none | Sign-extend low byte to 16 bits |
| `ZXTB` | `Rd` | 16 | none | Zero-extend low byte to 16 bits |

Notes:

- `LD`/`ST`/`LDB`/`LDSB`/`STB`/`LEA` with `Rs = PC` are PC-relative. The base
  is the address of the next instruction.
- `ST`/`STB` with `Rd = PC` store the next PC; this is legal but rarely used.
- `LD`/`LDB`/`LDSB`/`LEA` with `Rd = PC` or `Rd = SR` are illegal; use `MOV`
  or `POP` for those registers.
- `PUSH SP` stores the pre-decrement SP value. `POP SP` sets SP to the loaded
  value (the assembler should warn on `POP SP`).
- `PUSH PC` pushes the address of the next instruction. `POP PC` is a return.

#### 5.7.2 Arithmetic and Logic

| Mnemonic | Forms | Size | Flags | Notes |
|---|---|---|---|---|
| `ADD` | `Rd, Rs` / `Rd, #imm` | 16 / 32 | C Z N V | Add |
| `ADC` | `Rd, Rs` | 16 | C Z N V | Add with carry |
| `SUB` | `Rd, Rs` / `Rd, #imm` | 16 / 32 | C Z N V | Subtract |
| `SBC` | `Rd, Rs` | 16 | C Z N V | Subtract with borrow |
| `CMP` | `Rd, Rs` / `Rd, #imm` | 16 / 32 | C Z N V | Compare (no write) |
| `AND` | `Rd, Rs` / `Rd, #imm` | 16 / 32 | C=0 V=0 Z N | Bitwise AND |
| `OR` | `Rd, Rs` / `Rd, #imm` | 16 / 32 | C=0 V=0 Z N | Bitwise OR |
| `XOR` | `Rd, Rs` / `Rd, #imm` | 16 / 32 | C=0 V=0 Z N | Bitwise XOR |
| `TEST` | `Rd, Rs` / `Rd, #imm` | 16 / 32 | C=0 V=0 Z N | AND, flags only |
| `NOT` | `Rd` | 16 | C=0 V=0 Z N | Bitwise complement |
| `NEG` | `Rd` | 16 | C Z N V | Two's complement |
| `INC` | `Rd` | 16 | C Z N V | Increment |
| `DEC` | `Rd` | 16 | C Z N V | Decrement |
| `MUL` | `Rd, Rs` | 16 | Z N; C=0 V=0 | Low 16 bits of product |
| `MULS` | `Rd, Rs` / `Rd` | 16 | 32-bit Z N; C=0 V=0 | Signed 16x16 -> 32-bit pair; Rd = R0/R2 |
| `MULU` | `Rd, Rs` / `Rd` | 16 | 32-bit Z N; C=0 V=0 | Unsigned 16x16 -> 32-bit pair; Rd = R0/R2 |
| `DIV` | `Rd, Rs` | 16 | Z N; C=0 V=0 | Signed quotient; div-by-zero faults |
| `DIVU` | `Rd, Rs` | 16 | Z N; C=0 V=0 | Unsigned quotient; div-by-zero faults |
| `MOD` | `Rd, Rs` | 16 | Z N; C=0 V=0 | Signed remainder; div-by-zero faults |
| `MODU` | `Rd, Rs` | 16 | Z N; C=0 V=0 | Unsigned remainder; div-by-zero faults |

Immediate sizes: the 16-bit ALU-immediate form carries a signed 6-bit
immediate (-32..31). The 32-bit extended form carries a full 16-bit immediate
for ADD/SUB/CMP/AND/OR/XOR/TEST. The assembler chooses the smallest legal
form.

`DIV`/`MOD` with a zero divisor raise the divide-by-zero exception (vector 2)
and do not modify the destination register.

#### 5.7.2.1 Multiplication Semantics

There are three multiply instructions: `MUL` for a 16-bit result and
`MULS`/`MULU` for a full 32-bit result.

`MUL Rd, Rs` computes the full 32-bit product of the two 16-bit values but
writes only bits 15..0 to `Rd`; bits 31..16 are discarded. `Rd` is both a
source and the destination, and `Rs` is unchanged.

- The low 16 bits are identical for signed and unsigned multiplication, so a
  single `MUL` serves both `int` and `unsigned int`.
- Z and N describe the 16-bit result written to `Rd`, not the full 32-bit
  product; for example, `0x1000 * 0x0010 = 0x00010000`, so the low result is
  zero and Z=1 even though the full product is nonzero.
- C and V are cleared. `MUL` does not report whether the product overflowed
  16 bits.

`MULS Rd, Rs` and `MULU Rd, Rs` produce a full 32-bit product:

- `Rd` must be `R0` or `R2`. The low 16 bits are written to `Rd` and the high
  16 bits to `Rd+1` (`R0:R1` or `R2:R3`).
- `MULS` interprets both operands as signed; `MULU` interprets both as
  unsigned. Both forms are required because the high half differs:
  `0xFFFF * 0x0002` is `0x0001FFFE` unsigned but `0xFFFFFFFE` signed, while
  the low half is `0xFFFE` in both cases.
- Both operands are read before either destination is written. `Rs` may be
  the pair's high register (`R1` or `R3`), in which case the high result
  overwrites that source.
- `Rs` must be a general register (`R0`–`R4`); `SP`, `SR`, and `PC` are not
  valid multipliers and raise illegal instruction if encoded.
- Flags: Z = 1 when the full 32-bit result is zero; N = bit 31 (the high
  word's bit 15); C = 0; V = 0. The 32-bit product of two 16-bit values always
  fits in 32 bits.
- The one-operand shorthand `MULS R0` means `MULS R0, R1`; `MULU R2` means
  `MULU R2, R3`. The assembler may accept the shorthand and disassemble to
  the two-operand form.

Signed example:

```asm
    LD    R0, #factorA
    LD    R1, #factorB
    MULS  R0              ; R0:R1 = signed 32-bit product
    ST    [product_lo], R0
    ST    [product_hi], R1
```

Use `MULU R0` for unsigned factors.

**Assembler note.** `MULS`/`MULU` are pair-aligned instructions. Only `R0`
and `R2` are legal pair bases; `R1`, `R3`, and `R4`–`R7` must be rejected
with a clear diagnostic. The assembler should also reject `SP`, `SR`, and
`PC` as the multiplier. If an invalid pair base or multiplier register
reaches the CPU (for example, from a raw `.word` directive), it raises the
illegal-instruction exception (vector 0). The assembler should accept the
one-operand shorthand and emit the canonical two-operand encoding.

#### 5.7.3 Shifts and Bit Operations

| Mnemonic | Forms | Size | Flags | Notes |
|---|---|---|---|---|
| `SHL` | `Rd, Rs` / `Rd, #count` | 16 | C Z N; V=0 | Logical left |
| `SHR` | `Rd, Rs` / `Rd, #count` | 16 | C Z N; V=0 | Logical right |
| `SAR` | `Rd, Rs` / `Rd, #count` | 16 | C Z N; V=0 | Arithmetic right |
| `BTST` | `Rd, Rs` / `Rd, #bit` | 16 | Z | Test bit; Z = NOT bit |
| `BSET` | `Rd, Rs` / `Rd, #bit` | 16 | none | Set bit |
| `BCLR` | `Rd, Rs` / `Rd, #bit` | 16 | none | Clear bit |
| `BTOG` | `Rd, Rs` / `Rd, #bit` | 16 | none | Toggle bit |

- Variable shift/bit counts use the low 4 bits of `Rs` (0–15).
- Immediate counts are 0–15.
- A shift count of zero leaves all flags unchanged.
- For a nonzero shift, C is the last bit shifted out. `SAR` shifts in the
  original sign bit.

#### 5.7.4 Control Flow and System

The C-type control format:

```text
  15..12  11..8  7..5   4..0
  1000    subop  regA   rest
```

| Subop | Mnemonic | Size | Fields | Operation |
|---|---|---|---|---|
| 0 | `JMP rel16` | 32 | word 2 = signed word displacement | PC <- next PC + rel16*2 |
| 1 | `CALL rel16` | 32 | word 2 = signed word displacement | Push next PC; PC <- target |
| 2 | `JMP Rs` | 16 | regA = Rs | PC <- Rs |
| 3 | `CALL Rs` | 16 | regA = Rs | Push next PC; PC <- Rs |
| 4 | `RET` | 16 | — | PC <- pop |
| 5 | `RETI` | 16 | — | PC <- pop; SR <- pop |
| 6 | `TRAP #n` | 16 | bits[7:0] = n | Push next PC; PC <- `[0x0010 + 2n]`, n=0..119 |
| 7 | `BRK` | 16 | — | Breakpoint exception (vector 1) |
| 8 | `HLT` | 16 | — | Stop the CPU |
| 9 | `NOP` | 16 | — | No operation |
| 10 | `EI` | 16 | — | Set I |
| 11 | `DI` | 16 | — | Clear I |
| 12 | `WAIT` | 16 | — | Wait for interrupt |
| 13 | `PUSH Rs` | 16 | regA = Rs | Push register |
| 14 | `POP Rd` | 16 | regA = Rd | Pop register |
| 15 | `SYS #n` | 16 | bits[7:0] = n; R0 = block pointer | Host service call; VM handles; PC advances |

Conditional branches use the B-type format. Condition codes:

| Code | Name | Branch taken when | Typical meaning |
|---|---|---|---|
| 0 | `EQ` | Z=1 | Equal |
| 1 | `NE` | Z=0 | Not equal |
| 2 | `CS` / `HS` | C=1 | Unsigned >= |
| 3 | `CC` / `LO` | C=0 | Unsigned < |
| 4 | `MI` | N=1 | Negative |
| 5 | `PL` | N=0 | Positive or zero |
| 6 | `VS` | V=1 | Signed overflow |
| 7 | `VC` | V=0 | No signed overflow |
| 8 | `HI` | C=1 and Z=0 | Unsigned > |
| 9 | `LS` | C=0 or Z=1 | Unsigned <= |
| 10 | `GE` | N = V | Signed >= |
| 11 | `LT` | N != V | Signed < |
| 12 | `GT` | Z=0 and N = V | Signed > |
| 13 | `LE` | Z=1 or N != V | Signed <= |
| 14 | `AL` | always | Unconditional (`BRA`) |
| 15 | `NV` | never | Never |

All condition mnemonics are written as `B` + condition, e.g. `BEQ`, `BNE`,
`BCS`, `BHI`, `BGE`, `BLT`, `BLE`, `BRA`.

`JMP rel16` and `CALL rel16` use a signed 16-bit word displacement relative to
the instruction's next PC (address + 4). The range covers the entire 64 KiB
address space. `CALL` pushes the address of the next instruction (the return
address) before jumping.

`TRAP #n`:

- Pushes the address of the next instruction.
- Reads a 16-bit handler address from the BIOS jump table in RAM at
  `0x0010 + 2*n`, where n is 0..119.
- Does **not** push SR or change I. The handler is a normal function using the
  C ABI and returns with `RET`.
- The table is writable, so a student can hook a BIOS routine by overwriting
  the entry; the BIOS initializes the table at boot.
- If n is outside 0..119, the instruction is illegal (vector 0). If the table
  entry is zero, the VM treats it as an unhandled trap and halts with a
  diagnostic.

`BRK`:

- Raises the breakpoint exception (vector 1): push SR, push next PC, clear I,
  jump to the vector.
- A debugger may intercept the `BRK` before the vector is delivered and stop
  with PC at the `BRK` instruction. Without a debugger, the CPU vectors.

`HLT` stops the CPU. In run mode the VM exits with `R0 & 0xFF` as the process
exit code. Pending interrupts do not wake a halted CPU.

`WAIT` is implemented in v1. It suspends instruction execution until an
interrupt source is pending; the VM advances the timer while waiting. If I=1
the interrupt is taken; if I=0 execution resumes at the next instruction.

`SYS #n`:

- It is a **host service call**, not a guest BIOS call and not an exception.
  `n` is 0..255.
- R0 points to a 16-byte, 2-byte-aligned block in guest RAM:
  word 0 is status, words 1–3 are results, words 4–7 are arguments.
- The VM validates the block and any guest pointers inside the arguments,
  performs the service, writes status/results, leaves R0 unchanged, and
  advances PC.
- Flags are unchanged. The instruction costs a fixed 10 simulated cycles;
  host wall time does not advance the simulated timer.
- It does not push SR/PC and does not vector through guest memory. It is
  distinct from `TRAP` (BIOS) and `BRK` (debug).
- An unsupported service returns `ENOSYS`; an invalid block or guest pointer
  returns `EFAULT`; neither raises a CPU exception.
- See Section 9.6 for the block layout, service table, error codes, and
  sandbox policy.

#### 5.7.5 ALU Function Codes (R-type and I-type)

| Code | Mnemonic | Notes |
|---|---|---|
| 0 | `ADD` | Rd <- Rd + Rs |
| 1 | `ADC` | Rd <- Rd + Rs + C |
| 2 | `SUB` | Rd <- Rd - Rs |
| 3 | `SBC` | Rd <- Rd - Rs - (1-C) |
| 4 | `CMP` | flags of Rd - Rs, no write |
| 5 | `AND` | Rd <- Rd & Rs |
| 6 | `OR` | Rd <- Rd \| Rs |
| 7 | `XOR` | Rd <- Rd ^ Rs |
| 8 | `MOV` | Rd <- Rs (special-register writes allowed) |
| 9 | `TEST` | flags of Rd & Rs, no write |
| 10 | `NEG` | Rd <- -Rd |
| 11 | `NOT` | Rd <- ~Rd |
| 12 | `INC` | Rd <- Rd + 1 |
| 13 | `DEC` | Rd <- Rd - 1 |
| 14 | `SHL` | Rd <- Rd << (Rs & 0x0F) |
| 15 | `SHR` | Rd <- Rd >> (Rs & 0x0F) |
| 16 | `SAR` | Rd <- arithmetic shift right |
| 17 | `MUL` | Rd <- low16(Rd * Rs) |
| 18 | `DIV` | signed quotient |
| 19 | `DIVU` | unsigned quotient |
| 20 | `MOD` | signed remainder |
| 21 | `MODU` | unsigned remainder |
| 22 | `BTST` | Z <- NOT bit |
| 23 | `BSET` | set bit |
| 24 | `BCLR` | clear bit |
| 25 | `BTOG` | toggle bit |
| 26 | `SEXT` | sign-extend low byte |
| 27 | `ZXTB` | zero-extend low byte |
| 28 | `MULS` | Rd:Rd+1 <- signed(Rd) * signed(Rs); Rd = R0 or R2 |
| 29 | `MULU` | Rd:Rd+1 <- unsigned(Rd) * unsigned(Rs); Rd = R0 or R2 |
| 30–63 | — | Reserved |

Unary functions (`NEG`, `NOT`, `INC`, `DEC`, `SEXT`, `ZXTB`) use the `Rd`
field and ignore `Rs`; the assembler encodes `Rs = Rd` (or 0) for clarity.

`MULS`/`MULU` use `Rd` as the pair base (`R0` or `R2`) and `Rs` as the
multiplier; `Rd+1` receives the high word. Invalid pair bases or non-GPR
multipliers raise illegal instruction.

The I-type function field is 3 bits and has its own mapping: 0 = ADD,
1 = SUB, 2 = CMP, 3 = AND, 4 = OR, 5 = XOR, 6 = MOV, 7 = TEST. The
immediate is sign-extended from 6 bits.

#### 5.7.6 Extended (32-bit) Opcodes

| ext_op | Mnemonic | Operation |
|---|---|---|
| 0 | `MOV Rd, #imm16` | Rd <- imm16 |
| 1 | `ADD Rd, #imm16` | Rd <- Rd + imm16 |
| 2 | `SUB Rd, #imm16` | Rd <- Rd - imm16 |
| 3 | `CMP Rd, #imm16` | flags of Rd - imm16 |
| 4 | `AND Rd, #imm16` | Rd <- Rd & imm16 |
| 5 | `OR Rd, #imm16` | Rd <- Rd \| imm16 |
| 6 | `XOR Rd, #imm16` | Rd <- Rd ^ imm16 |
| 7 | `TEST Rd, #imm16` | flags of Rd & imm16 |
| 8 | `LD Rd, [addr16]` | word load, absolute |
| 9 | `ST [addr16], Rd` | word store, absolute |
| 10 | `LDB Rd, [addr16]` | byte load, zero-extend |
| 11 | `LDSB Rd, [addr16]` | byte load, sign-extend |
| 12 | `STB [addr16], Rd` | byte store, absolute |
| 13 | `LD Rd, [Rs+disp16]` | word load, indexed (aux = Rs) |
| 14 | `ST [Rs+disp16], Rd` | word store, indexed (aux = Rs) |
| 15 | `LEA Rd, [Rs+disp16]` | address computation (aux = Rs) |

The 16-bit displacement in extended load/store/LEA forms is signed and added
to `Rs` modulo 64 KiB. When `Rs = PC`, the base is the next PC (address + 4).

### 5.8 Addressing Modes

| Mode | Syntax | Encoding | Typical use |
|---|---|---|---|
| Register | `Rd, Rs` | R-type | Arithmetic, moves |
| Immediate | `#imm` | I/M8/X-type | Constants, masks |
| Register indirect | `[Rs]` | M-type with disp=0 | Pointers |
| Indexed | `[Rs+disp]` | M-type (disp6) or X-type (disp16) | Locals, arrays, structs |
| PC-relative | `[PC+disp]` | M-type/X-type with Rs=PC | Constant pools, position-independent data |
| Absolute | `[addr16]` | X-type | Globals, MMIO |
| PC-relative branch | `Bcc label` | B-type | Conditionals |
| PC-relative jump/call | `JMP/CALL label` | C-type 32-bit | Functions |
| Register jump/call | `JMP/CALL Rs` | C-type 16-bit | Function pointers, returns |

There is intentionally no auto-increment/decrement mode. `PUSH`/`POP` and
`LEA` cover the common cases, and the smaller decoder is easier to teach.

### 5.9 Flag Semantics

Summary of flag effects:

| Instruction group | C | Z | N | V | I |
|---|---|---|---|---|---|
| `ADD`, `ADC` | carry out | result==0 | result bit 15 | signed overflow | — |
| `SUB`, `SBC`, `CMP` | NOT borrow | result==0 | result bit 15 | signed overflow | — |
| `AND`, `OR`, `XOR`, `TEST` | 0 | result==0 | result bit 15 | 0 | — |
| `NOT` | 0 | result==0 | result bit 15 | 0 | — |
| `NEG`, `INC`, `DEC` | as ADD/SUB | result==0 | result bit 15 | signed overflow | — |
| `MUL` | 0 | low result==0 | low result bit 15 | 0 | — |
| `MULS`, `MULU` | 0 | full 32-bit result==0 | bit 31 (high word bit 15) | 0 | — |
| `DIV`, `DIVU`, `MOD`, `MODU` | 0 | result==0 | result bit 15 | 0 | — |
| `SHL`, `SHR`, `SAR` | last bit out | result==0 | result bit 15 | 0 | — |
| `BTST` | — | NOT bit | — | — | — |
| `BSET`, `BCLR`, `BTOG` | — | — | — | — | — |
| `MOV`, loads, stores, `LEA`, `PUSH`, `POP`, `SEXT`, `ZXTB` | — | — | — | — | — |
| `SYS` | — | — | — | — | — |
| `EI` | — | — | — | — | 1 |
| `DI` | — | — | — | — | 0 |
| `RETI` | restores SR | | | | |
| `MOV SR, ...` | writes all implemented bits | | | | |

Additional rules:

- A shift count of zero leaves C, Z, N, and V unchanged.
- `BTST` changes only Z; C, N, and V are unchanged.
- `DIV`/`MOD` by zero raises vector 2 before writing the destination.
- `MULS`/`MULU` require a pair base of `R0` or `R2`; other pair bases raise
  the illegal-instruction exception (vector 0).
- `SYS` leaves flags unchanged; host errors are returned in the call block,
  not as CPU exceptions.
- Signed division overflow (`-32768 / -1`) wraps to `0x8000`; the compiler may
  treat it as undefined, matching C.
- The `SBC` borrow is `1 - C`, consistent with C meaning "no borrow."

### 5.10 Interrupts and Exceptions

#### 5.10.1 Sources and Masking

| Source | Pending when | Enabled by | Vector |
|---|---|---|---|
| Timer 0 | `TIMER0_STATUS.MATCH` | `TIMER0_CTRL.IRQ_EN` | 3 (`0x0006`) |
| Keyboard | `KBD_STATUS.DATA_AVAILABLE` | `KBD_CTRL.IRQ_EN` | 4 (`0x0008`) |

The global mask is `SR.I`. `EI` enables interrupts; `DI` disables them. Reset
leaves I=0.

Priority is fixed: **keyboard before timer**. If both are pending and I=1, the
keyboard vector is taken first. After `RETI`, the timer is taken if it is
still pending.

#### 5.10.2 Interrupt Entry

Between instructions, if I=1 and a source is pending:

1. SP -= 2; `[SP] = SR`.
2. SP -= 2; `[SP] = PC` (the next instruction to execute).
3. Clear I (other flag bits are unchanged in the live SR).
4. PC = 16-bit handler address read from the vector table.
5. Entry costs 7 cycles.

`RETI`:

1. PC = `[SP]`; SP += 2.
2. SR = `[SP]`; SP += 2.
3. Costs 5 cycles.

The saved PC is at `[SP]` and the saved SR is at `[SP+2]` while the handler is
running. A handler can modify the saved PC to skip a faulting instruction.
Because I is cleared automatically, hardware interrupts do not nest. A handler
that wants nesting can execute `EI` after saving state.

#### 5.10.3 Exceptions

| Exception | Vector | Saved PC | Cause |
|---|---|---|---|
| Illegal instruction | 0 | Faulting instruction | Unknown opcode, illegal register write, reserved field, invalid `MULS`/`MULU` pair base or multiplier |
| Breakpoint | 1 | Next instruction | `BRK` |
| Divide by zero | 2 | Faulting instruction | `DIV`/`DIVU`/`MOD`/`MODU` with divisor 0 |
| Bus fault | 5 | Faulting instruction | Odd instruction fetch, unaligned MMIO word access, stack guard violation |

Exceptions use the same push-SR/push-PC/clear-I sequence as interrupts. A
fault handler that returns without fixing the cause will re-execute the
faulting instruction and fault again; this is intentional and is a useful
teaching moment.

If a vector table entry is zero, the VM halts with an unhandled-exception
diagnostic rather than jumping to address 0. This is a VM safety policy, not
CPU semantics.

### 5.11 Timer 0

Timer 0 is a 16-bit up-counter driven by simulated CPU cycles. It can poll or
interrupt.

| Register | Address | Width | Access | Meaning |
|---|---|---|---|---|
| `TIMER0_CTRL` | `0xF008` | 8 | R/W | bit 0 EN, bit 1 PERIODIC, bit 2 IRQ_EN |
| `TIMER0_COUNT` | `0xF00A` | 16 | R/W | Current count; writing resets it |
| `TIMER0_COMPARE` | `0xF00C` | 16 | R/W | Match value |
| `TIMER0_STATUS` | `0xF00E` | 8 | R/W | bit 0 MATCH; write 1 to clear |
| `TIMER0_PRESCALE` | `0xF010` | 16 | R/W | Tick every `PRESCALE+1` cycles |

Behavior:

- When EN=1, every `(PRESCALE+1)` CPU cycles, COUNT increments by 1.
- When COUNT equals COMPARE after an increment:
  - MATCH is set.
  - If PERIODIC=1, COUNT resets to 0.
  - If PERIODIC=0, EN is cleared (one-shot).
  - If IRQ_EN=1, the timer interrupt is asserted (level-sensitive).
- Clearing MATCH deasserts the interrupt. If the handler does not clear it,
  the interrupt remains pending and fires again after `RETI`.
- Writing COUNT also resets the prescaler phase.
- COMPARE=0 matches when COUNT wraps from `0xFFFF` to 0 (period 65,536 ticks).

The default reset state is EN=0, PERIODIC=0, IRQ_EN=0, COUNT=0, COMPARE=0,
PRESCALE=0, MATCH=0.

### 5.12 Console and Keyboard I/O

All device registers are memory-mapped bytes in the I/O page.

| Address | Name | Width | Access | Meaning |
|---|---|---|---|---|
| `0xF000` | `CONSOLE_OUT` | 8 | W | Write a byte to stdout |
| `0xF001` | `CONSOLE_STATUS` | 8 | R | bit 0 TX_READY (always 1 in v1) |
| `0xF002` | `CONSOLE_CTRL` | 8 | W | bit 0 ECHO input to output |
| `0xF004` | `KBD_DATA` | 8 | R | Pop the oldest key byte |
| `0xF005` | `KBD_STATUS` | 8 | R/W | bit 0 DATA_AVAILABLE, bit 1 OVERFLOW |
| `0xF006` | `KBD_CTRL` | 8 | R/W | bit 0 IRQ_EN |
| `0xF012` | `IRQ_STATUS` | 8 | R | bit 0 timer pending, bit 1 keyboard pending |

Behavior:

- The VM's console thread reads input bytes and pushes them into a keyboard
  FIFO. `KBD_DATA` pops one byte; reading when empty returns 0 and clears
  DATA_AVAILABLE.
- `KBD_STATUS.OVERFLOW` is set if a byte arrives while the FIFO is full.
  Writing 1 to bit 1 clears it.
- The keyboard interrupt is level-sensitive while DATA_AVAILABLE and IRQ_EN are
  both true. The handler should drain the FIFO.
- `CONSOLE_OUT` writes a byte to stdout. `CONSOLE_STATUS.TX_READY` is always 1
  in v1; a TX FIFO can be added later.
- `CONSOLE_CTRL.ECHO`, when set, makes the VM echo input bytes to output. It
  defaults to 0; a BIOS or program can enable it.
- Key bytes are raw 8-bit values, normally ASCII. Enter is translated to
  `0x0A` (`\n`); Backspace is `0x08`. UTF-8 input is delivered byte by byte.
- Reserved I/O addresses read as 0 and ignore writes. The VM may log writes to
  reserved addresses.

### 5.13 Reset and Boot

On reset:

- PC = little-endian 16-bit value at `0xFFFE`.
- SP = `0xF000` (one past the top of RAM); the VM lowers SP when it builds an
  argv block.
- R0–R4 = 0; the VM may set R1=argc and R2=argv for a program command line.
- SR = 0 (C=Z=N=V=I=0).
- I/O registers return to their reset values.
- RAM contents are undefined in hardware; the VM zeroes RAM by default for
  determinism.
- ROM is loaded before execution and is write-protected thereafter.
- The VM enforces a stack guard: SP must remain in
  `[__stack_limit, 0xF000]`, with `__stack_limit` defaulting to `0xE000`
  (a 4 KiB stack reserve) and configurable by the linker; a push, call, or
  pop that would move SP outside that range raises a bus fault (vector 5).

Boot protocol:

1. The VM loads a 3.75 KiB ROM image (default: the embedded reference BIOS, or
   a user-supplied image via the BIOS load command) at `0xF100–0xFFFF`.
2. The VM loads the program image. The standard entry is `0x0100`,
   immediately after the first page; the `.kex` header records the actual
   entry.
3. If a BIOS is present, the reset vector points to the BIOS entry (normally
   `0xF100`). The BIOS initializes devices, copies the default hardware
   vectors and BIOS jump table into the first page, then jumps to `0x0100`.
   Normal `.kex` images therefore place their entry code at `0x0100`.
4. If no BIOS is loaded (`--no-bios`), the VM starts directly at the `.kex`
   header entry (default `0x0100`). The C runtime initializes the first page
   as needed.
5. The C runtime startup at the entry (normally `0x0100`) sets `R4 = 0`,
   zeroes `.bss` if needed, installs default vectors and BIOS jump-table
   entries if needed, calls `main`, and executes `HLT` with the return value
   in R0.

The reference BIOS is optional for running C programs; libc may use MMIO
directly. The BIOS exists so students can write one, so assembly programs can
use `TRAP` services, and so the first-page vectors and jump table are
initialized by default. The final choice of whether libc uses BIOS traps or
direct MMIO is deferred to the compiler/runtime round.

### 5.14 Cycle Costs

The VM counts simulated cycles for the timer and for deterministic traces.
Counts are provisional and may be tuned, but they are part of the ISA/VM
contract once frozen.

| Instruction class | Cycles |
|---|---|
| 16-bit ALU register/immediate | 1 |
| 16-bit load/store/LEA | 2 |
| 16-bit conditional branch | 1 not taken, 2 taken |
| 16-bit `JMP Rs` | 2 |
| 16-bit `CALL Rs` | 3 |
| 16-bit `RET` | 3 |
| 16-bit `RETI` | 5 |
| 16-bit `PUSH`/`POP` | 2 |
| 16-bit `TRAP` | 4 |
| 16-bit `BRK` | 4 |
| 16-bit `SYS` | 10 |
| `HLT`, `NOP`, `EI`, `DI` | 1 |
| 32-bit instruction | +1 over the equivalent 16-bit form |
| Hardware interrupt entry | 7 |
| Unaligned RAM word access | +1 |
| `WAIT` | 1 to enter; cycles advance while waiting |

Examples: `MOV Rd, #imm16` = 2; `LD Rd, [addr16]` = 3; `JMP rel16` = 3;
`CALL rel16` = 4. Host wall time spent inside `SYS` does not advance the
simulated timer; the instruction's fixed cost is 10 cycles.

### 5.15 Calling Convention (Provisional)

The compiler round will finalize the ABI, but the CPU design assumes:

- **Arguments:** only 16-bit scalar and pointer arguments use R1, R2, R3. A
  32-bit `long`, struct, or union goes on the stack and starts stack mode;
  all later arguments also go on the stack. Stack arguments are pushed
  right-to-left, so the first stack argument is at `[FP+4]`; the caller cleans
  them up after the call.
- **Returns:** a 16-bit value is in R0; a 32-bit value in R0 (low) and R1
  (high); a struct/union uses a caller-supplied hidden pointer in R0 and
  returns that pointer in R0.
- **Variadic functions:** all arguments, including named ones, are on the
  stack. `va_list` is a 16-bit pointer into that area.
- **Caller-saved:** R0–R3, flags, SR. Normal C code must preserve the I bit.
- **Callee-saved:** R4 (FP), SP.
- **Stack:** full-descending, 2-byte aligned. SP points to the last pushed
  word; at reset it is `0xF000` when there is no command line, or just below
  the argv block when arguments are present.
- **Parameters:** the prologue spills R1–R3 parameters into frame slots before
  the first source line; stack parameters are already at `[FP+4...]`.
- **Entry:** the VM enters with R1=argc and R2=argv when a command line is
  present. `crt0` forwards them to `main`; a custom `_start` receives the same
  registers.

Frame layout:

```text
  higher addresses
  +----------------------+
  | caller's frame       |
  +----------------------+
  | stack arguments      |  [FP+4], [FP+6], ...
  +----------------------+
  | return address       |  [FP+2]
  +----------------------+
  | saved caller FP      |  [FP+0]  <- R4/FP
  +----------------------+
  | locals               |  [FP-2], [FP-4], ...
  +----------------------+
  | ...                  |
  +----------------------+
  lower addresses        <- SP
```

Prologue:

```asm
    PUSH  R4
    MOV   R4, SP
    SUB   SP, #frame_size
```

Epilogue:

```asm
    MOV   SP, R4
    POP   R4
    RET
```

Locals within -32..+31 of FP use the compact 16-bit indexed form; larger
frames use the 32-bit indexed form. The C runtime sets R4=0 before calling
`main`, terminating the frame chain for the debugger.

Interrupt handlers are not ordinary C functions: the CPU pushes SR and PC, and
the handler must save/restore any registers it uses before `RETI`. A future
`__interrupt` attribute can generate this wrapper. Trap handlers are ordinary
C functions using the standard ABI and return with `RET`.

`SYS` is not part of the C function ABI. It uses the 16-byte host-call
block in R0 (Section 9.6); libc wrappers translate between the C ABI and the
host-call block.

### 5.16 Debugging Hooks

- `BRK` raises a breakpoint exception and is interceptable by the DAP.
- `HLT` terminates the program and reports the exit code in R0.
- The frame-pointer chain (`[R4]` = caller FP, `[R4+2]` = return address)
  makes stack walks straightforward.
- No delay slots, pipelines, or caches: single-step is exact.
- The VM exposes registers, PC, SP, SR, cycle count, memory, and device state
  to the DAP.
- The compiler/linker emit symbols, types, source lines, and frame/location
  info; the DAP consumes them. The debug-info format is defined in Sections
  6.8 and 8.8.

## 6. Assembler and Disassembler

### 6.1 Design Summary

| Area | Decision |
|---|---|
| Syntax | Intel-style, destination first; `#` immediates; brackets for memory |
| Lexical | Mnemonics/registers case-insensitive; labels case-sensitive; `;` and `//` comments |
| Output | Relocatable `.o` by default; `--raw --base` for flat images |
| Object format | KR16 container; `.o` = REL, `.kex` = EXEC; 32-bit offsets/sizes |
| Symbols | Global/extern plus `.L` locals with `-g`; included in `.kex` by default |
| Relocations | `ABS16`, `PCREL8W`, `PCREL16W`, `PCREL16B` |
| Sizing | Smallest form; local branch relaxation |
| Debug | Binary `.debug_*`; assembler builds `.debug_line`, preserves compiler sections |
| Disassembly | Symbol-aware, canonical, round-trippable |
| Diagnostics | `file:line:column` with caret and suggestions; optional `-l` listing |

The assembler and disassembler share the ISA tables with the CPU, so encodings
never drift. The C compiler invokes the assembler **in-process**; the CLI
wraps the same library.

### 6.2 Syntax and Lexical Rules

- One instruction or directive per line. Labels start at column 1;
  instructions and directives may be indented.
- Comments run from `;` or `//` to the end of the line. `#` is the immediate
  prefix and is **not** a comment.
- Mnemonics and register names are case-insensitive. Labels and symbols are
  case-sensitive.
- Registers: `R0`–`R4`, `SP`, `SR`, `PC`. `FP` is an alias for `R4`.
- Operand order is destination first: `LD R0, [R1+4]`, `ADD R0, R1`,
  `ST [R2+6], R3`.
- Immediates are written `#expr`. Memory is `[Rs+expr]`, `[Rs]`, or
  `[addr]`; `[Rs]` is `[Rs+0]`.
- Numeric literals: decimal, `0x` hexadecimal, `0b` binary, `0o` octal, and
  character literals such as `'A'`. String and character escapes are C-style
  (`\n`, `\t`, `\\`, `\"`, `\xNN`, `\0`).
- Expressions support `+ - * / % & | ^ ~ << >>`, parentheses, unary `-`,
  symbols, and `.` for the current location. Division is integer division.
  `low(expr)` and `high(expr)` are available for 16-bit halves.
- Labels are bare identifiers: `loop:`, `main:`, `.L1:`. Local labels begin
  with `.L` and are omitted from the executable symbol table unless `-g`.
- Pseudo-instructions and aliases:
  - `BRA` for branch always, `BHS` for `BCS`, `BLO` for `BCC`.
  - `JMP #addr` assembles as `MOV PC, #imm16`.
  - `RET` is equivalent to `POP PC`; `RETI` is the interrupt return.
  - `NOP` is a real instruction.
  - `MULS R0` / `MULU R2` are one-operand shorthands for the pair forms.
- `MULS`/`MULU` validation is a hard requirement: only `R0`/`R2` are legal
  pair bases, only `R0`–`R4` are legal multipliers, and invalid forms produce
  a clear diagnostic. `MUL` is unaffected: no pair alignment, no shorthand.
- `SYS #n` selects a VM host service. The assembler recognizes `SYS_*`
  constants and prints known service names in listings/disassembly; unknown
  numbers remain legal. R0 points to the 16-byte host-call block.

### 6.3 Sections, Directives, and Macros

Sections:

| Section | Flags | Use |
|---|---|---|
| `.text` | ALLOC, EXEC | Code |
| `.rodata` | ALLOC | Constants and strings |
| `.data` | ALLOC, WRITE | Initialized data |
| `.bss` | ALLOC, WRITE, NOBITS | Zero-initialized data |
| `.debug_*` | DEBUG | Compiler-emitted debug information |

Directives (v1 practical teaching set):

| Directive | Meaning |
|---|---|
| `.text`, `.data`, `.rodata`, `.bss` | Switch to a standard section |
| `.section name` | Switch to or create a named section |
| `.byte expr, ...` | Emit 8-bit values |
| `.word expr, ...` | Emit 16-bit little-endian values |
| `.long expr, ...` | Emit 32-bit little-endian values |
| `.ascii "text"` | Emit bytes without a terminator |
| `.asciz "text"` | Emit bytes with a NUL terminator |
| `.space n`, `.skip n` | Reserve *n* zero bytes |
| `.balign n`, `.align n` | Align to an *n*-byte boundary |
| `.p2align n` | Align to a 2^*n*-byte boundary |
| `.global symbol` | Export a symbol |
| `.extern symbol` | Declare an external symbol |
| `.equ name, expr`, `.set name, expr` | Define an assembly-time constant |
| `.type symbol, @function/@object` | Symbol type metadata |
| `.size symbol, expr` | Symbol size metadata |
| `.file "path"`, `.loc file line [column]` | Source line mapping |
| `.org expr` | Set the absolute output address; allowed only in `--raw` mode |
| `.include "file"` | Include another source file |
| `.macro name params` / `.endm` | Simple parameter-substitution macro |
| `.if expr` / `.else` / `.endif` | Conditional assembly with constant expressions |

Notes:

- `.align n` means an *n*-byte boundary; `.balign n` is an alias. `.p2align n`
  is the explicit power-of-two form. The compiler uses `.balign`/`.p2align`
  so its intent is unambiguous.
- `.word` emits 16-bit little-endian values; `.long` emits 32-bit
  little-endian values.
- `.org` is for raw/absolute images such as BIOS ROMs. Relocatable code uses
  sections and the linker; `.org` in relocatable mode is an error.
- Macros are simple parameter substitution; recursive macros are not part of
  v1. `.include` searches the current directory and `-I` paths.
- `.debug_*` sections are opaque to the assembler. The compiler emits them;
  the assembler preserves them and applies relocations.

### 6.4 Symbols and Labels

- `.global` marks a symbol for export; `.extern` declares an undefined symbol;
  plain labels are local to the object unless exported.
- `.L...` labels are assembler-local: they are available for relocation and
  debugging but are omitted from the executable symbol table unless `-g`.
- `.equ`/`.set` constants are resolved at assembly time and are not link-time
  symbols.
- Symbol names use letters, digits, `_`, and `.`, and may not begin with a
  digit. Labels are case-sensitive.
- Symbol table entries carry: name, section, value/offset, size, binding
  (local/global/extern), type (notype/object/function/section/file), and
  visibility.
- Weak symbols are a later addition; v1 has local, global, and extern.
- The current-location symbol is `.`, not `$`.

### 6.5 Encoding, Size Selection, and Relaxation

- The assembler is two-pass. Pass 1 computes section sizes and symbol offsets;
  pass 2 encodes instructions and emits relocations.
- It chooses the smallest legal form:
  - 16-bit ALU-immediate (6-bit signed) before 32-bit ALU-immediate (16-bit).
  - 16-bit indexed/PC-relative forms (6-bit displacement) before 32-bit forms
    (16-bit displacement).
  - 16-bit `MOV #imm` forms before 32-bit `MOV #imm16`.
  - Short `Bcc` before a relaxed long branch.
- Local branch relaxation iterates to a fixed point (with a small pass limit,
  e.g. 8). An out-of-range local `Bcc` becomes the inverted condition over a
  32-bit `JMP`.
- External calls and jumps use the 32-bit `CALL rel16` / `JMP rel16` forms.
  The compiler emits long forms for external symbols; short external branches
  are emitted with a `PCREL8W` relocation and the linker reports an
  out-of-range error if the target does not fit.
- Pair-multiply validation applies before encoding: `MULS`/`MULU` require
  `R0`/`R2` pair bases and `R0`–`R4` multipliers.
- `.align`/`.org` affect the location counter and therefore branch
  displacements; the relaxation loop accounts for them.
- The encoder, decoder, and disassembler use the same ISA tables.

### 6.6 Relocations

| Type | Used for | Calculation | Range |
|---|---|---|---|
| `R_KR16_ABS16` | Symbol addresses in immediates, `.word`, absolute loads/stores | `S + A` | 0..65535 |
| `R_KR16_PCREL8W` | `Bcc` word displacement | `(S + A - P) / 2` | -128..127 words |
| `R_KR16_PCREL16W` | `JMP rel16` / `CALL rel16` word displacement | `(S + A - P) / 2` | -32768..32767 words |
| `R_KR16_PCREL16B` | PC-relative data `[PC+disp16]` byte displacement | `S + A - P` | -32768..32767 bytes |

`S` is the symbol address, `A` is the addend, and `P` is the address of the
relocation field. The assembler sets `A` to account for instruction length:
for a 16-bit instruction the next PC is `P+2`, and for a 32-bit instruction it
is `P+4`. The linker applies the formula and checks the range.

- Local symbols are resolved by the assembler; global/extern symbols produce
  relocations.
- Relocations may appear in `.text`, `.data`, `.rodata`, and debug sections.
- `R_KR16_ABS16` is also used for `.word symbol` data pointers.
- Out-of-range relocations are hard errors with the symbol name and the
  computed distance.
- Section-relative and weak-symbol relocations are not part of v1.
- The linker re-relocates debug sections as it merges them, so addresses in
  the final debug info are correct.

### 6.7 Object and Executable Container

KR16 uses **one self-describing container format** with two kinds:

- `.o` — relocatable object (`kind = REL`).
- `.kex` — executable image (`kind = EXEC`).

The same container is used for both so the assembler, linker, disassembler,
VM, and DAP share one reader/writer. The object file and the final executable
both carry program/data **and** symbols/debug information.

Header (fixed 64 bytes; all fields little-endian; offsets are fixed):

| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0 | 4 | Magic | `"KR16"` |
| 4 | 2 | Version | 8-bit major, 8-bit minor |
| 6 | 2 | Kind | `REL` = 1, `EXEC` = 2 |
| 8 | 4 | Flags | bit 0 symbols, bit 1 debug, bit 2 stripped, bit 3 objects; bits 4–31 reserved |
| 12 | 4 | Header size | 64 |
| 16 | 2 | Entry | 16-bit entry address; zero for REL |
| 18 | 2 | Reserved | Zero |
| 20 | 2 | Section count | Number of section headers |
| 22 | 2 | Reserved | Zero |
| 24 | 4 | Symbol count | Number of symbol entries |
| 28 | 4 | Section table offset | |
| 32 | 4 | Symbol table offset | |
| 36 | 4 | String table offset | Points to `.debug_str` |
| 40 | 4 | String table size | |
| 44 | 4 | Relocation table offset | Zero for EXEC |
| 48 | 4 | Relocation table size | Zero for EXEC |
| 52 | 4 | Checksum | CRC32 of the entire file with this field zeroed |
| 56 | 8 | Reserved | Zero |

Version handling: readers accept the same major version and newer minor
versions, and reject an unknown major version. Reserved fields are zero on
write. Unknown optional flags are ignored; unknown required bits are an error.

Section header (fixed 32 bytes):

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | Name offset |
| 4 | 2 | Type (`PROGBITS`=1, `NOBITS`=2) |
| 6 | 2 | Flags (`ALLOC`=1, `EXEC`=2, `WRITE`=4, `DEBUG`=8) |
| 8 | 2 | Address |
| 10 | 2 | Reserved |
| 12 | 4 | File offset |
| 16 | 4 | Size |
| 20 | 2 | Alignment |
| 22 | 2 | Link |
| 24 | 2 | Info |
| 26 | 2 | Entry size |
| 28 | 4 | Reserved |

Symbol entry (fixed 20 bytes):

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | Name offset |
| 4 | 2 | Value |
| 6 | 2 | Section index |
| 8 | 4 | Size |
| 12 | 1 | Binding (local/global/extern) |
| 13 | 1 | Type (notype/object/function/section/file) |
| 14 | 1 | Visibility |
| 15 | 1 | Reserved |
| 16 | 2 | Object index |
| 18 | 2 | Reserved |

Relocation entry (fixed 16 bytes):

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | Section index |
| 2 | 2 | Type |
| 4 | 4 | Offset |
| 8 | 4 | Symbol index |
| 12 | 4 | Addend |

`.debug_objects` header (16 bytes): magic `"DBGO"` (4), version (2), reserved
(2), object count (4), source count (4). Each object record is 48 bytes:
object ID (2), reserved (2), name offset (4), code address (2), reserved (2),
code size (4), line offset/size (4/4), types offset/size (4/4), vars
offset/size (4/4), frames offset/size (4/4). Each source record is 12 bytes:
path offset (4), build-directory offset (4), flags (2), reserved (2).

Global width rules: file offsets and section sizes are 32-bit; symbol counts
and relocation symbol indices are 32-bit; guest addresses, section counts,
object IDs, type IDs, and source-file indices are 16-bit. This lets a `.kex`
with debug info exceed 64 KiB while keeping guest-visible values 16-bit.

Layout (all tables/sections 2-byte aligned; this order is fixed):

```text
+----------------------------+
| Header                     |
+----------------------------+
| Section table              |
+----------------------------+
| .text                      |
| .rodata                    |
| .data                      |
| .bss                       |  (NOBITS: no file data)
| .debug_line                |
| .debug_types               |
| .debug_vars                |
| .debug_frames              |
| .debug_str                 |  (the single shared string table)
| .debug_objects             |
+----------------------------+
| Symbol table               |
+----------------------------+
| Relocation table           |  (REL only)
+----------------------------+
```

`.debug_str` is the one deduplicated string table shared by symbols and debug
data; the header's string-table offset/size point to it. Strings are stored in
first-use order. Symbols are written in input order; relocations are sorted by
section then offset. There is no separate trailing string table.

Behavior:

- The VM loads only sections flagged `ALLOC`; `.bss` is zeroed. It ignores
  symbols, relocations, and debug sections.
- The CRC32 checksum covers the entire file, including header, tables, debug
  data, and padding, with the checksum field itself zeroed during calculation.
  It uses the standard IEEE CRC32 polynomial (reflected `0xEDB88320`).
- The DAP reads the **same** `.kex` for symbols, line tables, types, variable
  locations, and frame information. There is no sidecar symbol file.
- The disassembler reads the same file for symbols and sections; `kr16 dis`
  is the human-readable view of the container.
- The linker merges `.text`/`.rodata`/`.data`/`.bss`, resolves symbols,
  applies relocations, relocates debug sections, deduplicates strings into
  `.debug_str`, builds `.debug_objects`, and writes the `.kex`.
- Symbols and debug sections are included by default. `kr16 ld --strip`
  produces a runnable `.kex` without them; the VM still runs it, but the DAP
  cannot provide source-level debugging.
- A `.map` file is optional and human-readable; it is not required for
  symbols or debugging.
- Debug line tables store source file paths (and optionally a source hash).
  Source text is not embedded by default; the DAP reads the source from disk
  so edits are visible. Embedding source can be a later option.
- The format is versioned and every offset/length is bounds-checked by the
  reader. A malformed `.kex` must produce a diagnostic, never a crash.
- The `.kex` is self-contained for distribution: program, data, symbols, and
  debug information travel together. The `.o` is only needed during linking.

Why a custom container rather than ELF32 + DWARF: the KR16 address space is
16-bit, the debug information is C-specific, and the DAP needs a small,
well-defined type/line/variable model. A custom mini-ELF-style container is
much less work than an ELF/DWARF emitter and importer, and it gives the VM and
DAP exactly the data they need. A zip-like container is also rejected: the
section table lets the VM load segments directly and the DAP read debug
sections without extraction or decompression.

### 6.8 Debug Information

- The assembler builds `.debug_line` (address to file/line/column) from the
  `.file` and `.loc` directives. This works for both compiler output and
  hand-written assembly.
- The compiler emits `.debug_types` (C types), `.debug_vars` (scopes,
  variables, locations), and `.debug_frames` (function/frame information) as
  PROGBITS data with relocations. Names go into `.debug_str`, the single shared
  string table.
- Debug data is compact binary, little-endian, using fixed headers,
  fixed-width records, and length-prefixed arrays. It is not JSON or text.
  `kr16 debug` renders it for students and for debugging the debugger.
- Exact layouts:

| Section | Header | Records |
|---|---|---|
| `.debug_line` | 8 bytes: count (4), version (2), reserved (2) | 8 bytes each: address (2), file (2), line (2), column (1), flags (1) |
| `.debug_types` | 8 bytes: count (4), version (2), reserved (2) | 16 bytes: type ID (2), kind (1), flags (1), size (2), align (1), reserved (1), name offset (4), extra count (2), reserved (2); then `extra count` 16-bit words |
| `.debug_vars` | 8 bytes: variable count (4), scope count (2), version (2) | scope 12 bytes: ID (2), parent (2), first var (2), var count (2), first child (2), child count (2); variable 16 bytes: name offset (4), type ID (2), scope ID (2), decl line (2), location kind (1), flags (1), offset (2), reserved (2) |
| `.debug_frames` | 8 bytes: function count (4), version (2), reserved (2) | function 20 bytes: name offset (4), low PC (2), high PC (2), frame size (2), param count (2), param offset (4), flags (2), reserved (2); parameter 8 bytes: name offset (4), type ID (2), frame offset (2) |
| `.debug_objects` | 16 bytes: magic `"DBGO"` (4), version (2), reserved (2), object count (4), source count (4) | object 48 bytes: ID (2), reserved (2), name offset (4), code address (2), reserved (2), code size (4), line offset/size (4/4), types offset/size (4/4), vars offset/size (4/4), frames offset/size (4/4); source 12 bytes: path offset (4), build-directory offset (4), flags (2), reserved (2) |
| `.debug_str` | none | NUL-terminated strings, first-use order, 32-bit offsets |

- `.debug_line` rows are sorted by address, one row per line change or
  instruction. File indices refer to the global source table in
  `.debug_objects`. Addresses are 16-bit guest addresses.
- `.debug_types` uses per-object 16-bit type IDs; there is no type
  deduplication in v1. Kind-specific arrays (members, enumerators, parameters)
  are 16-bit words.
- `.debug_vars` uses fixed-width scope and variable records. Parameters are
  spilled to frame slots before the first source line, so their locations are
  FP-relative; globals and statics use absolute addresses.
- `.debug_frames` maps PC ranges to functions and records frame layout and
  parameter locations for stack walks.
- `.debug_objects` is linker-built. It carries per-object records and the
  global deduplicated source table. Source paths are relative to the build
  directory with `/` separators, preserve case, normalize `.`/`..`, and use an
  absolute fallback for out-of-tree headers. Windows source dedup compares
  case-insensitively.
- `.debug_str` is the single deduplicated string table shared by symbols and
  debug data; the container header points to it. Strings are stored in
  first-use order.
- The assembler treats the compiler-emitted sections as opaque; it does not
  need to understand C types. The linker merges and relocates them and builds
  `.debug_objects`.
- Hand-written assembly can use `.file` and `.loc` for line mapping and
  `.type`/`.size` for symbol metadata. Type and variable info is
  compiler-generated.
- The DAP contract is: symbols + line table + type table + variable/scope
  locations + frame information. That is enough for breakpoints, stepping,
  call stacks, locals, globals, watch expressions, and memory views.
- Source files are referenced by path, not embedded. A source hash can warn
  about stale debug info later.
- `kr16 debug` is human-readable in v1; `--json` is a later addition.

### 6.9 Disassembler

- `kr16 dis file.o|file.kex` reads the container; `--raw --base addr` handles
  flat images.
- Default output is readable and symbol-aware: section headers, labels and
  symbol names, canonical mnemonics, and optional raw bytes.
- `--source` interleaves source lines using `.debug_line`.
- `--reloc` shows relocations for `.o` files.
- `--no-hex` suppresses raw bytes; `--symbols` controls symbol annotation.
- Data sections are dumped as `.byte`/`.word`/`.long` with symbols, not
  disassembled as code.
- `MULS`/`MULU` disassemble to the canonical two-operand form; the
  one-operand shorthand is assembler-only.
- `SYS #n` prints a symbolic `SYS_*` name when the service is known and a
  numeric value otherwise.
- `kr16 debug file.o|file.kex` decodes `.debug_*` tables; `--lines`, `--types`,
  `--vars`, `--frames`, and `--objects` filter the output.
- Round-trip test: assemble → disassemble → assemble → compare bytes for
  `.text`. This is a required regression test.

### 6.10 Diagnostics and Listing

- Errors use `file:line:column`, a source excerpt with a caret, and a
  suggestion where possible.
- The assembler reports multiple errors per run and recovers at statement
  boundaries.
- Warnings cover out-of-range immediates, unaligned data, shadowed labels,
  unused symbols, and suspicious `.org`/`.align` use.
- `-l` produces a listing with address, encoded bytes, and source line. This
  is a teaching aid and a debugging aid.
- Exit codes: 0 success, 1 assembly/link error, 2 usage error.
- Machine-readable JSON diagnostics can come later for the VS Code extension.

### 6.11 CLI and Integration

```text
kr16 as file.s -o file.o [-I dir] [-D name=value] [-l listing] [-g]
              [--raw --base addr]

kr16 dis file.o|file.kex [--source] [--reloc] [--no-hex] [--symbols]
              [--raw --base addr]
```

- `kr16 cc` emits assembly and calls the assembler library in-process; it does
  not spawn a separate process.
- `kr16 ld` reads `.o` files and writes the `.kex`; `--strip` removes symbols
  and debug sections from the executable.
- The assembler, linker, disassembler, VM, and DAP share the container reader
  and writer. The container is the single source of truth for sections,
  symbols, relocations, and debug data.
- Source files may use `.s` or `.asm`; objects use `.o`; executables use
  `.kex`.
- `--raw` emits a flat binary for BIOS/ROM and simple loaders; `.org` is
  meaningful there.

### 6.12 Remaining Details

- Exact expression grammar and operator precedence.
- Macro parameter substitution and conditional-expression rules.
- Linker script/layout syntax and section placement (later).
- Source path mapping and stale-source detection.
- Relaxation pass limit and diagnostics for unstable layouts.
- Whether `.kex` can also represent a ROM image directly, or whether BIOS
  images remain raw `--raw` output.

## 7. C Compiler

### 7.1 Design Summary

| Area | Decision |
|---|---|
| Dialect | C99 subset, no GNU extensions |
| Plain char | Unsigned 8-bit; `signed char` explicit |
| Data model | `char` 8, `short`/`int` 16, `long` 32, pointers 16, `_Bool` 8 |
| Types | char/short/int/long + unsigned, pointers, `void`, `enum`, `union`, `struct`, `typedef`, `const`/`volatile` |
| Arrays | Fixed-size multidimensional, row-major, full decay and pointer arithmetic |
| Preprocessor | Practical full C preprocessor, including function-like/variadic macros and `#`/`##` |
| ABI | 16-bit scalar/pointer args in R1–R3; `long`/aggregate stack-only; caller cleans; R0 / R0:R1 returns |
| Backend | Correctness-first `-O0`; typed AST to stack-machine assembly |
| Runtime | Practical embedded libc with varargs, printf family, malloc/free, string basics |
| Debug info | Full `-O0` symbols, lines, locals, types, scopes, frames; parameters spilled in prologue |
| Self-hosting | Out of scope; the compiler is a host tool |

### 7.2 Language Subset and Data Model

The compiler targets a practical **C99 subset**. It is standard C99 syntax and
semantics except for the explicit exclusions below; there are no GNU
extensions.

| Type | Size | Alignment | Notes |
|---|---|---|---|
| `char` | 1 | 1 | Plain char is unsigned (0..255) |
| `signed char` | 1 | 1 | −128..127 |
| `unsigned char` | 1 | 1 | 0..255 |
| `short`, `unsigned short` | 2 | 2 | 16-bit |
| `int`, `unsigned int` | 2 | 2 | 16-bit |
| `long`, `unsigned long` | 4 | 2 | 32-bit, 2-byte alignment |
| Pointer | 2 | 2 | 16-bit, including function pointers |
| `_Bool` | 1 | 1 | 0 or 1 |
| `enum` | 2 | 2 | Compatible with 16-bit `int` |
| `void` | — | — | No object representation |

- `size_t` is 16-bit unsigned; `ptrdiff_t` is 16-bit signed;
  `intptr_t`/`uintptr_t` are 16-bit.
- `short` is 16-bit (same range as `int`). `long` is 32-bit. `long long` is
  not supported.
- Plain `char` is unsigned. `signed char` is available when negative byte
  values are wanted; string and byte data stay in 0..255.
- `const` and `volatile` are supported. `volatile` accesses are side-effecting
  and are never folded or removed. `restrict` is parsed and ignored. `inline`
  is accepted and emitted as a normal function in v1.
- Storage classes: `typedef`, `extern`, `static`, `auto`, `register`.
  `auto`/`register` are accepted and ignored; `static` locals and globals are
  supported.
- `struct`, `union`, `enum`, and `typedef` are supported, including nested
  structs, arrays of structs, self-referential structs through pointers, and
  pointers to functions.
- Fixed-size multidimensional arrays are fully supported. `int MyArray[10][2][3]`
  is ten arrays of two arrays of three ints, laid out row-major, 120 bytes
  total. `sizeof(MyArray)` is 120, `sizeof(MyArray[0])` is 12,
  `sizeof(MyArray[0][0])` is 6, and `sizeof(MyArray[0][0][0])` is 2.
  Subscripting decays to the correct pointer type, and pointer arithmetic
  scales by the element size.
- Arrays are fixed-size. VLAs and flexible array members are not supported.
  Incomplete array types are allowed for `extern` declarations and function
  parameters.
- Struct layout is natural: members are laid out in declaration order with
  padding for alignment; `char` aligns to 1, `short`/`int`/pointer to 2, and
  `long` to 2. The struct size is rounded up to its maximum member alignment.
  There is no packing and no bitfield support.
- `union` members all start at offset 0; the union size is the largest member
  size rounded to alignment.
- `enum` constants are 16-bit `int` values and must fit in that range.
- No bitfields, VLAs, flexible array members, `long long`, `float`, `double`,
  `long double`, complex types, `_Atomic`, `_Thread_local`, `_Generic`,
  `_Static_assert`, anonymous structs/unions, or C11 features.
- C99 features included: `//` comments, mixed declarations, `for (int i...)`,
  `stdint.h`, `stdbool.h`, designated initializers for arrays and structs,
  `inline` (as a normal function), and `restrict` (ignored). Compound literals
  are a later addition.
- Function prototypes are required; implicit function declarations are
  errors. K&R old-style definitions are not supported in v1.
- `main` may be `int main(void)` or `int main(int argc, char **argv)`; both
  are supported in v1. Falling off the end of `main` returns 0.
- Integer promotions and the usual arithmetic conversions follow C99. `char`,
  `short`, `_Bool`, and enum promote to `int` when representable;
  `unsigned short` promotes to `unsigned int` because `int` cannot represent
  0..65535; `int`/`unsigned int` widen to `long`/`unsigned long` in mixed
  32-bit expressions.
- Signed overflow is undefined in C; the generated code wraps in two's
  complement. Divide by zero is undefined and raises the hardware
  divide-by-zero exception (vector 2).
- `sizeof` returns `size_t`. `offsetof` is supported. `_Alignof` is not.
- Initializers support scalars, arrays, structs, unions, strings, nested
  braces, and designated initializers.
- String literals are arrays of `char` placed in `.rodata`; modifying them is
  undefined behavior. Identical literals may be shared.
- There is no runtime bounds checking; array bounds are the programmer's
  responsibility.

### 7.3 Preprocessor

The preprocessor is a token-based, practical full C preprocessor. It preserves
source locations for diagnostics and debug info.

Supported directives:

- `#include "file"` and `#include <file>`.
- `#define`, `#undef`.
- `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif`, and `defined`.
  `#elseif` is accepted as an alias for `#elif`.
- `#error`, `#warning`, `#line`, and `#pragma once`.

Macro features:

- Object-like and function-like macros with parameters.
- Stringification (`#`), token pasting (`##`), and variadic macros
  (`...`/`__VA_ARGS__`).
- Standard argument prescan, recursion prevention, and redefinition rules.
- `#if` integer constant expressions with arithmetic, bitwise, comparison,
  logical, and ternary operators; casts and `sizeof` are not available in
  preprocessor expressions.

Include search:

- Quoted includes search the directory of the including file, then `-I`
  directories, then the built-in target headers.
- Angle includes search `-I` directories, then the built-in target headers.
- Include guards and `#pragma once` are honored. Include depth is limited to
  catch runaway recursion.

Built-in headers (shipped with `kr16`):

`stddef.h`, `stdint.h`, `stdbool.h`, `stdarg.h`, `limits.h`, `stdio.h`,
`stdlib.h`, `string.h`, `assert.h`. `ctype.h` is a later addition.

Predefined macros:

`__KR16__`, `__KR16_VERSION__`, `__STDC__`, `__STDC_VERSION__` (199901L),
`__FILE__`, `__LINE__`, `__DATE__`, `__TIME__`.

No trigraphs, digraphs, `#include_next`, or `_Pragma`. The preprocessor reports
errors with the include stack and macro expansion trace.

### 7.4 ABI and Runtime Interface

The ABI is the **simple stack ABI** described in Section 5.15:

- Only 16-bit scalar and pointer arguments use R1, R2, R3. A `long`, struct,
  or union goes on the stack and starts stack mode; every later argument is
  also on the stack.
- Stack arguments are pushed right-to-left, so the first stack argument is at
  `[FP+4]`. The caller cleans up stack arguments after the call. Alignment is
  2 bytes; no padding.
- A 16-bit result is returned in R0; an 8-bit result occupies R0's low byte
  with the upper bits zero. A 32-bit result uses R0:R1 (low:high).
- A struct or union result is returned through a hidden pointer supplied by
  the caller in R0; the callee writes the result there and returns the pointer
  in R0.
- Structs and unions are passed by value on the stack; the callee sees a copy.
- `long` arguments are 32-bit stack slots; `long` returns use R0:R1.
- Variadic functions pass **all** arguments on the stack, including named
  ones. `va_list` is a 16-bit pointer into the stack argument area;
  `va_start` is `FP+4+fixed_arg_bytes`. `va_arg` supports 16-bit scalars,
  pointers, and 32-bit longs; aggregate `va_arg` is not supported in v1.
  `va_end`/`va_copy` are compiler builtins/macros.
- R4 (FP) and SP are callee-saved; R0–R3, flags, and SR are caller-saved.
  Normal C code must preserve the I bit.
- The prologue pushes R4, sets R4=SP, allocates the frame, and spills R1–R3
  parameters into their frame slots before the first source line. Stack
  parameters are already at `[FP+4...]`. The epilogue restores SP, pops R4,
  and returns.
- Function pointers are 16-bit; calls use `CALL Rs`.

Startup and runtime interface:

- A user-defined `_start` wins; otherwise `crt0` is linked at the program
  entry `0x0100`.
- The VM enters with R1=argc and R2=argv when a command line is present. The
  VM builds the argument block on the stack, sets SP below it, and sets
  `argv[0]` to the program basename. `argv` is a null-terminated array of
  `char *`; `envp` is not provided. Limits are 16 arguments and 512 bytes
  total.
- `crt0` sets R4=0, zeroes `.bss` if the loader has not, installs default
  vectors and BIOS jump-table entries when no BIOS is present, forwards
  R1/R2 to `main`, and executes `HLT` with `main`'s return value in R0.
- `main(void)` ignores R1/R2; `main(int argc, char **argv)` uses them.
- `exit(status)` stores the status in R0 and executes `HLT`. `abort()` exits
  with a nonzero status.
- The linker exports `__heap_start`, `__heap_end` (= `__stack_limit`),
  `__stack_top` (`0xF000`), and `__stack_limit` (default `0xE000`, a 4 KiB
  stack reserve). `malloc` uses the heap region; the VM/runtime detect
  heap-stack collision.
- Interrupt handlers are not generated from C in v1. They can be written in
  assembly; a future `__interrupt` attribute can generate the wrapper.
- 32-bit helpers: 32-bit add/sub/compare are inline; multiply uses
  `MULS`/`MULU`; divide, modulo, and variable shifts call runtime helpers
  (`__divsi3`, `__udivsi3`, `__modsi3`, `__umodsi3`, `__ashlsi3`,
  `__ashrsi3`, `__lshrsi3`). Helpers follow the same C ABI.

### 7.5 Compiler Architecture

Pipeline:

```text
  source .c
     |
  preprocessor (tokens, includes, macros, conditionals)
     |
  lexer/parser (recursive descent, C99 subset AST)
     |
  semantic analysis (scopes, symbols, types, conversions)
     |
  typed AST
     |
  code generation (stack-machine assembly, .loc directives)
     |
  assembly text (.s)
     |
  built-in assembler (in-process) -> .o
```

- The driver handles `-E`, `-S`, `-c`, `-o`, `-I`, `-D`, `-U`, `-g`,
  `-O0`, `-Wall`, `-Werror`, and `-std=c99`.
- The parser uses a symbol table to distinguish typedef names from ordinary
  identifiers and handles the classic C declaration/expression ambiguities.
- Semantic analysis builds scopes and symbols, resolves tags and typedefs,
  checks lvalues and qualifiers, computes integer promotions and conversions,
  folds constant expressions, and validates initializers and calls.
- Code generation walks the typed AST and emits assembly text. There is no
  separate IR in v1; a small IR can be introduced for `-O1`.
- `-S` writes the assembly text; otherwise the compiler passes it to the
  assembler library in-process and writes the `.o` container.
- The compiler emits `.file`, `.loc`, `.type`, `.size`, `.global`, `.extern`,
  and section/data directives. Debug sections are emitted as data with
  relocations.
- One translation unit per `kr16 cc` invocation; `kr16 ld` links multiple
  objects.
- Built-in headers are embedded resources; `-I` adds search paths.
- The compiler exposes a `__host_call(n, block)` intrinsic for libc wrappers.
  It does not expose native Windows/Linux APIs; students use normal C file
  functions when those are added.

### 7.6 Code Generation

The `-O0` backend is deliberately straightforward and correctness-first:

- Expression results are produced in R0; intermediate values are pushed on
  the stack and popped into R1 as needed. Locals and parameters live in frame
  slots. There is no global register allocator.
- Lvalues are computed as addresses in R0/R1, then loaded or stored. Array
  subscripting, struct/union member access, pointer dereference, `&`, and `*`
  are handled by the type-aware address calculation.
- Multidimensional arrays use row-major address arithmetic; the compiler
  computes the linear element index from the declared dimensions.
- Struct and union assignment copies the object; arrays are not assignable.
- Function calls evaluate arguments, place 16-bit scalars/pointers in R1–R3,
  push `long`/struct/union arguments and everything after the first stack
  argument right-to-left, `CALL`, and clean up the stack. Struct returns use
  the R0 hidden pointer.
- Variadic calls push **all** arguments, including named ones; `va_list` points
  at the stack argument area; `va_arg` supports scalars, pointers, and 32-bit
  longs, not aggregates.
- Function prologues spill R1–R3 parameters into frame slots before the first
  source-line `.loc`, so parameters are visible at function-entry breakpoints.
- `main(void)` ignores R1/R2; `main(int argc, char **argv)` reads them.
- Control flow uses labels and branches: `if`/`else`, `while`, `do`/`while`,
  `for`, `break`, `continue`, `goto`, `switch` (an if-else chain in v1; jump
  tables later), ternary, and short-circuit `&&`/`||`.
- 32-bit `long` operations: add/sub/compare are inline; multiply uses
  `MULS`/`MULU`; divide/modulo/shifts call runtime helpers. 32-bit constants
  are loaded as two 16-bit words.
- Byte accesses use `LDB`/`LDSB`/`STB`; `_Bool` values are normalized to 0/1;
  `char` is unsigned by default.
- Globals and statics live in `.data`/`.bss`; string literals and `const`
  data live in `.rodata`; static locals get mangled names.
- Function pointers are 16-bit and calls go through `CALL Rs`.
- Simple constant folding handles integer constant expressions, `sizeof`, and
  address constants. There is no global register allocation and no aggressive
  optimization.
- `.loc` directives are emitted at statement and expression boundaries for
  debug line mapping.
- `-O1` is a later phase; v1 ships `-O0` only.

### 7.7 Runtime and Library

The runtime is a practical embedded libc, not a full hosted C library.

- `stdio.h`: `printf`, `vprintf`, `sprintf`, `vsprintf`, `snprintf`,
  `vsnprintf`, `putchar`, `getchar`, `puts`, and `EOF`. Console only; no files
  and no `scanf`.
- `printf` conversions: `%d`, `%i`, `%u`, `%o`, `%x`, `%X`, `%c`, `%s`, `%p`,
  and `%%`, with `h`/`l` length modifiers, width, precision, and the usual
  flags. No floating-point conversions and no `%n`.
- `stdlib.h`: `malloc`, `free`, `calloc`, `realloc`, `exit`, `abort`, `atoi`,
  `abs`, `labs`, `EXIT_SUCCESS`, and `EXIT_FAILURE`.
- `string.h`: `memcpy`, `memmove`, `memset`, `memcmp`, `strlen`, `strcmp`,
  `strncmp`, `strcpy`, `strncpy`, `strcat`, `strncat`, `strchr`, `strrchr`,
  and `strstr`.
- `stdint.h`, `stddef.h`, `stdbool.h`, `stdarg.h`, `limits.h`, and `assert.h`.
  `ctype.h` is a later addition.
- The console is accessed directly through MMIO: `putchar` writes
  `CONSOLE_OUT`; `getchar` blocks on the keyboard FIFO and returns a byte or
  `EOF`; `puts` appends a newline. No buffering is required in v1.
- `malloc` is a simple free-list allocator with 2-byte alignment and block
  coalescing. `calloc` zeroes; `realloc` copies and frees. `free(NULL)` is a
  no-op. The heap runs from `__heap_start` to `__heap_end`.
- `exit` halts with the status; `abort` halts with a nonzero status. `assert`
  prints a diagnostic and aborts; `NDEBUG` disables it.
- v1 is console-only. A later phase adds `open`/`read`/`write`/`close`/`lseek`/
  `stat` and `FILE*`/`fopen`/`fread`/`fwrite`/`fclose` on top of `SYS`.
  `SYS_VERSION` advertises the host ABI version and capabilities; `errno`
  carries the portable host error code. File access is sandboxed under
  `./sandbox` by default with a 5 MiB quota; `--no-host-fs` disables it.
- No file I/O, `scanf`, `time`, `math`, `setjmp`/`longjmp`, `signal`, locale,
  or wide-character support in v1.
- The BIOS jump table remains a teaching and extension feature; libc uses
  MMIO directly so it also works without a BIOS.

### 7.8 Debug Information

With `-g`, the compiler emits full `-O0` debug information:

- `.file` and `.loc` directives map instructions to source files, lines, and
  columns. The assembler builds the `.debug_line` section from these.
- The compiler emits `.debug_types`, `.debug_vars`, and `.debug_frames` as
  compact binary sections with fixed-width records and length-prefixed arrays;
  names go into the shared `.debug_str` table. `kr16 debug` renders them for
  inspection (human-readable in v1; `--json` later).
- Types: base types, signed/unsigned variants, pointers, arrays with
  dimensions, structs/unions with member offsets, enums with enumerators,
  typedefs, function types, qualifiers, and `void`. Type IDs are 16-bit; no
  type deduplication in v1.
- Variables: globals and statics at absolute addresses; locals and parameters
  at frame-pointer-relative locations; names, types, scopes, and declaration
  lines.
- Parameters are spilled into frame slots in the prologue before the first
  source-line `.loc`. A function-entry breakpoint maps to the first
  instruction after the prologue, where all parameters are visible.
- Frames: function name, PC range, FP/return-address layout, and parameter
  locations. At `-O0`, the frame-pointer chain is sufficient for stack walks.
- Strings are deduplicated in `first-use` order in `.debug_str`; symbols and
  debug records use 32-bit offsets into that one table.
- Source files: a global deduplicated source table with paths relative to the
  build directory, plus line/column mappings. Out-of-tree headers use an
  absolute fallback; source text is not embedded.
- The linker builds `.debug_objects`, merges/deduplicates `.debug_str`, and
  relocates the per-object sections; the `.kex` carries them unless `--strip`
  is used.
- The DAP contract is: symbols + line table + type table + variable/scope
  locations + frame information. This supports breakpoints, stepping, call
  stacks, locals, globals, watch expressions, and type expansion.
- `-O0` keeps all variables in memory, so debug locations are stable. `-O1`
  will need location lists and will come later.

### 7.9 Diagnostics and CLI

```text
kr16 cc [options] file.c -o out.o

  -E             preprocess only
  -S             emit assembly
  -c             compile and assemble (default when -o ends in .o)
  -o <file>      output file
  -I <dir>       add an include directory
  -D name=value  define a macro
  -U name        undefine a macro
  -g             emit full debug information
  -O0            no optimization (v1 default and only level)
  -Wall          enable warnings
  -Werror        treat warnings as errors
  -std=c99       select the C99 subset
  --version, --help
```

- One translation unit per invocation; use `kr16 ld` to link objects.
- Default output is `file.o`; `-S` defaults to `file.s`; `-E` writes to
  standard output unless `-o` is given.
- Diagnostics use `file:line:column`, a source excerpt with a caret, and notes
  for related locations. Multiple errors are reported per run.
- Warnings cover implicit conversions, unused variables, missing returns,
  unreachable code, shadowed declarations, and suspicious pointer/integer
  mixes.
- Exit codes: 0 success, 1 compile/assemble error, 2 usage error.

### 7.10 Testing

- Unit tests for the preprocessor, lexer/parser, type checker, constant
  folding, code generation, and diagnostics.
- Integration tests that compile and run C programs and compare console
  output, exit codes, and memory behavior.
- ABI tests: structs/unions by value, struct returns, 32-bit `long`
  arguments/returns, varargs, `printf`, and function pointers.
- Type tests: recursive arrays, typedefs, enums, unions, nested structs,
  pointer arithmetic, `sizeof`, `offsetof`, and designated initializers.
- Debug tests: line mapping, locals/parameters, scopes, type expansion, and
  call-stack walks through the DAP.
- Negative tests for syntax, type, and preprocessor errors.
- Windows and Linux CI, with the same expected results.
- A curated C99 test suite; self-hosting tests are not applicable because
  self-hosting is out of scope.

### 7.11 Limitations and Out of Scope

- Self-hosting is out of scope; the compiler is a C++20 host tool.
- No `float`, `double`, `long double`, `long long`, bitfields, VLAs, flexible
  array members, complex types, atomics, threads, `_Generic`,
  `_Static_assert`, anonymous structs/unions, or C11 features.
- No K&R old-style function definitions, implicit function declarations, GNU
  extensions, inline assembly, or `__attribute__`.
- No file I/O, `scanf`, `time`, `math`, `setjmp`/`longjmp`, signals, locale, or
  wide characters.
- No optimization beyond `-O0`; `-O1` later.
- No runtime bounds checking.
- `inline` and `restrict` are accepted but do not change code generation.
- Plain `char` is unsigned and fixed; `enum` is 16-bit; `long` has 2-byte
  alignment; `printf` has no floating-point conversions.
- The heap allocator is a simple free list, not a high-performance allocator.

### 7.12 Remaining Details

- Designated-initializer coverage and compound literals (later).
- `inline` semantics and `restrict` diagnostics.
- `ctype.h`, `qsort`, `bsearch`, and `assert` details.
- `%p` formatting and pointer-width conventions.
- `short`/enum corner cases and integer constant typing.
- Curated test-suite selection.
- `-O1` design (IR, register allocation, location lists).
- Future `__interrupt` attribute and C interrupt handlers.
- Future `setjmp`/`longjmp`.
- Future host file I/O: `FILE*`, open flags, stat layout, `errno` mapping, and
  sandbox integration.

### 7.13 Memory Budget: libc + User Program

The effective general RAM is `0x0100–0xEFFF` = 61,184 bytes (59.75 KiB),
after the first-page vectors/BIOS jump table. ROM and MMIO are separate, and
symbols/debug sections in the `.kex` are **not** loaded into RAM.

A rough `-O0` budget:

| Component | Hello world | Medium program | Large program |
|---|---:|---:|---:|
| `crt0` / startup | 0.1 KiB | 0.1 KiB | 0.2 KiB |
| libc code actually used | 5 KiB | 6 KiB | 10 KiB |
| libc data/bss | 0.2 KiB | 0.3 KiB | 0.5 KiB |
| user code (`-O0`) | 0.2 KiB | 15 KiB | 35 KiB |
| user data/bss | 0.1 KiB | 4 KiB | 25 KiB |
| heap | 0 KiB | 4 KiB | 8 KiB |
| stack | 1.5 KiB | 2.5 KiB | 6 KiB |
| **Total** | **~7 KiB** | **~32 KiB** | **~85 KiB** |
| **Free of 59.75 KiB** | **~53 KiB** | **~28 KiB** | **does not fit** |

Interpretation:

- A hello-world or small exercise fits easily.
- A medium teaching program (hundreds of lines, moderate globals, some
  `printf`) fits with substantial headroom.
- A large program with tens of kilobytes of code, large global arrays, a deep
  stack, and a large heap can exceed 60 KiB. A 100×100 `int` array alone is
  20,000 bytes; a 200×200 array is 80,000 bytes and cannot fit.
- The `printf` family is the largest libc component; unused libc functions
  should not be linked.
- `-O0` stack-machine code is intentionally fat. A later `-O1`/register
  allocator or a small peephole pass will improve the code-size margin.
- Self-hosting is not feasible in this budget; this is one more reason it is
  out of scope.

To keep the budget predictable:

1. **Runtime-only GC.** The compiler emits each function and data object in
   its own section; the linker removes unreferenced built-in runtime sections
   but keeps all user-defined sections. A program that only uses `puts`
   should not pay for `printf`.
2. **Lean libc.** The chosen runtime has no file I/O, `scanf`, `time`, math,
   floating point, or wide characters.
3. **Reserved stack, bounded heap.** The default layout reserves 4 KiB for the
   stack (`__stack_limit = 0xE000`), sets `__heap_end = __stack_limit`, and
   has the VM/runtime detect heap-stack collision. The reserve is
   linker-configurable for recursion-heavy examples.
4. **Size reporting.** `kr16 size program.kex` and `kr16 ld --map` report
   code/data/bss sizes and the free heap/stack gap, so students can see the
   budget instead of guessing.
5. **Use the heap for large data.** Large transient buffers should come from
   `malloc`; large permanent arrays should be sized deliberately.
6. **Debug info stays out of RAM.** Symbols and `.debug_*` sections live in
   the `.kex` file and are ignored by the loader.

The 60 KiB machine is comfortable for the intended high-school programs but
not for large applications, large global arrays, or deep recursion without
care. The 16-bit address space is the hard limit; if the curriculum ever needs
more, bank switching or a wider address space would be a major architectural
change, not a linker tweak.

## 8. Linker

### 8.1 Design Summary

| Area | Decision |
|---|---|
| Layout | Fixed order; max input alignment; `--base` only for raw/bare-metal; no script v1 |
| Runtime | User `_start` wins; otherwise `crt0` + libc auto-linked when `main` is present; `--nostdlib` disables |
| GC | Runtime-only: remove unused runtime sections, keep all user sections |
| Symbols | Strong-only; duplicate definitions are errors; object-scoped locals retained; no common/weak in v1 |
| First page | Loader zeroes; strict `.vectors`/`.bios_jump` inputs; `crt0` initializes; `--entry` overrides |
| ROM | `--rom` fixed BIOS mode plus generic `--raw --base`; stateless BIOS v1 |
| Debug | Fixed `.debug_objects` directory + source table; strings/source dedup; no type dedup |
| Map/size | Memory budget + sections + symbols + GC detail; JSON later |
| Relocations | Apply and range-check; no linker relaxation |

### 8.2 Default Layout

| Range | Use |
|---|---|
| `0x0000–0x000F` | Hardware vectors; reserved, or a provided `.vectors` section |
| `0x0010–0x00FF` | BIOS jump table; reserved, or a provided `.bios_jump` section |
| `0x0100–...` | `.text` |
| ... | `.rodata` |
| ... | `.data` |
| ... | `.bss` |
| `__heap_start` | Heap start, aligned to 2 |
| ... | Heap grows up |
| `__stack_limit` | Default `0xE000`; 4 KiB stack reserve |
| ... | Stack grows down |
| `0xF000` | `__stack_top`; MMIO starts here |
| `0xF000–0xF0FF` | Memory-mapped I/O |
| `0xF100–0xFFFF` | ROM |

- Default section order is `.text`, `.rodata`, `.data`, `.bss`; each section
  is aligned to its maximum input alignment, at least 2 bytes. Function/data
  sections are coalesced in input order; output is deterministic.
- `__heap_start` follows `.bss`, aligned to 2. `__heap_end =
  __stack_limit`. `__stack_top = 0xF000`.
- The linker errors if code/data/bss overlap the reserved first page, the MMIO
  page, the ROM, or if the heap start reaches the stack limit.
- `--base addr` is only for `--raw` or `--nostdlib` bare-metal images. A normal
  `.kex` is fixed at `0x0100`; combining `--base` with the built-in runtime is
  an error. `--entry symbol|addr` changes the entry point.
- `--stack-reserve bytes` changes the default 4 KiB stack reserve.
- The first page is not part of `.text`. It is reserved unless the inputs
  provide the strict special sections: `.vectors` exactly 16 bytes at
  `0x0000`, and `.bios_jump` exactly 240 bytes at `0x0010`. Wrong sizes or
  overlaps are errors; both sections are GC roots.
- Section order and the first-page locations are fixed in v1. A linker script
  can be added later if a custom layout is needed.

### 8.3 Runtime Linking

- The C runtime (`crt0`, libc, and 32-bit helpers) is embedded in `kr16` as
  built-in object images.
- If the inputs define `_start`, it wins: the linker does **not** auto-link
  `crt0`; the entry is `_start`, and libc is linked only if referenced. This
  supports custom bare-metal startup without `--nostdlib`.
- Otherwise, if `main` is present and `--nostdlib` is not given, `kr16 ld`
  includes the built-in runtime automatically and `crt0` is placed at
  `0x0100`. This makes `kr16 ld hello.o -o hello.kex` work without an archive
  or `-l` flag.
- `crt0` sets R4=0, zeroes `.bss` if needed, installs default vectors/jump-table
  entries, forwards R1/R2 to `main`, and halts with the return value.
- If both `main` and `_start` are present, `_start` wins. `--entry` overrides
  the entry point. If neither entry nor `_start` exists, `--entry` is required.
- With the default BIOS, the program entry must be `0x0100`, because the BIOS
  jumps there. A `--entry` outside `0x0100` requires `--no-bios` or raw mode;
  the linker still records the actual entry in the header.
- `--nostdlib` disables the built-in runtime entirely.
- The built-in runtime is tagged as runtime input so the GC policy can remove
  unused functions without touching user code.
- The runtime is not an archive and there is no `-l`/`-L` in v1. A user can
  supply a custom runtime with `--nostdlib` and explicit objects.
- The single `kr16` binary contains the runtime; no external runtime files
  are needed at link time.

### 8.4 Garbage Collection

- `--gc-sections` is on by default, but it operates **only on built-in runtime
  sections**.
- All user-defined functions and data objects are roots and are retained even
  if unreferenced. The strict `.vectors`/`.bios_jump` sections are roots too.
  This keeps student programs predictable and debug info stable.
- Unreferenced libc/runtime sections are removed. A program that only uses
  `puts` does not pay for `printf`; the 32-bit helpers are removed when no
  `long` arithmetic is used.
- Debug sections belonging to removed runtime sections are removed as well.
- `--no-gc-sections` disables runtime GC for debugging or comparison.
- `--gc-all` (standard reachability-based GC for user sections too) is a later
  addition.
- The map file reports which runtime sections were removed and how many bytes
  were saved.

### 8.5 Symbol Resolution

- Local symbols are scoped to their object. Global symbols are resolved across
  all inputs. Undefined symbols are errors with the referencing object and
  section.
- Strong-only: duplicate global definitions are link errors that report both
  definitions. Tentative definitions merge within a single translation unit
  because the compiler emits one definition; duplicates across translation
  units are errors.
- There are no common or weak symbols in v1. The C compiler uses modern
  strong-definition semantics; `extern` must be used for shared declarations.
- `static` functions and objects are local symbols and are retained with
  object identity. `.L` assembler-local labels are retained in the debug
  symbol table when `-g` is requested and omitted otherwise. The map prefixes
  object names; there is no name mangling in the `.kex` symbol table, and the
  DAP uses `.debug_objects` to disambiguate.
- `--strip` removes normal symbols and debug sections but preserves the
  special memory-layout symbols (`__stack_top`, `__stack_limit`,
  `__heap_start`, `__heap_end`) so the VM can enforce guards.
- The linker produces a merged symbol table for the `.kex`: functions,
  globals, statics, object-scoped locals, and special symbols. Local variables
  and types live in the debug sections.

### 8.6 First Page and Entry

- The VM/loader zeroes RAM, including the first page.
- Input `.vectors` must be exactly 16 bytes at `0x0000`; input `.bios_jump`
  must be exactly 240 bytes at `0x0010`. Wrong sizes or overlaps are errors.
  Both sections are GC roots and are loaded before execution.
- Otherwise the first page remains zero. `crt0` installs default hardware
  vectors and default BIOS jump-table entries where needed. It must not
  overwrite non-zero entries, so a BIOS or bare-metal image can pre-install
  handlers.
- Entry selection: a user `_start` wins; otherwise `crt0` at `0x0100` when
  `main` is present; `--entry` overrides. If neither exists, `--entry` is
  required. With the default BIOS the entry must be `0x0100`; other values
  require `--no-bios` or raw mode.
- The reset vector at ROM `0xFFFE` is separate from the RAM first page; it is
  handled by `--rom` mode or the BIOS image.
- A BIOS can initialize the first page and then jump to `0x0100`; `crt0`
  fills only what remains zero.

### 8.7 ROM and BIOS Images

- `--rom` produces a fixed 3,840-byte BIOS image at `0xF100–0xFFFF`.
  - Entry is `0xF100`.
  - The reset vector at `0xFFFE` is set to the entry address.
  - `.text` and `.rodata` are placed in ROM.
  - Unused bytes are filled with `0x00`.
  - The output is a raw ROM image, not a `.kex`; symbols/debug are not
    included. `kr16 dis --raw --base 0xF100` can inspect it.
- `.data` and `.bss` are rejected in v1; the reference BIOS is stateless.
  `--bios-ram` is deferred until a proper BIOS RAM map is designed, so a
  student BIOS that needs mutable data is a later feature.
- `--raw --base addr` produces a generic flat image for other ROM/loader
  cases. `.org` in the assembly can place absolute data such as the reset
  vector.
- The default embedded BIOS is a raw image; students can build their own with
  `--rom` and load it through the VM's BIOS command.

### 8.8 Debug Section Merging

- The linker concatenates per-object debug sections and relocates their
  addresses:
  - `.debug_line` from the assembler's `.file`/`.loc` data.
  - `.debug_types`, `.debug_vars`, `.debug_frames`, `.debug_str` from the
    compiler.
- `.debug_objects` is a fixed 16-byte header plus 48-byte per-object records
  and 12-byte source records: object ID/name, address range, the offsets/sizes
  of that object's line/types/vars/frames data, and the global source table.
- All debug records use fixed-width 16/32-bit fields and length-prefixed
  arrays; names are offsets into `.debug_str`. There is no varint encoding.
- The DAP uses the object directory and source table to resolve types,
  variables, line tables, and source locations.
- `.debug_str` is the single deduplicated string table shared by symbols and
  debug data; strings are stored in first-use order. Type deduplication is not
  done in v1; per-object type IDs stay object-local.
- Source paths are relative to the build directory with `/` separators,
  preserve case, normalize `.`/`..`, and use an absolute fallback for
  out-of-tree headers. Windows source dedup compares case-insensitively.
- Debug sections for GC'd runtime sections are removed. User debug sections
  are always retained.
- `--strip-debug` removes debug sections but keeps the normal symbol table;
  `--strip` removes both, except for the special memory-layout symbols.

### 8.9 Map and Size Reports

`kr16 ld --map file.map` produces a human-readable report:

- The final memory map and section order.
- Each output section: input objects, address, size, alignment, and flags.
- Removed runtime sections and bytes saved.
- The merged symbol table with addresses and sizes.
- The memory budget: code, rodata, data, bss, `__heap_start`,
  `__stack_limit`, `__stack_top`, free heap space, and free stack space.
- Any relocation or layout diagnostics.

`kr16 size program.kex` prints the same budget in a compact form: text,
rodata, data, bss, heap start/end, stack limit/top, and the free gap.

The map is generated only when `--map` is requested. A JSON map format is a
later addition; v1 keeps the human-readable report for students and tests.

The map is optional and never required for symbols or debugging; the `.kex`
carries those. A JSON map format can be added later.

### 8.10 Relocations and Errors

- The linker applies the four relocation types from Section 6.6:
  `ABS16`, `PCREL8W`, `PCREL16W`, and `PCREL16B`.
- It checks every result against the field width and reports the symbol,
  relocation type, object, section, computed value, and distance on overflow.
- There is no linker relaxation in v1. The assembler relaxes local branches,
  and the compiler uses 32-bit forms for external calls/jumps. A short
  external branch that is out of range is an error.
- Undefined symbols, duplicate definitions, overlapping sections, and
  first-page/MMIO/ROM collisions are errors with source/object context.
- Relocations in debug sections are applied with the same engine.
- Partial linking (`ld -r`) and weak/common symbols are out of scope for v1.

### 8.11 CLI

```text
kr16 ld [options] file.o ... -o out.kex

  --base addr          raw/bare-metal only; requires --raw or --nostdlib
  --entry symbol|addr  entry point (default _start, or 0x0100 with crt0)
  --stack-reserve n    stack reserve in bytes (default 4096)
  --nostdlib           do not auto-link crt0/libc
  --gc-sections        remove unused runtime sections (default)
  --no-gc-sections     keep all runtime sections
  --rom                fixed BIOS ROM image (0xF100-0xFFFF)
  --raw --base addr    generic flat image
  --map file           write a human-readable map
  --strip              remove symbols and debug (keep special symbols)
  --strip-debug        remove debug sections only
  --help
```

- The output is `.kex` by default; `--rom`/`--raw` produce flat images.
- A normal `.kex` is fixed at `0x0100`; `--base` with the built-in runtime is
  an error. Use `--raw --base` for flat images or `--nostdlib` for bare metal.
- A normal `.kex` with the default BIOS must enter at `0x0100`; other entries
  require `--no-bios` or raw mode. The header records the actual entry.
- `--strip` removes symbols/debug but keeps the header entry and the special
  memory-layout symbols.
- A custom `_start` disables `crt0` auto-linking; `--nostdlib` disables the
  entire built-in runtime.
- `kr16 size program.kex` prints the compact memory budget.
- Exit codes: 0 success, 1 link error, 2 usage error.

### 8.12 Testing

- Layout tests: fixed section order, maximum input alignment, first-page
  reservation, heap/stack symbols, and overflow errors.
- First-page tests: `.vectors` must be 16 bytes at `0x0000`; `.bios_jump` must
  be 240 bytes at `0x0010`; wrong sizes and overlaps are errors.
- Symbol tests: object-scoped locals, globals, externs, duplicate definitions,
  undefined references, and `--strip` special-symbol retention.
- Relocation tests: all four types, boundary values, overflow diagnostics, and
  debug-section relocation.
- Runtime tests: auto-linked `crt0`/libc, `--nostdlib`, user `_start` winning
  over `crt0`, `main` detection, `--entry`, and `crt0` first-page
  initialization.
- Base tests: `--base` succeeds only with `--raw`/`--nostdlib`; `--base` with
  the built-in runtime is an error.
- GC tests: unused runtime function removal, user-section retention, strict
  first-page sections as roots, and map reporting.
- ROM tests: 3,840-byte size, entry `0xF100`, reset vector `0xFFFE`, fill
  bytes, rejection of `.data`/`.bss`, and disassembly with `--raw --base`.
- Debug tests: multi-object line/type/variable merging, `.debug_objects`
  48-byte object and 12-byte source records, `.debug_str` dedup, and
  relative/absolute source-path handling.
- Budget/map tests: `kr16 size` and `--map` match the actual layout; map
  includes sections, symbols, GC removals, and the heap/stack budget; large
  programs fail with a clear heap/stack collision error.
- Deterministic output across repeated links; Windows/Linux parity.

### 8.13 Remaining Details

- Map-file column layout and debug-record merge/emit implementation details.
- `--bios-ram` and BIOS `.data`/`.bss` handling are deferred; v1 `--rom` is
  stateless.
- Linker script support and custom section order (later).
- `--gc-all`, partial linking (`-r`), weak symbols, and common symbols
  (later).
- JSON map format and machine-readable diagnostics (later).
- Error-message wording and source/object context formatting.

## 9. Virtual Machine Host

### 9.1 Design Summary

| Area | Decision |
|---|---|
| Host model | Console application; the program owns stdin/stdout |
| Modes | Batch `kr16 vm`, optional `-i` monitor, debug `kr16 dap` |
| Loading | `.kex` `ALLOC` sections; raw `--raw --base`; BIOS image |
| Execution | Deterministic simulated cycles; max speed default; `--realtime`/`--mhz` optional |
| Console I/O | Raw key mode with `--line-input` fallback; VM owns keyboard/output |
| BIOS | Embedded default; `--bios`/`--no-bios`; monitor `load-bios` |
| Threads | VM main thread + DAP thread + console thread |
| DAP | Control-only on fixed localhost port `22152`; console retains program I/O |
| Host services | `SYS` to sandboxed VM services; `./sandbox` default when file phase enabled |
| Headless | Scripted input, cycle limit, trace, exit code |

### 9.2 Host Modes and CLI

```text
kr16 vm program.kex [options]
kr16 vm -i program.kex [options]
kr16 dap [--port 22152] program.kex
```

- `kr16 vm` is the batch runner. It loads the program, runs it, and exits with
  the program status. The program owns stdin/stdout.
- `kr16 vm -i` adds a simple monitor. The program still owns stdin/stdout
  while it is running; `Ctrl+C` pauses/stops it and returns to the monitor.
- `kr16 dap` is the console host plus a DAP server thread. The console remains
  the program's terminal; VS Code controls execution only.
- `kr16 size` remains a separate, non-running report command.

Common options:

```text
--bios <file>       load a 3,840-byte ROM image
--no-bios           skip ROM; start at the .kex entry (default 0x0100)
--raw --base addr   load a flat binary at an address
--max-cycles n      stop with a timeout after n simulated cycles
--trace             trace instructions/registers/memory to stderr
--stats             print cycle/instruction counts at exit
--realtime          pace execution to a simulated clock
--mhz n             simulated clock rate for --realtime
--line-input        use line-buffered console input instead of raw keys
--headless          no console/DAP; for tests
--input <file>      scripted keyboard input for headless mode
--fs-root <dir>     use an existing sandbox root (default ./sandbox, lazy)
--fs-readonly       make the host filesystem sandbox read-only
--fs-quota 5M       cap total sandbox file data (default 5 MiB)
--no-host-fs        disable host file services
--trace-syscalls    log SYS service, args, and status
```

### 9.3 Loading, Reset, and BIOS

- Validate the `.kex` container (magic, version, kind, section bounds,
  checksum) before touching memory.
- Zero the 64 KiB RAM image; load every `ALLOC` section at its virtual
  address; zero `.bss`; load `.vectors`/`.bios_jump` if present.
- Load the ROM image at `0xF100–0xFFFF`. The default BIOS is embedded; a user
  BIOS comes from `--bios`, and `--no-bios` disables ROM and starts directly
  at the `.kex` header entry.
- The default BIOS is loaded automatically for normal runs. Precedence is
  `--bios` over the embedded default, and `--no-bios` disables ROM entirely.
  The default BIOS source lives in the repo, is built with the assembler, and
  is embedded into `kr16`, so students can inspect, disassemble, or replace it.
- `load-bios` in the monitor validates the image size and reset vector, then
  resets the CPU. ROM remains write-protected during execution.
- Build the command line before reset when arguments are present: place the
  argv array and strings in a block just below `0xF000`, set SP below the
  block, and set R1=argc, R2=argv. Limits are 16 arguments and 512 bytes.
  With no arguments, SP=`0xF000` and R1=R2=0.
- Reset the CPU: PC from `0xFFFE` (or the `.kex` header entry with
  `--no-bios`, or `--base` for raw images), SR=0, GPRs=0 except for the
  command-line R1/R2 values, I/O defaults; then start at the reset vector.
- The default BIOS preserves R1/R2 and SP across its initialization and jumps
  to the program entry. A custom BIOS must do the same if it wants
  command-line arguments to reach `main`/`_start`.
- Read `__stack_top`, `__stack_limit`, `__heap_start`, and `__heap_end` from
  the special symbols. Enforce the stack guard and heap-stack collision.
- Raw images (`--raw --base`) load bytes directly; no symbols or debug info.
  This is for BIOS/ROM experiments and small tests.
- Host services are initialized before execution. `SYS_VERSION` and the
  sandboxed service table are available. The default sandbox is `./sandbox`,
  created lazily on first use; `--no-host-fs` disables file services.
- A malformed image, bad BIOS, or missing entry point is a clear load error,
  not a crash.

### 9.4 Execution and Timing

- The VM executes fetch/decode/execute in a loop, advancing simulated cycles,
  updating Timer 0, checking pending interrupts, and checking breakpoints.
- The default is deterministic and as fast as possible. `--realtime` paces to
  a simulated clock (`--mhz`, default e.g. 1 MHz); `--max-cycles` bounds tests.
- Timer 0 counts simulated cycles and applies its prescaler; deterministic
  tests do not depend on wall-clock time.
- `HLT` stops the VM; the process exits with `R0 & 0xFF`. Unhandled
  exceptions, load errors, and timeouts produce nonzero host exit codes.
- `BRK` stops for the DAP when a debugger is attached; otherwise it vectors
  through the breakpoint vector.
- Illegal instructions, divide-by-zero, bus faults, and stack/heap violations
  become diagnostics with PC, registers, and a trace when enabled.
- `--trace` writes instruction/register/memory trace lines to stderr;
  `--stats` reports cycles and instructions at exit.

### 9.5 Console I/O

The console belongs to the VM, not to VS Code.

- In run mode, the program's keyboard input and character output use the
  `kr16` process's stdin/stdout.
- Default input is raw key mode: single keypresses, no host echo; the program
  controls echo through `CONSOLE_CTRL.ECHO`; keyboard interrupts fire
  immediately. `--line-input` uses terminal line editing/echo and delivers
  input after Enter.
- A console thread reads stdin and pushes bytes into the VM keyboard FIFO.
  `getchar` blocks until data is available; `KBD_STATUS` reports availability
  and overflow.
- `Ctrl+C` is the host break: it pauses/stops the program and returns to the
  monitor (or terminates in DAP mode). It is not delivered to the program.
- Program output goes to stdout; host diagnostics and trace logs go to stderr.
  The console translates `\n` to the host newline convention so Windows
  output looks natural.
- When the DAP is connected, the console **remains** the program's terminal.
  VS Code does not receive program output and does not supply program
  keyboard input. The Debug Console shows debugger messages only.
- If the process is launched without a TTY (for example, as a hidden child
  process), raw mode cannot work. The host reports that and falls back to
  `--line-input`/piped mode. See Section 10.7.

### 9.6 Host Services (SYS)

`SYS #n` is the guest-to-VM host service call. It is not a BIOS `TRAP`
and not a CPU exception. The VM is the trusted boundary; the guest never sees
native Windows/Linux API names, handles, or function pointers.

Call block: R0 points to a 16-byte, 2-byte-aligned block in guest RAM. The VM
validates the block and every guest pointer inside it.

| Word | Field | Meaning |
|---|---|---|
| 0 | `status` | 0 = success; otherwise a portable error code |
| 1 | `result0` | Primary result |
| 2 | `result1` | Second result / high word of a 32-bit result |
| 3 | `result2` | Third result |
| 4 | `arg0` | First argument |
| 5 | `arg1` | Second argument |
| 6 | `arg2` | Third argument |
| 7 | `arg3` | Fourth argument |

32-bit values use two consecutive words (low first). The block pointer stays
in R0; the VM writes status/results into the block and leaves R0 unchanged.
The C library builds this block on the stack and wraps it; students use normal
C functions, not raw `SYS`.

Service table (stable numbering):

| Number | Service | Arguments / result |
|---|---|---|
| 0 | `SYS_VERSION` | result0 = ABI version, result1 = capability bits |
| 1 | `SYS_OPEN` | arg0 = path, arg1 = flags, arg2 = mode; result0 = fd |
| 2 | `SYS_CLOSE` | arg0 = fd |
| 3 | `SYS_READ` | arg0 = fd, arg1 = buffer, arg2 = count; result0 = bytes |
| 4 | `SYS_WRITE` | arg0 = fd, arg1 = buffer, arg2 = count; result0 = bytes |
| 5 | `SYS_SEEK` | arg0 = fd, arg1 = offset low, arg2 = offset high, arg3 = whence; result0:result1 = new offset |
| 6 | `SYS_STAT` | arg0 = path, arg1 = stat buffer |
| 7 | `SYS_UNLINK` | arg0 = path |
| 8 | `SYS_RENAME` | arg0 = old path, arg1 = new path |
| 9 | `SYS_MKDIR` | arg0 = path |
| 10 | `SYS_READDIR` | arg0 = directory fd, arg1 = buffer, arg2 = count |
| 11 | `SYS_TIME` | result0:result1 = time value |
| 12 | `SYS_RANDOM` | result0 = 16-bit random value |
| 13 | `SYS_FSINFO` | arg0 = 6-word info buffer (used/quota/free, 32-bit pairs) |
| 14+ | reserved | |

- The sandboxed table is implemented. `SYS_VERSION` reports the capability bits
  for the active configuration; disabled host file services return `ENOSYS`.
- `SYS_VERSION` result1 capability bits: bit 0 host FS, bit 1 directories,
  bit 2 time, bit 3 random, bit 4 console fds, bit 5 FSINFO/quota.
- File descriptors 0/1/2 are the console; 3+ are files once file services are
  enabled.
- `SYS_FSINFO` returns used, quota, and free bytes as three 32-bit pairs in
  the caller's buffer; the 5 MiB default fits comfortably in 32 bits.

Portable error codes (word 0):

| Code | Name | Meaning |
|---|---|---|
| 0 | `OK` | Success |
| 1 | `ENOENT` | No such file or directory |
| 2 | `EACCES` | Permission/sandbox denial |
| 3 | `EINVAL` | Invalid argument |
| 4 | `EBADF` | Bad file descriptor |
| 5 | `EFAULT` | Invalid guest block or pointer |
| 6 | `ENOSPC` | No space |
| 7 | `EEXIST` | Already exists |
| 8 | `ENOSYS` | Service unavailable or host FS disabled |
| 9 | `EIO` | Host I/O error |
| 10 | `EMFILE` | Too many open files |
| 11 | `ENAMETOOLONG` | Path too long |
| 12 | `ENOTDIR` | Not a directory |
| 13 | `EISDIR` | Is a directory |
| 14 | `ELOOP` | Symlink loop or escape |

Sandbox policy:

- Host file services are enabled by default under the sandbox root. The
  following policy applies.
- When enabled, the default sandbox root is `./sandbox` relative to the
  process CWD. It is created **lazily on the first file operation**, with a
  one-line notice showing the root and quota. If the path exists as a file or
  symlink, the VM fails clearly. An explicit `--fs-root <dir>` must already
  exist and is never auto-created. `--no-host-fs` disables file services
  entirely.
- `--fs-readonly` allows reads/stat/readdir but denies create/write/truncate/
  unlink/rename/mkdir. `--fs-quota 5M` sets the quota (default 5 MiB).
- Guest paths use a **virtual root**: `/foo.txt` maps to `<sandbox>/foo.txt`
  and `sub/bar.txt` maps to `<sandbox>/sub/bar.txt`. The guest never sees a
  host absolute path.
- Reject, do not sanitize: absolute host paths, drive letters, UNC paths,
  backslashes, NUL/control characters, `..` components, symlinks/reparse
  points, Windows reserved device names, alternate data streams (`:`),
  trailing dots/spaces, and names that violate the portable filename policy.
  Denials return `EACCES` and are logged.
- Canonicalize the sandbox root and requested paths, then check containment
  component-wise. Do not use string-prefix checks (`sandbox2` must not match
  `sandbox`). On POSIX use `openat`/`O_NOFOLLOW`-style checks where practical;
  on Windows reject reparse points/junctions and handle reserved names.
- Directories are allowed **inside** the sandbox only. Limits: depth 16,
  1,024 entries, no `.`/`..` names, no devices/FIFOs/sockets, no
  `chmod`/`chown`/`link`/`symlink` services in v1. `SYS_UNLINK` removes files
  only; directory removal is a later service.
- Maximum open files is 16; maximum single transfer is 4,096 bytes; maximum
  path length is 255 bytes.
- Quota is the total logical size of regular files under the sandbox,
  including files that existed before the VM started. It is enforced on
  create, write, truncate, and rename into the sandbox; over-quota operations
  return `ENOSPC` (writes may be shortened, and reads remain allowed). Host
  disk exhaustion also maps to `ENOSPC`. Directory metadata is not counted.
- Quota accounting is per sandbox root and assumes one VM per sandbox. If the
  sandbox is modified externally, rescan at open/stat boundaries; a future
  lock file can prevent multiple VMs from sharing one quota.
- No native API passthrough, no host function pointers or handles, no shell,
  environment, registry, or arbitrary OS access. Guest error codes never
  expose host absolute paths.
- Windows uses `CreateFileW`/`ReadFile`/`WriteFile`/`SetFilePointerEx`/
  `GetFileAttributesEx`; Linux uses `openat`/`read`/`write`/`lseek`/`stat`.
  These live behind a `HostFs` interface and are never visible to the guest.
- `--trace-syscalls` logs the guest path, normalized decision, service,
  arguments, status, and result. Denied paths and quota failures are logged
  with the reason.

Execution and diagnostics:

- Host calls are synchronous. The VM thread performs the call; the console and
  DAP threads wait as needed. File I/O is expected to be short.
- The instruction charges a fixed 10 simulated cycles; host wall time does not
  advance the simulated timer. Interrupts are checked after the call; flags
  are unchanged; no guest vector is taken.
- `--trace-syscalls` logs the service number, arguments, status, and result to
  stderr. The DAP can step over a `SYS` and can expose the trace as
  debugger output.
- `SYS_VERSION` lets the C library detect host capabilities; file APIs are a
  later phase. v1 console I/O stays on MMIO.

### 9.7 Monitor

`-i` starts a small monitor for teaching and quick inspection:

| Command | Meaning |
|---|---|
| `run` | Run until halt, breakpoint, `Ctrl+C`, or timeout |
| `pause` | Pause the running VM |
| `step` | Execute one instruction |
| `regs` | Show R0–R4, PC, SP, SR, and cycle count |
| `mem addr [count]` | Dump 16-bit words from memory |
| `reset` | Reset the CPU and I/O; keep RAM/ROM |
| `load-bios file` | Load and validate a ROM image, then reset |
| `quit` | Exit the host |
| `help` | Command help |

While the program is running, the monitor is not reading commands; the
program owns the console. `Ctrl+C` pauses and returns to the prompt. In DAP
mode the monitor is disabled; the console is program I/O only.

### 9.8 Threading and DAP Takeover

- The VM executes on the main thread.
- A console thread reads stdin, feeds the keyboard FIFO, and handles `Ctrl+C`.
- A DAP thread owns the TCP listener and the DAP protocol.
- A command queue and mutex/condition variable serialize control. The DAP
  thread never touches CPU state directly; it submits requests and waits for
  the VM thread to complete them.
- `kr16 dap`:
  1. Loads the program and BIOS.
  2. Starts the DAP thread and binds `127.0.0.1:22152` (or `--port`).
  3. Prints the listening message to stderr.
  4. Starts the VM paused at the entry point.
  5. Waits for VS Code; after the launch/configuration handshake, VS Code
     controls run/pause/step while the console continues to serve program I/O.
- If the port is busy, the host reports a clear error and suggests `--port`.
- If VS Code disconnects, the VM pauses, the console prints a message, and the
  DAP thread resumes listening for a new connection. `Ctrl+C` exits.
- `--no-wait` can let the VM run until a breakpoint if a run-first session is
  wanted; the default is launch-paused.
- Windows/Linux caveat: the process must have a real console/TTY for raw
  keyboard input. Launch `kr16 dap` from a terminal or an integrated VS Code
  terminal, not as a hidden child process. The fixed port makes the
  terminal/attach workflow simple.

### 9.9 Headless Mode

- `--headless` disables the console and DAP threads.
- `--input file` (or stdin) supplies keyboard bytes; program output goes to
  stdout; `--max-cycles` bounds the run.
- `--trace` and `--stats` work as in normal mode; the process exits with the
  program status or a host error code.
- Headless mode is the primary CI and regression-test path. It is
  deterministic by default.

### 9.10 Diagnostics and Exit Codes

- Load errors, bad BIOS images, port conflicts, and usage errors are reported
  with actionable messages.
- CPU faults report PC, registers, and a trace excerpt; DAP-connected faults
  produce a `stopped` event with a reason.
- Exit codes:
  - `0..255`: normal `HLT` status from R0.
  - `1`: load/runtime/host error.
  - `2`: usage error.
  - `124`: `--max-cycles` timeout.
  - `130`: interrupted/terminated by `Ctrl+C`.
- Host logs go to stderr; program output goes to stdout.

### 9.11 Testing

- Load/run tests for `.kex`, raw images, BIOS/no-BIOS, first-page vectors, and
  entry points.
- CPU/device tests: timer, keyboard FIFO, interrupts, console output, raw and
  line input.
- Monitor tests: run/pause/step/regs/mem/reset/load-bios, `Ctrl+C` behavior.
- DAP tests: fixed port 22152, port-conflict error, launch-paused handshake,
  breakpoints, stepping, disconnect/reconnect.
- Headless tests: scripted input, max cycles, trace, exit codes, deterministic
  output.
- Host-call tests: `SYS_VERSION`, `ENOSYS` for unimplemented services, invalid
  block/pointer `EFAULT`, sandbox path escapes, symlink/reparse rejection,
  Windows reserved names/ADS, read-only denial, quota exhaustion, fd limits,
  lazy sandbox creation, and `--trace-syscalls`.
- Memory tests: stack guard, heap-stack collision, malformed `.kex`.
- Windows and Linux; use a PTY on POSIX for raw-input tests and line/headless
  mode in CI where a TTY is unavailable.

### 9.12 Remaining Details

- Exact monitor command syntax and output formatting.
- `Ctrl+C`/`Ctrl+D` behavior in raw, line, and DAP modes.
- Newline/echo translation details on Windows and POSIX.
- DAP disconnect/reconnect policy and `--run-first`/attach mode.
- Real-time pacing accuracy and default `--mhz`.
- BIOS `.bss`/`--bios-ram` loading is deferred; v1 loads stateless ROM
  images.
- Multiple DAP instances and `--port` handling.
- Program/console I/O when the process lacks a TTY.
- Host-call details: exact open flags, stat layout, `FILE*` integration, fd
  allocation, path canonicalization, and blocking behavior.

## 10. DAP Adapter and VS Code

### 10.1 Design Summary

| Area | Decision |
|---|---|
| Transport | TCP on `127.0.0.1:22152` by default; `--port` override |
| Role | DAP controls execution only; program I/O stays on the console |
| Startup | Load, listen, start paused at entry, wait for VS Code |
| Threading | DAP thread + VM main thread + console thread; command queue |
| Debug info | Symbols and `.debug_*` sections from the `.kex` |
| Features | Breakpoints, stepping, stack, locals, globals, registers, memory, disassembly, evaluate/watch |
| Extension | `DebugAdapterServer(22152)`; launch `kr16 dap` in an integrated terminal |
| Disconnect | Pause, print a message, resume listening |

### 10.2 Transport

- `kr16 dap [--port 22152] program.kex` binds `127.0.0.1` only. The default
  port `22152` is in the user range and is fixed; `--port` overrides it for
  multiple instances.
- One client at a time. A busy port is a clear startup error with a suggestion
  to use `--port`.
- DAP messages are JSON objects framed with `Content-Length` headers over TCP,
  UTF-8. The server implements the minimal DAP request/response/event set it
  needs.
- There is no port handshake or port file in v1. VS Code connects to the fixed
  port through `DebugAdapterServer`/`debugServer`. This is the simplest
  classroom setup; ephemeral ports can be added later if needed.
- Prior art: VS Code's `debugServer`/`DebugAdapterServer` accepts a fixed port;
  netcoredbg and lldb-dap expose a `--server`/`--port` TCP mode; debugpy uses
  a TCP socket with extension-coordinated ports. Fixed `22152` is the simplest
  single-instance workflow; a handshake line or port file is the more robust
  pattern if multiple instances or hidden launches become common.
- Localhost-only binding is the security boundary. There is no TLS or
  authentication in v1.
- DAP `output` events carry debugger messages (for example, "paused at main").
  Program output is **not** sent through DAP; it stays on the `kr16` console.

### 10.3 Startup and Takeover

`kr16 dap program.kex`:

1. Loads the `.kex`, BIOS, and first page.
2. Starts the DAP thread and binds `127.0.0.1:22152`.
3. Prints `DAP listening on 127.0.0.1:22152` to stderr.
4. Starts the VM paused at the entry point.
5. Waits for VS Code's `initialize`/`launch`/`configurationDone` handshake.
6. VS Code controls run/pause/step; the console continues to serve the
   program's keyboard and character I/O.

- `stopOnEntry` in the launch configuration controls whether VS Code stops at
  the entry point or runs to `main`.
- `--no-wait` can let the VM run until a breakpoint for a run-first session;
  the default is launch-paused.
- If VS Code disconnects, the VM pauses, the console prints a message, and the
  DAP thread resumes listening. `Ctrl+C` exits the host.
- The console monitor is disabled in DAP mode. The console is the program's
  terminal; VS Code is the debugger UI.

### 10.4 Threading

- VM main thread: executes instructions, checks breakpoints/watchpoints,
  services the command queue.
- Console thread: reads stdin, feeds the keyboard FIFO, handles `Ctrl+C`.
- DAP thread: owns the TCP connection and protocol parsing. It never mutates
  CPU state directly; it submits commands and receives snapshots/results.
- A mutex and condition variable protect the command queue and shared state.
  Stop/continue/step commands wake the VM thread; breakpoint hits produce
  `stopped` events.
- The DAP thread sends `stopped`, `continued`, `output`, and `terminated`
  events at the appropriate points.

### 10.5 Debug Features

- Breakpoints: source line (via `.debug_line`), address, function entry.
  Function-entry breakpoints resolve to the first instruction after the
  prologue so spilled parameters are visible. Watchpoints can be added later.
- Stepping: continue, pause, step in, step over, step out, single instruction.
- Call stack: FP-chain walk using `[R4]` and `[R4+2]`, with function names and
  source locations.
- Scopes and variables: locals, parameters, globals, statics; expandable
  structs/unions/arrays/pointers; enum/typedef display.
- Registers: R0–R4, PC, SP, SR, flags, cycle count.
- Memory: read/write views with byte and word formats; no I/O side effects on
  debug reads.
- Disassembly: symbol-aware `kr16 dis` output for the current PC and a window
  around it.
- Evaluate/watch: C expressions over the current frame; identifiers, member
  access, indexing, dereference, casts, and integer arithmetic. Function calls
  with side effects are out of scope for v1.
- Host calls: `SYS` is a single instruction and can be stepped over; the
  DAP can expose `--trace-syscalls` records as debugger output. Program
  console I/O remains outside DAP.
- Exceptions and `BRK` produce `stopped` events with reasons; `HLT` produces
  `terminated`.
- The single CPU is one DAP thread; no multi-thread debugging.

### 10.6 Debug Information

- The DAP reads the `.kex` directly: symbols, `.debug_line`,
  `.debug_types`, `.debug_vars`, `.debug_frames`, `.debug_str`, and the
  `.debug_objects` directory.
- Source paths are stored in the line table and the deduplicated source table;
  the DAP resolves relative paths against the recorded build directory, maps
  Windows/Linux paths, and can apply a source-map configuration later.
- `-g`/debug sections are required for source-level variables and types; a
  stripped `.kex` still supports registers, memory, disassembly, and address
  breakpoints, but not source-level locals.
- The DAP does not require a sidecar symbol or map file.
- The debugger's memory reads use a side-effect-free bus path; I/O registers
  are shown as snapshots, not read through device side effects.

### 10.7 VS Code Extension

- The extension contributes a `kr16` debugger type and a configuration
  provider.
- The adapter descriptor is a `DebugAdapterServer` on `127.0.0.1:22152`; the
  extension connects after the `kr16 dap` process is listening.
- Two supported workflows:
  1. **Extension-launched terminal.** The extension opens an integrated
     terminal running `kr16 dap --port 22152 program.kex`, waits for the port
     to accept, then starts the debug session. The terminal is the program's
     console and remains visible.
  2. **Manual launch/attach.** The student runs `kr16 dap --port 22152
     program.kex` in a terminal; the extension connects to the fixed port.
- The extension must **not** launch `kr16 dap` as a hidden child process for a
  program that uses keyboard input. On Windows there may be no visible console;
  on Linux there may be no TTY. Raw mode needs a real console. The integrated
  terminal workflow solves this on both platforms.
- The terminal shows program output and accepts keyboard input; the VS Code
  Debug Console shows debugger messages only. This separation is intentional.
- Launch configuration fields: `program`, `stopOnEntry`, optional `bios`,
  `fsRoot`, `fsReadonly`, `fsQuota`, `noHostFs`, `traceSyscalls`, `port`, and
  `args`/environment for the terminal command. A later `attach` request can
  connect to an already-running `kr16 dap`.
- The extension is a separate `vscode/` TypeScript/npm project packaged with
  `vsce` into a `.vsix`; the release archive includes it. Marketplace
  publishing is later. It is not built inside CMake. The toolchain and DAP
  server remain in the single `kr16` binary.

### 10.8 Testing

- Protocol tests with a scripted DAP client: initialize, launch, breakpoints,
  continue, pause, step in/over/out, stack, scopes, variables, registers,
  memory, disassembly, evaluate, disconnect.
- Transport tests: fixed port, busy-port error, `--port` override, localhost
  binding, one client.
- Startup tests: launch-paused, `stopOnEntry`, `--no-wait`, disconnect and
  reconnect.
- Debug-info tests: multi-object line/type/variable merging, source mapping,
  stripped executables.
- Console separation tests: program output appears on the terminal, not in
  DAP `output` events; DAP messages do not pollute the program stream.
- Host-call tests: `SYS` step-over and trace output; program console I/O
  stays outside DAP.
- Windows and Linux, including integrated-terminal/PTY behavior where
  available.
- VS Code extension integration tests where practical; at minimum, a manual
  smoke test of the launch configuration and terminal workflow.

### 10.9 Remaining Details

- Exact expression-evaluator scope and supported C syntax.
- Source-map configuration for relocated source trees.
- Marketplace publisher/signing and VSIX release versioning (`vsce` packaging
  is fixed).
- Attach/run-first mode and multiple simultaneous sessions.
- DAP logging/trace and protocol error recovery.
- Exact `stopped` event reasons and debugger message wording.
- Terminal automation details for the two launch workflows.
- Security beyond localhost if remote debugging is ever needed.

## 11. Build, Test, and Distribution

### 11.1 Build

- C++20, CMake 3.25+. CMake presets are the single build entry point; there is
  no secondary Makefile/Python build system.
- Windows: Visual Studio 2026 generator; fall back to Ninja + MSVC if the
  generator is not yet available.
- Linux: GCC 13+ or Clang 16+ with Ninja.
- One `kr16` executable target; internal static libraries per module.
- x64 Windows and Linux are the v1 targets; arm64 is a later addition.
- Release linkage: static C++ runtime, system libc. Windows uses `/MT`; Linux
  links libstdc++/libgcc statically and uses the system glibc.
- Warnings as errors: `/W4` on MSVC, `-Wall -Wextra -Wpedantic` on GCC/Clang.
- Sanitizers (ASan/UBSan) for Linux test builds.
- No platform APIs outside `platform/`.
- Repository layout: `src/`, `include/`, `tests/`, `third_party/doctest/`,
  `resources/prebuilt/`, `cmake/`, `vscode/`, `docs/`, `examples/`, and
  `LICENSE`.
- Embedded resources: checked-in prebuilt BIOS and runtime objects are
  converted by a CMake custom command into generated C++ byte arrays in a
  static library. Code uses `std::span<const std::byte>`; no `#embed`.
- CMake presets: `windows-vs2026`, `windows-ninja`, `linux-gcc`,
  `linux-clang`, with Debug/Release and a Linux sanitizer preset.
- Unit tests use a vendored doctest single header and CTest.

### 11.2 Test

- doctest unit tests, registered with CTest; local `ctest` is the primary
  developer path.
- Unit tests for ISA encode/decode, CPU instructions, flags, interrupts,
  timer, assembler, disassembler, compiler, linker, DAP protocol, and host
  services.
- Container tests: fixed offsets and sizes (64/32/20/16/48/12), 2-byte table
  order, 32-bit offsets/sizes, 16-bit addresses, CRC32 over the whole file,
  version/reserved handling, string dedup, section/symbol/relocation bounds,
  and malformed-file diagnostics.
- ABI tests: 16-bit register arguments, stack-only `long`/aggregate arguments,
  right-to-left caller cleanup, struct-return hidden pointer, varargs all-stack,
  `argc`/`argv`, parameter spill, and custom `_start`.
- Debug tests: fixed 8-byte line rows, `.debug_objects` 48-byte object and
  12-byte source records, per-object type IDs, global source table, source-path
  normalization, `kr16 debug` output, and parameter locations after the
  prologue.
- Host-call tests: `SYS_VERSION`, `ENOSYS`, invalid blocks/pointers, sandbox
  escapes, symlink/reparse rejection, read-only denial, quota exhaustion,
  Windows reserved names, lazy sandbox creation, and fd limits.
- Integration tests that assemble/link/run example programs and compare
  console output and exit codes.
- Headless VM mode with scripted input and cycle limits.
- CI: GitHub Actions on `windows-2025` and `ubuntu-22.04` for push/PR/tag
  workflows, with releases on `v*` tags. Windows/Linux Debug and Release,
  Linux ASan/UBSan, bootstrap/resource hash checks, and archive hash checks.
  Windows 10/11 x64 and glibc 2.35 are the baselines. CI uses Ninja + MSVC;
  local development uses Visual Studio 2026. The same commands run locally
  through CTest.

### 11.3 Distribution

- A single `kr16` executable per platform. No external runtime or resource
  files are needed at run time.
- Bootstrap strategy:
  - Check in prebuilt BIOS and runtime objects under `resources/prebuilt/`,
    plus `manifest.json`.
  - Normal builds embed generated arrays from those objects.
  - `bootstrap-check` regenerates into the build directory and diffs against
    `resources/prebuilt/`; CI runs it and fails on any difference.
  - `bootstrap-update` writes the regenerated resources deliberately; it is a
    maintainer command, not part of the normal build.
  - `manifest.json` records the generating `kr16` version, ISA/container/
    host-ABI/debug versions, source hash, and SHA256 of each resource. It
    contains no timestamp, so it stays reproducible.
  - This is not self-hosting: the host C++ compiler builds `kr16`; `kr16`
    rebuilds its own target resources.
- Versioning: development starts at `0.1.0`; `1.0.0` is the first classroom
  release. `kr16 version` prints
  `kr16 <semver> (ISA 1, container 1, host ABI 1, debug 1)`; development builds
  may append a git hash. `--json` is a later addition.
- Reproducible builds: no timestamps, sorted inputs, fixed resource order, and
  `SOURCE_DATE_EPOCH` support. CI compares binary, resource, and archive
  hashes. Release zip/tar archives normalize timestamps and permissions and
  sort entries so the archives themselves are reproducible.
- Release artifacts:
  - `kr16-<version>-windows-x64.zip`
  - `kr16-<version>-linux-x64.tar.gz`
  - `kr16-vscode-<version>.vsix`
  - `SHA256SUMS`
  - Archives contain `bin/`, `vscode/`, `docs/`, `examples/`, `README.md`, and
    `LICENSE`.
- CI/release: GitHub Actions on `windows-2025` and `ubuntu-22.04`, with
  push/PR/tag workflows and release publishing on `v*` tags. Windows 10/11 x64
  and glibc 2.35 (Ubuntu 22.04) are the baselines. CI uses Ninja + MSVC; local
  development uses Visual Studio 2026.
- License: MIT, with a `LICENSE` file and third-party notices for vendored
  doctest.
- VS Code extension: a separate `vscode/` TypeScript/npm project packaged with
  `vsce` into a `.vsix`. It is included in the release archive; it is not
  built inside CMake.
- The extension remains thin: it opens an integrated terminal running
  `kr16 dap --port 22152 program.kex` and connects to the fixed port. It must
  not launch the host as a hidden child process for interactive programs,
  because the program needs a real console/TTY.
- Windows x64 and Linux x64 are the v1 release targets. Marketplace
  publishing, code signing, MSI/deb/rpm installers, and arm64 builds are later
  additions.

## 12. Roadmap

### Phase 0 — Plan and ISA freeze

- Review this document.
- Freeze encodings, mnemonics, flags, memory map, and I/O map.
- Write `docs/isa.md` as the normative reference.
- Adopt the MIT license, `0.1.0` development version, and release naming.

### Phase 1 — CPU, memory, VM skeleton, assembler

- CMake skeleton, presets, vendored doctest, CTest, and generated-resource
  plumbing.
- Implement `isa`, `cpu`, memory bus, a minimal container reader/writer, and a
  minimal `vm` runner.
- Implement `as` for the core instruction set and simple sections/symbols.
- Run hand-written assembly programs and check output.

### Phase 2 — Interrupts, timer, console, disassembler

- Timer 0, keyboard FIFO, console output, interrupt vectors.
- `dis` with symbol annotations.
- CLI polish, diagnostics, tests, examples.

### Phase 3 — Full object format, linker, debug info

- Relocatable objects with symbols, relocations, lines, and types.
- `ld` with section layout, relocation, map file, and `.kex` output.
- Debug-section merging and relocation.
- ROM/BIOS image generation.

### Phase 4 — C compiler and runtime

- Preprocessor, parser, semantic analysis, codegen, ABI.
- `crt0`, libc subset, BIOS trap services if desired.
- Compile and run C examples, including interrupts and structs.

### Phase 5 — DAP and VS Code

- DAP server on TCP, VM debug state, breakpoints, stepping, stack, variables,
  registers, memory, disassembly.
- VS Code extension and launch configuration.
- End-to-end debug session tests.

### Phase 6 — Polish and curriculum

- Examples, exercises, documentation, error messages.
- Bootstrap check/update targets, resource manifest, deterministic archive
  checks, GitHub Actions matrix, release zip/tarball with SHA256SUMS, and
  VSIX packaging.
- Stretch: more devices, more C library, more debugger features.

## 13. Decisions Log

| Area | Decision | Status |
|---|---|---|
| Instruction encoding | 16/32-bit variable, 16-bit base | Confirmed |
| Register roles | R4 reserved as frame pointer | Confirmed |
| Addressing modes | Register, immediate, `[Rn]`, `[Rn+disp]`, PC-relative, absolute | Confirmed |
| Flags | C, Z, N, V, I; generic `Bcc` with 4-bit condition | Confirmed |
| Arithmetic | 16-bit `MUL`/`DIV`/`MOD`, signed and unsigned; `MULS`/`MULU` pair multiply | Confirmed |
| Multiply pair rule | `MULS`/`MULU` base `R0`/`R2` only, multiplier `R0`–`R4`; invalid encodings trap vector 0 | Confirmed |
| Interrupts | RAM vectors, maskable, fixed priority, no automatic nesting | Confirmed |
| BIOS calls | `TRAP #n` through a writable RAM jump table at `0x0010`; no privilege modes | Confirmed |
| I/O | 256-byte MMIO page at `0xF000` | Confirmed |
| Reset/stack | SP=`0xF000` (below argv block when args), full-descending; ROM writes ignored | Confirmed |
| Data | Little-endian; unaligned RAM data allowed | Confirmed |
| Memory map | Vectors `0x0000`, BIOS jump table `0x0010–0x00FF`, program `0x0100`, I/O `0xF000`, ROM `0xF100` | Proposed |
| BIOS jump table | RAM `0x0010–0x00FF`, 120 entries, entry n at `0x0010+2n` | Proposed |
| ROM size | 3.75 KiB (`0xF100–0xFFFF`); a 64-byte I/O page would allow 4,032 bytes | Proposed |
| Reset vector | ROM `0xFFFE–0xFFFF` | Confirmed |
| Instruction formats | R/I/M/B/M8/S/BIT/C/X as in Section 5.6 | Proposed |
| `HLT` exit | VM exits with `R0 & 0xFF` in run mode | Proposed |
| Assembler syntax | Intel-style, destination first; case-insensitive mnemonics/registers | Confirmed |
| Assembler output | Relocatable `.o` default; `--raw --base` for flat images | Confirmed |
| Object format | KR16 container; `.o`=REL, `.kex`=EXEC; 32-bit offsets/sizes, 16-bit addresses; fixed 64-byte header; CRC32 whole file | Confirmed |
| Container layout | Fixed offsets; header 64, section 32, symbol 20, reloc 16, object 48, source 12 bytes; 2-byte alignment | Confirmed |
| Version/reserved | Major/minor; zero reserved; ignore optional flags; reject unknown major; accept newer minor | Confirmed |
| String table | `.debug_str` shared by symbols/debug; dedup first-use; 32-bit offsets | Confirmed |
| Table order | Fixed order; symbols input order; relocations by section/offset | Confirmed |
| Relocations | `ABS16`, `PCREL8W`, `PCREL16W`, `PCREL16B` | Confirmed |
| Sizing/relaxation | Smallest form; local branch relaxation | Confirmed |
| Symbols | Global/extern plus `.L` locals with `-g`; labels case-sensitive | Confirmed |
| Directives/macros | Practical teaching set; simple macros/includes/conditionals | Confirmed |
| Debug format | Compact binary fixed-width records + length-prefixed arrays; `.debug_objects` + global source table; `kr16 debug` | Confirmed |
| Debug line | Count header + fixed 8-byte absolute rows sorted by address | Confirmed |
| Type/source IDs | Per-object 16-bit type IDs; one global deduplicated source table | Confirmed |
| Source paths | Relative to build dir, `/` separators, case-preserving, absolute fallback, Windows case-insensitive dedup | Confirmed |
| Debug dump | Human-readable v1; `--json` later | Confirmed |
| Disassembler | Symbol-aware, canonical, round-trippable | Confirmed |
| Diagnostics | `file:line:column`, caret, suggestions, optional `-l` listing | Confirmed |
| C dialect | C99 subset; no GNU extensions | Confirmed |
| C data model | `char` 8 unsigned, `short`/`int` 16, `long` 32, pointers 16, `_Bool` 8 | Confirmed |
| C types | `union`, `enum`, `_Bool`, `const`/`volatile`; no bitfields, VLAs, flexible arrays, `long long`, or floating point | Confirmed |
| C arrays | Fixed-size multidimensional, row-major, full decay/pointer arithmetic | Confirmed |
| C struct layout | Natural alignment; `long` aligned to 2 bytes | Confirmed |
| C preprocessor | Practical full preprocessor with function-like/variadic macros and `#`/`##` | Confirmed |
| C ABI | 16-bit scalar/pointer args R1–R3; long/aggregate stack-only; right-to-left, caller cleans; R0 / R0:R1; struct hidden R0 | Confirmed |
| Stack ABI | Right-to-left, caller cleans, 2-byte alignment, first stack arg at [FP+4] | Confirmed |
| Varargs | All arguments on stack; `va_list` 16-bit pointer; no aggregate `va_arg` | Confirmed |
| argc/argv | VM sets R1=argc, R2=argv on a stack block; argv[0] basename; 16 args/512B | Confirmed |
| Entry storage | `.kex` header entry field plus symbol; `--strip` keeps the header entry | Confirmed |
| C backend | Correctness-first `-O0`; typed AST to stack-machine assembly | Confirmed |
| C runtime | Practical embedded libc with varargs, printf family, malloc/free, string basics | Confirmed |
| C debug info | Full `-O0`; parameters spilled in prologue; function-entry breakpoint after prologue | Confirmed |
| Self-hosting | Out of scope; compiler is a C++20 host tool | Confirmed |
| Build | CMake 3.25+ presets; no secondary build system | Confirmed |
| Bootstrap | `bootstrap-check`/`bootstrap-update`; manifest with versions/hashes; CI check byte-identical | Confirmed |
| Embedded resources | Generated C++ byte arrays in a static library; `std::span` | Confirmed |
| Runtime linkage | Static C++ runtime, system libc; Windows `/MT`, Linux static libstdc++/libgcc | Confirmed |
| Versioning | `0.1.0` dev, `1.0.0` first classroom; semver + ISA/container/host ABI/debug versions; human-readable, `--json` later | Confirmed |
| Reproducibility | Deterministic binaries, resources, and archives; CI hash checks | Confirmed |
| CI | GitHub Actions `windows-2025`/`ubuntu-22.04`; push/PR/tags; release `v*`; Win10/11 x64 + glibc 2.35; sanitizers | Confirmed |
| Test framework | Vendored doctest + CTest | Confirmed |
| Packaging | OS/arch archives + VSIX + SHA256SUMS; deterministic; no installers | Confirmed |
| Extension build | Separate `vscode/` TypeScript + `vsce`; marketplace later | Confirmed |
| Architectures | x64 Windows/Linux v1; arm64 later | Confirmed |
| License | MIT with third-party notices | Confirmed |
| Release extras | Marketplace, code signing, installers, arm64 deferred | Confirmed |
| Memory budget | 60 KiB shared by code/data/bss/heap/stack; typical teaching programs fit | Proposed |
| Linker layout | Fixed order `.text`/`.rodata`/`.data`/`.bss`, max input alignment, input order; `--base` raw/bare-metal only; no script v1 | Confirmed |
| Runtime linking | User `_start` wins; otherwise `crt0`/libc auto-linked when `main` is present; `--nostdlib` | Confirmed |
| Linker GC | Runtime-only GC; all user sections and strict first-page sections retained | Confirmed |
| Symbols | Strong-only; duplicate definitions error; object-scoped locals retained; no common/weak in v1 | Confirmed |
| First page | Loader zeroes; strict `.vectors` (16B) and `.bios_jump` (240B); `crt0` initializes; `--entry` overrides | Confirmed |
| ROM images | Stateless v1: `.text`/`.rodata` only; `.data`/`.bss` rejected; `--bios-ram` deferred; plus `--raw --base` | Confirmed |
| Debug merging | Fixed `.debug_objects` directory + per-object records + deduplicated source table; no type dedup in v1 | Confirmed |
| Relocation range | Hard error with details; no linker relaxation | Confirmed |
| Stack/heap | 4 KiB stack reserve; `__stack_limit=0xE000`; heap ends at stack limit | Confirmed |
| Size reporting | `kr16 size` and `ld --map`: budget + sections + symbols + GC; JSON later | Confirmed |
| Deferred linker features | Linker scripts, `--gc-all`, partial linking `-r`, weak/common symbols | Confirmed |
| VM host | Console application; the program owns stdin/stdout | Confirmed |
| VM modes | Batch `vm`, optional `-i` monitor, `dap` debug host | Confirmed |
| VM timing | Deterministic max speed default; `--realtime`/`--mhz` optional | Confirmed |
| VM keyboard | Raw keys default; `--line-input` fallback; `Ctrl+C` host break | Confirmed |
| BIOS loading | Embedded default; `--bios`/`--no-bios`; monitor `load-bios` | Confirmed |
| VM threading | VM main + DAP thread + console thread; command queue | Confirmed |
| DAP port | Fixed `127.0.0.1:22152`; `--port` override | Confirmed |
| DAP I/O | Control-only; program keyboard/output stay on the console | Confirmed |
| DAP takeover | Launch paused; disconnect pauses and re-listens | Confirmed |
| Headless | Scripted input, cycle limit, trace, exit code | Confirmed |
| Host calls | `SYS #n` to VM host services; never raw native APIs | Confirmed |
| Host ABI | 16-byte block in R0: status, results, args | Confirmed |
| `SYS` encoding | C-type subop 15, `0x8Fnn`; 10 simulated cycles | Confirmed |
| Host FS | Later file phase; default `./sandbox` lazy-created; 5 MiB quota; `--fs-root`/`--no-host-fs`/`--fs-readonly`/`--fs-quota` | Confirmed |
| Host services v1 | `SYS_VERSION`; unimplemented services return `ENOSYS` | Confirmed |
| Reference BIOS | Embedded default loaded automatically; `--bios` overrides, `--no-bios` disables | Confirmed |

## 14. Open Questions

### CPU-level

Frozen for ISA v1 in `docs/isa.md`: opcode assignments and reserved-bit rules,
`WAIT` included, `ROL`/`ROR` excluded, cycle counts, 256-byte I/O page, and
BIOS jump-table numbering 0–13.

Remaining:

1. Stack-overflow/heap-collision detection: VM policy or runtime policy?
2. Should the VM zero RAM at reset by default? (Proposed: yes.)
3. Console key encoding beyond ASCII (arrows, function keys) in v1?

### Next-round questions

- VM implementation details: monitor command syntax, `Ctrl+C`/newline
  handling, DAP disconnect policy, real-time pacing accuracy, BIOS RAM, and
  no-TTY fallback.
- Host-call implementation details: open flags, stat layout, `FILE*`
  integration, fd allocation, path canonicalization, quota accounting/locking,
  lazy sandbox creation, and blocking behavior.
- DAP implementation details: expression-evaluator scope, source mapping,
  attach/multi-session, and protocol error recovery.
- Release details: marketplace publisher, code signing, installers, arm64, and
  `kr16 version --json` are later additions.
- Assembler implementation details: expression grammar, macro substitution,
  and listing format.
- Compiler implementation details: emitting the fixed 6.8 debug records,
  designated initializers, `inline`/`restrict`, and test-suite selection.
- Linker implementation details: map column layout, debug merge/emit
  mechanics, error wording; `--bios-ram` deferred; JSON map later.

## 15. Glossary

- **Word:** 16 bits.
- **Byte:** 8 bits.
- **KiB:** 1024 bytes.
- **MMIO:** Memory-mapped I/O; device registers appear at memory addresses.
- **Relocatable object:** Machine code with unresolved addresses, linked later.
- **Relocation:** A record telling the linker how to fix up an address.
- **KR16 container:** The self-describing `.o`/`.kex` file that carries
  sections, symbols, relocations (REL only), and debug sections.
- **REL / EXEC:** The two container kinds: relocatable object and executable.
- **DAP:** Debug Adapter Protocol, the protocol VS Code uses for debugging.
- **Trap:** A software-initiated call into the BIOS through the writable
  jump table at `0x0010`.
- **Host call / SYS:** A guest instruction that traps to the VM host for a
  service; distinct from a BIOS `TRAP`.
- **Host service ABI:** The stable, portable service table and call block the
  VM implements over native OS APIs.
- **Sandbox:** The `./sandbox` (or `--fs-root`) policy that limits guest file
  access to one directory and a configurable quota (default 5 MiB).
- **Exception:** A synchronous fault or breakpoint, delivered through a vector.
- **Interrupt:** An asynchronous device event, delivered through a vector.
- **Frame pointer (FP):** R4, pointing at the saved-FP/return-address pair of
  the current stack frame.
- **Full-descending stack:** SP points to the last pushed item; PUSH
  decrements SP before storing, POP loads before incrementing.

---

## Appendix A — Instruction Summary (Review Table)

This is the consolidated v1 instruction list for review. The detailed
encodings are in Section 5.6; the flag rules are in Section 5.9. All
instructions are 16-bit unless a 32-bit form is shown.

### A.1 Data Movement

| Mnemonic | Name | Forms | Description |
|---|---|---|---|
| `MOV` | Move | `Rd, Rs`; `Rd, #imm` | Copies a register or immediate into a destination register, including the special registers SP, SR, and PC. |
| `LD` | Load word | `Rd, [Rs+disp]`; `Rd, [addr16]` | Loads a 16-bit word from memory into a register. |
| `LDB` | Load byte | `Rd, [Rs+disp]`; `Rd, [addr16]` | Loads a byte from memory and zero-extends it to 16 bits. |
| `LDSB` | Load signed byte | `Rd, [Rs+disp]`; `Rd, [addr16]` | Loads a byte from memory and sign-extends it to 16 bits. |
| `ST` | Store word | `[Rs+disp], Rd`; `[addr16], Rd` | Stores a 16-bit word from a register into memory. |
| `STB` | Store byte | `[Rs+disp], Rd`; `[addr16], Rd` | Stores the low byte of a register into memory. |
| `LEA` | Load effective address | `Rd, [Rs+disp]` | Computes a memory address without accessing memory. |
| `PUSH` | Push | `Rs` | Pushes a register onto the full-descending stack. |
| `POP` | Pop | `Rd` | Pops a 16-bit value from the stack into a register. |
| `SEXT` | Sign-extend byte | `Rd` | Sign-extends the low byte of a register to 16 bits. |
| `ZXTB` | Zero-extend byte | `Rd` | Zero-extends the low byte of a register to 16 bits. |

### A.2 Arithmetic and Logic

| Mnemonic | Name | Forms | Description |
|---|---|---|---|
| `ADD` | Add | `Rd, Rs`; `Rd, #imm` | Adds two 16-bit values and updates C, Z, N, and V. |
| `ADC` | Add with carry | `Rd, Rs` | Adds two values plus the carry flag and updates C, Z, N, and V. |
| `SUB` | Subtract | `Rd, Rs`; `Rd, #imm` | Subtracts one value from another and updates C, Z, N, and V. |
| `SBC` | Subtract with borrow | `Rd, Rs` | Subtracts one value and the borrow from another and updates C, Z, N, and V. |
| `CMP` | Compare | `Rd, Rs`; `Rd, #imm` | Subtracts without writing the result, updating C, Z, N, and V. |
| `AND` | Bitwise AND | `Rd, Rs`; `Rd, #imm` | Computes the bitwise AND, clears C and V, and updates Z and N. |
| `OR` | Bitwise OR | `Rd, Rs`; `Rd, #imm` | Computes the bitwise OR, clears C and V, and updates Z and N. |
| `XOR` | Bitwise XOR | `Rd, Rs`; `Rd, #imm` | Computes the bitwise XOR, clears C and V, and updates Z and N. |
| `TEST` | Test bits | `Rd, Rs`; `Rd, #imm` | Computes the bitwise AND without writing the result, clears C and V, and updates Z and N. |
| `NOT` | Bitwise NOT | `Rd` | Computes the bitwise complement, clears C and V, and updates Z and N. |
| `NEG` | Negate | `Rd` | Computes the two's-complement negation and updates C, Z, N, and V. |
| `INC` | Increment | `Rd` | Adds one and updates C, Z, N, and V. |
| `DEC` | Decrement | `Rd` | Subtracts one and updates C, Z, N, and V. |
| `MUL` | Multiply low | `Rd, Rs` | Multiplies two 16-bit values and leaves the low 16 bits of the product in the destination. |
| `MULS` | Signed multiply | `Rd, Rs`; `Rd` shorthand | Computes the signed 16x16 product and writes the low word to `Rd` and the high word to `Rd+1`, where `Rd` is `R0` or `R2`. |
| `MULU` | Unsigned multiply | `Rd, Rs`; `Rd` shorthand | Computes the unsigned 16x16 product and writes the low word to `Rd` and the high word to `Rd+1`, where `Rd` is `R0` or `R2`. |
| `DIV` | Signed divide | `Rd, Rs` | Performs signed division and leaves the quotient in the destination, raising vector 2 on division by zero. |
| `DIVU` | Unsigned divide | `Rd, Rs` | Performs unsigned division and leaves the quotient in the destination, raising vector 2 on division by zero. |
| `MOD` | Signed remainder | `Rd, Rs` | Performs signed division and leaves the remainder in the destination, raising vector 2 on division by zero. |
| `MODU` | Unsigned remainder | `Rd, Rs` | Performs unsigned division and leaves the remainder in the destination, raising vector 2 on division by zero. |

### A.3 Shifts and Bit Operations

| Mnemonic | Name | Forms | Description |
|---|---|---|---|
| `SHL` | Shift left | `Rd, Rs`; `Rd, #count` | Shifts the destination left by a register or immediate count, setting C to the last bit shifted out. |
| `SHR` | Logical shift right | `Rd, Rs`; `Rd, #count` | Shifts the destination right logically by a register or immediate count, setting C to the last bit shifted out. |
| `SAR` | Arithmetic shift right | `Rd, Rs`; `Rd, #count` | Shifts the destination right arithmetically by a register or immediate count, preserving the sign bit. |
| `BTST` | Bit test | `Rd, Rs`; `Rd, #bit` | Tests a bit selected by a register or immediate index and sets Z to the complement of that bit. |
| `BSET` | Bit set | `Rd, Rs`; `Rd, #bit` | Sets a bit selected by a register or immediate index. |
| `BCLR` | Bit clear | `Rd, Rs`; `Rd, #bit` | Clears a bit selected by a register or immediate index. |
| `BTOG` | Bit toggle | `Rd, Rs`; `Rd, #bit` | Toggles a bit selected by a register or immediate index. |

### A.4 Control Flow and System

| Mnemonic | Name | Forms | Description |
|---|---|---|---|
| `Bcc` | Branch on condition | `target` | Branches PC-relative when the selected condition is true. |
| `JMP` | Jump | `target`; `Rs` | Jumps to a PC-relative target or to the address in a register. |
| `CALL` | Call | `target`; `Rs` | Pushes the return address and jumps to a PC-relative target or to the address in a register. |
| `RET` | Return | — | Pops a return address into PC. |
| `RETI` | Return from interrupt | — | Pops PC and SR, returning from an interrupt or exception. |
| `TRAP` | BIOS call | `#n` | Pushes the return address and calls the BIOS function at jump-table entry n (`0x0010 + 2n`). |
| `SYS` | Host call | `#n` | Calls VM host service n using the 16-byte block pointed to by R0. |
| `BRK` | Breakpoint | — | Raises the breakpoint exception (vector 1), which a debugger may intercept. |
| `HLT` | Halt | — | Stops the CPU, and run mode exits with R0 & 0xFF. |
| `NOP` | No operation | — | Does nothing. |
| `EI` | Enable interrupts | — | Enables interrupts by setting I. |
| `DI` | Disable interrupts | — | Disables interrupts by clearing I. |
| `WAIT` | Wait for interrupt | — | Waits for an interrupt. |

### A.5 Branch Conditions

`Bcc` conditions are written as `B` + suffix, for example `BEQ`, `BNE`,
`BCS`, `BHI`, `BGE`, `BLT`, `BLE`, and `BRA`.

| Suffix | Name | Taken when | Meaning |
|---|---|---|---|
| `EQ` | Equal | Z=1 | Equal |
| `NE` | Not equal | Z=0 | Not equal |
| `CS` / `HS` | Carry set / higher or same | C=1 | Unsigned >= |
| `CC` / `LO` | Carry clear / lower | C=0 | Unsigned < |
| `MI` | Minus | N=1 | Negative |
| `PL` | Plus | N=0 | Positive or zero |
| `VS` | Overflow set | V=1 | Signed overflow |
| `VC` | Overflow clear | V=0 | No signed overflow |
| `HI` | Higher | C=1 and Z=0 | Unsigned > |
| `LS` | Lower or same | C=0 or Z=1 | Unsigned <= |
| `GE` | Greater or equal | N = V | Signed >= |
| `LT` | Less than | N != V | Signed < |
| `GT` | Greater than | Z=0 and N = V | Signed > |
| `LE` | Less or equal | Z=1 or N != V | Signed <= |
| `AL` | Always | always | Unconditional (`BRA`) |
| `NV` | Never | never | Never |

### A.6 Notes and Aliases

- `BRA` is an alias for `BAL` (branch always).
- `BHS` is an alias for `BCS`; `BLO` is an alias for `BCC`.
- `JMP #addr` may be assembled as `MOV PC, #imm16`.
- `RET` is equivalent to `POP PC`; `RETI` is the interrupt/exception return.
- `MOV` also provides the special-register forms: `MOV Rd, SP`, `MOV SP, Rd`,
  `MOV Rd, SR`, `MOV SR, Rd`, `MOV Rd, PC`, and `MOV PC, Rd`.
- `PUSH` and `POP` accept any of the eight register codes, including SR and PC.
- `MUL` remains the 16-bit low-only, sign-agnostic multiply.
- `MULS`/`MULU` require a pair base of `R0` or `R2`; `R1`, `R3`, and `R4`–`R7`
  are illegal and raise the illegal-instruction exception (vector 0).
- The `MULS`/`MULU` multiplier must be `R0`–`R4`; `SP`, `SR`, and `PC` are also
  illegal.
- `MULS`/`MULU` set Z from the full 32-bit result and N from bit 31, unlike
  the 16-bit `MUL`.
- `MULS R0` is shorthand for `MULS R0, R1`; `MULU R2` is shorthand for
  `MULU R2, R3`.
- Pair multiplies write low to `Rd` and high to `Rd+1`; both sources are read
  before either destination is written.
- `SYS #n` calls a VM host service using the 16-byte block at R0. It is
  distinct from `TRAP` (BIOS) and `BRK` (debug), leaves flags unchanged, and
  reports host errors in the block rather than as CPU exceptions.
- `LD`, `LDB`, `LDSB`, `ST`, `STB`, and `LEA` support a PC-relative base when
  `Rs = PC`; the base is the address of the next instruction.
- 32-bit forms are chosen by the assembler when a full 16-bit immediate,
  address, or displacement is needed.
