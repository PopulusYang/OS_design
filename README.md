# UPFS — Unix File System Simulator

模拟 UNIX 文件系统的操作系统课程设计项目。在虚拟磁盘镜像上实现完整文件系统（自定义布局、i 节点、成组链接空闲块、目录与路径解析），并在此基础上扩展多用户 Shell、虚拟内核（进程/内存/调度/系统调用）、汇编器与 TCP 多终端服务。

## 快速开始

```bash
# CMake 构建（推荐）
cmake -B build -S . -DCMAKE_C_FLAGS="-D_GNU_SOURCE"
cmake --build build
./build/bin/OS_design

# 或使用 Makefile
make
./upfs
```

依赖：C17 编译器（GCC / Clang）、pthread。当前主要在 **Linux / macOS** 上开发与运行（使用 `fork`、`mmap`、`termios` 等 POSIX 接口）。

默认虚拟盘路径：`testimg/vfs_disk.img`（运行时自动创建 `testimg/` 目录）。

## 使用方式

启动后进入交互相 Shell。首次使用需 `format` 格式化，或 `mount` 挂载已有镜像。

### 文件系统

| 命令 | 说明 |
|------|------|
| `format [path]` | 格式化虚拟磁盘 |
| `mount [path]` | 挂载镜像（`restore` 同义） |
| `umount` | 卸载并保存 |
| `ls [path]` | 列出目录 |
| `mkdir <path>` | 创建目录 |
| `create <path>` | 创建普通文件 |
| `cat <path>` | 打印文件内容 |
| `write <path> <text>` | 写入文本（覆盖） |
| `rm <path>` | 删除文件 |
| `cd <path>` | 切换目录 |
| `pwd` | 打印当前路径 |

### 用户与环境

| 命令 | 说明 |
|------|------|
| `login` | 登录 |
| `logout` | 登出 |
| `su <user>` | 切换用户 |
| `useradd <name> <passwd>` | 添加用户 |
| `passwd [user]` | 修改口令 |
| `whoami` / `users` | 当前用户 / 用户列表 |
| `export KEY=VALUE` | 设置环境变量 |
| `env` / `unset KEY` | 查看 / 删除环境变量 |

### 进程与程序

| 命令 | 说明 |
|------|------|
| `run <binary>` | 运行预置或已编译程序 |
| `cmd1 \| cmd2 [\| ...]` | 管道连接 2–8 个程序 |
| `kill <pid> [sig]` | 发送信号（9=SIGKILL，15=SIGTERM，10=SIGUSR1） |
| `mkfifo </path>` | 创建命名 FIFO |
| `ps` | 查看进程 |
| `asm <file.asm> [out]` | 汇编 |
| `vim <file>` | 简易编辑器 |

虚拟内核 IPC（System V 风格）：**信号量、消息队列、共享内存、命名 FIFO、信号**；详见 `doc/asm-reference.md` 与 `involve_src/ipc*.asm`。

### 其他

| 命令 | 说明 |
|------|------|
| `help` | 显示帮助 |
| `clear` | 清屏 |
| `exit` / `quit` | 退出（自动保存） |

## 多终端服务

```bash
# 启动 TCP 服务端（默认端口 4096）
./upfs --serve
./upfs --serve 8080

# 编译 HTTP/WebSocket 辅助服务（可选）
make -C script
```

`--serve` 模式下各终端子进程通过 `mmap` 共享内核状态；HTTP/WebSocket 终端见 `script/websrv.c`。

## 磁盘布局

共 546 个逻辑块，每块 512 字节：

| 块范围 | 大小 | 用途 |
|--------|------|------|
| 0# | 1 块 | 引导块（保留） |
| 1# | 1 块 | 超级块（魔数 `0x55504653`） |
| 2# – 33# | 32 块 | i 节点区（512 个 i 节点，各 32 字节） |
| 34# – 545# | 512 块 | 数据区 |

- i 节点 0 保留，i 节点 1 为根目录（数据块 34#）
- 混合索引：8 直接 + 1 一级间接 + 1 二级间接，单文件最大约 32 MiB
- 空闲块采用成组链接法，每组最多 50 块

## 项目结构

```
├── include/
│   ├── vfs_core.h          # 磁盘布局、SuperBlock、DiskINode、DirEntry
│   └── kernel_shared.h     # 多终端共享内核状态
├── src/
│   ├── fs/                 # 文件系统层
│   │   ├── disk_io.c       # 块级读写与镜像持久化
│   │   ├── format.c        # mkfs 初始化
│   │   ├── allocator.c     # 块/i 节点分配、iget/iput、挂载
│   │   ├── dir_sys.c       # 目录、namei、mkdir、ls
│   │   └── file_sys.c      # vfs_create/open/read/write/close/delete
│   ├── kernel/             # 虚拟内核
│   │   ├── memory.c        # 分页内存
│   │   ├── cpu.c           # 指令执行
│   │   ├── process.c       # 进程管理
│   │   ├── scheduler.c     # 时间片调度
│   │   ├── syscall.c       # 系统调用
│   │   └── kernel_shared.c # mmap 共享内核
│   ├── user/               # 用户子系统
│   │   ├── user_mgmt.c     # /etc/passwd、口令哈希
│   │   └── env.c           # 环境变量
│   ├── main.c              # Shell 主控、upfs_session()
│   ├── serve.c             # TCP 多终端服务
│   ├── assembler.c         # 汇编器
│   ├── editor.c            # 简易编辑器
│   └── binaries.c          # 预置程序
├── script/                 # websrv 辅助服务源码
├── involve_src/            # 示例汇编程序
├── doc/                    # 汇报与验收文档
├── tests/                  # 自动化测试（待补充）
├── CMakeLists.txt
└── Makefile
```

### 架构层次

```
main.c / serve.c (Shell / 网络会话)
  ├─► user_mgmt.c / env.c
  ├─► kernel/* (进程、内存、调度、syscall)
  └─► file_sys.c ──► dir_sys.c ──► allocator.c ──► disk_io.c
                        format.c
```

## 并发设计

- 每个内存 i 节点（`MemINode`）绑定 `pthread_rwlock_t`
- 打开文件表记录锁类型（`OF_RDLOCKED` / `OF_WRLOCKED`），`vfs_close()` 时释放
- `--serve` 模式下内核与磁盘路径通过共享内存协调多终端

## 注意事项

- `#pragma pack(1)` + `_Static_assert` 保证落盘结构跨平台一致
- 关键 Shell 命令后自动 `fs_sync_disk()`；退出时回写脏 i 节点与超级块
- `*.img` 与 `testimg/` 已在 `.gitignore` 中，镜像需本地 `format` 或 `mount` 生成
