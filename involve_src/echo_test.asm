; Simple echo: read chars and print their ASCII codes until Enter
.text
.entry main

main:
    MOVI R9, 0xFFF

loop:
    ; Prompt
    LUI R0, prompt
    MOVI R1, prompt
    AND R1, R1, R9
    ADD R0, R0, R1
    CALL pr

    ; Read one char
    MOVI R1, 0
    MOV R2, R15        ; use stack as buffer
    MOVI R3, 1
    SYSCALL 7          ; R0 = bytes read

    ; Print "got: " and the count
    LUI R0, got
    MOVI R1, got
    AND R1, R1, R9
    ADD R0, R0, R1
    CALL pr
    CALL pi             ; print R0 (bytes read)
    CALL psp

    ; Print "char=" and the character value
    LUI R0, ch
    MOVI R1, ch
    AND R1, R1, R9
    ADD R0, R0, R1
    CALL pr
    LD R1, [R15+0]
    MOVI R2, 0xFF
    AND R1, R1, R2      ; R1 = char value
    CALL pi
    CALL pnl

    ; If char is 'q', exit
    MOVI R2, 'q'
    CMP R1, R2
    JZ done
    MOVI R2, 3           ; Ctrl-C
    CMP R1, R2
    JZ done

    JMP loop
done:
    SYSCALL 0
    HALT

pr:
    MOV R3, R0
    MOVI R2, 0
prc:
    ADD R1, R3, R2
    LD R1, [R1+0]
    MOVI R4, 0xFF
    AND R1, R1, R4
    MOVI R4, 0
    CMP R1, R4
    JZ prw
    MOVI R4, 1
    ADD R2, R2, R4
    JMP prc
prw:
    MOVI R4, 0
    CMP R2, R4
    JZ prr
    MOV R0, R3
    MOV R3, R2
    MOV R2, R0
    MOVI R1, 1
    SYSCALL 8
prr:
    RET

pi:
    MOV R5, R1
    MOVI R6, 0
    MOVI R0, 0
    CMP R5, R0
    JNZ pin
    MOVI R0, 48
    PUSH R0
    MOVI R6, 1
    JMP pio
pin:
    MOVI R4, 10
pid:
    MOVI R0, 0
    CMP R5, R0
    JZ pio
    DIV R2, R5, R4
    MUL R3, R2, R4
    SUB R3, R5, R3
    MOV R5, R2
    MOVI R0, 48
    ADD R3, R3, R0
    PUSH R3
    MOVI R0, 1
    ADD R6, R6, R0
    JMP pid
pio:
    MOVI R0, 0
pip:
    CMP R6, R0
    JZ pir
    POP R2
    PUSH R2
    MOVI R1, 1
    MOV R2, R15
    MOVI R3, 1
    SYSCALL 8
    POP R2
    MOVI R2, 1
    SUB R6, R6, R2
    JMP pio
pir:
    RET

psp:
    MOVI R0, 32
    PUSH R0
    MOVI R1, 1
    MOV R2, R15
    MOVI R3, 1
    SYSCALL 8
    POP R0
    RET

pnl:
    MOVI R0, 10
    PUSH R0
    MOVI R1, 1
    MOV R2, R15
    MOVI R3, 1
    SYSCALL 8
    POP R0
    RET

.data
prompt: .asciz "> "
got:    .asciz "got="
ch:     .asciz " char="

.stack 4096
