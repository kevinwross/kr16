.text
.extern put
.global _start
_start:
  CALL put
  MOV R0, #0
  HLT
