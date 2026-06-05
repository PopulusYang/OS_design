;runtime.s
;C 编译器运行时库：系统调用包装、比较、移位、打印和内存操作。
;编译生成的 .s 会附带本文件，供生成的 C 代码通过 CALL 调用。

.text

; 系统调用包装，参数在 R1 到 R3，返回值在 R0

_sys_exit:
    SYSCALL 0
    RET

_sys_fork:
    SYSCALL 1
    RET

_sys_exec:
    SYSCALL 2
    RET

_sys_wait:
    SYSCALL 3
    RET

_sys_getpid:
    SYSCALL 4
    RET

_sys_open:
    SYSCALL 5
    RET

_sys_close:
    SYSCALL 6
    RET

_sys_read:
    SYSCALL 7
    RET

_sys_write:
    SYSCALL 8
    RET

_sys_seek:
    SYSCALL 9
    RET

_sys_getcwd:
    SYSCALL 10
    RET

_sys_chdir:
    SYSCALL 11
    RET

_sys_sbrk:
    SYSCALL 12
    RET

_sys_getenv:
    SYSCALL 13
    RET

_sys_setenv:
    SYSCALL 14
    RET

_sys_unsetenv:
    SYSCALL 15
    RET

_sys_stat:
    SYSCALL 16
    RET

_sys_create:
    SYSCALL 17
    RET

_sys_delete:
    SYSCALL 18
    RET

_sys_mkdir:
    SYSCALL 19
    RET

_sys_pipe:
    SYSCALL 22
    RET

_sys_kill:
    SYSCALL 23
    RET

_sys_semget:
    SYSCALL 24
    RET

_sys_semop:
    SYSCALL 25
    RET

_sys_msgget:
    SYSCALL 26
    RET

_sys_msgsnd:
    SYSCALL 27
    RET

_sys_msgrcv:
    SYSCALL 28
    RET

_sys_shmget:
    SYSCALL 29
    RET

_sys_shmat:
    SYSCALL 30
    RET

_sys_shmdt:
    SYSCALL 31
    RET

_sys_mkfifo:
    SYSCALL 32
    RET

_sys_getsig:
    SYSCALL 33
    RET

; 有符号比较，R1=a R2=b，R0 返回 0 或 1。VM 只有 ZF，用高位判断符号

__rt_lt:
    SUB R0, R1, R2
    MOVI R3, 256
    MOVI R4, 256
    MUL R3, R3, R4
    MOVI R4, 256
    MUL R3, R3, R4
    MOVI R4, 128
    MUL R3, R3, R4
    MOV R4, R0
    DIV R4, R4, R3
    MOVI R3, 128
    MOVI R5, 256
    MUL R5, R3, R5
    DIV R0, R4, R5
    MOVI R3, 0
    CMP R0, R3
    JZ .rt_lt_ret
    RET
.rt_lt_ret:
    MOVI R0, 0
    RET

__rt_gt:
    MOV R3, R1
    MOV R1, R2
    MOV R2, R3
    JMP __rt_lt

__rt_le:
    MOV R3, R1
    MOV R1, R2
    MOV R2, R3
    CALL __rt_lt
    MOVI R3, 0
    CMP R0, R3
    JZ .rt_le_one
    MOVI R0, 0
    RET
.rt_le_one:
    MOVI R0, 1
    RET

__rt_ge:
    CALL __rt_lt
    MOVI R3, 0
    CMP R0, R3
    JZ .rt_ge_one
    MOVI R0, 0
    RET
.rt_ge_one:
    MOVI R0, 1
    RET

; 左移右移，每次乘 2 或除 2

__rt_lshift:
    MOV R0, R1
    MOVI R3, 0
    CMP R2, R3
    JZ .rt_ls_done
    MOVI R3, 2
.rt_ls_loop:
    CMP R2, R3
    JZ .rt_ls_done
    MUL R0, R0, R3
    MOVI R4, 1
    SUB R2, R2, R4
    CMP R2, R3
    JNZ .rt_ls_loop
.rt_ls_done:
    RET

__rt_rshift:
    MOV R0, R1
    MOVI R3, 0
    CMP R2, R3
    JZ .rt_rs_done
    MOVI R3, 2
.rt_rs_loop:
    CMP R2, R3
    JZ .rt_rs_done
    DIV R0, R0, R3
    MOVI R4, 1
    SUB R2, R2, R4
    CMP R2, R3
    JNZ .rt_rs_loop
.rt_rs_done:
    RET

; 把整数按十进制打印到标准输出

__rt_print_int:
    MOVI R5, 10
    MOVI R6, 0
    MOVI R11, 0
    CMP R1, R11
    JNZ .rt_pi_nonzero
    MOVI R7, 48
    PUSH R7
    MOVI R1, 1
    MOV R2, R15
    MOVI R3, 1
    SYSCALL 8
    MOVI R7, 10
    ST R7, R15, 0
    MOV R2, R15
    MOVI R3, 1
    SYSCALL 8
    POP R7
    RET
.rt_pi_nonzero:
    MOV R9, R15
.rt_pi_loop:
    CMP R1, R11
    JZ .rt_pi_print_digits
    DIV R4, R1, R5
    MUL R7, R4, R5
    SUB R7, R1, R7
    MOVI R10, 48
    ADD R7, R7, R10
    PUSH R7
    MOVI R8, 1
    ADD R6, R6, R8
    MOV R1, R4
    JMP .rt_pi_loop
.rt_pi_print_digits:
    MOVI R1, 1
.rt_pi_out_loop:
    CMP R6, R11
    JZ .rt_pi_done
    POP R2
    PUSH R2
    MOV R2, R15
    MOVI R3, 1
    SYSCALL 8
    POP R7
    MOVI R8, 1
    SUB R6, R6, R8
    JMP .rt_pi_out_loop
.rt_pi_done:
    MOVI R7, 10
    PUSH R7
    MOV R2, R15
    MOVI R3, 1
    SYSCALL 8
    POP R7
    MOV R15, R9
    RET

; 按 4 字节步长拷贝和填充

__rt_memcpy:
    MOV R0, R1
    MOVI R4, 0
    CMP R3, R4
    JZ __rt_memcpy_done
__rt_memcpy_loop:
    LD R5, R2, 0
    ST R5, R1, 0
    MOVI R6, 4
    ADD R1, R1, R6
    ADD R2, R2, R6
    ADD R4, R4, R6
    CMP R4, R3
    JNZ __rt_memcpy_loop
__rt_memcpy_done:
    RET

__rt_memset:
    MOV R0, R1
    MOVI R4, 0
    CMP R3, R4
    JZ __rt_memset_done
__rt_memset_loop:
    ST R2, R1, 0
    MOVI R5, 4
    ADD R1, R1, R5
    ADD R4, R4, R5
    CMP R4, R3
    JNZ __rt_memset_loop
__rt_memset_done:
    RET
