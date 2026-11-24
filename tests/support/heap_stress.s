; Deterministic malloc/free churn over 32 slots, with the invariant checker run
; after every operation.  Prints "OK" on success or "F<code>" plus the checker
; code on the first violation.  The LCG state keeps the run reproducible.
;
; Build:
;   kr16 as tests/support/heap_stress.s -o heap_stress.o
;   kr16 ld libc.o heapcheck.o heap_stress.o -o heap_stress.kr16

.extern __kr16_heap_check
.extern __kr16_heap_used
.extern malloc
.extern free
.extern putchar

.bss
.align 2
hs_state:
  .space 2
hs_iter:
  .space 2
hs_slots:
  .space 64

.text
.global _start
_start:
  MOV R0, #1
  MOV R1, #hs_state
  ST [R1], R0
  MOV R0, #400
  MOV R1, #hs_iter
  ST [R1], R0
  ; clear the slot table
  MOV R2, #hs_slots
  MOV R3, #32
.Lclear:
  MOV R0, #0
  ST [R2], R0
  ADD R2, #2
  DEC R3
  BNE .Lclear
.Lloop:
  ; state = state * 25173 + 13849
  MOV R0, #hs_state
  LD R1, [R0]
  MOV R2, #25173
  MUL R1, R2
  MOV R2, #13849
  ADD R1, R2
  ST [R0], R1
  ; slot = &hs_slots[(state & 31) * 2]
  MOV R2, R1
  AND R2, #31
  SHL R2, #1
  MOV R3, #hs_slots
  ADD R2, R3
  MOV R4, R2
  ; half the time free, half the time allocate
  MOV R3, R1
  AND R3, #64
  BEQ .Lalloc
  LD R1, [R4]
  CMP R1, #0
  BEQ .Lcheck
  CALL free
  MOV R0, #0
  ST [R4], R0
  BRA .Lcheck
.Lalloc:
  LD R1, [R4]
  CMP R1, #0
  BNE .Lcheck
  LD R1, [R0]
  AND R1, #62
  ADD R1, #2
  CALL malloc
  ST [R4], R0
.Lcheck:
  CALL __kr16_heap_check
  CMP R0, #0
  BEQ .Lnext
  MOV R1, #70
  CALL putchar
  ADD R0, #48
  MOV R1, R0
  CALL putchar
  MOV R1, #10
  CALL putchar
  MOV R0, #1
  HLT
.Lnext:
  MOV R0, #hs_iter
  LD R1, [R0]
  DEC R1
  ST [R0], R1
  CMP R1, #0
  BNE .Lloop
  ; release whatever is still allocated
  MOV R2, #hs_slots
  MOV R3, #32
.Lsweep:
  LD R1, [R2]
  CMP R1, #0
  BEQ .Lskip
  PUSH R2
  PUSH R3
  CALL free
  POP R3
  POP R2
.Lskip:
  MOV R0, #0
  ST [R2], R0
  ADD R2, #2
  DEC R3
  BNE .Lsweep
  CALL __kr16_heap_used
  CMP R0, #0
  BNE .Lleak
  MOV R1, #79
  CALL putchar
  MOV R1, #75
  CALL putchar
  MOV R1, #10
  CALL putchar
  MOV R0, #0
  HLT
.Lleak:
  MOV R1, #76
  CALL putchar
  MOV R1, #75
  CALL putchar
  MOV R1, #10
  CALL putchar
  MOV R0, #1
  HLT
