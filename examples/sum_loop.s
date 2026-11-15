; Sum 1..10 in a hand-written loop. The VM exits with R0 & 0xFF = 55.
.text
.global _start
_start:
    MOV R0, #0
    MOV R1, #1
loop:
    CMP R1, #10
    BGT done
    ADD R0, R1
    ADD R1, #1
    BRA loop
done:
    HLT
