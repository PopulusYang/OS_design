.text
.entry main

; pipe_test.asm — fork + pipe IPC 演示
; 子进程向管道写入消息，父进程读取并打印

print_buf:
    MOVI R1, 1
    SYSCALL 8
    RET

main:
    LUI  R10, fds
    MOVI R11, fds
    ADD  R1, R10, R11
    SYSCALL 22             ; pipe(fds)
    MOVI R5, 0
    CMP  R0, R5
    JNZ  fail

    SYSCALL 1              ; fork
    MOV  R4, R0
    CMP  R4, R5
    JZ   child

parent:
    LUI  R10, fds
    MOVI R11, fds
    ADD  R10, R10, R11
    LD   R1, R10, 4        ; close write end
    SYSCALL 6

    LD   R1, R10, 0        ; read from read end
    LUI  R2, buf
    MOVI R11, buf
    ADD  R2, R2, R11
    MOVI R3, 32
    SYSCALL 7

    MOV  R3, R0            ; 实际读取字节数
    MOVI R1, 1
    SYSCALL 8              ; print buffer to stdout

    LUI  R2, ok_msg
    MOVI R11, ok_msg
    ADD  R2, R2, R11
    MOVI R3, 18
    CALL print_buf

    MOVI R1, 0
    SYSCALL 0
    HALT

child:
    LUI  R10, fds
    MOVI R11, fds
    ADD  R10, R10, R11
    LD   R1, R10, 0        ; close read end
    SYSCALL 6

    LD   R1, R10, 4        ; write to write end
    LUI  R2, msg
    MOVI R11, msg
    ADD  R2, R2, R11
    MOVI R3, 15
    SYSCALL 8

    MOVI R1, 0
    SYSCALL 0
    HALT

fail:
    LUI  R2, err_msg
    MOVI R11, err_msg
    ADD  R2, R2, R11
    MOVI R3, 14
    CALL print_buf
    MOVI R1, 1
    SYSCALL 0
    HALT

.data
fds:
    .space 8
buf:
    .space 32
msg:
    .asciz "Hello via pipe\n"
ok_msg:
    .asciz "[Parent] done\n"
err_msg:
    .asciz "pipe() failed\n"
.stack 4096
