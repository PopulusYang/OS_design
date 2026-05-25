// vfs_core.h —— 模拟 UNIX 文件系统核心常量、磁盘布局与 VFS 数据结构定义
//
// 本头文件定义虚拟磁盘的几何参数、超级块/i 节点/目录项等驻留磁盘结构，
// 以及运行时内存中的 i 节点、打开文件表和用户上下文。
// 所有落盘结构使用 1 字节对齐（#pragma pack(1)），确保跨平台布局一致。

#ifndef VFS_CORE_H
#define VFS_CORE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 一、宏定义常量

// 磁盘逻辑块大小（字节）
#define BLOCK_SIZE              512

// i 节点区占用的连续块数
#define INODE_ZONE_BLOCKS       32

// 数据区占用的连续块数
#define DATA_ZONE_BLOCKS        512

// 成组链接法：每组空闲块栈的最大容量
#define MAX_FREE_BLOCKS         50

// 单个磁盘 i 节点的固定大小（字节）
#define DISK_INODE_SIZE         32

// 目录项固定大小（2 字节 i 节点号 + 14 字节文件名）
#define DIR_ENTRY_SIZE          16

// 每个用户进程上下文最多同时打开的文件数
#define MAX_OPEN_FILES          20

// 系统允许同时登录的最大用户数
#define MAX_USERS               10

// 由基本常量派生的几何参数

// 每块可容纳的 i 节点数：512 / 32 = 16
#define INODES_PER_BLOCK        (BLOCK_SIZE / DISK_INODE_SIZE)

// 磁盘 i 节点总数：32 × 16 = 512
#define TOTAL_INODES            (INODE_ZONE_BLOCKS * INODES_PER_BLOCK)

// 每块可容纳的目录项数：512 / 16 = 32
#define DIRS_PER_BLOCK          (BLOCK_SIZE / DIR_ENTRY_SIZE)

// 每个索引块可存放的逻辑块号个数（块号 16 位）：512 / 2 = 256
#define ADDRS_PER_BLOCK         (BLOCK_SIZE / (int)sizeof(uint16_t))

// 目录项中文件名的最大长度（不含 '\0'，定长域）
#define MAX_FILENAME_LEN        (DIR_ENTRY_SIZE - (int)sizeof(uint16_t))

// 虚拟磁盘总块数：引导(1) + 超级块(1) + i 节点区(32) + 数据区(512) = 546
#define TOTAL_DISK_BLOCKS       (1 + 1 + INODE_ZONE_BLOCKS + DATA_ZONE_BLOCKS)

// 引导块所在逻辑块号
#define BOOT_BLOCKNO            0

// 超级块所在逻辑块号
#define SUPERBLOCK_BLOCKNO      1

// i 节点区起始逻辑块号（2# .. k+1#，k = INODE_ZONE_BLOCKS）
#define INODE_ZONE_START        2

// 数据区起始逻辑块号
#define DATA_ZONE_START         (INODE_ZONE_START + INODE_ZONE_BLOCKS)

// 根目录 i 节点号（i 节点 0 保留不用）
#define ROOT_INODE_NO           1

// 根目录数据块号（数据区首块，格式化时被占用）
#define ROOT_DIR_BLOCK          DATA_ZONE_START

// 文件系统魔数，用于挂载时校验（"UPFS"）
#define VFS_MAGIC               0x55504653U

// 文件类型与权限位（八进制，兼容 UNIX stat 风格）

#define IFDIR                   0040000U
#define IFREG                   0100000U
#define IEXEC                   0000100U
#define IWRITE                  0000200U
#define IREAD                   0000400U

// 打开文件访问模式

#define O_RDONLY                0x0001U
#define O_WRONLY                0x0002U
#define O_RDWR                  0x0003U
#define O_APPEND                0x0008U

// 内存 i 节点状态标志位

#define MINODE_DIRTY            0x01U  // 内容已修改，需回写磁盘
#define MINODE_LOCKED           0x02U  // 正在被系统调用使用，暂不可回收

// 超级块内空闲 i 节点栈的静态容量。
// 超级块必须恰好占满一个 512 字节块；扣除固定字段与空闲块栈后，
// 剩余空间全部用于 s_inode_free_stack[]。
#define SUPERBLOCK_FIXED_SIZE   (                       \
    (int)sizeof(uint32_t) * 5 +                         \
    (int)sizeof(uint16_t) * 2 +                         \
    MAX_FREE_BLOCKS * (int)sizeof(uint16_t) +           \
    (int)sizeof(uint16_t)                                 \
)

#define INODE_FREE_STACK_SIZE   (                       \
    (BLOCK_SIZE - SUPERBLOCK_FIXED_SIZE) / (int)sizeof(uint16_t) \
)

// 二、磁盘驻留结构

#if defined(_MSC_VER)
#pragma pack(push, 1)
#else
#pragma pack(push, 1)
#endif

// SuperBlock —— 超级块，文件系统全局元数据，占用磁盘第 1 块
//
// 空闲块管理采用成组链接法：
//   s_free_block_stack[] 为当前组的栈（最多 MAX_FREE_BLOCKS 个块号）；
//   栈耗尽时，读取 s_free_block_chain 指向的登记块，载入下一组。
//
// 空闲 i 节点管理采用栈式：
//   s_inode_free_stack[] 存放空闲 i 节点号；
//   s_inode_stack_top 为栈顶下标（-1 表示空栈；由实现维护）。
typedef struct SuperBlock {
    uint32_t s_magic;                               // 文件系统魔数
    uint32_t s_inode_total;                         // i 节点总数
    uint32_t s_inode_free_count;                    // 当前空闲 i 节点个数
    uint32_t s_block_total;                         // 数据块总数
    uint32_t s_block_free_count;                    // 当前空闲数据块个数
    uint16_t s_free_block_count;                    // 空闲块栈有效条目数
    uint16_t s_free_block_chain;                    // 成组链接下一组登记块号
    uint16_t s_free_block_stack[MAX_FREE_BLOCKS];   // 当前组空闲块号栈
    uint16_t s_inode_stack_top;                     // 空闲 i 节点栈顶索引（-1 表示空栈）
    uint16_t s_inode_free_stack[INODE_FREE_STACK_SIZE]; // 空闲 i 节点号栈
} SuperBlock;

// DiskINode —— 磁盘 i 节点，描述文件元数据与块映射（固定 32 字节）
//
// 混合索引布局（块号均为 16 位，指向数据区逻辑块）：
//   d_direct[8]  : 8  个直接索引，共 8 × 512 = 4 KiB
//   d_sindirect  : 一级间接，256 × 512 = 128 KiB
//   d_dindirect  : 二级间接，256 × 256 × 512 ≈ 32 MiB
//
// 字段偏移（packed 后）：
//   [0..1]   d_mode      [2..3]   d_nlink
//   [4..7]   d_size      [8..9]   d_uid
//   [10..11] d_gid       [12..27] d_direct[8]
//   [28..29] d_sindirect [30..31] d_dindirect
typedef struct DiskINode {
    uint16_t d_mode;                    // 文件类型 OR 权限位
    uint16_t d_nlink;                   // 硬链接计数
    uint32_t d_size;                    // 文件逻辑长度（字节）
    uint16_t d_uid;                     // 所有者用户 ID
    uint16_t d_gid;                     // 所有者组 ID
    uint16_t d_direct[8];               // 直接索引：8 个数据块号
    uint16_t d_sindirect;               // 一次间址：指向索引块的块号
    uint16_t d_dindirect;               // 二次间址：指向一级间址块的块号
} DiskINode;

// DirEntry —— 目录项，定长 16 字节，将文件名映射到 i 节点号
// de_name 为定长 14 字节域，不以 '\0' 结尾时由实现按 MAX_FILENAME_LEN 截断比较；
// 若文件名不足 14 字节，剩余字节以 '\0' 填充。
typedef struct DirEntry {
    uint16_t de_inode;                  // 关联的 i 节点号；0 表示空闲目录项
    char     de_name[MAX_FILENAME_LEN]; // 文件名（最多 14 字符）
} DirEntry;

#pragma pack(pop)

// 三、运行时内存结构

struct MemINode;

// OpenFileTable —— 打开文件表项，描述一次"打开"实例的读写状态
typedef struct OpenFileTable {
    uint32_t     oft_read_pos;          // 读指针：距文件头字节偏移
    uint32_t     oft_write_pos;         // 写指针：距文件头字节偏移
    uint16_t     oft_mode;              // 打开模式（O_RDONLY 等）
    uint16_t     oft_flags;             // 附加标志（如 O_APPEND），预留扩展
    struct MemINode *oft_inode;         // 指向关联的内存 i 节点；NULL 表示空闲
} OpenFileTable;

// MemINode —— 内存 i 节点，磁盘 i 节点的运行时缓存与引用管理
//
// 在 DiskINode 基础上增加：
//   m_inode_no  : 对应磁盘 i 节点号，用于回写寻址
//   m_ref_count : 引用计数（打开文件表 / 目录硬链接遍历等持有）
//   m_flags     : 状态标志（MINODE_DIRTY / MINODE_LOCKED）
//
// 当 m_ref_count 降为零且未被锁定时，可回收该内存 i 节点槽位。
typedef struct MemINode {
    DiskINode    m_dinode;              // 磁盘 i 节点内容的内存副本
    uint16_t     m_inode_no;            // 磁盘 i 节点号（0 .. TOTAL_INODES-1）
    uint16_t     m_ref_count;           // 引用计数
    uint8_t      m_flags;               // 状态标志位
    uint8_t      m_pad[3];              // 填充至 8 字节边界，避免伪共享
} MemINode;

// User —— 用户进程上下文，模拟单用户登录后的系统调用环境
//
// u_ofile[] 为 per-process 打开文件表，下标即模拟的 fd（0 .. MAX_OPEN_FILES-1）。
// u_cdir 为当前工作目录对应的 i 节点号。
typedef struct User {
    char           u_name[32];          // 登录用户名
    uint16_t       u_uid;               // 用户 ID
    uint16_t       u_gid;               // 组 ID
    uint16_t       u_cdir;              // 当前目录 i 节点号
    uint8_t        u_active;            // 非 0 表示该用户槽位已登录
    uint8_t        u_pad;               // 对齐填充
    OpenFileTable  u_ofile[MAX_OPEN_FILES]; // 打开文件表
} User;

// 四、编译期布局校验

#if defined(__cplusplus)
static_assert(sizeof(DiskINode) == DISK_INODE_SIZE,
              "DiskINode must be exactly 32 bytes");
static_assert(sizeof(DirEntry) == DIR_ENTRY_SIZE,
              "DirEntry must be exactly 16 bytes");
static_assert(sizeof(SuperBlock) == BLOCK_SIZE,
              "SuperBlock must occupy exactly one disk block");
#elif defined(_Static_assert)
_Static_assert(sizeof(DiskINode) == DISK_INODE_SIZE,
               "DiskINode must be exactly 32 bytes");
_Static_assert(sizeof(DirEntry) == DIR_ENTRY_SIZE,
               "DirEntry must be exactly 16 bytes");
_Static_assert(sizeof(SuperBlock) == BLOCK_SIZE,
               "SuperBlock must occupy exactly one disk block");
#endif

#ifdef __cplusplus
}
#endif

#endif
