; Minimal stateless reference BIOS for KR16.
.org 0xF100
  JMP #0x0100

.org 0xFFFE
  .word 0xF100
