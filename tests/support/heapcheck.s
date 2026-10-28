; ---------------------------------------------------------------------------
; __kr16_heap_check() -> 0 when the heap obeys the allocator invariants.
;
; Test-only companion to resources/libc.s; link it explicitly from tests so the
; production runtime stays small.  Returns the code of the first violation:
;
;   1  block address is not 2-byte aligned
;   2  block size is below 4 bytes or odd
;   3  block runs past the top of the heap
;   4  free block is missing from the free list
;   5  free list is out of address order, or names a used block
;   6  two adjacent blocks are both free (coalescing was missed)
;   7  the free list still has entries after the last block
;   8  top is below the heap start
;
; The walk compares the heap against the free list, so it checks coverage and
; ordering at the same time: every free block must be the next list entry.
; ---------------------------------------------------------------------------

.extern __kr16_heap_start
.extern __kr16_heap_top
.extern __kr16_free_list

.text
.global __kr16_heap_check
__kr16_heap_check:
  PUSH R4
  MOV R4, SP
  SUB SP, #6
  MOV R0, #0
  ST [R4-4], R0            ; previous block was free?
  MOV R0, #__kr16_heap_top
  LD R2, [R0]              ; top
  MOV R0, #__kr16_heap_start
  LD R1, [R0]              ; addr
  MOV R0, #__kr16_free_list
  LD R3, [R0]              ; list
  CMP R2, R1
  BHS .Lhc_loop
  MOV R0, #8
  BRA .Lhc_exit
.Lhc_loop:
  CMP R1, R2
  BHS .Lhc_done
  TEST R1, #1
  BEQ .Lhc_aligned
  MOV R0, #1
  BRA .Lhc_exit
.Lhc_aligned:
  LD R0, [R1]
  AND R0, #0xFFFE
  CMP R0, #4
  BHS .Lhc_size_ok
  MOV R0, #2
  BRA .Lhc_exit
.Lhc_size_ok:
  ADD R0, R1               ; end of the block
  CMP R0, R2
  BLS .Lhc_in_bounds
  MOV R0, #3
  BRA .Lhc_exit
.Lhc_in_bounds:
  ST [R4-2], R0            ; remember the next address
  LD R0, [R1]
  TEST R0, #1
  BNE .Lhc_used
  ; ---- free block: must be the next list entry and must not touch a free peer
  CMP R3, #0
  BNE .Lhc_list_nonempty
  MOV R0, #4
  BRA .Lhc_exit
.Lhc_list_nonempty:
  CMP R3, R1
  BEQ .Lhc_list_matches
  MOV R0, #5
  BRA .Lhc_exit
.Lhc_list_matches:
  LD R0, [R4-4]
  CMP R0, #0
  BEQ .Lhc_not_adjacent
  MOV R0, #6
  BRA .Lhc_exit
.Lhc_not_adjacent:
  MOV R0, #1
  ST [R4-4], R0
  MOV R0, R1
  ADD R0, #2
  LD R3, [R0]              ; advance the free list
  BRA .Lhc_next
.Lhc_used:
  MOV R0, #0
  ST [R4-4], R0
.Lhc_next:
  LD R1, [R4-2]
  BRA .Lhc_loop
.Lhc_done:
  CMP R3, #0
  BEQ .Lhc_ok
  MOV R0, #7
  BRA .Lhc_exit
.Lhc_ok:
  MOV R0, #0
.Lhc_exit:
  MOV SP, R4
  POP R4
  RET
