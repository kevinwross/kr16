; Minimal console output through the memory-mapped serial port at 0xF000.
.text
.global _start
_start:
    MOV R1, #'K'
    STB [0xF000], R1
    MOV R1, #'R'
    STB [0xF000], R1
    MOV R1, #'1'
    STB [0xF000], R1
    MOV R1, #'6'
    STB [0xF000], R1
    MOV R1, #10
    STB [0xF000], R1
    MOV R0, #0
    HLT

