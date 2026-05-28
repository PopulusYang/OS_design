.text
.entry main

main:
    LUI  R1, fpath
    MOVI R11, fpath
    ADD  R1, R1, R11
    SYSCALL 32

    SYSCALL 1
    MOV  R4, R0
    MOVI R5, 0
    CMP  R4, R5
    JZ   child

parent:
    LUI  R1, fpath
    MOVI R11, fpath
    ADD  R1, R1, R11
    MOVI R2, 1
    SYSCALL 5

    MOV  R1, R0
    LUI  R2, buf
    MOVI R11, buf
    ADD  R2, R2, R11
    MOVI R3, 32
    SYSCALL 7

    MOVI R1, 1
    SYSCALL 8

    MOVI R1, 0
    SYSCALL 0
    HALT

child:
    LUI  R1, fpath
    MOVI R11, fpath
    ADD  R1, R1, R11
    MOVI R2, 2
    SYSCALL 5

    MOV  R1, R0
    LUI  R2, msg
    MOVI R11, msg
    ADD  R2, R2, R11
    MOVI R3, 13
    SYSCALL 8

    MOVI R1, 0
    SYSCALL 0
    HALT

.data
fpath:
    .asciz "/tmp/f"
buf:
    .space 32
msg:
    .asciz "FIFO: hello\n"
.stack 4096
