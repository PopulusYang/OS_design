# 模拟实现 UNIX 文件系统 —— 说明文档

---

## 一、项目概述

本项目实现了一个模拟 UNIX 文件系统的完整系统，命名为 UPFS。在宿主操作系统上通过单个磁盘镜像文件模拟虚拟磁盘设备，在其上构建了 ext2 风格块组、XFS 风格动态 Inode 分配、Extent 变长区段映射、元数据日志和块缓存等现代文件系统机制。此外，还附带了操作系统内核层（进程管理、内存管理、虚拟机执行引擎、时间片轮转调度、系统调用接口），以更完整地呈现操作系统各子系统间的协作关系。

编程语言为 C（C17 标准），仅依赖标准 C 库和 POSIX 线程库，可在 Linux 和 macOS 上编译运行。

---

## 二、环境配置与编译运行

### 2.1 编译环境要求

| 项目 | 要求 |
|------|------|
| 操作系统 | Linux 或 macOS（需 POSIX 兼容） |
| 编译器 | GCC 或 Clang（支持 C17） |
| 构建工具 | CMake 3.10 及以上版本 |
| 线程库 | pthreads（一般系统自带） |

无需安装任何第三方库。

### 2.2 编译步骤

```
cmake -B build -S . -DCMAKE_C_FLAGS="-D_GNU_SOURCE"
cmake --build build
```

编译完成后，可执行文件位于 `build/bin/OS_design`。

### 2.3 运行方式

**交互式终端模式**（推荐验收使用）：

```
./build/bin/OS_design
```

程序启动后会自动扫描当前目录及上级目录中的磁盘镜像文件。若首次运行，需输入 `format` 命令创建文件卷；之后可通过 `mount` 命令恢复已有数据。

**Web 管理界面模式**（推荐）：

```
./build/bin/OS_design --serve        # HTTP :8080, 原始终端 :4096
./build/bin/OS_design --serve 9999   # 自定义原始终端端口
```

启动后打开浏览器访问 `http://localhost:8080`，即可进入三栏 Web 管理界面：

- **左侧**：文件浏览器 —— 树形展开目录结构，右键菜单支持新建/删除/查看文件
- **中央**：交互式终端 —— 支持多标签、ANSI 24-bit 颜色、自动滚动
- **右侧**：系统监控仪表盘 —— 块组使用率、进程列表、内存页面热力图，每 3 秒自动刷新

也可通过 `nc localhost 4096` 连接原始文本终端，多个终端可同时操作同一文件系统。

> **首次使用**：先在终端中执行 `format` 格式化磁盘，创建用户后即可在文件浏览器和仪表盘中看到实时数据。

---

## 三、文件卷组织结构

### 3.1 磁盘布局

虚拟磁盘以 512 字节为块大小，采用三段式布局：

| 区域 | 块号范围 | 块数 | 说明 |
|------|----------|------|------|
| 引导块 | 0 号 | 1 | 存放引导标识 |
| 超级块 | 1 号 | 1 | 文件系统全局管理信息 |
| 块组区 | 2～513 号 | 512 | 8 个块组，每组 64 块 |
| 日志区 | 514～545 号 | 32 | 元数据日志区 |

**总计 546 块**（约 273 KB）。

### 3.2 块组结构（ext2 风格）

磁盘数据区被划分为 **8 个块组**，每组 64 块，采用 ext2 风格的布局：

| 组内偏移 | 用途 | 说明 |
|----------|------|------|
| 第 0 块 | Anchor 块 | 存放本组的空闲块栈和链头信息 |
| 第 1～63 块 | 数据块 | 文件数据、目录数据、Inode Chunk、B+ 树节点 |

每组通过成组链接法独立管理 63 个数据块的空闲/占用状态。块分配函数 `bg_balloc_for(ino_hint)` 优先在与父 Inode 同组的块组中分配，提高数据局部性。

核心参数设定：

| 参数名 | 值 | 说明 |
|--------|-----|------|
| 块大小 | 512 字节 | |
| 块组数 | 8 | 每组 64 块 |
| 有效数据块数 | 504 | 8 × 63（扣除 anchor 块） |
| 空闲块栈容量 | 50 | 成组链接法每组最多 50 块 |
| 磁盘索引节点大小 | 32 字节 | 每块可容纳 16 个 |
| 目录项大小 | 16 字节 | 文件名 14 字节 + 节点号 2 字节 |
| 每用户最大打开文件数 | 20 | |
| 系统打开文件表容量 | 40 | |
| 最大用户数 | 10 | |

### 3.3 成组链接法

在每个块组内部，空闲数据块采用成组链接法管理，工作原理如下：

**分配 `bg_balloc_for()`**：从目标块组的空闲块栈顶弹出一个块号。若栈空，则从链表中读取下一个登记块，将其记录的一组块号装入栈中，继续弹出。若本组已无空闲块，则遍历其他块组寻找。

**回收 `bg_bfree()`**：将释放的块号压入对应块组的栈顶。若栈已满（已有 50 个），则将当前栈中所有块号写入被释放块（该块成为新的登记块），清空栈，仅保留该登记块自身。Inode Chunk 块不可通过 `bg_bfree` 释放，防止误回收。

### 3.4 超级块结构

超级块存放在磁盘 1 号块，包含以下关键字段：

- **魔数**（`s_magic`）：用于挂载时校验文件系统有效性
- **Inode 统计**：总数（`s_inode_total`）、空闲数（`s_inode_free_count`）、下一个分配号（`s_inode_next`）
- **块统计**：总数（`s_block_total`）、空闲数（`s_block_free_count`）
- **块组描述符表**（`s_bg_table[8]`）：每组的 anchor 块号、数据起始块号、数据块数
- **动态 Inode 映射**：Loc 树根（`s_imap_loc_root`、`s_imap_loc_level`）和 Chunk 树根（`s_imap_chk_root`、`s_imap_chk_level`）
- **Inode 回收栈**（`s_imap_free_stack[32]`）：缓存的已释放 Inode 号

### 3.5 动态 Inode 分配（XFS 风格）

本系统**取消了固定的 Inode 区域**，采用 XFS 风格的按需动态分配：

**Inode Chunk**：一个数据块作为 Inode Chunk，可容纳 16 个磁盘索引节点（512 / 32 = 16）。当需要新 Inode 时，从空闲数据块中分配一个 Chunk，格式化为全零后使用。

**两棵 B+ 树管理映射**：
- **Loc 树**（叶块 magic `IMLF`）：`inode号 → (chunk块号, slot偏移)`，用于快速定位任意 Inode 在磁盘上的物理位置
- **Chunk 树**（叶块 magic `IMCK`）：`chunk块号 → 16位空闲掩码`，用于快速找到有空闲槽位的 Chunk

**Loc 树支持叶节点分裂**：当单个叶块装满 63 条记录时，自动分裂为两个叶块并创建 B+ 树索引层，理论可管理数千个 Inode。

**分配流程**（`inomap_ialloc_for`）：
1. 优先从回收栈弹出已释放的 Inode 号
2. 若栈空，通过 Chunk 树查找已有 Chunk 中的空闲槽位（优先与父 Inode 同块组）
3. 若无空闲槽位，分配新的数据块作为 Inode Chunk

这种设计突破了文件数量的硬性上限——不再受格式化时写死的 Inode 数约束，理论上限约 504 × 16 ≈ 8064 个文件/目录。

### 3.6 磁盘索引节点

每个文件或目录对应一个 32 字节的磁盘索引节点（`DiskINode`），包含：

| 字段 | 类型 | 说明 |
|------|------|------|
| 文件类型与权限（`d_mode`） | 2 字节 | 目录/普通文件标识，读写执行权限位 |
| 硬链接计数（`d_nlink`） | 2 字节 | 指向该节点的目录项数量 |
| 文件长度（`d_size`） | 4 字节 | 以字节为单位 |
| 所有者用户号（`d_uid`） | 2 字节 | |
| 所有者组号（`d_gid`） | 2 字节 | |
| 内联 Extent（`d_extent`） | 8 字节 | 首个 extent：逻辑块号、物理块号、块数 |
| Extent 树根（`d_tree_root`） | 2 字节 | 指向 extent B+ 树根块（0 表示仅使用内联 extent） |
| Extent 树层数（`d_tree_level`） | 2 字节 | |
| 保留字段 | 8 字节 | |

### 3.7 Extent 变长区段映射

本系统采用 **Extent** 机制替代传统的直接/间接块号索引，每个 Extent 表示一段连续的逻辑到物理块映射 `(逻辑块号, 物理块号, 块数)`。

**内联 Extent**：每个 Inode 中内置一个 Extent，无需额外存储即可描述一段连续分配。对于小文件（通常只占 1～2 块），无需建树。

**Extent B+ 树**：当文件跨越多个不连续区段时，自动建立 Extent B+ 树。叶块（magic `XTLF`）最多存储 62 个 Extent 条目，索引块（magic `XTIX`）用于多层树结构。**相邻的逻辑/物理块号区段会自动合并**，减少 Extent 条目数。

优点：
- 连续写入的文件通常只需一个 Extent，元数据开销极小
- 支持大文件的高效块寻址
- 空间回收时按区段批量释放

### 3.8 块缓存（LRU + 哈希）

系统实现了全局块缓存（`buf.c`），采用 LRU 替换 + 哈希查找双重组织：

- **64 个缓存槽位**，每个缓存一个 512 字节的磁盘块
- **哈希表**（32 桶）按 `(dev, blockno)` 查找，命中时无需磁盘 I/O
- **LRU 双向链表**：最近使用的块移至链头，需要淘汰时从链尾选取无引用的块
- **延迟写**（`bdwrite`）：标记脏块，等到被淘汰时或显式刷盘时才写回

所有上层文件操作（`read_block` / `write_block`）均通过块缓存完成，显著减少磁盘 I/O 次数。

### 3.9 元数据日志

系统实现了元数据日志（`journal.c`），日志区占据磁盘 514～545 号块（32 块）：

**写入流程**：
1. `journal_begin()`：开始事务
2. `journal_log_block()`：将待写入的块数据记录到日志
3. `journal_commit()`：先将日志写盘，再将数据写到最终位置，最后标记事务已提交

**元数据保护**：超级块、块组 Anchor 块、Inode Chunk 块等关键元数据通过日志写入；普通数据块也经过日志（`journal_write_dir_block`），确保在意外断电时数据一致性。

**挂载回放**：`fs_mount()` 时自动调用 `journal_replay()`，检查未完成的事务并重放日志，恢复到一致状态。

### 3.10 内存索引节点与哈希缓存

磁盘索引节点被读入内存后，封装为内存索引节点（`MemINode`），增加以下管理信息：

- 磁盘节点号
- 引用计数
- 脏标志（是否需要回写磁盘）
- 读写锁（支持并发访问）

内存中维护 128 个哈希桶，以节点号取模为键，链地址法解决冲突。缓存池容量 2048 个。`iget()` 获取节点时优先查缓存，命中则增加引用计数；`iput()` 释放节点时递减计数，归零后若为脏则回写磁盘，若链接计数为零则回收该节点。

### 3.11 两层打开文件表

本系统实现了课本所述的两层打开文件表结构：

**用户打开文件表**：每个用户持有一张，最多 20 个表项，每项记录文件读写位置、打开模式、关联的内存索引节点。用户通过文件描述符（0～19 的整数）访问。

**系统打开文件表**：全局唯一，最多 40 个表项。每当用户打开文件时，同时在系统表中分配一个表项，记录文件偏移量、打开模式、引用计数和索引节点指针。多个用户打开同一文件时共享同一系统表项，引用计数随之增减。关闭文件时递减引用计数，归零则释放表项。

### 3.12 目录结构

目录是一种特殊文件，其数据内容由定长 16 字节的目录项组成。每个目录项包含 2 字节的索引节点号和 14 字节的文件名。节点号为零表示该目录项空闲。

每个目录至少包含 `.`（指向自身）和 `..`（指向父目录）两个条目。

---

## 四、全部功能说明

### 4.1 系统管理命令

| 命令 | 格式 | 功能说明 |
|------|------|----------|
| format | `format [镜像路径]` | 格式化虚拟磁盘，初始化块组、日志区、动态 Inode 映射、根目录，创建系统目录，注入预置程序，提示创建首个用户 |
| mount | `mount [镜像路径]` | 挂载已有磁盘镜像，回放日志，恢复块组和 Inode 映射状态，若已有用户则要求登录验证 |
| umount | `umount` | 卸载文件系统，保存用户数据、环境变量，回写全部脏数据，同步块组 Anchor 和超级块，持久化到镜像文件 |

### 4.2 目录操作命令

| 命令 | 格式 | 功能说明 |
|------|------|----------|
| mkdir | `mkdir <路径> [权限]` | 创建目录，默认权限 0755。自动写入 `.` 和 `..` 条目，更新父目录链接计数 |
| cd | `cd [路径]` | 切换当前工作目录，无参数或 `~` 表示回到家目录，支持 `.` 和 `..` 的解析 |
| pwd | `pwd` | 显示当前工作目录的绝对路径 |
| ls | `ls [路径]` | 列出目录内容，显示节点号、文件类型、权限、大小和名称 |

### 4.3 文件操作命令

| 命令 | 格式 | 功能说明 |
|------|------|----------|
| create | `create <路径> [权限]` | 创建普通文件，默认权限 0644 |
| write | `write <路径> <内容>` | 向文件写入文本数据 |
| cat | `cat <路径>` | 显示文件内容 |
| rm | `rm <路径>` | 删除文件，递减硬链接计数，归零时释放全部 Extent 数据块和索引节点 |
| cp | `cp <源路径> <目标路径>` | 复制文件，逐块读取源文件内容并写入新创建的目标文件 |
| ln | `ln <已有文件> <链接名>` | 创建硬链接，在目标目录中新增一个目录项指向同一索引节点，递增链接计数 |
| stat | `stat <路径>` | 显示文件详细信息：节点号、类型、权限、链接数、大小、所有者 |
| chmod | `chmod <路径> <权限>` | 修改文件权限（八进制），仅文件所有者或超级用户可操作 |

### 4.4 用户管理命令

| 命令 | 格式 | 功能说明 |
|------|------|----------|
| useradd | `useradd <用户名> <口令>` | 添加新用户，自动分配用户号（普通用户从 1000 起编），创建家目录 |
| login | `login <用户名> <口令>` | 切换到指定用户，验证口令后更新当前工作目录为该用户家目录 |
| su | `su [用户名]` | 切换用户身份（默认切到超级用户），需输入目标用户口令 |
| logout | `logout` | 登出当前用户，恢复为超级用户身份 |
| whoami | `whoami` | 显示当前登录的用户名 |
| passwd | `passwd <用户名> <新口令>` | 修改用户口令，仅超级用户或用户本人可操作 |
| users | `users` | 列出所有注册用户的用户名、用户号和家目录 |

口令安全机制：使用随机盐值和万次迭代哈希运算，口令以哈希形式存储于 `/etc/passwd`，不可逆推出明文。

### 4.5 权限控制

每个文件和目录的索引节点中存储了 9 位权限位（所有者读/写/执行、同组读/写/执行、其他人读/写/执行），遵循 UNIX 的三级权限模型。

`vfs_access()` 函数根据当前用户的身份（用户号和组号），按照所有者、同组、其他人的优先级依次匹配权限位，判断是否具有指定的访问权限。超级用户不受权限限制。

### 4.6 进程与程序执行命令

| 命令 | 格式 | 功能说明 |
|------|------|----------|
| asm | `asm <源文件> [输出文件]` | 将汇编源码编译为可执行文件 |
| cc | `cc <源文件> [输出文件]` | 将 C 源码编译为可执行文件 |
| run | `run <程序路径>` | 运行指定的可执行程序 |
| ps | `ps` | 列出当前所有进程的编号、名称和状态 |
| kill | `kill <进程号> [信号]` | 向进程发送信号 |
| mkfifo | `mkfifo <路径>` | 创建命名管道 |
| 管道 | `命令1 \| 命令2 [...]` | 管道连接，最多支持 8 级 |

系统配备了 32 位精简指令集虚拟机，可执行自定义格式的程序。调度器采用时间片轮转算法，每个进程分配 100 条指令的执行时间片，到期后切换至就绪队列中的下一个进程。

### 4.7 环境变量命令

| 命令 | 格式 | 功能说明 |
|------|------|----------|
| env | `env` | 显示所有环境变量 |
| export | `export 变量名=值` | 设置环境变量 |
| unset | `unset 变量名` | 删除环境变量 |

环境变量分为系统级（`/etc/environment`，全局共享）和用户级（`~/.env`，各用户独立）两层，查询时用户级优先。

### 4.8 调试查看命令

| 命令 | 格式 | 功能说明 |
|------|------|----------|
| design_debug super | 查看超级块详情 | 显示魔数、块组状态、动态 Inode 映射（Loc/Chunk B+ 树） |
| design_debug inodes | 查看节点缓存 | 显示内存节点池使用情况和各活跃节点详情 |
| design_debug sof | 查看系统打开文件表 | 显示当前打开文件的表项及其关联信息 |
| design_debug memory | 查看内存使用 | 显示物理页框分配情况 |
| design_debug process | 查看进程详情 | 显示进程控制块、寄存器状态、段布局 |
| design_debug all | 查看全部 | 依次输出以上所有信息 |

### 4.9 其他命令

| 命令 | 功能说明 |
|------|----------|
| help | 显示命令列表 |
| clear | 清屏 |
| exit | 退出系统 |

---

## 五、核心数据结构间的关系

```
用户打开文件表 (每用户 20 项)          系统打开文件表 (全局 40 项)
┌──────────────────────┐          ┌──────────────────────┐
│ fd=0 → 系统表项 [k]  │ ───────► │ [k] 偏移量 / 模式    │
│ fd=1 → 系统表项 [m]  │          │     引用计数 / 标志   │
│ ...                  │          │     索引节点指针 ─────┼──┐
└──────────────────────┘          └──────────────────────┘  │
                                                            ▼
                                     内存索引节点表 (哈希缓存 128 桶)
                                  ┌──────────────────────────┐
                                  │ 节点号 / 引用计数 / 脏标志 │
                                  │ 磁盘索引节点副本:          │
                                  │   类型 / 权限 / 大小       │
                                  │   Extent 映射             │
                                  │   Extent B+ 树根          │
                                  └──────────────────────────┘
                                              │
                                              ▼ inomap_write_disk_inode
                                  动态 Inode Chunk (数据块中)
```

```
超级块 (1 号块)
┌─────────────────────────────────────────────────────┐
│ 块组描述符表 ──► 8 个块组，各自维护空闲块栈          │
│                                                     │
│ Inode Loc B+ 树根 ──► ino → (chunk, slot) 映射      │
│ Inode Chunk B+ 树根 ──► chunk → 空闲掩码            │
│ Inode 回收栈 ──弹出──► 重用已释放的 Inode 号        │
│                                                     │
│ 块总数 / 空闲块数 / Inode 总数 / 空闲 Inode 数      │
└─────────────────────────────────────────────────────┘
```

```
块组 Anchor (每组第 0 块)           数据块 (每组第 1~63 块)
┌──────────────────────┐     ┌─────────────────────────────┐
│ 空闲块栈 [0..49]     │     │ 目录数据块                   │
│ 栈空时 ← 读取链指针  │     │ 文件数据块                   │
│ 指向的登记块          │     │ Inode Chunk (16 个 DiskINode)│
│                      │     │ Extent B+ 树叶/索引块        │
│ 成组链接法管理        │     │ Inode Loc/Chunk B+ 树块      │
└──────────────────────┘     └─────────────────────────────┘
```

---

## 六、格式化所做的工作

执行 `format` 命令时，系统依次完成以下步骤：

1. 在内存中分配一块与虚拟磁盘等大的空间（546 × 512 字节）
2. 写入引导块（0 号块），标记引导标识
3. 初始化 8 个块组（`bg_format_init`）：构建每组的 Anchor 块和空闲块栈（成组链接法）
4. 初始化日志区（`journal_init_format`）：清零日志头和日志数据块
5. 初始化动态 Inode 映射（`inomap_format_init`）：分配首个 Inode Chunk，创建根 Inode（inode 1），建立 Loc 树和 Chunk 树的初始叶块
6. 同步块组 Anchor（`bg_sync`）：将内存中的空闲块栈状态写回磁盘，确保 Inode 映射占用的块不被误分配
7. 汇总并写入超级块（1 号块）
8. 创建根目录：写入 `.` 和 `..` 两个目录项
9. 将内存中的磁盘镜像保存到宿主机文件
10. 自动挂载刚创建的文件系统
11. 创建系统目录结构（`/bin`、`/etc`、`/home`、`/root`、`/src` 等）
12. 初始化用户数据库
13. 将预置的演示程序注入 `/bin` 目录和 `/src` 目录
14. 设置系统环境变量（`PATH=/bin:/usr/bin`）
15. 提示创建首个用户账户

---

## 七、影响可管理文件长度的因素

1. **Extent 映射**：内联 Extent 可直接描述一段连续块；当有多个不连续区段时，Extent B+ 树叶块最多存储 62 个条目，每个条目描述一段连续分配
2. **块大小**：512 字节，块越大则单个 Extent 覆盖范围越大
3. **块号宽度**：本系统使用 16 位块号，最多寻址 65536 个块
4. **数据区块数**：本系统有效数据块 504 块，扣除系统使用后，实际可用约 460+ 块
5. **文件长度字段宽度**：使用 32 位无符号整数记录文件字节数，理论上限为 4 GB
6. **Extent 合并**：相邻的逻辑和物理块号会自动合并为一个 Extent，连续写入的文件通常只需 1 个 Extent 条目

---

## 八、运行结果展示

### 8.1 首次启动与格式化

启动程序后，系统自动扫描磁盘镜像。首次运行时，提示执行格式化：

```
╔══════════════════════════════════════════════════════════╗
║  UPFS . Unix File System Simulator                       ║
╚══════════════════════════════════════════════════════════╝

  Scanning for disk image...

  No disk image found. Run:
    format  — create testimg/vfs_disk.img

  Type  help  for command list
upfs:(unmounted) > format
  Format complete

  Create the first user account
Username: testuser
Password: ******
Set root password: ******

  User created and logged in
testuser:~ >
```

格式化完成后，自动挂载文件系统，创建首个用户并登录。

### 8.2 目录操作

```
testuser:~ > mkdir /testdir
  Directory created

testuser:~ > ls /
inode  type mode     size  name
-----  ---- ------ --------  ----
    1  d    000700      128  .
    1  d    000700      128  ..
    2  d    000755      112  bin
    3  d    000755       48  home
    4  d    000700       32  root
    5  d    000755       64  etc
   12  d    000755      256  src
   29  d    000755       32  testdir

testuser:~ > cd /testdir
  Directory changed

testuser:/testdir > pwd
/testdir
```

### 8.3 文件基本操作

创建文件、写入内容、查看内容：

```
testuser:/testdir > create /testdir/hello.txt
  File created

testuser:/testdir > write /testdir/hello.txt HelloWorld
  Write successful

testuser:/testdir > cat /testdir/hello.txt
HelloWorld
```

### 8.4 文件状态与权限

```
testuser:/testdir > stat /testdir/hello.txt

  File:     /testdir/hello.txt
  Inode:    30
  Type:     regular file
  Mode:     000644
  Links:    1
  Size:     10 bytes
  Owner:    uid=1000 gid=1000

testuser:/testdir > chmod /testdir/hello.txt 755
  Mode changed

testuser:/testdir > stat /testdir/hello.txt

  File:     /testdir/hello.txt
  Inode:    30
  Type:     regular file
  Mode:     000755
  Links:    1
  Size:     10 bytes
  Owner:    uid=1000 gid=1000
```

### 8.5 文件复制与硬链接

```
testuser:/testdir > cp /testdir/hello.txt /testdir/copy.txt
  File copied

testuser:/testdir > ln /testdir/hello.txt /testdir/link.txt
  Hard link created

testuser:/testdir > stat /testdir/hello.txt

  File:     /testdir/hello.txt
  Inode:    30
  Type:     regular file
  Mode:     000755
  Links:    2
  Size:     10 bytes
  Owner:    uid=1000 gid=1000

testuser:/testdir > ls /testdir
inode  type mode     size  name
-----  ---- ------ --------  ----
   29  d    000755       80  .
    1  d    000700      128  ..
   30  -    000755       10  hello.txt
   31  -    000644       10  copy.txt
   30  -    000755       10  link.txt
```

`hello.txt` 和 `link.txt` 共享同一索引节点（30），链接计数为 2。

### 8.6 文件删除与 Inode 回收重用

```
testuser:/testdir > rm /testdir/copy.txt
  File deleted

testuser:/testdir > ls /testdir
inode  type mode     size  name
-----  ---- ------ --------  ----
   29  d    000755       80  .
    1  d    000700      128  ..
   30  -    000755       10  hello.txt
   30  -    000755       10  link.txt
```

删除文件后，被释放的 Inode 号进入回收栈。下次创建新文件时优先重用已释放的号码。

### 8.7 超级块与动态 Inode 映射状态

```
testuser:/ > design_debug super

  ── Disk SuperBlock ──────────────────────────────
  Magic:              0x55504653  (valid)
  Inodes total:       31
  Inodes free:        0
  Blocks total:       504
  Blocks free:        459
  Block groups:       8
  Next inode:         32

  ── Block Groups (8) ───────────────────────────
  BG0   anchor=2  data[3..66)  free_blk=18
  BG1   anchor=66  data[67..130)  free_blk=63
  BG2   anchor=130  data[131..194)  free_blk=63
  ...

  ── Dynamic Inode Map ────────────────────────────
  Next inode:         32
  Inodes in use:      31
  Inodes free (pool): 0
  Loc tree root:      51 (level 0)
  Chunk tree root:    52 (level 0)
  Max per chunk:      16 inodes
```

可以看到：8 个块组各自独立管理空闲块，动态 Inode 映射通过 Loc 树（根块 51）和 Chunk 树（根块 52）管理所有 Inode 的物理位置。

### 8.8 再次启动与恢复

退出后重新启动程序，使用 `mount` 恢复之前的文件系统，所有数据均完好保存：

```
upfs:(unmounted) > mount

  Login required

Username: testuser
Password: ******

  Login successful
testuser:~ > cat /testdir/hello.txt
HelloWorld

testuser:~ > ls /testdir
inode  type mode     size  name
-----  ---- ------ --------  ----
   29  d    000755       80  .
    1  d    000700      128  ..
   30  -    000755       10  hello.txt
   30  -    000755       10  link.txt
```

数据、权限、硬链接关系全部完好保存。

---

## 九、源码结构

```
src/
  main.c              # 交互式 shell 入口
  binaries.h/c        # 预置演示程序二进制（.upx 格式）
  assembler.h/c       # 两遍汇编器：.s 源码 → .upx 二进制
  serve.h/c           # TCP 多终端服务器（HTTP + WebSocket + raw TCP）
  web_api.c           # JSON API 会话：解析请求、调用 VFS/内核接口、序列化 JSON 响应
  web_page.h          # 内嵌的三栏 Web UI 页面（HTML/CSS/JS）
  editor.h/c          # 内置文本编辑器
  fs/                 # 文件系统层
    disk_io.h/c       # 块级读写、磁盘持久化（mmap）
    format.h/c        # mkfs：初始化块组、日志、Inode 映射、根目录
    bg.h/c            # ext2 风格块组：每组 anchor + 成组链接法空闲管理
    inomap.h/c        # XFS 风格动态 Inode：Chunk + Loc/Chunk B+ 树
    extent.h/c        # Extent 变长区段映射 + B+ 树
    buf.h/c           # 全局块缓存（LRU + 哈希，64 槽）
    journal.h/c       # 元数据日志（事务、提交、挂载回放）
    vfs.c + vfs.h     # VFS 抽象层，操作表分发
    allocator.h/c     # 块/Inode 分配、Inode 缓存、mount/umount
    dir_sys.h/c       # 路径解析（namei）、mkdir、chdir、ls
    file_sys.h/c      # 文件 create/open/read/write/close/delete
  kernel/             # 内核层
    memory.h/c        # 128MB 物理内存，页面分配器（4KB 页）
    cpu.h/c           # 32 位 RISC 虚拟机：21 条指令
    process.h/c       # PCB、进程表、fork/exec/wait/exit、UPX 加载器
    scheduler.h/c     # 时间片轮转调度器（100 条指令/时间片）
    syscall.h/c       # 20 个系统调用
    pipe.h/c          # 管道通信
    ipc.h/c           # 进程间通信
    kernel_shared.h/c # 内核共享状态
  user/               # 用户管理层
    user_mgmt.h/c     # 多用户账户、口令哈希、/etc/passwd
    env.h/c           # 环境变量：系统级 + 用户级，文件持久化
  compiler/           # 内置 C 编译器
    c2s.c             # C 到汇编的编译驱动
    lexer.c           # 词法分析
    parser.c          # 语法分析
    codegen.c         # 代码生成
    regalloc.c        # 寄存器分配
    ast.c             # 抽象语法树
include/
  vfs_core.h          # 共享常量、磁盘结构体、运行时类型定义
```

---

## 十、Web 管理界面

### 10.1 架构

```
浏览器 (http://localhost:8080)
├── GET /         → HTTP 响应 → 三栏单页应用（HTML/CSS/JS 内嵌于 C 源码）
├── /ws/N         → WebSocket → term_spawn() → upfs_session()   [交互终端]
└── /api          → WebSocket → api_spawn()  → upfs_api_session() [JSON API]
```

`--serve` 模式下，`serve.c` 作为父进程运行事件循环（`poll`），对每个新连接 `fork` 子进程：
- 终端连接（`/ws/N`）的子进程运行 `upfs_session()`，与交互式模式完全相同
- API 连接（`/api`）的子进程运行 `upfs_api_session()`，接收 JSON 请求并返回结构化 JSON 响应

父子进程通过 `socketpair` 传递 I/O，通过 `mmap` 共享内核状态（进程表、内存、调度器）和磁盘路径。

### 10.2 三栏布局

| 区域 | 功能 | 数据来源 |
|------|------|----------|
| 左侧面板 | 文件浏览器：树形目录、点击展开/折叠、右键菜单（新建/删除/查看/stat） | `/api` WebSocket |
| 中央区域 | 交互终端：多标签、ANSI 全彩解析、自动滚动、命令输入 | `/ws/N` WebSocket |
| 右侧面板 | 监控仪表盘：块组使用率图、进程列表、内存热力图，3 秒自动刷新 | `/api` WebSocket |

### 10.3 JSON API 命令集

| 命令 | 请求示例 | 响应说明 |
|------|----------|----------|
| 列目录 | `{"cmd":"ls","path":"/"}` | `entries` 数组，含 name/type/ino/mode/size |
| 文件属性 | `{"cmd":"stat","path":"/foo"}` | ino/mode/size/type/nlink/uid/gid |
| 读文件 | `{"cmd":"cat","path":"/foo"}` | `data` 字段含文件文本内容 |
| 创建目录 | `{"cmd":"mkdir","path":"/new"}` | `ok:true` |
| 创建文件 | `{"cmd":"create","path":"/f"}` | `ok:true` |
| 删除 | `{"cmd":"rm","path":"/f"}` | `ok:true` |
| 超级块 | `{"cmd":"debug","type":"super"}` | 超级块信息 + 8 组块组统计（含各组空闲块数） |
| 进程表 | `{"cmd":"debug","type":"process"}` | 所有活跃进程的 PID/名称/状态 |
| 内存 | `{"cmd":"debug","type":"memory"}` | 总页数/已用页数/页面位图（1024 采样点） |

### 10.4 设计决策

- **零外部依赖**：前端 HTML/CSS/JS 全部内嵌在 C 字符串中，无需 CDN 或外部库
- **自实现 ANSI 解析**：支持 24-bit RGB 颜色（SGR 38/48;2;R;G;B）、粗体、暗淡等样式
- **手写 JSON 解析器**：仅处理 `{"key":"value"}` 扁平对象，约 60 行 C 代码
- **文件浏览器使用 API 通道**：避免解析终端 ANSI 彩色输出，直接获取结构化数据

---

## 十一、主要数据结构间的关系总结

系统运行时涉及的核心数据结构及其关联关系如下：

1. **用户结构**持有用户标识、当前目录节点号和用户打开文件表
2. **用户打开文件表**中每个表项通过指针指向**系统打开文件表**中的对应表项
3. **系统打开文件表**中每个表项记录文件偏移量和引用计数，并通过指针指向**内存索引节点**
4. **内存索引节点**通过哈希表组织缓存，包含磁盘索引节点的副本；回写时通过 **Inode Loc B+ 树**定位物理位置，经**元数据日志**写入磁盘
5. **磁盘索引节点**中的 **Extent 映射**（内联 + B+ 树）指向数据区中的实际数据块
6. **块组 Anchor 块**中的空闲块栈管理各组数据块的分配回收（成组链接法）
7. **超级块**汇总全局统计信息和动态 Inode 映射树的根节点
8. **目录文件**的数据块中存放目录项数组，每个目录项通过节点号关联到对应的索引节点
9. **块缓存层**（LRU + 哈希）透明加速所有上层的块读写操作

以上各层结构自顶向下形成了从用户操作到物理存储的完整映射链路。
