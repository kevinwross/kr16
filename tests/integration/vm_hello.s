.text
start:
  MOV R1, #0xF000
  MOV R0, #72
  STB [R1], R0
  MOV R0, #0
  HLT
