# UPFS — Unix File System Simulator

模拟 UNIX 文件系统的操作系统课程设计项目。在单个虚拟磁盘镜像文件上实现完整的文件系统，包括自定义磁盘布局、i 节点元数据管理、成组链接空闲块分配、目录与路径解析，以及一个 ANSI 风格的交互相 Shell。

## 快速开始

```bash
# CMake 构建
cmake -B build -S . -DCMAKE_C_FLAGS="-D_GNU_SOURCE"
cmake --build build
./build/bin/OS_design

# 或使用 Makefile
make
./upfs
```

依赖：仅 C 标准库与 pthreads，需要 GCC 或 Clang，建议在 Linux/macOS 上运行。

## 使用方式

启动后进入交互相 Shell，常用命令：

| 命令 | 说明 |
|------|------|
| `format` | 格式化虚拟磁盘，创建文件系统 |
| `mount` | 挂载已有虚拟磁盘镜像 |
| `umount` | 卸载文件系统并保存 |
| `ls [path]` | 列出目录内容 |
| `mkdir <path>` | 创建目录 |
| `touch <path>` | 创建普通文件 |
| `cat <path>` | 读取并打印文件内容 |
| `write <path> <text>` | 向文件写入文本（覆盖） |
| `rm <path>` | 删除文件 |
| `cd <path>` | 切换当前工作目录 |
| `pwd` | 打印当前路径 |
| `help` | 显示帮助信息 |
| `exit` | 退出（自动保存） |

启动时若检测到 `vfs_disk.img` 存在，会提示可直接 mount 恢复；否则需先 format 初始化。

## 磁盘布局

共 546 个逻辑块，每块 512 字节：

| 块范围 | 大小 | 用途 |
|--------|------|------|
| 0# | 1 块 | 引导块（保留） |
| 1# | 1 块 | 超级块（魔数 `0x55504653`，空闲块/i 节点栈） |
| 2# – 33# | 32 块 | i 节点区（共 512 个 i 节点，每个 32 字节） |
| 34# – 545# | 512 块 | 数据区 |

- i 节点 0 保留，i 节点 1 为根目录（数据块 34#）
- 混合索引：8 个直接块 + 1 个一级间接块 + 1 个二级间接块，单文件最大约 32 MiB
- 空闲块采用成组链接法管理，每组最多 50 个空闲块号

## 项目结构

```
├── include/vfs_core.h    # 核心数据结构与常量（SuperBlock, DiskINode, DirEntry 等）
├── src/
│   ├── disk_io.c/.h      # 虚拟磁盘块级读写与持久化
│   ├── format.c/.h       # mkfs：超级块、空闲块栈、根目录初始化
│   ├── allocator.c/.h    # 块/i 节点分配器、内存 i 节点 Hash 缓存、挂载/卸载
│   ├── dir_sys.c/.h      # 目录管理、路径解析 (namei)、chdir/ls
│   ├── file_sys.c/.h     # 文件创建/打开/读写/关闭/删除
│   └── main.c            # 交互相 Shell 主控
├── tests/                # 测试（待补充）
├── CMakeLists.txt
└── Makefile
```

### 架构层次

```
main.c (Shell)
  └─► file_sys.c  ──► dir_sys.c  ──► allocator.c  ──► disk_io.c
      文件操作         路径解析        块/i 节点分配      块级读写
                                      mount/umount       持久化
                       format.c
                       初始化
```

## 并发设计

每个内存 i 节点（`MemINode`）在 Hash 缓存中绑定独立的 `pthread_rwlock_t`。文件操作通过 `inode_rdlock()`/`inode_wrlock()` 获取对应锁，打开文件表记录锁类型（`OF_RDLOCKED`/`OF_WRLOCKED`），`close()` 时释放。

## 注意事项

- `pragma pack(1)` 确保所有落盘结构跨平台布局一致，`_Static_assert` 编译期校验结构体大小
- 退出 Shell 时自动回写脏 i 节点与超级块并保存镜像文件
- 仓库根目录的 `vfs_disk.img` 是预构建的演示镜像（已在 `.gitignore` 中）
