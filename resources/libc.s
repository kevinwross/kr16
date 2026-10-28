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

.global exit
exit:
  HLT
