.text
.entry main

; pipe_reader.asm — 从 stdin 读取并写到 stdout（配合 Shell 管道）

main:
    LUI  R2, buf
    MOVI R11, buf
    ADD  R2, R2, R11
    MOVI R3, 32
    MOVI R1, 0
    SYSCALL 7

    MOVI R1, 1
    SYSCALL 8

    MOVI R1, 0
    SYSCALL 0
    HALT

.data
buf:
    .space 32
.stack 4096
