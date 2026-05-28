.text
.entry main

main:
    MOVI R1, 99
    MOVI R2, 4096
    SYSCALL 29
    MOV  R12, R0

    SYSCALL 1
    MOV  R4, R0
    MOVI R5, 0
    CMP  R4, R5
    JZ   child

parent:
    MOV  R1, R12
    LUI  R10, shmaddr
    MOVI R11, shmaddr
    ADD  R10, R10, R11
    LD   R2, R10, 0
    SYSCALL 30

pw:
    LUI  R10, shmaddr
    MOVI R11, shmaddr
    ADD  R10, R10, R11
    LD   R2, R10, 0
    LD   R3, R2, 0
    MOVI R5, 0
    CMP  R3, R5
    JZ   pw

    MOVI R4, 48
    ADD  R3, R3, R4
    ST   R3, R2, 0
    MOVI R1, 1
    SYSCALL 8

    MOVI R1, 0
    SYSCALL 0
    HALT

child:
    MOV  R1, R12
    LUI  R10, shmaddr
    MOVI R11, shmaddr
    ADD  R10, R10, R11
    LD   R2, R10, 0
    SYSCALL 30

    LUI  R10, shmaddr
    MOVI R11, shmaddr
    ADD  R10, R10, R11
    LD   R2, R10, 0
    MOVI R3, 5
    ST   R3, R2, 0

    MOVI R1, 0
    SYSCALL 0
    HALT

.data
shmaddr:
    .word 0x00200000
.stack 4096
