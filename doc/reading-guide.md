# UPFS 项目代码阅读指南

两天内理解文件管理和进程管理模块的阅读路线。

## 前置：先读这一份文件（30 分钟）

**`include/vfs_core.h`**（303 行）

这是整个项目的**核心数据结构定义文件**，理解它就读懂了一半。重点关注：

| 结构体 | 作用 | 类比 |
|--------|------|------|
| `SuperBlock` | 磁盘超级块，记录 inode/block 总量、B+树根位置 | ext2 superblock |
| `DiskINode` | 磁盘上的 inode（仅32字节），内含 `Extent` | ext4 inode |
| `Extent` | `(逻辑块号, 物理块号, 长度)` 三元组，描述文件的连续磁盘区间 | ext4 extent |
| `MemINode` | 内存中的 inode，引用计数 + 锁标志 | Linux `struct inode` |
| `DirEntry` | 目录项：`(inode号, 文件名)`，16字节 | ext2 dir entry |
| `BlockGroupDesc` | 块组描述符，每组的锚块 + 数据区 | ext2 block group |
| `SysOpenFile` | 系统打开文件表项（全局40个） | Linux `struct file` |
| `OpenFileTable` | 用户打开文件表项（每用户20个） | fd table |
| `User` | 用户结构体（含工作目录 inode、打开文件表） | — |

**关键常量**：`BLOCK_SIZE=512`, `BG_COUNT=8`, `INODES_PER_CHUNK=16`

---

## 第一天：文件管理（约 3500 行代码）

按**从下到上**的层次读，每一层都建立在上一层之上。

### 第 1 层：磁盘读写（20 分钟）

**`src/fs/disk_io.h` + `src/fs/disk_io.c`**（~230 行）

- 这是最底层，一个 512 字节块设备的模拟
- 核心函数：`disk_create()` / `disk_load()` / `disk_read_block()` / `disk_write_block()`
- 整个"磁盘"实际在内存中（一个 malloc 的大数组），通过 `disk_save()` 持久化到 `.img` 文件

**读完后你应能回答**：一个 block 多大？磁盘总共多少块？数据存在哪里？

---

### 第 2 层：块组管理（40 分钟）

**`src/fs/bg.h` + `src/fs/bg.c`**（~362 行）

- ext2 风格的块组：8 组 × 64 块/组 = 512 块
- 每组的第 0 块是**锚块（anchor block）**，用成组链接法管理本组的空闲块
- 核心函数：`bg_balloc_for(ino_hint)` 分配块、`bg_bfree()` 释放块

**读完后你应能回答**：为什么要分组？`ino_hint` 参数是干什么的？

---

### 第 3 层：inode 映射 + Extent 树（1.5 小时）

**`src/fs/inomap.h` + `src/fs/inomap.c`**（~769 行）

- XFS 风格的动态 inode 分配：没有固定的 inode 区
- 每个 **Inode Chunk** = 一个数据块存 16 个 inode（512/32=16）
- 用 B+ 树做两件事：① inode号→(chunk块号, 槽位) 查找 ② 空闲 chunk 位图管理
- 核心函数：`inomap_ialloc_for()`、`inomap_lookup()`、`inomap_ifree()`

**`src/fs/extent.h` + `src/fs/extent.c`**（~470 行）

- 每个文件的数据块用 extent 树管理（类似 ext4）
- `DiskINode` 内嵌一个 `Extent`（小文件够用），大文件扩展为 B+ 树
- 核心函数：`extent_get_block()` 查找/分配、`extent_truncate()` 截断

**读完后你应能回答**：一个小文件（< 1 extent）如何映射？大文件如何扩展？

---

### 第 4 层：缓冲 + 日志（40 分钟）

**`src/fs/buf.h` + `src/fs/buf.c`**（~258 行）

- 全局块缓存（LRU + Hash），类似 Linux 的 buffer cache
- 核心函数：`bread()` 读块到缓存、`bwrite()` 写回、`bdwrite()` 延迟写

**`src/fs/journal.h` + `src/fs/journal.c`**（~227 行）

- 元数据日志，32 块日志区（block 514–545）
- 核心函数：`journal_replay()` 挂载时重放、`journal_write_commit()` 写日志+提交

---

### 第 5 层：分配器 + 挂载（40 分钟）

**`src/fs/allocator.h` + `src/fs/allocator.c`**（~603 行）

- 封装了 bg + inomap 的高层接口：`balloc()` / `bfree()` / `ialloc()` / `ifree()`
- **inode 缓存**：活跃 inode 的哈希表，`iget()` 获取 / `iput()` 释放
- 挂载/卸载：`fs_mount()` / `fs_umount()`

**读完后你应能回答**：从挂载到可以读写文件，经历了哪些步骤？

---

### 第 6 层：目录 + 文件操作（1 小时）

**`src/fs/dir_sys.h` + `src/fs/dir_sys.c`**（~697 行）

- 核心是 `namei()` — 路径解析，从 "/a/b/c.txt" 找到对应 inode
- `dir_split_path()` 把路径拆成父目录 + 文件名
- `dir_link_entry()` / `dir_unlink_entry()` 在目录块中增删目录项
- `chdir()`、`dir_list()`（即 `ls`）

**`src/fs/file_sys.h` + `src/fs/file_sys.c`**（~667 行）

- 文件 CRUD：`upfs_create()` / `upfs_open()` / `upfs_read()` / `upfs_write()` / `upfs_close()` / `upfs_unlink()`
- **两级打开文件表**：User 级（每用户 20 个 fd）→ System 级（全局 40 个）→ MemINode
- `upfs_lseek()`、`upfs_stat()`、`upfs_chmod()`、`upfs_copy()`、`upfs_link()`

---

### 第 7 层：VFS 调度层（20 分钟）

**`src/fs/vfs.c`**（~260 行）

- 函数指针表（类似 Linux VFS 的 `inode_operations` / `file_operations`）
- `vfs_upfs_register()` 把 UPFS 的操作函数注册到全局表
- 所有的 `vfs_xxx()` 函数都是简单的一行转发

---

## 第二天：进程管理（约 2500 行代码）

### 第 1 层：物理内存（30 分钟）

**`src/kernel/memory.h` + `src/kernel/memory.c`**（~179 行）

- 128MB 字节数组，分 4KB 页，共 32768 页
- 前 4096 页（16MB）保留给内核
- 位图管理空闲页：`mem_alloc_pages()` / `mem_free_pages()`
- 物理地址读写：`mem_read32()` / `mem_write32()`

**读完后你应能回答**：128MB 的布局是什么样的？内核区和用户区如何划分？

---

### 第 2 层：CPU / 虚拟机（1 小时）

**`src/kernel/cpu.h` + `src/kernel/cpu.c`**（~238 行）

- 32 位 RISC 虚拟机，18 条指令，固定 32 位编码
- `CPUContext` 结构体：16 个通用寄存器 + PC + FLAGS + 时间片剩余 tick
- `cpu_init()` 初始化上下文、`cpu_step()` 取指-译码-执行 1 条指令
- **地址转换**：`cpu_virt_to_phys()` 通过进程页表做虚拟→物理地址翻译
- 内存访问：`cpu_read32()` / `cpu_write32()` 先翻译再访问物理内存

**读完后你应能回答**：一条 `MOVI R0, 42` 指令从取指到执行完经历了什么？

---

### 第 3 层：进程管理（1.5 小时）

**`src/kernel/process.h` + `src/kernel/process.c`**（~476 行）

这是进程管理的核心，重点关注：

- **PCB 结构体**：pid、ppid、状态、CPU 上下文、页表（最多 4096 页 → 16MB 虚拟地址空间）、文件描述符表（16 个）、环境变量、子进程列表、信号
- **UPX 格式**：magic + entry + text/data/bss/stack 段描述
- `proc_create_init()` — 创建 init 进程
- `proc_fork()` — fork 子进程（复制页表、复制文件描述符）
- `proc_exec()` — 加载 UPX 可执行文件：`upx_load()` 解析头、分配页、复制段
- `proc_wait()` / `proc_exit()` — 等待子进程 / 退出
- 文件描述符：`proc_alloc_fd()` / `proc_free_fd()`，支持文件、管道、FIFO 三种类型

**读完后你应能回答**：fork + exec 的完整流程是怎样的？PCB 的页表如何映射虚拟地址？

---

### 第 4 层：调度器（30 分钟）

**`src/kernel/scheduler.h` + `src/kernel/scheduler.c`**（~149 行）

- **轮转调度（Round-Robin）**，时间片 100 条指令
- 就绪队列 + `sched_tick()` 时间片检查 + 上下文切换
- 5 种进程状态：FREE → READY → RUNNING → BLOCKED / ZOMBIE

**读完后你应能回答**：`sched_tick()` 在什么条件下触发切换？切换时保存/恢复什么？

---

### 第 5 层：系统调用（1 小时）

**`src/kernel/syscall.h` + `src/kernel/syscall.c`**（~496 行）

- 34 个系统调用
- `syscall_dispatch()` — 根据 `syscall_no` 分发到具体处理函数
- **文件类**（5–19）：OPEN/CLOSE/READ/WRITE/SEEK/CREATE/DELETE/MKDIR/STAT/GETCWD/CHDIR → 调用 VFS 层
- **进程类**（0–4, 12）：EXIT/FORK/EXEC/WAIT/GETPID/SBRK
- **环境变量**（13–15）：GETENV/SETENV/UNSETENV
- VM 和宿主之间的数据传输：`syscall_read_str()` / `syscall_write_str()` — 从 VM 虚拟地址拷贝字符串

**读完后你应能回答**：用户程序调用 `OPEN("/bin/hello", 0)` 经历了哪些步骤？

---

### 第 6 层：汇编器（可选，30 分钟）

**`src/assembler.h` + `src/assembler.c`**（~645 行）

- 两遍扫描汇编器：`.s` 汇编源码 → `.upx` 二进制
- 如果时间不够可跳过，需要时再回来看

---

## 实操建议

每读完一层后，用以下命令实际运行验证理解：

```bash
# 1. 编译运行
cmake -B build -S . -DCMAKE_C_FLAGS="-D_GNU_SOURCE" && cmake --build build
./build/bin/OS_design

# 2. 在 shell 中实验文件管理
format                    # 格式化磁盘
mount                     # 挂载
mkdir /test               # 创建目录
create /test/f.txt        # 创建文件
write /test/f.txt         # 写入内容
cat /test/f.txt           # 读取内容
ls /test                  # 列出目录
stat /test/f.txt          # 查看文件状态
design_debug all          # 查看 superblock、inode、block 等内部状态

# 3. 实验进程管理
asm doc/demo/             # 汇编 demo 程序
run /bin/hello            # 运行程序
ps                        # 查看进程列表
```

**最有效的学习方法**：在 `design_debug all` 的输出中追踪一个文件从创建到写入数据的过程中，superblock、inode、block group 各项数值的变化。

---

## 总结：依赖关系图

```
文件管理（自底向上）：
disk_io ──► bg ──► inomap ──► allocator ──► dir_sys ──► vfs.c ──► main.c (shell)
         ──► extent ──┘           buf          file_sys ──┘
                                   journal

进程管理（自底向上）：
memory ──► cpu ──► process ──► scheduler ──► syscall ──► main.c (shell)
                                     └── 调用 ──► vfs.c (文件操作)
```

建议严格按照箭头方向阅读，底层没读懂不要跳到上层。两天时间足够把这两条线完整过一遍。
