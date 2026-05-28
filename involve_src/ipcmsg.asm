.text
.entry main

main:
    MOVI R1, 77
    SYSCALL 26
    MOV  R12, R0

    SYSCALL 1
    MOV  R4, R0
    MOVI R5, 0
    CMP  R4, R5
    JZ   child

parent:
    LUI  R2, buf
    MOVI R11, buf
    ADD  R2, R2, R11
    MOVI R3, 64
    MOVI R1, 0
    SYSCALL 28

    MOVI R1, 1
    SYSCALL 8

    MOVI R1, 0
    SYSCALL 0
    HALT

child:
    LUI  R2, buf
    MOVI R11, buf
    ADD  R2, R2, R11
    MOVI R3, 1
    ST   R3, R2, 0          ; type = 1
    MOVI R3, 'h'
    ST   R3, R2, 4
    MOVI R3, 'i'
    ST   R3, R2, 8
    MOVI R3, 10
    ST   R3, R2, 12

    MOV  R1, R12
    MOVI R3, 16
    SYSCALL 27

    MOVI R1, 0
    SYSCALL 0
    HALT

.data
buf:
    .space 64
.stack 4096
