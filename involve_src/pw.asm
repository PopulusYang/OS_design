.text
.entry main

; pipe_writer.asm — 向 stdout（可被 Shell 管道重定向）写入一行

main:
    LUI  R2, msg
    MOVI R11, msg
    ADD  R2, R2, R11
    MOVI R3, 15
    MOVI R1, 1
    SYSCALL 8
    MOVI R1, 0
    SYSCALL 0
    HALT

.data
msg:
    .asciz "Hello via pipe\n"
.stack 4096
