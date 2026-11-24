# KR16 ISA Reference

Status: frozen for v1
Applies to: KR16 ISA version 1
Companion design: [../kr16-plan.md](../kr16-plan.md)

This document is the normative instruction-set reference. If implementation
and this document disagree, this document wins for the ISA. Changes to the ISA
must update this document and the design before code changes land.

## 1. Overview

KR16 is a 16-bit, byte-addressable, little-endian CPU with a 64 KiB address
space. Instructions are 16 bits, with 32-bit forms when a full immediate,
address, or branch displacement is needed. There are five general registers
plus PC, SP, and SR.

- Word size: 16 bits.
- Address space: 64 KiB.
- Endianness: little-endian.
- Unaligned RAM data access: allowed; unaligned word access costs one extra
  cycle.
- ROM is write-protected while the CPU runs.
- There are no privilege modes, caches, pipelines, or delay slots.

## 2. Registers

| Code | Register | Role |
|---|---|---|
| 0 | `R0` | Return value / scratch / pair base |
| 1 | `R1` | First argument / return high word |
| 2 | `R2` | Second argument / pair base |
| 3 | `R3` | Third argument |
| 4 | `R4` / `FP` | Frame pointer; callee-saved |
| 5 | `SP` | Stack pointer; full-descending |
| 6 | `SR` | Status register |
| 7 | `PC` | Program counter |

`SR` layout:

| Bit | Name | Meaning |
|---|---|---|
| 0 | C | Carry / no-borrow |
| 1 | Z | Zero |
| 2 | N | Negative |
| 3 | V | Signed overflow |
| 4 | I | Interrupt enable |
| 15..5 | — | Reserved, read as zero |

`C` is carry-out for addition and "no borrow" for subtraction. `RETI` restores
`SR` from the stack. `EI` sets I; `DI` clears I.

## 3. Memory Map

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
  0xF100 | BIOS ROM code/data         |  3,838 bytes
  0xFFFD +----------------------------+
  0xFFFE | Reset vector               |  16-bit address
  0xFFFF +----------------------------+
```

- RAM: `0x0000–0xEFFF`, 60 KiB.
- First page: `0x0000–0x00FF`; 16 bytes of vectors, 240 bytes of BIOS jump
  table.
- Program entry: `0x0100` by convention.
- MMIO: `0xF000–0xF0FF`.
- ROM: `0xF100–0xFFFF`, 3,840 bytes including the reset vector.
- Stack top: `0xF000`; default stack limit: `0xE000`.

Hardware vectors (16-bit handler addresses):

| Address | Vector | Source |
|---|---|---|
| `0x0000` | 0 | Illegal instruction |
| `0x0002` | 1 | Breakpoint |
| `0x0004` | 2 | Divide by zero |
| `0x0006` | 3 | Timer 0 |
| `0x0008` | 4 | Keyboard |
| `0x000A` | 5 | Bus fault |
| `0x000C–0x000F` | 6–7 | Reserved |

BIOS jump table entry `n` is at `0x0010 + 2n`, `n = 0..119`. `TRAP #n` reads
this table. Proposed standard services:

| n | Service | n | Service |
|---|---|---|---|
| 0 | `putchar` | 7 | `kbd_status` |
| 1 | `getchar` | 8 | `random` |
| 2 | `puts` | 9 | `memcpy` |
| 3 | `exit` | 10 | `memset` |
| 4 | `timer_set` | 11 | `strlen` |
| 5 | `timer_read` | 12 | `version` |
| 6 | `kbd_read` | 13 | `delay` |

`14–119` are reserved.

## 4. Instruction Fetch and Length

Fetch the 16-bit word at PC.

- If the top nibble is `0xF`, the instruction is 32-bit; fetch word 2 at
  `PC + 2`.
- If the top nibble is `0x8` and subop (bits 11–8) is `0` or `1`, the
  instruction is 32-bit.
- Otherwise it is 16-bit.

An odd instruction-fetch address raises the bus-fault exception (vector 5).

## 5. Instruction Formats

Notation: `Rd` and `Rs` are 3-bit register codes. All fields are unsigned
unless marked signed. Reserved fields must be zero; a nonzero reserved field
raises the illegal-instruction exception (vector 0).

### R-type — register ALU

```text
  15..12  11..9  8..6  5..0
  0000    Rd     Rs    function
```

### I-type — immediate ALU

```text
  15..12  11..9  8..6  5..0
  0001    Rd     func  imm6 (signed, -32..31)
```

I-type functions: 0 ADD, 1 SUB, 2 CMP, 3 AND, 4 OR, 5 XOR, 6 MOV, 7 TEST.

### M-type — load/store/LEA, 16-bit

```text
  15..12  11..9  8..6  5..0
  opcode  Rd     Rs    disp6 (signed, -32..31)
```

Address = `Rs + sign_extend(disp6)`, modulo 64 KiB. When `Rs = PC`, the base
is the address of the next instruction.

### M8-type — MOV immediate 8-bit

```text
  15..12  11..9  8     7..0
  1001    Rd     sign  imm8
```

`sign = 0`: zero-extend `imm8`. `sign = 1`: sign-extend `imm8`.

### B-type — conditional branch

```text
  15..12  11..8  7..0
  0111    cond   disp8 (signed, in words)
```

Target = `PC + 2 + disp8 * 2`. Range: `-256..+254` bytes.

### S-type — shift by immediate

```text
  15..12  11..9  8..6  5..0
  1011    Rd     func  count (0..15)
```

`func`: 0 SHL, 1 SHR, 2 SAR.

### BIT-type — bit operation by immediate

```text
  15..12  11..9  8..6  5..0
  1100    Rd     func  bit (0..15)
```

`func`: 0 BTST, 1 BSET, 2 BCLR, 3 BTOG.

### C-type — control / special

```text
  15..12  11..8  7..5     4..0
  1000    subop  regA     rest
```

`subop` 0 and 1 are 32-bit; the rest are 16-bit. See the control table below.

### X-type — extended 32-bit

```text
  Word 1:
  15..12  11..8  7..5  4..0
  1111    ext_op Rd    aux

  Word 2:
  15..0
  imm16 / address / displacement (little-endian)
```

For `ext_op` 0–12, `aux` must be 0. For 13–15, `aux` is `Rs`.

## 6. Major Opcode Map

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
| `0x8` | C | Control / special |
| `0x9` | M8 | `MOV Rd, #imm8` |
| `0xA` | M | `LEA` |
| `0xB` | S | Shift by immediate |
| `0xC` | BIT | Bit operation by immediate |
| `0xD` | — | Reserved |
| `0xE` | — | Reserved |
| `0xF` | X | Extended 32-bit |

## 7. R-type Function Codes

| Code | Mnemonic | Operation |
|---|---|---|
| 0 | `ADD` | Rd <- Rd + Rs |
| 1 | `ADC` | Rd <- Rd + Rs + C |
| 2 | `SUB` | Rd <- Rd - Rs |
| 3 | `SBC` | Rd <- Rd - Rs - (1 - C) |
| 4 | `CMP` | flags of Rd - Rs |
| 5 | `AND` | Rd <- Rd & Rs |
| 6 | `OR` | Rd <- Rd \| Rs |
| 7 | `XOR` | Rd <- Rd ^ Rs |
| 8 | `MOV` | Rd <- Rs |
| 9 | `TEST` | flags of Rd & Rs |
| 10 | `NEG` | Rd <- -Rd |
| 11 | `NOT` | Rd <- ~Rd |
| 12 | `INC` | Rd <- Rd + 1 |
| 13 | `DEC` | Rd <- Rd - 1 |
| 14 | `SHL` | Rd <- Rd << (Rs & 0x0F) |
| 15 | `SHR` | Rd <- Rd >> (Rs & 0x0F) |
| 16 | `SAR` | arithmetic right shift |
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
| 28 | `MULS` | Rd:Rd+1 <- signed product; Rd = R0 or R2 |
| 29 | `MULU` | Rd:Rd+1 <- unsigned product; Rd = R0 or R2 |
| 30–63 | — | Reserved |

Unary functions ignore `Rs`; the assembler encodes `Rs = Rd` (or 0).

`MULS`/`MULU` require `Rd` to be `R0` or `R2` and `Rs` to be `R0–R4`.
Invalid pair bases or multipliers raise vector 0. Both sources are read before
either destination is written.

## 8. C-type Subops

| Subop | Mnemonic | Size | Fields | Operation |
|---|---|---|---|---|
| 0 | `JMP rel16` | 32 | word 2 = signed word displacement | PC <- next PC + rel16*2 |
| 1 | `CALL rel16` | 32 | word 2 = signed word displacement | Push next PC; PC <- target |
| 2 | `JMP Rs` | 16 | regA = Rs | PC <- Rs |
| 3 | `CALL Rs` | 16 | regA = Rs | Push next PC; PC <- Rs |
| 4 | `RET` | 16 | — | PC <- pop |
| 5 | `RETI` | 16 | — | PC <- pop; SR <- pop |
| 6 | `TRAP #n` | 16 | bits 7..0 = n | Push next PC; PC <- `[0x0010 + 2n]`, n=0..119 |
| 7 | `BRK` | 16 | — | Breakpoint exception (vector 1) |
| 8 | `HLT` | 16 | — | Stop CPU |
| 9 | `NOP` | 16 | — | No operation |
| 10 | `EI` | 16 | — | Set I |
| 11 | `DI` | 16 | — | Clear I |
| 12 | `WAIT` | 16 | — | Wait for interrupt |
| 13 | `PUSH Rs` | 16 | regA = Rs | Push register |
| 14 | `POP Rd` | 16 | regA = Rd | Pop register |
| 15 | `SYS #n` | 16 | bits 7..0 = n; R0 = block | Host service call |

`JMP rel16`/`CALL rel16` target = instruction address + 4 + rel16*2.

`TRAP #n` does not push SR or change I. It pushes the return address and jumps
through the RAM table. A zero table entry is an unhandled trap in the VM.

`BRK` pushes SR and the next PC, clears I, and vectors to vector 1. A debugger
may intercept it before delivery.

`HLT` stops the CPU. In run mode the VM exits with `R0 & 0xFF`.

`WAIT` suspends instruction execution until an interrupt source is pending.
If I=1, the interrupt is taken; if I=0, execution resumes at the next
instruction. The VM continues to advance the timer while waiting.

`SYS #n` is a VM host call, not a guest exception and not a BIOS `TRAP`.
R0 points to a 16-byte, 2-byte-aligned block:

| Word | Field |
|---|---|
| 0 | status |
| 1 | result0 |
| 2 | result1 / high word |
| 3 | result2 |
| 4 | arg0 |
| 5 | arg1 |
| 6 | arg2 |
| 7 | arg3 |

The VM validates the block and guest pointers, performs the service, writes
status/results, leaves R0 unchanged, and advances PC. Flags are unchanged; the
instruction costs 10 simulated cycles. An invalid block returns `EFAULT`; an
unimplemented service returns `ENOSYS`. v1 implements `SYS_VERSION`; other
services are reserved.

## 9. Branch Conditions

| Code | Name | Taken when | Meaning |
|---|---|---|---|
| 0 | `EQ` | Z=1 | Equal |
| 1 | `NE` | Z=0 | Not equal |
| 2 | `CS`/`HS` | C=1 | Unsigned >= |
| 3 | `CC`/`LO` | C=0 | Unsigned < |
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

Mnemonics are `B` + condition: `BEQ`, `BNE`, `BCS`, `BHI`, `BGE`, `BLT`,
`BLE`, `BRA`.

## 10. Extended (X-type) Operations

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
| 8 | `LD Rd, [addr16]` | word load |
| 9 | `ST [addr16], Rd` | word store |
| 10 | `LDB Rd, [addr16]` | byte load, zero-extend |
| 11 | `LDSB Rd, [addr16]` | byte load, sign-extend |
| 12 | `STB [addr16], Rd` | byte store |
| 13 | `LD Rd, [Rs+disp16]` | word load, indexed |
| 14 | `ST [Rs+disp16], Rd` | word store, indexed |
| 15 | `LEA Rd, [Rs+disp16]` | address computation |

The 16-bit displacement is signed and added modulo 64 KiB. When `Rs = PC`, the
base is the next PC (instruction address + 4).

## 11. Addressing Modes

| Mode | Syntax | Encoding |
|---|---|---|
| Register | `Rd, Rs` | R-type |
| Immediate | `#imm` | I/M8/X-type |
| Register indirect | `[Rs]` | M-type, disp=0 |
| Indexed | `[Rs+disp]` | M-type disp6 or X-type disp16 |
| PC-relative | `[PC+disp]` | M/X-type with Rs=PC |
| Absolute | `[addr16]` | X-type |
| Branch | `Bcc label` | B-type |
| Jump/call label | `JMP/CALL label` | C-type 32-bit |
| Jump/call register | `JMP/CALL Rs` | C-type 16-bit |

There is no auto-increment/decrement addressing.

## 12. Flags

| Group | C | Z | N | V |
|---|---|---|---|---|
| ADD/ADC | carry out | result==0 | bit 15 | signed overflow |
| SUB/SBC/CMP | NOT borrow | result==0 | bit 15 | signed overflow |
| AND/OR/XOR/TEST | 0 | result==0 | bit 15 | 0 |
| NOT | 0 | result==0 | bit 15 | 0 |
| NEG/INC/DEC | as ADD/SUB | result==0 | bit 15 | signed overflow |
| MUL | 0 | low result==0 | low bit 15 | 0 |
| MULS/MULU | 0 | full 32-bit==0 | bit 31 | 0 |
| DIV/DIVU/MOD/MODU | 0 | result==0 | bit 15 | 0 |
| SHL/SHR/SAR | last bit out | result==0 | bit 15 | 0 |
| BTST | — | NOT bit | — | — |
| BSET/BCLR/BTOG | — | — | — | — |
| MOV/loads/stores/LEA/PUSH/POP/SEXT/ZXTB | — | — | — | — |
| SYS | — | — | — | — |
| EI/DI | — | — | — | — |

A shift count of zero leaves C, Z, N, and V unchanged. `DIV`/`MOD` by zero
raises vector 2 before writing the destination.

## 13. Cycle Counts

| Class | Cycles |
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
| 32-bit instruction | +1 over equivalent 16-bit form |
| Hardware interrupt entry | 7 |
| Unaligned RAM word access | +1 |
| `WAIT` | 1 to enter; cycles advance while waiting |

Host wall time inside `SYS` does not advance the simulated timer.

## 14. Interrupts and Exceptions

Interrupts are checked between instructions when I=1. Priority is keyboard
before timer.

Entry:

1. SP -= 2; `[SP] = SR`.
2. SP -= 2; `[SP] = PC` (next instruction).
3. Clear I.
4. PC = vector table entry.
5. 7 cycles.

`RETI` pops PC then SR and costs 5 cycles. Hardware interrupts do not nest
automatically.

Exceptions:

| Exception | Vector | Saved PC |
|---|---|---|
| Illegal instruction | 0 | Faulting instruction |
| Breakpoint | 1 | Next instruction |
| Divide by zero | 2 | Faulting instruction |
| Bus fault | 5 | Faulting instruction |

A zero vector entry halts the VM with an unhandled-exception diagnostic.

## 15. Reset and Boot

- PC = little-endian 16-bit value at `0xFFFE`.
- SP = `0xF000`; the VM lowers it when building an argv block.
- R0–R4 = 0; the VM may set R1=argc and R2=argv at program entry.
- SR = 0.
- I/O registers return to reset values.
- RAM is zeroed by the VM for determinism.
- ROM is loaded before execution and write-protected.

Normal `.kr16` programs enter at `0x0100`. With `--no-bios`, the VM starts at the
`.kr16` header entry (default `0x0100`). Raw images start at `--base`.

## 16. Reserved and Illegal Encodings

The following are illegal and raise vector 0 unless stated otherwise:

- Top nibbles `0xD` or `0xE`.
- R-type function codes 30–63.
- Nonzero reserved fields or `aux` fields that must be zero.
- `MULS`/`MULU` with a pair base other than R0/R2 or a multiplier outside
  R0–R4.
- `LD`/`LDB`/`LDSB`/`LEA` with `Rd = PC` or `Rd = SR`.
- `TRAP` with n outside 0..119.
- A `SYS` with no valid 16-byte block is an `EFAULT` result, not an illegal
  instruction.

`ROL`/`ROR` are not part of v1. `WAIT` is implemented in v1.

## 17. Encoding Examples

```text
ADD R0, R1       0x0040        R-type: 0x0, Rd=0, Rs=1, func=0
ADD R0, #1       0x1001        I-type: 0x1, Rd=0, func=0, imm6=1
MOV R0, #0x12    0x9012        M8: sign=0
MOV R0, #0x1234  0xF000        X-type: ext_op=0; word2=0x1234
LD R0, [R1+4]    0x2044        M-type: 0x2, Rd=0, Rs=1, disp=4
ST [R1+4], R0    0x3044        M-type: 0x3, Rd=0, Rs=1, disp=4
BEQ label        0x7000|disp   B-type: cond=0
JMP label        0x8000        C-type subop=0; word2=rel16
CALL R1          0x8320        C-type subop=3, regA=1
RET              0x8400        C-type subop=4
SYS #0       0x8F00        C-type subop=15, n=0
```

The assembler chooses the smallest legal form, subject to relocation range.
