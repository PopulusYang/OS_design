# UPFS VM 汇编语言参考

## 概述

UPFS 虚拟机使用 32 位定长 RISC 指令集，共 18 条指令，16 个通用寄存器（R0-R15，其中 R15 为栈指针 SP）。

汇编器 `asm` 将 `.s` 汇编源码编译为 `.upx` 可执行文件。

### 命令用法

```
asm <source.s> [output.upx]
```

默认输出文件名为 `a.upx`。

---

## 指令格式

32 位编码：`[opcode:8][rd:4][rs1:4][rs2:4][imm12:12]`

| 字段 | 位宽 | 说明 |
|------|------|------|
| opcode | 8 | 操作码 |
| rd | 4 | 目标寄存器 |
| rs1 | 4 | 源寄存器 1 |
| rs2 | 4 | 源寄存器 2 |
| imm12 | 12 | 立即数 / 偏移量（有符号） |

---

## 寄存器

| 名称 | 编号 | 用途 |
|------|------|------|
| R0 – R14 | 0–14 | 通用寄存器 |
| SP / R15 | 15 | 栈指针 |

---

## 指令集

### 数据传输

| 指令 | 语法 | 编码 | 说明 |
|------|------|------|------|
| MOVI | `MOVI Rd, imm` | `rd=reg, imm12=imm` | Rd = 符号扩展(imm) |
| MOV | `MOV Rd, Rs` | `rd=reg, rs1=reg` | Rd = Rs |
| LUI | `LUI Rd, imm` | `rd=reg, imm12=imm` | Rd = imm << 12 (加载高 20 位地址) |

### 内存访问

| 指令 | 语法 | 编码 | 说明 |
|------|------|------|------|
| LD | `LD Rd, [Rs+off]` | `rd=reg, rs1=base, imm12=off` | Rd = mem[Rs + off] |
| LD | `LD Rd, Rs, off` | 同上 | (替代语法) |
| ST | `ST Rs, [Rb+off]` | `rs2=src, rs1=base, imm12=off` | mem[Rb + off] = Rs |
| ST | `ST Rs, Rb, off` | 同上 | (替代语法) |

### 算术运算

| 指令 | 语法 | 编码 | 说明 |
|------|------|------|------|
| ADD | `ADD Rd, Rs1, Rs2` | `rd=reg, rs1=reg, rs2=reg` | Rd = Rs1 + Rs2 |
| SUB | `SUB Rd, Rs1, Rs2` | `rd=reg, rs1=reg, rs2=reg` | Rd = Rs1 - Rs2 |
| MUL | `MUL Rd, Rs1, Rs2` | `rd=reg, rs1=reg, rs2=reg` | Rd = Rs1 * Rs2 |
| DIV | `DIV Rd, Rs1, Rs2` | `rd=reg, rs1=reg, rs2=reg` | Rd = Rs1 / Rs2 |

### 逻辑运算

| 指令 | 语法 | 编码 | 说明 |
|------|------|------|------|
| AND | `AND Rd, Rs1, Rs2` | `rd=reg, rs1=reg, rs2=reg` | Rd = Rs1 & Rs2 |
| OR | `OR Rd, Rs1, Rs2` | `rd=reg, rs1=reg, rs2=reg` | Rd = Rs1 \| Rs2 |
| XOR | `XOR Rd, Rs1, Rs2` | `rd=reg, rs1=reg, rs2=reg` | Rd = Rs1 ^ Rs2 |

### 比较与跳转

| 指令 | 语法 | 编码 | 说明 |
|------|------|------|------|
| CMP | `CMP Rs1, Rs2` | `rs1=reg, rs2=reg` | FLAGS = Rs1 - Rs2 |
| JMP | `JMP label` | `imm12=offset` | PC += offset（无条件跳转） |
| JZ | `JZ label` | `imm12=offset` | 若 ZF=1，PC += offset |
| JNZ | `JNZ label` | `imm12=offset` | 若 ZF=0，PC += offset |
| CALL | `CALL label` | `imm12=offset` | PUSH PC; PC += offset |
| RET | `RET` | — | POP PC（子程序返回） |

### 栈操作

| 指令 | 语法 | 编码 | 说明 |
|------|------|------|------|
| PUSH | `PUSH Rs` | `rs1=reg` | SP-=4; mem[SP] = Rs |
| POP | `POP Rd` | `rd=reg` | Rd = mem[SP]; SP+=4 |

### 系统

| 指令 | 语法 | 编码 | 说明 |
|------|------|------|------|
| SYSCALL | `SYSCALL n` | `imm12=n` | 触发系统调用 n |
| HALT | `HALT` | — | 停止执行，进程退出 |

跳转偏移量以**指令数**为单位（非字节）。`JMP label` 的 imm12 = 目标指令索引 - 当前指令索引 - 1。

---

## 系统调用表

| 编号 | 名称 | 说明 |
|------|------|------|
| 0 | EXIT | 退出进程，R0 为返回值 |
| 1 | FORK | 复制当前进程 |
| 2 | EXEC | 加载执行程序 |
| 3 | WAIT | 等待子进程 |
| 4 | GETPID | 获取进程 ID |
| 5 | OPEN | 打开文件 |
| 6 | CLOSE | 关闭文件 |
| 7 | READ | 读取文件 |
| 8 | WRITE | 写入文件/终端 |
| 9 | SEEK | 文件定位 |
| 10 | GETCWD | 获取当前目录 |
| 11 | CHDIR | 切换目录 |
| 12 | SBRK | 扩展堆空间 |
| 13 | GETENV | 获取环境变量 |
| 14 | SETENV | 设置环境变量 |
| 15 | UNSETENV | 删除环境变量 |
| 16 | STAT | 获取文件状态 |
| 17 | CREATE | 创建文件 |
| 18 | DELETE | 删除文件 |
| 19 | MKDIR | 创建目录 |
| 20 | HOST_EDIT | 调用宿主编编辑器 |
| 21 | HOST_ASM | 调用宿主汇编器 |
| 22 | PIPE | 创建匿名管道，`R1`=fds 数组地址 |
| 23 | KILL | 发送信号，`R1`=pid，`R2`=sig（9/15/10） |
| 24 | SEMGET | 创建/获取信号量，`R1`=key，`R2`=初值 |
| 25 | SEMOP | P/V 操作，`R1`=semid，`R2`=delta（-1=P，+1=V） |
| 26 | MSGGET | 创建/获取消息队列，`R1`=key |
| 27 | MSGSND | 发消息，`R1`=qid，`R2`=buf（首字 type），`R3`=总长 |
| 28 | MSGRCV | 收消息，`R1`=qid，`R2`=buf，`R3`=type（0=任意） |
| 29 | SHMGET | 创建/获取共享内存，`R1`=key，`R2`=size |
| 30 | SHMAT | 挂接共享内存，`R1`=shmid，`R2`=虚拟地址 |
| 31 | SHMDT | 分离共享内存，`R1`=shmid |
| 32 | MKFIFO | 创建命名 FIFO，`R1`=路径 |
| 33 | GETSIG | 读取并清零 SIGUSR1 计数 |

### 进程通信（IPC）

**匿名管道**：`SYSCALL 22` = `pipe(fds[2])`；Shell 支持 `cmd1 | cmd2 | ...`（最多 8 段）。

**信号**：`KILL(23)` 发送信号；`GETSIG(33)` 读取 SIGUSR1。见 `involve_src/ipcsig.asm`。

**信号量**：`SEMGET` + `SEMOP`（P=-1，V=+1）。见 `involve_src/ipcsem.asm`。

**消息队列**：`MSGGET` + `MSGSND` + `MSGRCV`。见 `involve_src/ipcmsg.asm`。

**共享内存**：`SHMGET` + `SHMAT` + `SHMDT`，多进程映射同一物理页。见 `involve_src/ipcshm.asm`。

**命名 FIFO**：Shell `mkfifo /path` 或 `MKFIFO(32)`；`OPEN` 路径即可读写。见 `involve_src/ipcfifo.asm`。

Shell 管道示例：`run /pw.upx | run /pr.upx`（先 `asm /src/pw.asm` 等）。

---

## 伪指令

### 段声明

| 伪指令 | 说明 |
|--------|------|
| `.text` | 开始代码段 |
| `.data` | 开始数据段 |
| `.bss` | 开始 BSS 段（未初始化数据） |

### 入口与符号

| 伪指令 | 说明 |
|--------|------|
| `.entry label` | 设置程序入口点 |
| `label:` | 定义标签（用于跳转目标） |

### 数据定义

| 伪指令 | 说明 |
|--------|------|
| `.word value` | 定义 32 位常量 |
| `.space N` | 保留 N 字节空间（填 0） |
| `.ascii "str"` | 定义 ASCII 字符串 |
| `.asciz "str"` | 定义以 `\0` 结尾的字符串 |

### 栈设置

| 伪指令 | 说明 |
|--------|------|
| `.stack N` | 设置栈大小（字节），默认 4096 |

---

## 立即数格式

| 格式 | 示例 | 说明 |
|------|------|------|
| 十进制 | `42`, `-1` | 有符号十进制 |
| 十六进制 | `0x2A`, `0xFF` | 十六进制 |
| 二进制 | `0b101010` | 二进制 |
| 字符 | `'A'` | 字符常量（ASCII 值） |

---

## 注释

支持以下注释格式：

```
; 分号注释
// 双斜线注释
```

---

## 示例

### 例 1: 求和

```asm
; sum = 1 + 2 + ... + 10
.text
.entry start

start:
    MOVI R0, 0        ; R0 = sum = 0
    MOVI R1, 10       ; R1 = counter
    MOVI R2, 0        ; R2 = 0 (for comparison)

loop:
    ADD R0, R0, R1    ; sum += counter
    MOVI R3, 1
    SUB R1, R1, R3    ; counter--
    CMP R1, R2        ; counter == 0?
    JNZ loop          ; if not, loop
    SYSCALL 0         ; exit(R0)
    HALT

.data
    .word 0x1234
    .word 42

.bss
    .space 64

.stack 2048
```

### 例 2: 子程序调用

```asm
.text
.entry main

main:
    MOVI R0, 5
    CALL double_it     ; R0 = double(5)
    SYSCALL 0          ; exit(10)
    HALT

double_it:
    ADD R0, R0, R0     ; R0 = R0 + R0
    RET

.stack 1024
```

### 例 3: 访问数据和 BSS

```asm
.text
.entry main

main:
    ; 加载 data 段常量
    LUI R0, mydata>>12  ; R0 = mydata 地址高位（需手动对齐）
    MOVI R0, mydata&0xFFF
    LD R1, [R0+0]       ; R1 = mem[mydata]
    
    ; 写入 BSS 区域
    LUI R2, buffer>>12
    MOVI R2, buffer&0xFFF
    MOVI R3, 99
    ST R3, [R2+0]       ; buffer[0] = 99

    SYSCALL 0
    HALT

.data
mydata:
    .word 0xDEADBEEF
    .word 100

.bss
buffer:
    .space 256

.stack 4096
```

---

## .upx 文件格式

```
偏移  大小  字段
0     4     魔数 "UPX\0"
4     4     入口指令索引
8     4     代码段大小（字节）
12    4     数据段大小（字节）
16    4     BSS 段大小（字节）
20    4     栈大小（字节）
24    N     代码段
24+N  M     数据段
```
