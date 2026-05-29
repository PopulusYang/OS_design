# UPFS C 语言子集参考

## 概述

UPFS C 编译器 (`cc`) 将 C 语言子集编译为 UPFS VM 汇编 (`.s`)，再由内置汇编器生成 `.upx` 可执行文件。

### 命令用法

```
cc <source.c> [output.upx] [--asm]
```

| 参数 | 说明 |
|------|------|
| `source.c` | C 源文件（VFS 内路径） |
| `output.upx` | 输出文件路径，默认 `a.upx` |
| `--asm` | 保留中间汇编文件 `<source>.s`，不删除 |

### 编译流程

```
source.c (VFS)
  →  导出到宿主机 /tmp/
  →  [词法分析] → Token 流
  →  [语法分析] → AST
  →  [代码生成] → assembly .s
  →  [附加运行时] 合并 runtime.s (比较/移位/I/O 辅助函数)
  →  [汇编器]    → .upx
  →  导入回 VFS
```

编译器自动将运行时库（`src/compiler/runtime.s`）附加到输出中，用户无需手动链接。如果指定了 `--asm`，中间的 `.s` 文件会保留在 VFS 中以便调试。

---

## 支持的语言特性

### 数据类型

| 类型 | 大小 | 说明 |
|------|------|------|
| `int` | 32 位有符号 | 唯一的基本类型 |
| `void` | — | 仅用于函数返回类型 |

> **注意**: 词法分析器只识别 `int` 和 `void` 关键字。声明 `char`、`short` 等类型会因被当作标识符而导致解析错误。在 VFS 文件 I/O 场景中，`char` 语义可通过 `int` 数组手动处理。

### 关键字

```
int  void  if  else  while  for  do  return  break  continue  struct  sizeof
```

### 运算符

| 优先级 | 运算符 | 说明 |
|--------|--------|------|
| 最高 | `()` `[]` `.` | 函数调用、数组下标、结构体成员 |
| | `-` `!` `~` `*` `&` | 一元运算符 |
| | `*` `/` `%` | 乘法、除法、取余 |
| | `+` `-` | 加法、减法 |
| | `<<` `>>` | 移位（调用运行时库） |
| | `<` `>` `<=` `>=` | 比较（调用运行时库） |
| | `==` `!=` | 相等、不等 |
| | `&` | 按位与 |
| | `^` | 按位异或 |
| | `\|` | 按位或 |
| | `&&` | 逻辑与（短路求值） |
| | `\|\|` | 逻辑或（短路求值） |
| 最低 | `=` `+=` `-=` `*=` `/=` | 赋值和复合赋值 |

#### 运算符实现说明

VM 指令集只有 `ADD/SUB/MUL/DIV/AND/OR/XOR/CMP` 和 ZF 标志位，以下运算符通过运行时库或内联展开实现：

| 运算符 | 实现方式 |
|--------|----------|
| `<` `>` `<=` `>=` | `CALL __rt_lt/gt/le/ge`（运行时库函数，返回 0 或 1） |
| `<<` `>>` | `CALL __rt_lshift/rshift`（循环乘/除 2 逐位实现） |
| `%` | 内联 `DIV → MUL → SUB`：`a % b = a - (a/b)*b` |
| `~` | 内联 `XOR -1` |
| `!` | 内联 `CMP 0 → JZ/JNZ` |
| `==` `!=` | 内联 `CMP → JZ/JNZ` |
| `&&` `\|\|` | 短路求值：左操作数为假（`&&`）或真（`\|\|`）时跳过右操作数 |

### 字面量

| 格式 | 示例 | 说明 |
|------|------|------|
| 十进制 | `42`, `-1` | 有符号整数 |
| 十六进制 | `0x2A`, `0xFF` | 十六进制 |
| 字符 | `'A'`, `'\n'` | 字符常量（实际为 int 值） |
| 字符串 | `"hello\n"` | 字符串字面量（自动放入数据段，`.asciz`） |

转义序列：`\n` `\r` `\t` `\0` `\\` `\"`

### 注释

```c
// 单行注释
/* 块注释 */
```

---

## 控制流

### 条件语句

```c
if (x != 0) {
    x = x + 1;
} else {
    x = 0;
}
```

### 循环语句

```c
// while
while (i != 10) {
    i = i + 1;
}

// for
for (i = 0; i != 10; i = i + 1) {
    sum = sum + i;
}

// do-while
do {
    i = i + 1;
} while (i != 0);
```

`for` 循环的三个表达式均可省略：`for (;;)` 等价于 `while (1)`。

### break / continue

```c
while (1) {
    if (done) break;       // 跳出最内层循环
    if (skip) continue;    // 跳到循环条件判断
    process();
}
```

`break` 和 `continue` 作用域为最内层的 `while`/`for`/`do-while`，不支持跨层跳转。

---

## 函数

### 定义与调用

```c
int add(int a, int b) {
    return a + b;
}

int main() {
    int result = add(3, 4);
    return result;
}
```

函数支持递归调用，最多 8 个参数。

### 程序入口

编译器自动生成 `_start` 包装：

```asm
_start:
    CALL main
    SYSCALL 0          ; exit(main 返回值)
```

`main` 函数的返回值直接传给 `SYSCALL 0`（EXIT）。因此 `main` 必须返回 `int` 类型。

### 调用约定

| 项 | 规则 |
|----|------|
| 参数传递 | R1–R8（前 8 个参数），超过 8 个不支持 |
| 返回值 | R0 |
| 临时寄存器 | R1–R8，调用者负责保存 |
| 被调用者保存 | R9–R13 |
| 帧指针 | FP = R14 |
| 栈指针 | SP = R15 |

### 参数布局

函数调用时，参数存入 R1–R8。进入函数后，参数被拷贝到栈帧中 FP 正方向：

```
FP + 24:  参数1 (来自 R1)
FP + 28:  参数2 (来自 R2)
FP + 32:  参数3 (来自 R3)
...
```

偏移从 24 开始：CALL 压入返回地址（4 字节）+ 序言中 PUSH R13/R12/R11/R10/R9/R14（6×4=24 字节）。

### 栈帧布局

```
FP + 24+    参数（调用者压入）
FP          旧 FP 值、被调用者保存的寄存器
FP - N      局部变量和临时数据
SP          栈顶（向下增长）
```

局部变量空间在序言中通过 `SUB R15, R15, <N+64>` 分配，其中额外 64 字节为安全边距，防止被调用者的 PUSH/POP 踩到局部变量。

---

## 变量与作用域

### 全局变量

```c
int counter = 0;   // 放在 .data 段，初始化为 0

int main() {
    counter = counter + 1;
    return counter;
}
```

全局变量在 `.data` 段分配，地址通过 `LUI + MOVI + ADD` 加载。初始化只支持单个整数字面量；未初始化的全局变量预留 `.space 4`。全局数组使用 `.space` 指令分配。

### 局部变量

```c
int main() {
    int x = 5;           // 栈上分配，地址在 FP - offset
    int y = x * 2;
    return y;
}
```

局部变量在函数栈帧中分配，通过 `LD R, R14, offset` 和 `ST R, R14, offset` 访问。

### 数组

```c
int buf[16];            // 全局数组，.data 段分配

int main() {
    int arr[4];         // 局部数组，栈上分配
    arr[0] = 10;
    arr[1] = 20;
    return arr[0] + arr[1];
}
```

数组下标 `arr[i]` 按 `base + i * 4` 计算地址后 LD/ST。全局数组必须在声明时就指定大小，变量长度数组（VLA）不支持。

---

## 结构体

```c
struct Point {
    int x;    // offset 0
    int y;    // offset 4
};

int main() {
    struct Point p;
    p.x = 10;
    p.y = 20;
    return p.x + p.y;   // 返回 30
}
```

结构体成员访问 `p.x` 被翻译为 `*(&p + member_offset)`：取变量地址后加上成员偏移量（按声明顺序，0, 4, 8...），再 LD/ST。不支持结构体嵌套。

### 与 sizeof 配合

```c
struct Item {
    int id;
    int val;
};
int items[10];  // sizeof(Item) = 8, items 可用 80 字节
```

`sizeof(struct Name)` 返回结构体总字节数（成员数 × 4）。

---

## 运行时库

编译器自动将运行时库附加到编译输出中，用户无需手动链接。

### 比较函数

| 函数 | 参数 | 返回值 | 说明 |
|------|------|--------|------|
| `__rt_lt(a, b)` | R1=a, R2=b | R0=1 if a<b, else 0 | 有符号小于 |
| `__rt_gt(a, b)` | R1=a, R2=b | R0=1 if a>b, else 0 | 有符号大于 |
| `__rt_le(a, b)` | R1=a, R2=b | R0=1 if a<=b, else 0 | 有符号小于等于 |
| `__rt_ge(a, b)` | R1=a, R2=b | R0=1 if a>=b, else 0 | 有符号大于等于 |

实现原理：利用有符号除法的符号传播特性，通过移位和除法逐位比较符号位。

### 移位函数

| 函数 | 参数 | 返回值 | 说明 |
|------|------|--------|------|
| `__rt_lshift(v, s)` | R1=v, R2=s | R0=v<<s | 循环乘 2 实现 |
| `__rt_rshift(v, s)` | R1=v, R2=s | R0=v>>s | 循环除 2 实现（算术右移） |

### I/O 函数

| 函数 | 参数 | 说明 |
|------|------|------|
| `__rt_print_int(v)` | R1=v | 打印整数到 stdout（十进制 + 换行） |

### 内存函数

| 函数 | 参数 | 说明 |
|------|------|------|
| `__rt_memcpy(d, s, c)` | R1=dst, R2=src, R3=count | 按 4 字节步长拷贝 |
| `__rt_memset(d, v, c)` | R1=dst, R2=value, R3=count | 按 4 字节步长填充 |

> **注意**: `memcpy` 和 `memset` 按 4 字节对齐步进，`count` 应为字节数且最好是 4 的倍数，非对齐访问行为未定义。

---

## 系统调用

使用系统调用前需要声明 `extern` 函数原型（无需 `#include`）：

```c
extern int  _sys_open(const char *path, int flags);
extern int  _sys_read(int fd, char *buf, int len);
// ...
```

### 进程管理

| 函数 | 系统调用号 | 说明 |
|------|-----------|------|
| `void _sys_exit(int code)` | 0 | 退出进程 |
| `int  _sys_fork(void)` | 1 | 复制进程，子进程返回 0，父进程返回子 PID |
| `int  _sys_exec(const char *path)` | 2 | 加载并执行程序 |
| `int  _sys_wait(int *status)` | 3 | 等待子进程退出，status 存入退出码 |
| `int  _sys_getpid(void)` | 4 | 获取进程 ID |

### 文件系统

| 函数 | 系统调用号 | 说明 |
|------|-----------|------|
| `int  _sys_open(const char *path, int flags)` | 5 | 打开文件，flags: 0=RDONLY, 1=WRONLY, 2=RDWR |
| `int  _sys_close(int fd)` | 6 | 关闭文件 |
| `int  _sys_read(int fd, char *buf, int len)` | 7 | 读取数据，返回读取字节数 |
| `int  _sys_write(int fd, const char *buf, int len)` | 8 | 写入数据，fd=1 为 stdout |
| `int  _sys_seek(int fd, int offset, int whence)` | 9 | 文件定位 |
| `int  _sys_create(const char *path, int mode)` | 17 | 创建文件，mode 为八进制权限 |
| `int  _sys_delete(const char *path)` | 18 | 删除文件 |
| `int  _sys_stat(const char *path, char *buf)` | 16 | 获取文件状态信息 |

### 目录

| 函数 | 系统调用号 | 说明 |
|------|-----------|------|
| `int  _sys_getcwd(char *buf, int size)` | 10 | 获取当前工作目录路径 |
| `int  _sys_chdir(const char *path)` | 11 | 切换工作目录 |
| `int  _sys_mkdir(const char *path, int mode)` | 19 | 创建目录 |

### 内存与环境

| 函数 | 系统调用号 | 说明 |
|------|-----------|------|
| `int  _sys_sbrk(int increment)` | 12 | 扩展堆空间，返回旧堆顶地址 |
| `int  _sys_getenv(const char *name, char *buf, int size)` | 13 | 获取环境变量值 |
| `int  _sys_setenv(const char *name, const char *value)` | 14 | 设置环境变量 |
| `int  _sys_unsetenv(const char *name)` | 15 | 删除环境变量 |

### IPC（进程间通信）

| 函数 | 系统调用号 | 说明 |
|------|-----------|------|
| `int  _sys_pipe(int *fds)` | 22 | 创建匿名管道，fds[0]=读端 fds[1]=写端 |
| `int  _sys_kill(int pid, int sig)` | 23 | 向进程发送信号（9=SIGKILL, 15=SIGTERM） |
| `int  _sys_semget(int key, int flags)` | 24 | 创建/获取信号量，返回 semid |
| `int  _sys_semop(int semid, int op)` | 25 | 信号量操作（-1=P, +1=V） |
| `int  _sys_msgget(int key)` | 26 | 创建/获取消息队列，返回 mqid |
| `int  _sys_msgsnd(int mqid, char *msg, int size)` | 27 | 发送消息 |
| `int  _sys_msgrcv(int mqid, char *buf, int size)` | 28 | 接收消息 |
| `int  _sys_shmget(int key, int size)` | 29 | 创建/获取共享内存段，返回 shmid |
| `int  _sys_shmat(int shmid, int *addr)` | 30 | 附加共享内存到进程地址空间 |
| `int  _sys_shmdt(int shmid)` | 31 | 分离共享内存 |
| `int  _sys_mkfifo(const char *path)` | 32 | 创建命名 FIFO |
| `int  _sys_getsig(void)` | 33 | 获取当前进程待处理信号 |

---

## 内存布局

```
虚拟地址空间 (32-bit)
┌─────────────────────────────────────┐
│ 0x00000000                          │
│   代码段 (.text)                     │ ← _start 入口
│   ↓ 向上增长                         │
├─────────────────────────────────────┤
│ 0x00001000 (页 1)                    │
│   数据段 (.data / .bss)              │ ← 全局变量、字符串
│   ↓ 向上增长                         │
├─────────────────────────────────────┤
│   未映射区域                         │
│   （堆可通过 _sys_sbrk 扩展）        │
├─────────────────────────────────────┤
│ 0x01000000                          │
│   栈 (向下增长)                      │ ← SP 初始值
│   ↑ 向下增长                         │
│   局部变量、参数、返回地址            │
└─────────────────────────────────────┘
```

页面大小：4096 字节。每进程最多 4096 页 = 16MB 用户空间。`_sys_sbrk()` 可扩展代码/数据段上方的堆区域。

---

## 汇编器指令参考

生成的汇编中使用的 `.s` 指令：

| 指令 | 说明 | 示例 |
|------|------|------|
| `.text` | 代码段开始 | `.text` |
| `.data` | 数据段开始 | `.data` |
| `.entry <label>` | 指定入口点 | `.entry _start` |
| `.word <value>` | 放入一个 32 位整数值 | `.word 42` |
| `.space <bytes>` | 预留指定字节的空间 | `.space 16` |
| `.asciz "<str>"` | 放入以 `\0` 结尾的字符串 | `.asciz "hello\n"` |
| `.stack <bytes>` | 指定栈大小 | `.stack 4096` |

---

## 限制

| 限制 | 值 | 说明 |
|------|------|------|
| 最大函数参数 | 8 | R1–R8，超出部分报错 |
| 最大代码段 | 64 KB | 汇编器限制 |
| 最大数据段 | 64 KB | 汇编器限制 |
| 类型系统 | 仅 int / void | 不支持 char、short、long、float、double |
| 指针 / 解引用 | 不支持 | 解析器接受 `*` 和 `&` 语法但代码生成为空操作，不可用 |
| struct | 有限支持 | 仅一层成员，不支持嵌套和指针成员 |
| 浮点数 | 不支持 | |
| 多文件编译 | 不支持 | 仅单文件编译 |
| 预处理指令 | 不支持 | `#include`、`#define` 等不可用 |
| main 参数 | 不支持 | `main()` 不接受 argc/argv |
| 变量长度数组 | 不支持 | 数组大小必须为编译时常量 |
| 字符串修改 | 不支持 | 字符串字面量位于 .data 段，写入会导致未定义行为 |
| memcpy/memset 对齐 | 4 字节 | 非 4 字节对齐的 count 行为未定义 |

---

## 示例

### 例 1: Hello World

```c
int main() {
    _sys_write(1, "Hello, World!\n", 14);
    return 0;
}
```

```bash
cc hello.c hello.upx
run hello.upx
```

### 例 2: 求和循环

```c
int main() {
    int i = 0;
    int sum = 0;
    while (i != 100) {
        sum = sum + i;
        i = i + 1;
    }
    return sum;    // 返回 4950
}
```

### 例 3: 递归阶乘

```c
int fact(int n) {
    if (n == 0) return 1;
    return n * fact(n - 1);
}

int main() {
    return fact(5);    // 返回 120
}
```

### 例 4: 数组操作

```c
int buf[8];

int main() {
    int i = 0;
    while (i != 8) {
        buf[i] = i * i;
        i = i + 1;
    }
    return buf[7];    // 返回 49
}
```

### 例 5: 文件读写

```c
int main() {
    _sys_create("/data.txt", 0644);
    int fd = _sys_open("/data.txt", 2);    // O_WRONLY
    _sys_write(fd, "test data\n", 10);
    _sys_close(fd);

    fd = _sys_open("/data.txt", 1);         // O_RDONLY
    int buf[4];  // 用 int 数组作缓冲区
    _sys_read(fd, buf, 10);
    _sys_write(1, buf, 10);  // 回显到 stdout
    _sys_close(fd);
    return 0;
}
```

### 例 6: 进程 fork

```c
int main() {
    int pid = _sys_fork();

    if (pid == 0) {
        _sys_write(1, "child\n", 6);
        _sys_exit(42);
    } else {
        _sys_write(1, "parent\n", 7);
        int status;
        _sys_wait(&status);
        return status;    // 返回 42
    }
}
```

### 例 7: IPC 信号量

```c
int main() {
    int sem = _sys_semget(0x100, 0);   // 创建信号量
    _sys_semop(sem, 1);                // V 操作 (+1)
    int v = _sys_semop(sem, -1);       // P 操作 (-1)
    return v;
}
```

### 例 8: 结构体

```c
struct Rect {
    int x;
    int y;
    int w;
    int h;
};

int main() {
    struct Rect r;
    r.x = 5;
    r.y = 10;
    r.w = 100;
    r.h = 200;
    return r.x + r.y + r.w + r.h;  // 返回 315
}
```

---

## 调试技巧

### 查看生成的汇编

```bash
cc test.c --asm     # 保留 test.s 文件
cat test.s           # 查看汇编输出
```

### 检查编译错误

编译器在以下阶段可能报错：
- **词法错误**: 不认识的字符或格式错误的字面量
- **语法错误**: 缺少分号、括号不匹配、意外的 token
- **符号错误**: 未声明的变量或函数

错误信息包含源码行号，方便定位问题。

### 运行时调试

- `__rt_print_int()` 可打印中间变量值
- `_sys_write(1, ...)` 可输出调试字符串
- `ps` 查看进程状态
- `kill <pid> 9` 终止卡死的进程

---

## 相关文档

- [asm-reference.md](asm-reference.md) — 汇编语言参考
- [CLAUDE.md](../CLAUDE.md) — 项目整体说明
