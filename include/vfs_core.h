#ifndef VFS_CORE_H
#define VFS_CORE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif



// 块大小
#define BLOCK_SIZE              512

// 日志区域块数量
#define JOURNAL_ZONE_BLOCKS     32

// 块组数量
#define BG_COUNT                8

// 每个块组中的块数量
#define BG_BLOCKS_PER_GROUP     64

// 块组中数据块数量
#define BG_DATA_BLOCKS_PER_GROUP (BG_BLOCKS_PER_GROUP - 1)

// 块组区域的起始块号（块0：boot,块1：superblock,块2-513：数据区域，块514-545：日志区）
#define BG_ZONE_START           2

// 块组区域总块数（块组数量 × 每组块数）
#define BG_TOTAL_BLOCKS         (BG_COUNT * BG_BLOCKS_PER_GROUP)

// 日志区开始块数
#define JOURNAL_ZONE_START      (BG_ZONE_START + BG_TOTAL_BLOCKS)

// 数据区总块数，与块组区域等价
#define DATA_ZONE_BLOCKS        BG_TOTAL_BLOCKS

// 文件数据块数量
#define FILE_DATA_BLOCKS        (BG_COUNT * BG_DATA_BLOCKS_PER_GROUP)

// 最大空闲块数量，也就是空闲块栈大小
#define MAX_FREE_BLOCKS         50

// 磁盘索引节点大小
#define DISK_INODE_SIZE         32

// 目录项大小（16字节）
#define DIR_ENTRY_SIZE          16

// 每个用户最多同时打开的文件数量20个
#define MAX_OPEN_FILES          20

// 最多10个用户
#define MAX_USERS               10

// 全局最多打开文件数量40个
#define SYS_OPEN_FILE_MAX       40

// Extent树叶子块中最多可存放的Extent项数量
#define EXTENTS_PER_LEAF        62



// 每个块可容纳的磁盘inode数量
#define INODES_PER_CHUNK        (BLOCK_SIZE / DISK_INODE_SIZE)

// 索引缓存大小
#define INODE_CACHE_SIZE        2048

// 最大索引数
#define MAX_INODE_NUMBER        0xFFFFFFFEU

// 超级块中缓存的空闲inode号的最大容量
#define IMAP_FREE_SB_CAP        32

// 一个块能塞多少个目录
#define DIRS_PER_BLOCK          (BLOCK_SIZE / DIR_ENTRY_SIZE)

// 地址为16位整型，一个块能塞多少个地址
#define ADDRS_PER_BLOCK         (BLOCK_SIZE / (int)sizeof(uint16_t))

// 文件名最大长度（目录项大小-地址大小）
#define MAX_FILENAME_LEN        (DIR_ENTRY_SIZE - (int)sizeof(uint16_t))

// 磁盘总块数（2+块组总块数+日志区块数）
#define TOTAL_DISK_BLOCKS       (2 + BG_TOTAL_BLOCKS + JOURNAL_ZONE_BLOCKS)

// boot块位置0
#define BOOT_BLOCKNO            0

// 超级块位置1
#define SUPERBLOCK_BLOCKNO      1

// 数据区起始块号=块组起始块号
#define DATA_ZONE_START         BG_ZONE_START

// 根目录索引节点（/ 的inode号恒定为1）
#define ROOT_INODE_NO           1U

// 根目录块号固定为块组起点+2
#define ROOT_DIR_BLOCK          (BG_ZONE_START + 2)

// 文件系统识别魔数,对应ASCII(UPFS)
#define VFS_MAGIC               0x55504653U


// 模式标志位
#define IFDIR                   0040000U 
#define IFREG                   0100000U
#define IEXEC                   0000100U
#define IWRITE                  0000200U
#define IREAD                   0000400U


// 文件打开权限标识
#define O_RDONLY                0x0001U
#define O_WRONLY                0x0002U
#define O_RDWR                  0x0003U
#define O_APPEND                0x0008U


// 内存inode标志
// 内存inode为脏数据
#define MINODE_DIRTY            0x01U  
// 内存inode被锁定
#define MINODE_LOCKED           0x02U  



// 空闲inode号栈最大容量
#define IMAP_FREE_CAP           128


// 控制结构体内存对齐
#if defined(_MSC_VER)
#pragma pack(push, 1)
#else
#pragma pack(push, 1)
#endif









// 块组描述符：记录单个块组的元信息（锚点块、数据区起止）
typedef struct BlockGroupDesc {
    uint16_t bgd_anchor_block; // 块组锚点块号
    uint16_t bgd_data_start; // 数据块的起始块号
    uint16_t bgd_data_blocks; // 该块组中可用的数据
    uint16_t bgd_pad; // 填充字段
} BlockGroupDesc;

// 块组描述符表：存储全部8个块组的描述符，刚好占一个磁盘块
typedef struct GroupDescTable {
    BlockGroupDesc bg[BG_COUNT]; // 8个块组的描述符
    uint8_t        gdt_reserved[BLOCK_SIZE - sizeof(BlockGroupDesc) * BG_COUNT]; // 填充字段，块大小512字节
} GroupDescTable; 

// 超级块：文件系统的核心元数据结构，存放全局统计信息、空闲inode缓存和块组描述符表，位于磁盘块1
typedef struct SuperBlock {
    uint32_t         s_magic; // 超级块魔数
    uint32_t         s_inode_total; // 总索引节点数
    uint32_t         s_inode_free_count; // 空闲索引节点数
    uint32_t         s_inode_next; // 空闲索引栈栈顶
    uint32_t         s_block_total; // 块总数
    uint32_t         s_block_free_count; // 空闲块数量
    uint16_t         s_bg_count; // 块组数量
    uint16_t         s_imap_loc_root; // inode定位B+树根块号
    uint16_t         s_imap_loc_level; // inode定位B+树层数
    uint16_t         s_imap_chk_root; // chunk空闲位图B+树根块号
    uint16_t         s_imap_chk_level; // chunk空闲位图B+树层数
    uint16_t         s_imap_free_top; // 超级块中缓存的空闲inode栈顶
    uint16_t         s_reserved16; // 保留字段
    uint32_t         s_imap_free_stack[IMAP_FREE_SB_CAP]; // 超级块内空闲inode号缓存栈
    BlockGroupDesc   s_bg_table[BG_COUNT]; // 块组描述符表
    uint8_t          s_pad[BLOCK_SIZE - 56 - sizeof(BlockGroupDesc) * BG_COUNT
                            - IMAP_FREE_SB_CAP * (int)sizeof(uint32_t)]; // 填充至一个磁盘块大小
} SuperBlock;













// Extent（区段）：描述文件数据的一段连续物理块映射，(lblk, pblk, len) 表示从逻辑块lblk起、len个连续块映射到物理块pblk起
typedef struct Extent {
    uint32_t e_lblk; // 逻辑块号（文件内偏移，以块为单位）
    uint16_t e_pblk; // 物理块号（磁盘上实际块号）
    uint16_t e_len;  // 连续块数量（该extent覆盖的块数）
} Extent;


// 磁盘inode：存储在磁盘上的索引节点，记录文件元数据（权限、大小、extent映射等），每个固定32字节
typedef struct DiskINode {
    uint16_t d_mode;       // 文件类型与权限（IFREG/IFDIR | IREAD/IWRITE/IEXEC）
    uint16_t d_nlink;      // 硬链接计数
    uint32_t d_size;       // 文件大小（字节）
    uint16_t d_uid;        // 所有者用户ID
    uint16_t d_gid;        // 所有者组ID
    Extent   d_extent;     // 第一个extent（内联存放，文件较小时无需B+树）
    uint16_t d_tree_root;  // extent B+树根块号（0表示无B+树，仅内联extent）
    uint16_t d_tree_level; // extent B+树层数（0表示仅有内联extent）
    uint32_t d_reserved;   // 保留字段
    uint32_t d_pad;        // 填充字段（对齐到32字节）
} DiskINode;




// 目录项：目录数据块中每条记录对应一个文件/子目录，包含inode号和定长文件名，每个16字节
typedef struct DirEntry {
    uint16_t de_inode;                  // 文件/目录的inode号
    char     de_name[MAX_FILENAME_LEN]; // 文件名（定长，非'\0'结尾）
} DirEntry;

#pragma pack(pop)



struct MemINode;


// 系统打开文件表项：记录一个已打开文件的运行时状态（偏移、模式、引用计数），多个用户可共享同一项，全局最多40项
typedef struct SysOpenFile {
    uint32_t     f_offset;              // 当前文件读写偏移
    uint16_t     f_mode;                // 打开模式（O_RDONLY/O_WRONLY/O_RDWR）
    uint16_t     f_flags;               // 打开标志位（O_APPEND等）
    uint16_t     f_count;               // 引用计数（多个用户可共享同一系统打开文件）
    uint16_t     f_pad;                 // 填充字段
    struct MemINode *f_inode;           // 指向对应内存inode的指针
} SysOpenFile;


// 用户打开文件表项：每个用户独立的文件描述符视图，记录独立的读写位置，通过指针关联到系统打开文件表
typedef struct OpenFileTable {
    uint32_t     oft_read_pos;          // 读指针位置
    uint32_t     oft_write_pos;         // 写指针位置
    uint16_t     oft_mode;              // 打开模式
    uint16_t     oft_flags;             // 打开标志位
    struct MemINode *oft_inode;         // 指向对应内存inode的指针
} OpenFileTable;









// 内存inode：磁盘inode的运行时内存缓存，附带inode号、引用计数和同步状态标志，通过哈希表索引
typedef struct MemINode {
    DiskINode    m_dinode;              // 磁盘inode的内存副本
    uint16_t     m_inode_no;            // inode号
    uint16_t     m_ref_count;           // 引用计数（为0时可回收）
    uint8_t      m_flags;               // 状态标志（MINODE_DIRTY/MINODE_LOCKED）
    uint8_t      m_pad[3];              // 内存对齐填充
} MemINode;





// 用户账户：记录用户身份、当前工作目录及个人打开文件表，系统最多支持10个用户
typedef struct User {
    char           u_name[32];          // 用户名
    uint16_t       u_uid;               // 用户ID
    uint16_t       u_gid;               // 用户组ID
    uint16_t       u_cdir;              // 当前工作目录的inode号
    uint8_t        u_active;            // 是否已登录（1=在线，0=离线）
    uint8_t        u_pad;               // 填充字段
    OpenFileTable  u_ofile[MAX_OPEN_FILES]; // 用户打开文件表（最多20个）
} User;



#if defined(__cplusplus)
static_assert(sizeof(Extent) == 8, "Extent must be 8 bytes");
static_assert(sizeof(DiskINode) == DISK_INODE_SIZE,
              "DiskINode must be exactly 32 bytes");
static_assert(sizeof(DirEntry) == DIR_ENTRY_SIZE,
              "DirEntry must be exactly 16 bytes");
static_assert(sizeof(SuperBlock) == BLOCK_SIZE,
              "SuperBlock must occupy exactly one disk block");
#elif defined(_Static_assert)
_Static_assert(sizeof(Extent) == 8, "Extent must be 8 bytes");
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
