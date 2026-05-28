; runtime.s — C 运行时库
;
; 提供给编译器生成的代码调用的辅助函数。
; 在编译输出后自动附加到 .s 文件中。

.text

; ---------- 系统调用包装函数 ----------
; C 调用约定: 参数在 R1-R3，返回值在 R0
; 这些函数将被编译的 C 代码通过 CALL 指令调用

_sys_exit:
    SYSCALL 0               ; exit(code)
    RET

_sys_fork:
    SYSCALL 1               ; fork() → child_pid
    RET

_sys_exec:
    SYSCALL 2               ; exec(path)
    RET

_sys_wait:
    SYSCALL 3               ; wait(&status) → child_pid
    RET

_sys_getpid:
    SYSCALL 4               ; getpid()
    RET

_sys_open:
    SYSCALL 5               ; open(path, flags) → fd
    RET

_sys_close:
    SYSCALL 6               ; close(fd)
    RET

_sys_read:
    SYSCALL 7               ; read(fd, buf, count) → n
    RET

_sys_write:
    SYSCALL 8               ; write(fd, buf, count) → n
    RET

_sys_seek:
    SYSCALL 9               ; seek(fd, offset, whence)
    RET

_sys_getcwd:
    SYSCALL 10              ; getcwd(buf, size)
    RET

_sys_chdir:
    SYSCALL 11              ; chdir(path)
    RET

_sys_sbrk:
    SYSCALL 12              ; sbrk(increment) → old_brk
    RET

_sys_getenv:
    SYSCALL 13              ; getenv(name, buf, size)
    RET

_sys_setenv:
    SYSCALL 14              ; setenv(name, value)
    RET

_sys_unsetenv:
    SYSCALL 15              ; unsetenv(name)
    RET

_sys_stat:
    SYSCALL 16              ; stat(path, buf)
    RET

_sys_create:
    SYSCALL 17              ; create(path, mode)
    RET

_sys_delete:
    SYSCALL 18              ; delete(path)
    RET

_sys_mkdir:
    SYSCALL 19              ; mkdir(path, mode)
    RET

; ---------- 比较辅助函数 ----------
; VM 只有 ZF 标志位。有符号比较通过无符号除法技巧实现：
;   a < b (signed) 等价于 (uint32_t)(a - b) >= 0x80000000
;   即 (a-b) 作为无符号数 >= 2^31
;   用 DIV 除以 2^31：商为 1 表示负数差，0 表示非负
;
; 调用约定: R1=a, R2=b → R0=结果 (0 或 1)

__rt_lt:
    SUB R0, R1, R2          ; R0 = a - b
    ; 构建 2^31 = 2147483648
    MOVI R3, 256            ; 256
    MOVI R4, 256
    MUL R3, R3, R4          ; 65536
    MOVI R4, 256
    MUL R3, R3, R4          ; 16777216
    MOVI R4, 128
    MUL R3, R3, R4          ; 2147483648 = 2^31
    ; DIV: 如果 R0 (作为有符号) 为负，则 R0/正大数 = 0
    ; 但我们用无符号解释：负数的大无符号值 / 2^31 >= 1
    ; 实际上 VM 的 DIV 是有符号的，所以负数 / 正 = 负数或 0
    ; 换个思路：
    ; 如果 a-b 是负数，那么 (a-b)/2^31 = 0 (有符号)
    ; 如果 a-b 是正数，那么 (a-b)/2^31 = 0 (因为 < 2^31)
    ; 这行不通。用另一种方法：
    ; 检查 a-b 的高位 bit
    ; 用 SUB 后检查：如果 a-b 的最高位为 1，则为负
    ; 方法：R0 右移 31 位（用连续 DIV 2 实现）
    ; 但太慢。用 AND 检查符号位：
    ; LUI R3, 0x800 → 0x800000 (bit 23)
    ; 我们需要 bit 31。分两步：
    ; 先 DIV R0 右移 16 位得到高 16 位，再检查 bit 15
    MOVI R3, 256
    MOVI R4, 256
    MUL R3, R3, R4          ; R3 = 65536 = 2^16
    MOV R4, R0
    DIV R4, R4, R3          ; R4 = (a-b) >> 16 (高 16 位)
    ; 现在检查 R4 的 bit 15 (符号位 of 高16位)
    ; R4 的 bit 15 = 1 意味着原数为负
    ; 方法：R4 >= 32768?
    ; 32768 = 128 * 256
    MOVI R3, 128
    MOVI R5, 256
    MUL R5, R3, R5          ; R5 = 32768
    ; 检查 R4 >= R5:
    ; R4 / R5: 如果 R4 >= R5 → 商 >= 1
    ; 但 R4 最大 65535, R5=32768, 所以商最多 1
    ; R4 / 32768:
    ;   R4 in [0, 32767] → 商 0 → a >= b
    ;   R4 in [32768, 65535] → 商 1 → a < b
    DIV R0, R4, R5          ; R0 = R4 / 32768
    CMP R0, R0              ; 设置 FLAGS
    JZ .rt_lt_ret
    ; R0 != 0, 确认是 1
    RET
.rt_lt_ret:
    MOVI R0, 0
    RET

__rt_gt:
    ; a > b → b < a
    MOV R3, R1
    MOV R1, R2
    MOV R2, R3
    JMP __rt_lt

__rt_le:
    ; a <= b → !(b < a)
    MOV R3, R1
    MOV R1, R2
    MOV R2, R3
    CALL __rt_lt
    CMP R0, R0
    JZ .rt_le_one
    MOVI R0, 0
    RET
.rt_le_one:
    MOVI R0, 1
    RET

__rt_ge:
    ; a >= b → !(a < b)
    CALL __rt_lt
    CMP R0, R0
    JZ .rt_ge_one
    MOVI R0, 0
    RET
.rt_ge_one:
    MOVI R0, 1
    RET

; ---------- 移位辅助函数 ----------

__rt_lshift:
    ; R1=value, R2=shift → R0=value<<shift
    MOV R0, R1
    MOVI R3, 0
    CMP R2, R3
    JZ .rt_ls_done
    MOVI R3, 2
.rt_ls_loop:
    CMP R2, R3
    JZ .rt_ls_done
    MUL R0, R0, R3          ; *2 = <<1
    MOVI R4, 1
    SUB R2, R2, R4
    CMP R2, R3
    JNZ .rt_ls_loop
.rt_ls_done:
    RET

__rt_rshift:
    ; R1=value, R2=shift → R0=value>>shift
    MOV R0, R1
    MOVI R3, 0
    CMP R2, R3
    JZ .rt_rs_done
    MOVI R3, 2
.rt_rs_loop:
    CMP R2, R3
    JZ .rt_rs_done
    DIV R0, R0, R3          ; /2 = >>1
    MOVI R4, 1
    SUB R2, R2, R4
    CMP R2, R3
    JNZ .rt_rs_loop
.rt_rs_done:
    RET

; ---------- 打印整数 ----------

__rt_print_int:
    ; R1=integer → 打印十进制到 stdout
    MOVI R5, 10             ; divisor
    MOVI R6, 0              ; digit count
    ; 处理 0
    CMP R1, R0
    JNZ .rt_pi_nonzero
    MOVI R7, 48             ; '0'
    MOV R8, R7
    MOVI R1, 1              ; fd=stdout
    MOV R2, R8
    MOVI R3, 1
    SYSCALL 8
    RET
.rt_pi_nonzero:
    ; 缓冲区：利用栈空间
    MOV R9, R15             ; 保存 SP
    ; 逐位提取（逆序）
.rt_pi_loop:
    CMP R1, R0
    JZ .rt_pi_print_digits
    DIV R4, R1, R5          ; R4 = R1/10
    MUL R7, R4, R5          ; R7 = quotient*10
    SUB R7, R1, R7          ; R7 = R1%10
    MOVI R10, 48            ; '0'
    ADD R7, R7, R10         ; + '0'
    PUSH R7
    MOVI R8, 1
    ADD R6, R6, R8
    MOV R1, R4
    JMP .rt_pi_loop
.rt_pi_print_digits:
    ; 逆序弹出（恢复正确顺序）
    MOVI R1, 1               ; fd=stdout
.rt_pi_out_loop:
    CMP R6, R0
    JZ .rt_pi_done
    POP R2                  ; 弹出字符到 R2
    ; R2 是整数值，需要地址
    PUSH R2                 ; 压回去
    ; write(1, SP, 1)
    MOV R2, R15
    MOVI R3, 1
    SYSCALL 8
    POP R7                  ; 清理栈
    MOVI R8, 1
    SUB R6, R6, R8
    JMP .rt_pi_out_loop
.rt_pi_done:
    ; 打印换行
    MOVI R7, 10
    PUSH R7
    MOV R2, R15
    MOVI R3, 1
    SYSCALL 8
    POP R7
    MOV R15, R9             ; 恢复 SP
    RET

; ---------- memcpy / memset ----------

__rt_memcpy:
    ; R1=dst, R2=src, R3=count → R0=dst
    MOV R0, R1
    MOVI R4, 0
    CMP R3, R4
    JZ __rt_memcpy_done
__rt_memcpy_loop:
    LD R5, R2, 0
    ST R5, R1, 0
    MOVI R6, 4              ; 4 字节步长
    ADD R1, R1, R6
    ADD R2, R2, R6
    ADD R4, R4, R6
    CMP R4, R3
    JNZ __rt_memcpy_loop
__rt_memcpy_done:
    RET

__rt_memset:
    ; R1=dst, R2=value, R3=count → R0=dst
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
