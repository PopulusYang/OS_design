; ============================================================
; Memory Manager — UPFS VM Assembly Program
;
; 内存管理器，运行在 UPFS 虚拟机上。
; 功能: 1.申请内存  2.释放内存(按标号)  3.退出
;
; 实现:
;   - SYSCALL 12 (SBRK) 向内核申请堆内存，表空间也用 sbrk 分配
;   - 固定大小的分配表 (最多 16 条)，运行时动态分配避免 BSS 寻址问题
;   - 释放仅标记条目为未使用，进程退出时 OS 回收物理页
;   - SYSCALL 7/8 (READ/WRITE) 与终端交互
;
; 使用: asm /involve_src/mem_mgr.asm /bin/mm.upx && run /bin/mm.upx
; ============================================================

.text
.entry main

; ============================================================
; 寄存器全局状态:
;   R8  = alloc_count (当前活跃分配数)
;   R9  = 0x00000FFF (取地址低位时的掩码)
;   R10 = 0x80000000 (符号位检测掩码)
;   R11 = &alloc_addr[0]  (table base)
;   R12 = &alloc_size[0]  (table + 64)
;   R13 = &alloc_used[0]  (table + 128)
; ============================================================

main:
    ; --- 初始化 ---
    MOVI R8, 0                  ; alloc_count = 0

    ; R9 = 0xFFF 地址掩码
    MOVI R9, 0xFFF

    ; R10 = 0x80000000 (2^31) 符号位检测
    MOVI R10, 2
    MOVI R0, 256
    MUL R10, R10, R0            ; 512
    MOVI R0, 256
    MUL R10, R10, R0            ; 131072
    MOVI R0, 256
    MUL R10, R10, R0            ; 33554432
    MOVI R0, 64
    MUL R10, R10, R0            ; 2147483648 = 0x80000000

    ; 分配表空间: sbrk(192) — 16条目 × 12字节/条目
    MOVI R1, 192
    SYSCALL 12                  ; R0 = table base
    MOV R11, R0                 ; R11 = alloc_addr 列基址
    MOVI R1, 64
    ADD R12, R11, R1            ; R12 = alloc_size 列基址
    ADD R13, R12, R1            ; R13 = alloc_used 列基址

    ; 清零 alloc_used 列 (64 字节)
    MOVI R4, 0                  ; i = 0
zero_used:
    MOVI R5, 64
    CMP R4, R5
    JZ zero_done
    MOVI R5, 0
    ADD R0, R13, R4
    ST R5, [R0+0]               ; alloc_used[i] = 0
    MOVI R5, 4
    ADD R4, R4, R5
    JMP zero_used
zero_done:

    ; 打印欢迎信息
    LUI R0, str_welcome
    MOVI R1, str_welcome
    AND R1, R1, R9
    ADD R0, R0, R1
    CALL print

main_loop:
    ; --- 打印菜单 ---
    LUI R0, str_menu
    MOVI R1, str_menu
    AND R1, R1, R9
    ADD R0, R0, R1
    CALL print

    ; --- 读取选择 ---
    CALL read_int
    MOV R4, R0                  ; R4 = 选择

    ; 分支: 1=申请, 2=释放, 3=退出
    MOVI R1, 1
    CMP R4, R1
    JZ do_alloc

    MOVI R1, 2
    CMP R4, R1
    JZ do_free

    MOVI R1, 3
    CMP R4, R1
    JZ do_exit

    ; 无效输入
    LUI R0, str_invalid
    MOVI R1, str_invalid
    AND R1, R1, R9
    ADD R0, R0, R1
    CALL print
    JMP main_loop

; ============================================================
; do_alloc — 申请内存
; ============================================================
do_alloc:
    ; 表满检查
    MOVI R1, 16
    CMP R8, R1
    JNZ da_ok
    LUI R0, str_full
    MOVI R1, str_full
    AND R1, R1, R9
    ADD R0, R0, R1
    CALL print
    JMP main_loop

da_ok:
    ; 提示输入大小
    LUI R0, str_size_prompt
    MOVI R1, str_size_prompt
    AND R1, R1, R9
    ADD R0, R0, R1
    CALL print

    CALL read_int
    MOV R5, R0                  ; R5 = size

    ; size > 0 检查
    MOVI R1, 0
    CMP R5, R1
    JNZ da_sz_ok
    LUI R0, str_bad_sz
    MOVI R1, str_bad_sz
    AND R1, R1, R9
    ADD R0, R0, R1
    CALL print
    JMP main_loop

da_sz_ok:
    ; sbrk 分配
    MOV R1, R5
    SYSCALL 12                  ; R0 = addr
    MOV R6, R0                  ; R6 = addr

    ; 找空闲表槽
    CALL find_slot
    MOV R7, R0                  ; R7 = slot

    ; slot * 4
    MOVI R0, 4
    MUL R2, R7, R0              ; R2 = slot*4

    ; alloc_addr[slot] = addr
    ADD R0, R11, R2
    ST R6, [R0+0]

    ; alloc_size[slot] = size
    ADD R0, R12, R2
    ST R5, [R0+0]

    ; alloc_used[slot] = 1
    ADD R0, R13, R2
    MOVI R1, 1
    ST R1, [R0+0]

    ; count++
    MOVI R1, 1
    ADD R8, R8, R1

    ; --- 输出 "Allocated #N, addr=A, size=S\n" ---
    ; 用栈保护 addr(R6) 和 size(R5), 因为 print_int 会破坏 R5,R6
    PUSH R5                     ; 保存 size
    PUSH R6                     ; 保存 addr

    LUI R0, str_alloc1
    MOVI R1, str_alloc1
    AND R1, R1, R9
    ADD R0, R0, R1
    CALL print                  ; "Allocated #"
    MOV R1, R7
    CALL print_int              ; 打印 slot (R7 不会被 print_int 破坏)

    LUI R0, str_addr
    MOVI R1, str_addr
    AND R1, R1, R9
    ADD R0, R0, R1
    CALL print                  ; ", addr="
    POP R1                      ; R1 = 保存的 addr
    CALL print_int              ; 打印 addr

    LUI R0, str_sz
    MOVI R1, str_sz
    AND R1, R1, R9
    ADD R0, R0, R1
    CALL print                  ; ", size="
    POP R1                      ; R1 = 保存的 size
    CALL print_int              ; 打印 size

    CALL print_nl

    JMP main_loop

; ============================================================
; do_free — 释放内存
; ============================================================
do_free:
    MOVI R1, 0
    CMP R8, R1
    JNZ df_has
    LUI R0, str_none
    MOVI R1, str_none
    AND R1, R1, R9
    ADD R0, R0, R1
    CALL print
    JMP main_loop

df_has:
    ; --- 列出所有活跃分配 ---
    LUI R0, str_list_hdr
    MOVI R1, str_list_hdr
    AND R1, R1, R9
    ADD R0, R0, R1
    CALL print

    MOVI R7, 0                  ; R7 = slot 索引 (print_int 会破坏 R5)

df_list_loop:
    MOVI R1, 16
    CMP R7, R1
    JZ df_list_done

    ; 读 alloc_used[R7]
    MOVI R1, 4
    MUL R2, R7, R1              ; R2 = slot*4
    PUSH R2                     ; 保存 slot*4 (后续 CALL 会破坏 R2)
    ADD R0, R13, R2
    LD R3, [R0+0]
    MOVI R1, 1
    CMP R3, R1
    JNZ df_skip_pop

    ; 打印 ID
    MOV R1, R7
    CALL print_int
    CALL print_spc

    ; 打印 Size: 从栈恢复 slot*4, 从 R12 列读取
    POP R2
    PUSH R2                     ; 再保存 (后面不需要了, 但保持栈平衡)
    ADD R0, R12, R2
    LD R1, [R0+0]
    CALL print_int
    CALL print_nl
    POP R2                      ; 丢弃
    JMP df_skip

df_skip_pop:
    POP R2                      ; 丢弃保存的 slot*4

df_skip:
    MOVI R1, 1
    ADD R7, R7, R1
    JMP df_list_loop

df_list_done:
    ; 提示输入 ID
    LUI R0, str_free_prompt
    MOVI R1, str_free_prompt
    AND R1, R1, R9
    ADD R0, R0, R1
    CALL print

    CALL read_int
    MOV R4, R0                  ; R4 = ID

    ; 验证 ID: 0 <= ID < 16 且 used[ID]==1
    ; 检查 ID < 0
    AND R0, R4, R10
    MOVI R1, 0
    CMP R0, R1
    JNZ df_bad_id

    ; 检查 ID < 16
    MOVI R1, 16
    DIV R2, R4, R1
    MOVI R1, 0
    CMP R2, R1
    JNZ df_bad_id

    ; 检查 used[ID] == 1
    MOVI R1, 4
    MUL R2, R4, R1
    ADD R0, R13, R2
    LD R3, [R0+0]
    MOVI R1, 1
    CMP R3, R1
    JZ df_doit

df_bad_id:
    LUI R0, str_bad_id
    MOVI R1, str_bad_id
    AND R1, R1, R9
    ADD R0, R0, R1
    CALL print
    JMP main_loop

df_doit:
    ; 标记未使用
    MOVI R3, 0
    ST R3, [R0+0]

    ; count--
    MOVI R1, 1
    SUB R8, R8, R1

    ; 输出 "Freed #N\n"
    LUI R0, str_freed
    MOVI R1, str_freed
    AND R1, R1, R9
    ADD R0, R0, R1
    CALL print
    MOV R1, R4
    CALL print_int
    CALL print_nl

    JMP main_loop

; ============================================================
; do_exit — 退出
; ============================================================
do_exit:
    LUI R0, str_bye
    MOVI R1, str_bye
    AND R1, R1, R9
    ADD R0, R0, R1
    CALL print
    SYSCALL 0
    HALT

; ============================================================
; find_slot — 查找第一个空闲表槽
; 返回: R0 = 槽索引 (0-15)
; 前置: alloc_count < 16
; ============================================================
find_slot:
    MOVI R0, 0
fs_loop:
    MOVI R1, 4
    MUL R3, R0, R1
    ADD R1, R13, R3
    LD R2, [R1+0]
    MOVI R3, 0
    CMP R2, R3
    JZ fs_found
    MOVI R2, 1
    ADD R0, R0, R2
    JMP fs_loop
fs_found:
    RET

; ============================================================
; print — 打印 null 结尾字符串
; 输入: R0 = 字符串地址
; ============================================================
print:
    MOV R3, R0                  ; R3 = 基址
    MOVI R2, 0                  ; R2 = 长度

pr_count:
    ADD R1, R3, R2
    LD R1, [R1+0]
    MOVI R4, 0xFF
    AND R1, R1, R4
    MOVI R4, 0
    CMP R1, R4
    JZ pr_write
    MOVI R4, 1
    ADD R2, R2, R4
    JMP pr_count

pr_write:
    MOVI R4, 0
    CMP R2, R4
    JZ pr_ret
    ; R1=1(fd), R2=buf, R3=len
    MOV R0, R3                  ; R0 = buf
    MOV R3, R2                  ; R3 = len
    MOV R2, R0                  ; R2 = buf
    MOVI R1, 1                  ; R1 = fd=1
    SYSCALL 8
pr_ret:
    RET

; ============================================================
; print_int — 打印非负整数（十进制，无换行）
; 输入: R1 = 值 (>= 0)
; ============================================================
print_int:
    MOV R5, R1
    MOVI R6, 0                  ; digit count

    MOVI R0, 0
    CMP R5, R0
    JNZ pi_nonzero
    MOVI R0, '0'
    PUSH R0
    MOVI R6, 1
    JMP pi_out

pi_nonzero:
    MOVI R4, 10

pi_digit_loop:
    MOVI R0, 0
    CMP R5, R0
    JZ pi_out
    DIV R2, R5, R4
    MUL R3, R2, R4
    SUB R3, R5, R3
    MOV R5, R2
    MOVI R0, '0'
    ADD R3, R3, R0
    PUSH R3
    MOVI R0, 1
    ADD R6, R6, R0
    JMP pi_digit_loop

pi_out:
    MOVI R0, 0
pi_pop_loop:
    CMP R6, R0
    JZ pi_done
    POP R2
    PUSH R2
    MOVI R1, 1
    MOV R2, R15
    MOVI R3, 1
    SYSCALL 8
    POP R2
    MOVI R2, 1
    SUB R6, R6, R2
    JMP pi_out
pi_done:
    RET

; ============================================================
; print_spc — 打印空格
; ============================================================
print_spc:
    MOVI R0, 32                 ; ' '
    PUSH R0
    MOVI R1, 1
    MOV R2, R15
    MOVI R3, 1
    SYSCALL 8
    POP R0
    RET

; ============================================================
; print_nl — 打印换行
; ============================================================
print_nl:
    MOVI R0, 10
    PUSH R0
    MOVI R1, 1
    MOV R2, R15
    MOVI R3, 1
    SYSCALL 8
    POP R0
    RET

; ============================================================
; read_int — 读取整数，带回显
; 累积值放在 R5 (不被任何 syscall 破坏)，最后移入 R0 返回
; 返回: R0 = 整数值
; ============================================================
read_int:
    PUSH R5
    PUSH R6
    PUSH R7

    MOVI R5, 0                  ; R5 = 累积值 (不用 R0, 避免被 syscall 覆盖)
    MOVI R6, 0                  ; R6 = 负号标志
    MOVI R7, 0                  ; R7 = 已读到数字标志

ri_read_loop:
    ; 分配 4 字节栈空间用于 read(0, SP, 1)
    MOVI R0, 4
    SUB R15, R15, R0
    MOVI R1, 0                  ; fd = stdin
    MOV R2, R15                 ; buf = SP
    MOVI R3, 1                  ; 读 1 字节
    SYSCALL 7                   ; R0 = 读取字节数
    MOVI R1, 1
    CMP R0, R1
    JNZ ri_fail

    ; 取得字符
    LD R1, [R15+0]
    MOVI R2, 0xFF
    AND R1, R1, R2              ; R1 = 字符
    MOVI R0, 4
    ADD R15, R15, R0            ; 释放栈空间

    ; ---- 回显 (只破坏 R0, 不影响 R5/R6/R7) ----
    PUSH R1
    MOVI R1, 1                  ; fd = stdout
    MOV R2, R15                 ; buf = SP
    MOVI R3, 1
    SYSCALL 8
    POP R1

    ; 检查 \r 或 \n → 输入结束
    MOVI R2, 13                 ; \r
    CMP R1, R2
    JZ ri_eol
    MOVI R2, 10                 ; \n
    CMP R1, R2
    JNZ ri_not_eol
ri_eol:
    ; 回显换行
    MOVI R0, 10
    PUSH R0
    MOVI R1, 1
    MOV R2, R15
    MOVI R3, 1
    SYSCALL 8
    POP R0
    JMP ri_done

ri_not_eol:
    ; 检查退格 (127=DEL, 8=BS)
    MOVI R2, 127
    CMP R1, R2
    JZ ri_backspace
    MOVI R2, 8
    CMP R1, R2
    JNZ ri_chk_sign

ri_backspace:
    ; 如果已有数字, 移除最后一位: R5 = R5 / 10
    MOVI R2, 0
    CMP R7, R2
    JZ ri_read_loop             ; 没有数字, 忽略退格
    MOVI R2, 10
    DIV R5, R5, R2              ; R5 = R5 / 10 (去掉个位)
    ; 若 R5 变为 0, 清除已读标志
    MOVI R2, 0
    CMP R5, R2
    JNZ ri_bs_done
    MOVI R7, 0                  ; 没有数字了
ri_bs_done:
    ; 回显 "\b \b" 擦除屏幕上的字符
    MOVI R0, 8                  ; backspace
    PUSH R0
    MOVI R1, 1
    MOV R2, R15
    MOVI R3, 1
    SYSCALL 8
    POP R0
    MOVI R0, 32                 ; space
    PUSH R0
    MOVI R1, 1
    MOV R2, R15
    MOVI R3, 1
    SYSCALL 8
    POP R0
    MOVI R0, 8                  ; backspace
    PUSH R0
    MOVI R1, 1
    MOV R2, R15
    MOVI R3, 1
    SYSCALL 8
    POP R0
    JMP ri_read_loop

    ; 检查负号 (仅第一个非数字字符)
ri_chk_sign:
    MOVI R2, '-'
    CMP R1, R2
    JNZ ri_chk_digit
    MOVI R2, 1
    CMP R7, R2                  ; 已经读过数字?
    JZ ri_read_loop             ; 是 → 忽略负号
    ; 检查是否已有累积值
    MOVI R2, 0
    CMP R5, R2
    JNZ ri_read_loop            ; 已有累积值 → 忽略
    MOVI R6, 1                  ; 标记负数
    JMP ri_read_loop

    ; 检查数字 '0'..'9'
ri_chk_digit:
    MOVI R2, '0'
    SUB R2, R1, R2              ; R2 = c - '0'
    ; 若 R2 < 0 → 不是数字
    AND R3, R2, R10
    MOVI R4, 0
    CMP R3, R4
    JNZ ri_read_loop
    ; 若 R2 > 9 → 不是数字 (digit-10 >= 0 则 bit31=0 → 非数字)
    MOVI R3, 10
    SUB R4, R2, R3              ; R4 = digit - 10
    AND R4, R4, R10             ; 若 digit<10, R4<0, bit31=1, AND≠0 → 保留
    MOVI R3, 0                  ; 若 digit>=10, R4>=0, bit31=0, AND=0 → 跳过
    CMP R4, R3
    JZ ri_read_loop

    ; 合法数字, 累积到 R5
    MOVI R7, 1                  ; 标记读过数字
    MOVI R3, 10
    MUL R5, R5, R3
    ADD R5, R5, R2
    JMP ri_read_loop

ri_fail:
    MOVI R0, 4
    ADD R15, R15, R0

ri_done:
    ; 应用负号, 结果移入 R0
    MOV R0, R5                  ; R0 = 累积值
    MOVI R1, 0
    CMP R6, R1
    JZ ri_ret
    MOV R2, R0
    MOVI R0, 0
    SUB R0, R0, R2              ; R0 = -R0

ri_ret:
    POP R7
    POP R6
    POP R5
    RET

; ============================================================
; 数据段
; ============================================================
.data

str_welcome:
    .asciz "=== UPFS Memory Manager ===\n"
str_menu:
    .asciz "\n1. Alloc\n2. Free\n3. Exit\n> "
str_invalid:
    .asciz "Invalid choice!\n"
str_full:
    .asciz "Table full (max 16)!\n"
str_size_prompt:
    .asciz "Size: "
str_bad_sz:
    .asciz "Size must be > 0!\n"
str_alloc1:
    .asciz "Allocated #"
str_addr:
    .asciz ", addr="
str_sz:
    .asciz ", size="
str_none:
    .asciz "No allocated blocks.\n"
str_list_hdr:
    .asciz "ID  Size\n------\n"
str_free_prompt:
    .asciz "Free ID: "
str_bad_id:
    .asciz "Invalid ID!\n"
str_freed:
    .asciz "Freed #"
str_bye:
    .asciz "Goodbye!\n"

; No .bss section — table allocated via sbrk at runtime

.stack 4096
