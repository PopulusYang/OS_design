.text
.entry main

print_ok:
    LUI  R2, ok
    MOVI R11, ok
    ADD  R2, R2, R11
    MOVI R3, 12
    MOVI R1, 1
    SYSCALL 8
    RET

main:
    SYSCALL 1
    MOV  R4, R0
    MOVI R5, 0
    CMP  R4, R5
    JZ   child

parent:
    MOVI R5, 50
pdelay:
    MOVI R6, 1
    SUB  R5, R5, R6
    CMP  R5, R6
    JNZ  pdelay

    MOVI R2, 10
    MOV  R1, R4
    SYSCALL 23              ; kill(child, SIGUSR1)

    MOVI R1, 0
    SYSCALL 0
    HALT

child:
cloop:
    SYSCALL 33              ; getsig
    MOVI R5, 0
    CMP  R0, R5
    JZ   cloop
    CALL print_ok
    MOVI R1, 0
    SYSCALL 0
    HALT

.data
ok:
    .asciz "got SIGUSR1\n"
.stack 4096
