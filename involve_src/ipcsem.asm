.text
.entry main

print_msg:
    MOVI R1, 1
    SYSCALL 8
    RET

main:
    MOVI R1, 42
    MOVI R2, 0
    SYSCALL 24
    MOV  R12, R0

    SYSCALL 1
    MOV  R4, R0
    MOVI R5, 0
    CMP  R4, R5
    JZ   child

parent:
    MOVI R2, 0xFFF         ; semop P (-1)
    MOV  R1, R12
    SYSCALL 25

    LUI  R2, pmsg
    MOVI R11, pmsg
    ADD  R2, R2, R11
    MOVI R3, 16
    CALL print_msg

    MOVI R1, 0
    SYSCALL 0
    HALT

child:
    LUI  R2, cmsg
    MOVI R11, cmsg
    ADD  R2, R2, R11
    MOVI R3, 15
    CALL print_msg

    MOVI R2, 1
    MOV  R1, R12
    SYSCALL 25              ; semop V (+1)

    MOVI R1, 0
    SYSCALL 0
    HALT

.data
pmsg:
    .asciz "parent: go\n"
cmsg:
    .asciz "child: post\n"
.stack 4096
