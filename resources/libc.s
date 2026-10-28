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
  LDB R0, [0xF004]
  RET

; Minimal printf: %d, %s, %c, and %%.
.global printf
printf:
  PUSH R4
  MOV R4, SP
  SUB SP, #8
  ST [R4-2], R1
  ST [R4-4], R2
  ST [R4-6], R3
  MOV R2, #0
  LD R1, [R4-2]
.Lpf_loop:
  LDB R0, [R1]
  CMP R0, #0
  BEQ .Lpf_done
  CMP R0, #'%'
  BEQ .Lpf_format
  MOV R3, #0xF000
  STB [R3], R0
  INC R1
  BRA .Lpf_loop
.Lpf_format:
  INC R1
  LDB R0, [R1]
  CMP R0, #'d'
  BEQ .Lpf_decimal
  CMP R0, #'s'
  BEQ .Lpf_string
  CMP R0, #'c'
  BEQ .Lpf_character
  CMP R0, #'%'
  BEQ .Lpf_percent
  INC R1
  BRA .Lpf_loop
.Lpf_percent:
  MOV R0, #'%'
  MOV R3, #0xF000
  STB [R3], R0
  INC R1
  BRA .Lpf_loop
.Lpf_character:
  PUSH R1
  PUSH R2
  CALL .Lpf_getarg
  POP R2
  INC R2
  POP R1
  MOV R3, #0xF000
  STB [R3], R0
  INC R1
  BRA .Lpf_loop
.Lpf_decimal:
  PUSH R1
  PUSH R2
  CALL .Lpf_getarg
  CALL .Lpf_print_dec
  POP R2
  INC R2
  POP R1
  INC R1
  BRA .Lpf_loop
.Lpf_string:
  PUSH R1
  PUSH R2
  CALL .Lpf_getarg
  MOV R3, R0
.Lpf_str_loop:
  LDB R0, [R3]
  CMP R0, #0
  BEQ .Lpf_str_done
  MOV R2, #0xF000
  STB [R2], R0
  INC R3
  BRA .Lpf_str_loop
.Lpf_str_done:
  POP R2
  INC R2
  POP R1
  INC R1
  BRA .Lpf_loop
.Lpf_done:
  MOV SP, R4
  POP R4
  MOV R0, #0
  RET

; Fetch the next printf argument into R0, advancing the index in R2.
.Lpf_getarg:
  CMP R2, #0
  BEQ .Lpf_arg0
  CMP R2, #1
  BEQ .Lpf_arg1
  MOV R3, R2
  SUB R3, #2
  SHL R3, #1
  ADD R3, #4
  ADD R3, R4
  LD R0, [R3]
  INC R2
  RET
.Lpf_arg0:
  LD R0, [R4-4]
  INC R2
  RET
.Lpf_arg1:
  LD R0, [R4-6]
  INC R2
  RET

; Print R0 as a signed decimal integer.
.Lpf_print_dec:
  CMP R0, #0
  BGE .Lpd_positive
  MOV R3, R0
  MOV R0, #'-'
  MOV R2, #0xF000
  STB [R2], R0
  MOV R0, R3
  NEG R0
.Lpd_positive:
  CMP R0, #10
  BLT .Lpd_digit
  MOV R1, #10
  MOV R2, R0
  MOD R2, R1
  DIV R0, R1
  PUSH R2
  CALL .Lpf_print_dec
  POP R0
  ADD R0, #'0'
  MOV R3, #0xF000
  STB [R3], R0
  RET
.Lpd_digit:
  ADD R0, #'0'
  MOV R3, #0xF000
  STB [R3], R0
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

.data
.align 2
__kr16_heap_ptr:
  .word __heap_start

.text
.global malloc
malloc:
  CMP R1, #0
  BEQ .Lmalloc_fail
  PUSH R4
  MOV R3, #__kr16_heap_ptr
  LD R2, [R3]
  MOV R0, R2
  ADD R2, R1
  ST [R3], R2
  POP R4
  RET
.Lmalloc_fail:
  MOV R0, #0
  RET

.global calloc
calloc:
  PUSH R4
  MUL R1, R2
  PUSH R1
  CALL malloc
  POP R2
  MOV R4, R0
  MOV R1, R4
.Lcalloc_zero:
  CMP R2, #0
  BEQ .Lcalloc_done
  MOV R3, #0
  STB [R1], R3
  INC R1
  DEC R2
  BRA .Lcalloc_zero
.Lcalloc_done:
  MOV R0, R4
  POP R4
  RET

.global free
free:
  MOV R0, #0
  RET

.global exit
exit:
  HLT
