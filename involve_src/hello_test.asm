.text
.entry start

start:
    LUI  R10, msg
    MOVI R11, msg
    ADD  R2, R10, R11     ; buf = msg
    MOVI R3, 5            ; "Hello"
    MOVI R1, 1
    SYSCALL 8

    ; print newline from nl
    LUI  R10, nl
    MOVI R11, nl
    ADD  R2, R10, R11
    MOVI R3, 1
    MOVI R1, 1
    SYSCALL 8

    MOVI R1, 0
    SYSCALL 0
    HALT

.data
msg:
    .ascii "Hello"
nl:
    .word 10
.stack 4096
