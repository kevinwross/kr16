; Small freestanding C runtime used by the taught subset.
.text
.global putchar
putchar:
  MOV R0, R1
  MOV R2, #0xF000
  STB [R2], R0
  MOV R0, #0
  RET

.global puts
puts:
  MOV R2, R1
.Lputs_loop:
  LDB R0, [R2]
  CMP R0, #0
  BEQ .Lputs_done
  MOV R3, #0xF000
  STB [R3], R0
  INC R2
  BRA .Lputs_loop
.Lputs_done:
  MOV R0, #10
  MOV R3, #0xF000
  STB [R3], R0
  MOV R0, #0
  RET

.global getchar
getchar:
  LD R0, [stdin_index]
  LD R1, [stdin_count]
  CMP R0, R1
  BLT .Lgetchar_pop
  LDB R1, [0xF005]
  AND R1, #1
  BEQ .Lgetchar_empty
  MOV R2, #0
  MOV R3, #stdin_buffer
.Lgetchar_fill:
  LDB R0, [0xF004]
  STB [R3], R0
  INC R3
  INC R2
  CMP R2, #64
  BEQ .Lgetchar_filled
  LDB R1, [0xF005]
  AND R1, #1
  BNE .Lgetchar_fill
.Lgetchar_filled:
  ST [stdin_count], R2
  MOV R0, #0
  ST [stdin_index], R0
.Lgetchar_pop:
  MOV R2, #stdin_buffer
  ADD R2, R0
  LDB R2, [R2]
  INC R0
  ST [stdin_index], R0
  MOV R0, R2
  RET
.Lgetchar_empty:
  MOV R0, #0
  RET

.bss
.align 2
stdin_buffer:
  .space 64
stdin_index:
  .space 2
stdin_count:
  .space 2
.text

  LDB R0, [0xF004]
  RET

.global abs
abs:
  MOV R0, R1
  CMP R0, #0
  BGE .Labs_done
  NEG R0
.Labs_done:
  RET

.global atoi
atoi:
  PUSH R4
  MOV R4, #10
  MOV R0, #0
.Latoi_loop:
  LDB R3, [R1]
  CMP R3, #'0'
  BLT .Latoi_done
  CMP R3, #'9'
  BGT .Latoi_done
  SUB R3, #'0'
  MUL R0, R4
  ADD R0, R3
  INC R1
  BRA .Latoi_loop
.Latoi_done:
  POP R4
  RET

.global strcat
strcat:
  MOV R0, R1
.Lstrcat_find:
  LDB R3, [R1]
  CMP R3, #0
  BEQ .Lstrcat_copy
  INC R1
  BRA .Lstrcat_find
.Lstrcat_copy:
  LDB R3, [R2]
  STB [R1], R3
  CMP R3, #0
  BEQ .Lstrcat_done
  INC R1
  INC R2
  BRA .Lstrcat_copy
.Lstrcat_done:
  RET

.global strlen
strlen:
  MOV R0, #0
.Lstrlen_loop:
  LDB R2, [R1]
  CMP R2, #0
  BEQ .Lstrlen_done
  INC R0
  INC R1
  BRA .Lstrlen_loop
.Lstrlen_done:
  RET

.global strcpy
strcpy:
  MOV R0, R1
.Lstrcpy_loop:
  LDB R3, [R2]
  STB [R1], R3
  CMP R3, #0
  BEQ .Lstrcpy_done
  INC R1
  INC R2
  BRA .Lstrcpy_loop
.Lstrcpy_done:
  RET

.global strcmp
strcmp:
.Lstrcmp_loop:
  LDB R0, [R1]
  LDB R3, [R2]
  CMP R0, R3
  BNE .Lstrcmp_diff
  CMP R0, #0
  BEQ .Lstrcmp_equal
  INC R1
  INC R2
  BRA .Lstrcmp_loop
.Lstrcmp_diff:
  SUB R0, R3
  RET
.Lstrcmp_equal:
  MOV R0, #0
  RET

.global memset
memset:
  MOV R0, R1
.Lmemset_loop:
  CMP R3, #0
  BEQ .Lmemset_done
  STB [R1], R2
  INC R1
  DEC R3
  BRA .Lmemset_loop
.Lmemset_done:
  RET

.global memcpy
memcpy:
  PUSH R4
  MOV R4, R1
.Lmemcpy_loop:
  CMP R3, #0
  BEQ .Lmemcpy_done
  LDB R0, [R2]
  STB [R1], R0
  INC R1
  INC R2
  DEC R3
  BRA .Lmemcpy_loop
.Lmemcpy_done:
  MOV R0, R4
  POP R4
  RET

; ---------------------------------------------------------------------------
; Heap allocator
;
; Block layout (sizes in bytes, always even, everything 2-byte aligned):
;
;   allocated              free
;   +----------------+     +----------------+
;   | size | 1       |     | size           |
;   +----------------+     +----------------+
;   |    payload     |     | next free addr |
;   |                |     +----------------+
;   |                |     | unused ...     |
;   +----------------+     +----------------+
;
; Bit 0 of a header word is the used flag; block sizes are even, so the low
; bit is otherwise unused.  "size" includes the 2-byte header, so a block
; holds size-2 payload bytes and the smallest block is 4 bytes.
;
; The free list is singly linked and sorted by address, with the link word
; stored in the block's own payload.  free() coalesces with the neighbours it
; finds while inserting, and hands the topmost block back to the untouched
; pool so the footprint tracks live data instead of the historical peak.
;
; Invariants (verified by tests/support/heapcheck.s):
;   1. blocks tile [heap_start, top) with no gaps and no overlaps
;   2. every header is even-addressed, size >= 4, even, addr+size <= top
;   3. the free list is address-ordered and holds every free block once
;   4. no two free blocks are adjacent
;   5. top - heap_start == sum of all block sizes
; ---------------------------------------------------------------------------

.extern __heap_start
.extern __stack_limit

.data
.align 2
.global __kr16_heap_start
__kr16_heap_start:
  .word __heap_start
__kr16_heap_end:
  .word __stack_limit
.global __kr16_heap_top
__kr16_heap_top:
  .word __heap_start

; The heap occupies the region between the loaded image and the stack reserve.
; The linker resolves both bounds, so no fixed pool is reserved in .bss.
.bss
.align 2
.global __kr16_free_list
__kr16_free_list:
  .space 2
__kr16_used_bytes:
  .space 2
__kr16_peak_bytes:
  .space 2

.text

; ---------------------------------------------------------------------------
; malloc(size) -> void *
; ---------------------------------------------------------------------------
.global malloc
malloc:
  CMP R1, #0
  BLE .Lmalloc_null
  PUSH R4
  MOV R4, SP
  SUB SP, #8
  ; need = header + payload, rounded up to an even size
  MOV R0, R1
  ADD R0, #3
  AND R0, #0xFFFE
  ST [R4-2], R0
  ; pred = &__kr16_free_list, cur = *pred
  MOV R2, #__kr16_free_list
  ST [R4-4], R2
  LD R2, [R2]
  ST [R4-6], R2
.Lmalloc_scan:
  LD R2, [R4-6]
  CMP R2, #0
  BEQ .Lmalloc_carve
  LD R0, [R2]
  AND R0, #0xFFFE
  LD R1, [R4-2]
  CMP R0, R1
  BHS .Lmalloc_found
  MOV R3, R2
  ADD R3, #2
  ST [R4-4], R3
  LD R2, [R3]
  ST [R4-6], R2
  BRA .Lmalloc_scan
.Lmalloc_found:
  ; R2 = block, R0 = block size, R1 = need
  MOV R3, R0
  SUB R3, R1
  CMP R3, #4
  BHS .Lmalloc_split
  ; --- take the whole block: *pred = block->next ---
  LD R0, [R4-4]
  MOV R3, R2
  ADD R3, #2
  LD R1, [R3]
  ST [R0], R1
  LD R0, [R2]
  AND R0, #0xFFFE
  OR R0, #1
  ST [R2], R0
  MOV R1, #__kr16_used_bytes
  LD R0, [R1]
  LD R3, [R2]
  AND R3, #0xFFFE
  ADD R0, R3
  ST [R1], R0
  MOV R0, R2
  ADD R0, #2
  BRA .Lmalloc_done
.Lmalloc_split:
  ; --- split: the block keeps need bytes, the rest becomes free ---
  MOV R3, R2
  ADD R3, R1
  LD R0, [R2]
  AND R0, #0xFFFE
  SUB R0, R1
  ST [R3], R0
  MOV R0, R2
  ADD R0, #2
  LD R0, [R0]
  MOV R1, R3
  ADD R1, #2
  ST [R1], R0
  LD R0, [R4-4]
  ST [R0], R3
  LD R0, [R4-2]
  OR R0, #1
  ST [R2], R0
  MOV R1, #__kr16_used_bytes
  LD R0, [R1]
  LD R3, [R4-2]
  ADD R0, R3
  ST [R1], R0
  MOV R0, R2
  ADD R0, #2
  BRA .Lmalloc_done
.Lmalloc_carve:
  MOV R2, #__kr16_heap_top
  LD R1, [R2]
  MOV R3, #__kr16_heap_end
  LD R3, [R3]
  CMP R1, R3
  BHS .Lmalloc_fail
  MOV R0, R3
  SUB R0, R1
  LD R3, [R4-2]
  CMP R0, R3
  BLO .Lmalloc_fail
  ; header at top, top += need
  MOV R0, R3
  OR R0, #1
  ST [R1], R0
  ADD R1, R3
  ST [R2], R1
  ; peak = max(peak, top - heap_start)
  MOV R0, #__kr16_heap_start
  LD R2, [R0]
  MOV R0, R1
  SUB R0, R2
  MOV R2, #__kr16_peak_bytes
  LD R3, [R2]
  CMP R0, R3
  BLS .Lmalloc_peak_done
  ST [R2], R0
.Lmalloc_peak_done:
  MOV R0, #__kr16_used_bytes
  LD R2, [R0]
  LD R3, [R4-2]
  ADD R2, R3
  ST [R0], R2
  MOV R0, R1
  SUB R0, R3
  ADD R0, #2
  BRA .Lmalloc_done
.Lmalloc_fail:
  MOV R0, #0
.Lmalloc_done:
  MOV SP, R4
  POP R4
  RET
.Lmalloc_null:
  MOV R0, #0
  RET

; ---------------------------------------------------------------------------
; free(void *)
; ---------------------------------------------------------------------------
.global free
free:
  CMP R1, #0
  BEQ .Lfree_null
  PUSH R4
  MOV R4, SP
  SUB SP, #2
  MOV R2, R1
  SUB R2, #2
  ST [R4-2], R2
  LD R0, [R2]
  TEST R0, #1
  BEQ .Lfree_done
  AND R0, #0xFFFE
  ; used -= size
  MOV R1, #__kr16_used_bytes
  LD R3, [R1]
  SUB R3, R0
  ST [R1], R3
  LD R1, [R4-2]
  CALL __kr16_block_free
.Lfree_done:
  MOV SP, R4
  POP R4
.Lfree_null:
  MOV R0, #0
  RET

; ---------------------------------------------------------------------------
; __kr16_block_free(R1 = block address, free header already written)
; Coalesces with both neighbours, inserts into the address-ordered list and
; releases the block to the virgin region when it is topmost.
; Clobbers R0-R3; preserves R4 and SP.
; ---------------------------------------------------------------------------
__kr16_block_free:
  PUSH R4
  MOV R4, SP
  SUB SP, #12
  ST [R4-2], R1
  LD R0, [R1]
  AND R0, #0xFFFE
  ST [R4-4], R0
  ; slot is the word that points at cur; ppred the one that points at the
  ; block before cur.  Both settle on the head until the scan advances.
  MOV R2, #__kr16_free_list
  ST [R4-6], R2
  ST [R4-10], R2
  LD R2, [R2]
  ST [R4-8], R2
.Lbf_scan:
  LD R2, [R4-8]
  CMP R2, #0
  BEQ .Lbf_insert
  LD R3, [R4-2]
  CMP R2, R3
  BHS .Lbf_insert
  LD R0, [R4-6]
  ST [R4-10], R0         ; ppred = slot
  MOV R3, R2
  ADD R3, #2
  ST [R4-6], R3
  LD R2, [R3]
  ST [R4-8], R2
  BRA .Lbf_scan
.Lbf_insert:
  ; coalesce with the following block
  LD R2, [R4-8]
  CMP R2, #0
  BEQ .Lbf_prev
  LD R0, [R4-2]
  LD R1, [R4-4]
  ADD R0, R1
  CMP R0, R2
  BNE .Lbf_prev
  LD R0, [R2]
  AND R0, #0xFFFE
  LD R1, [R4-4]
  ADD R1, R0
  ST [R4-4], R1
  MOV R3, R2
  ADD R3, #2
  LD R0, [R3]
  ST [R4-8], R0
.Lbf_prev:
  ; coalesce with the preceding block
  LD R2, [R4-6]
  MOV R0, #__kr16_free_list
  CMP R2, R0
  BEQ .Lbf_noprev
  MOV R0, R2
  SUB R0, #2
  LD R1, [R0]
  AND R1, #0xFFFE
  ADD R1, R0
  LD R3, [R4-2]
  CMP R1, R3
  BNE .Lbf_noprev
  ; the new block extends the preceding one
  LD R1, [R0]
  AND R1, #0xFFFE
  LD R3, [R4-4]
  ADD R1, R3
  ST [R0], R1
  MOV R1, R0
  ADD R1, #2
  LD R3, [R4-8]
  ST [R1], R3
  ST [R4-2], R0
  LD R1, [R0]
  AND R1, #0xFFFE
  ST [R4-4], R1
  ; the merged block now sits where the predecessor did
  LD R1, [R4-10]
  ST [R4-6], R1
  BRA .Lbf_top
.Lbf_noprev:
  ; write the header and link the block in at pred
  LD R1, [R4-2]
  LD R2, [R4-4]
  ST [R1], R2
  ADD R1, #2
  LD R2, [R4-8]
  ST [R1], R2
  LD R1, [R4-6]
  LD R2, [R4-2]
  ST [R1], R2
.Lbf_top:
  ; hand the block back to the untouched pool when it is topmost
  LD R2, [R4-2]
  LD R3, [R4-4]
  ADD R2, R3
  MOV R3, #__kr16_heap_top
  LD R0, [R3]
  CMP R2, R0
  BNE .Lbf_done
  LD R2, [R4-2]
  ST [R3], R2
  LD R1, [R4-6]
  LD R2, [R4-8]
  ST [R1], R2
.Lbf_done:
  MOV SP, R4
  POP R4
  RET

; ---------------------------------------------------------------------------
; __kr16_unlink(R1 = block address known to be in the free list)
; ---------------------------------------------------------------------------
__kr16_unlink:
  MOV R2, #__kr16_free_list
  MOV R3, R2
.Lun_scan:
  LD R0, [R3]
  CMP R0, #0
  BEQ .Lun_done
  CMP R0, R1
  BEQ .Lun_found
  MOV R3, R0
  ADD R3, #2
  BRA .Lun_scan
.Lun_found:
  MOV R2, R0
  ADD R2, #2
  LD R0, [R2]
  ST [R3], R0
.Lun_done:
  RET

; ---------------------------------------------------------------------------
; calloc(count, size) -> void *
; ---------------------------------------------------------------------------
.global calloc
calloc:
  CMP R1, #0
  BEQ .Lcalloc_null
  CMP R2, #0
  BEQ .Lcalloc_null
  PUSH R4
  MOV R4, SP
  SUB SP, #2
  ; 16x16 -> 32 bit product in R2:R3
  MOV R3, R2
  MULU R2, R3
  CMP R3, #0
  BNE .Lcalloc_fail
  MOV R0, #32767
  CMP R2, R0
  BHI .Lcalloc_fail
  ST [R4-2], R2
  MOV R1, R2
  CALL malloc
  CMP R0, #0
  BEQ .Lcalloc_done
  MOV R1, R0
  MOV R2, #0
  LD R3, [R4-2]
  CALL memset
.Lcalloc_done:
  MOV SP, R4
  POP R4
  RET
.Lcalloc_fail:
  MOV R0, #0
  MOV SP, R4
  POP R4
  RET
.Lcalloc_null:
  MOV R0, #0
  RET

; ---------------------------------------------------------------------------
; realloc(ptr, size) -> void *
; ---------------------------------------------------------------------------
.global realloc
realloc:
  CMP R1, #0
  BNE .Lrealloc_size
  MOV R1, R2
  CALL malloc
  RET
.Lrealloc_size:
  CMP R2, #0
  BGT .Lrealloc_begin
  CALL free
  MOV R0, #0
  RET
.Lrealloc_begin:
  PUSH R4
  MOV R4, SP
  SUB SP, #16
  ST [R4-2], R1          ; p
  ST [R4-16], R2         ; requested size
  MOV R0, R1
  SUB R0, #2
  ST [R4-4], R0          ; block address
  LD R1, [R0]
  TEST R1, #1
  BNE .Lrealloc_valid
  JMP .Lrealloc_null
.Lrealloc_valid:
  AND R1, #0xFFFE
  ST [R4-6], R1          ; old size
  MOV R3, R2
  ADD R3, #3
  AND R3, #0xFFFE
  ST [R4-8], R3          ; need
  CMP R3, R1
  BHI .Lrealloc_grow
  ; ---- shrink in place ----
  LD R1, [R4-6]
  LD R2, [R4-8]
  MOV R3, R1
  SUB R3, R2             ; remainder
  CMP R3, #4
  BHS .Lrealloc_shrink
  JMP .Lrealloc_return
.Lrealloc_shrink:
  LD R1, [R4-4]
  MOV R0, R1
  ADD R0, R2             ; rest = block + need
  ST [R0], R3            ; rest.size = remainder
  MOV R3, R2
  OR R3, #1
  ST [R1], R3            ; block.size = need | 1
  MOV R3, #__kr16_used_bytes
  LD R1, [R3]
  LD R2, [R4-6]
  LD R0, [R4-8]
  SUB R2, R0
  SUB R1, R2
  ST [R3], R1
  LD R1, [R4-4]
  LD R2, [R4-8]
  ADD R1, R2
  CALL __kr16_block_free
  JMP .Lrealloc_return
  ; ---- grow in place ----
.Lrealloc_grow:
  LD R1, [R4-4]
  LD R2, [R4-6]
  MOV R3, R1
  ADD R3, R2             ; next
  ST [R4-10], R3
  MOV R0, #__kr16_heap_top
  LD R0, [R0]
  CMP R3, R0
  BLO .Lrealloc_neighbour
  JMP .Lrealloc_top
.Lrealloc_neighbour:
  LD R0, [R3]
  TEST R0, #1
  BNE .Lrealloc_to_move
  AND R0, #0xFFFE
  ST [R4-12], R0         ; neighbour size
  LD R1, [R4-6]
  ADD R0, R1             ; combined
  LD R1, [R4-8]
  CMP R0, R1
  BHS .Lrealloc_absorb
  JMP .Lrealloc_move
.Lrealloc_absorb:
  LD R1, [R4-10]
  CALL __kr16_unlink
  MOV R0, #__kr16_used_bytes
  LD R1, [R0]
  LD R2, [R4-8]
  LD R3, [R4-6]
  SUB R2, R3
  ADD R1, R2
  ST [R0], R1
  LD R2, [R4-6]
  LD R3, [R4-12]
  ADD R2, R3             ; combined
  LD R1, [R4-8]          ; need
  MOV R0, R2
  SUB R0, R1             ; remainder
  CMP R0, #4
  BHS .Lrealloc_absorb_split
  LD R3, [R4-4]
  MOV R0, R2
  OR R0, #1
  ST [R3], R0
  JMP .Lrealloc_return
.Lrealloc_absorb_split:
  LD R3, [R4-4]
  MOV R2, R3
  ADD R2, R1             ; rest
  ST [R2], R0
  MOV R0, R1
  OR R0, #1
  ST [R3], R0
  MOV R1, R2
  CALL __kr16_block_free
  JMP .Lrealloc_return
.Lrealloc_to_move:
  JMP .Lrealloc_move
  ; ---- extend the topmost block into virgin space ----
.Lrealloc_top:
  MOV R0, #__kr16_heap_top
  LD R0, [R0]
  LD R1, [R4-10]
  CMP R1, R0
  BEQ .Lrealloc_extend
  JMP .Lrealloc_move
.Lrealloc_extend:
  LD R1, [R4-8]
  LD R2, [R4-6]
  SUB R1, R2             ; delta
  MOV R0, #__kr16_heap_end
  LD R0, [R0]
  MOV R2, #__kr16_heap_top
  LD R2, [R2]
  MOV R3, R0
  SUB R3, R2             ; available
  CMP R3, R1
  BHS .Lrealloc_do_extend
  JMP .Lrealloc_move
.Lrealloc_do_extend:
  ADD R2, R1
  MOV R0, #__kr16_heap_top
  ST [R0], R2
  LD R1, [R4-4]
  LD R3, [R4-8]
  OR R3, #1
  ST [R1], R3
  LD R3, [R4-8]
  LD R2, [R4-6]
  SUB R3, R2             ; delta
  MOV R1, #__kr16_used_bytes
  LD R2, [R1]
  ADD R2, R3
  ST [R1], R2
  MOV R0, #__kr16_heap_top
  LD R2, [R0]
  MOV R0, #__kr16_heap_start
  LD R1, [R0]
  SUB R2, R1             ; top - heap_start
  MOV R0, #__kr16_peak_bytes
  LD R3, [R0]
  CMP R2, R3
  BLS .Lrealloc_return
  ST [R0], R2
.Lrealloc_return:
  LD R0, [R4-2]
  BRA .Lrealloc_done
  ; ---- move: allocate, copy, release ----
.Lrealloc_move:
  LD R1, [R4-16]
  CALL malloc
  CMP R0, #0
  BEQ .Lrealloc_null
  ST [R4-14], R0
  LD R1, [R4-6]
  SUB R1, #2             ; old payload
  LD R2, [R4-16]
  CMP R1, R2
  BLS .Lrealloc_copy
  MOV R1, R2
.Lrealloc_copy:
  MOV R3, R1
  LD R1, [R4-14]
  LD R2, [R4-2]
  CALL memcpy
  LD R1, [R4-2]
  CALL free
  LD R0, [R4-14]
.Lrealloc_done:
  MOV SP, R4
  POP R4
  RET
.Lrealloc_null:
  MOV R0, #0
  BRA .Lrealloc_done
; ---------------------------------------------------------------------------
; __kr16_heap_used() -> bytes currently held by allocated blocks
; __kr16_heap_peak() -> largest top - heap_start ever reached
; ---------------------------------------------------------------------------
.global __kr16_heap_used
__kr16_heap_used:
  MOV R0, #__kr16_used_bytes
  LD R1, [R0]
  MOV R0, R1
  RET

.global __kr16_heap_peak
__kr16_heap_peak:
  MOV R0, #__kr16_peak_bytes
  LD R1, [R0]
  MOV R0, R1
  RET

.global exit
exit:
  MOV R0, R1
  HLT


