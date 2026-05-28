# UPFS C 语言子集参考

## 概述

UPFS C 编译器 (`cc`) 将 C 语言子集编译为 UPFS VM 汇编 (`.s`)，再由内置汇编器生成 `.upx` 可执行文件。

### 命令用法

```
cc <source.c> [output.upx]
```

默认输出文件名为 `a.upx`。

---

## 支持的语言特性

### 数据类型

| 类型 | 大小 | 说明 |
|------|------|------|
| `int` | 32 位有符号 | 唯一的基本类型 |
| `void` | — | 仅用于函数返回类型 |

### 关键字

```
int void if else while for do return break continue struct sizeof
```

### 运算符

| 优先级 | 运算符 | 说明 |
|--------|--------|------|
| 最高 | `()` `[]` `.` | 函数调用、数组下标、结构体成员 |
| | `-` `!` `~` `*` `&` | 一元运算符（负号、逻辑非、按位取反、解引用、取地址） |
| | `*` `/` `%` | 乘法、除法、取余 |
| | `+` `-` | 加法、减法 |
| | `<<` `>>` | 左移、右移（通过运行时库实现） |
| | `<` `>` `<=` `>=` | 比较（通过运行时库实现） |
| | `==` `!=` | 相等、不等 |
| | `&` | 按位与 |
| | `^` | 按位异或 |
| | `|` | 按位或 |
| | `&&` | 逻辑与（短路） |
| | `||` | 逻辑或（短路） |
| 最低 | `=` `+=` `-=` `*=` `/=` | 赋值和复合赋值 |

### 字面量

| 格式 | 示例 | 说明 |
|------|------|------|
| 十进制 | `42`, `-1` | 有符号整数 |
| 十六进制 | `0x2A`, `0xFF` | 十六进制 |
| 字符 | `'A'`, `'\n'` | 字符常量 |
| 字符串 | `"hello\n"` | 字符串字面量（自动放入数据段） |

转义序列支持：`\n` `\r` `\t` `\0` `\\` `\"`

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

### break / continue

```c
while (1) {
    if (done) break;
    if (skip) continue;
    process();
}
```

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

### 调用约定

| 项 | 规则 |
|----|------|
| 参数传递 | R1-R8（前 8 个参数），更多参数暂不支持 |
| 返回值 | R0 |
| 寄存器保存 | R1-R8 调用者保存，R9-R13 被调用者保存 |
| 栈帧 | FP=R14，SP=R15，局部变量在 `[FP - N]` |

### 递归

```c
int fact(int n) {
    if (n == 0) {
        return 1;
    }
    return n * fact(n - 1);
}
```

---

## 变量与作用域

### 全局变量

```c
int counter = 0;   // 放在数据段，初始化为 0

int main() {
    counter = counter + 1;
    return counter;
}
```

### 局部变量

```c
int main() {
    int x = 5;           // 栈上分配
    int y = x * 2;       // 可以使用前面的变量
    return y;
}
```

### 数组

```c
int buf[16];            // 全局数组，.data 段

int main() {
    int arr[4];         // 局部数组，栈上分配
    arr[0] = 10;
    arr[1] = 20;
    return arr[0] + arr[1];
}
```

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

### 移位函数

| 函数 | 参数 | 返回值 | 说明 |
|------|------|--------|------|
| `__rt_lshift(v, s)` | R1=v, R2=s | R0=v<<s | 左移 |
| `__rt_rshift(v, s)` | R1=v, R2=s | R0=v>>s | 算术右移 |

### I/O 函数

| 函数 | 参数 | 说明 |
|------|------|------|
| `__rt_print_int(v)` | R1=v | 打印整数到 stdout（十进制+换行） |

### 内存函数

| 函数 | 参数 | 说明 |
|------|------|------|
| `__rt_memcpy(d, s, c)` | R1=dst, R2=src, R3=count | 按 4 字节拷贝 |
| `__rt_memset(d, v, c)` | R1=dst, R2=value, R3=count | 按 4 字节填充 |

---

## 系统调用

通过调用运行时包装函数使用系统调用。以下包装函数在运行时库中提供：

```c
extern void _sys_exit(int code);
extern int  _sys_getpid(void);
extern int  _sys_open(const char *path, int flags);
extern int  _sys_close(int fd);
extern int  _sys_read(int fd, char *buf, int len);
extern int  _sys_write(int fd, const char *buf, int len);
extern int  _sys_seek(int fd, int offset, int whence);
extern int  _sys_getcwd(char *buf, int size);
extern int  _sys_chdir(const char *path);
extern int  _sys_sbrk(int increment);
extern int  _sys_getenv(const char *name, char *buf, int size);
extern int  _sys_setenv(const char *name, const char *value);
extern int  _sys_unsetenv(const char *name);
extern int  _sys_stat(const char *path, char *buf);
extern int  _sys_create(const char *path, int mode);
extern int  _sys_delete(const char *path);
extern int  _sys_mkdir(const char *path, int mode);
extern int  _sys_fork(void);
extern int  _sys_exec(const char *path);
extern int  _sys_wait(int *status);
```

### 系统调用编号

| 编号 | 名称 | 说明 |
|------|------|------|
| 0 | EXIT | 退出进程 |
| 1 | FORK | 复制进程 |
| 2 | EXEC | 执行程序 |
| 3 | WAIT | 等待子进程 |
| 4 | GETPID | 获取 PID |
| 5 | OPEN | 打开文件 |
| 6 | CLOSE | 关闭文件 |
| 7 | READ | 读取 |
| 8 | WRITE | 写入 |
| 9 | SEEK | 文件定位 |
| 10 | GETCWD | 当前目录 |
| 11 | CHDIR | 切换目录 |
| 12 | SBRK | 扩展堆 |
| 13 | GETENV | 获取环境变量 |
| 14 | SETENV | 设置环境变量 |
| 15 | UNSETENV | 删除环境变量 |
| 16 | STAT | 文件状态 |
| 17 | CREATE | 创建文件 |
| 18 | DELETE | 删除文件 |
| 19 | MKDIR | 创建目录 |

---

## 内存布局

```
虚拟地址空间 (32-bit)
┌─────────────────────────────────────┐
│ 0x00000000                          │
│   代码段 (.text)                     │ ← 入口点在此
│   ↓ 增长                              │
├─────────────────────────────────────┤ 0x00000FFF (页 0)
│ 0x00001000                          │
│   数据段 (.data / .bss)              │ ← 全局变量、字符串
│   ↓ 增长                              │
├─────────────────────────────────────┤ 页边界
│                                     │
│   未映射区域                          │
│                                     │
├─────────────────────────────────────┤
│ 0x01000000                          │
│   栈 (向下增长)                       │ ← SP = 0x01000000
│   ↑ 增长                              │
│   局部变量、函数参数、返回地址          │
└─────────────────────────────────────┘
```

页面大小 = 4096 字节。每进程最多 4096 页 = 16MB 用户空间。

---

## 限制

| 限制 | 值 | 说明 |
|------|------|------|
| 最大函数参数 | 8 | 超出部分暂不支持 |
| 最大嵌套语句块 | 无限（受栈空间限制） | |
| 最大代码段 | 64 KB | 汇编器限制 |
| 最大数据段 | 64 KB | 汇编器限制 |
| 指针/解引用 | 不支持 | 后续版本添加 |
| struct | 声明支持，访问未完全实现 | 后续版本完善 |
| 浮点数 | 不支持 | |
| 多文件编译 | 不支持 | 单文件编译 |
| 头文件 `#include` | 不支持 | |
| 宏 `#define` | 不支持 | |

---

## 编译流程

```
source.c  →  [词法分析]  →  Token 流
            [语法分析]  →  AST (抽象语法树)
            [代码生成]  →  assembly (.s)
            [运行时库]  →  附加 runtime.s
            [汇编器]    →  .upx 可执行文件
```

---

## 示例

### 例 1: Hello World（通过 write 系统调用）

```c
int main() {
    _sys_write(1, "Hello, World!\n", 14);
    return 0;
}
```

编译和运行：

```
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
    if (n == 0) {
        return 1;
    }
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
        buf[i] = i * i;    // 0, 1, 4, 9, 16, 25, 36, 49
        i = i + 1;
    }
    return buf[7];    // 返回 49
}
```

### 例 5: 文件读写

```c
int main() {
    int fd;

    // 创建并写入
    _sys_create("/data.txt", 0644);
    fd = _sys_open("/data.txt", 2);    // O_WRONLY
    _sys_write(fd, "test data\n", 10);
    _sys_close(fd);

    // 读取验证
    fd = _sys_open("/data.txt", 1);    // O_RDONLY
    char buf[16];
    _sys_read(fd, buf, 10);
    _sys_close(fd);

    return 0;
}
```

### 例 6: 进程 fork

```c
int main() {
    int pid = _sys_fork();

    if (pid == 0) {
        // 子进程
        _sys_write(1, "child\n", 6);
        _sys_exit(42);
    } else {
        // 父进程
        _sys_write(1, "parent\n", 7);
        int status;
        _sys_wait(&status);    // 等待子进程
        return status;         // 返回 42
    }
}
```

---

## 与汇编的关系

C 编译器生成的汇编代码可以直接查看，用于调试：

```
cc test.c           # 生成 test.s + test.upx
cat test.s          # 查看生成的汇编
```

生成的汇编遵循以下约定：

```asm
.text
.entry main

main:
    PUSH R13        ; 保存被调用者保存的寄存器
    PUSH R12
    PUSH R11
    PUSH R10
    PUSH R9
    PUSH R14        ; 保存旧 FP
    MOV  R14, R15   ; FP = SP
    SUB  R15, R15, <局部变量大小>   ; 分配栈空间

    ; ... 函数体 ...

main_epilogue:
    MOV  R15, R14   ; 恢复 SP
    POP  R14        ; 恢复旧 FP
    POP  R9
    POP  R10
    POP  R11
    POP  R12
    POP  R13
    RET             ; 返回

.data               ; 全局变量和字符串

.stack 4096
```

---

## 相关文档

- [asm-reference.md](asm-reference.md) — 汇编语言参考
- [CLAUDE.md](../CLAUDE.md) — 项目整体说明
