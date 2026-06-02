#ifndef VFS_CORE_H
#define VFS_CORE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif




#define BLOCK_SIZE              512


#define JOURNAL_ZONE_BLOCKS     32


#define BG_COUNT                8


#define BG_BLOCKS_PER_GROUP     64


#define BG_DATA_BLOCKS_PER_GROUP (BG_BLOCKS_PER_GROUP - 1)


#define BG_ZONE_START           2


#define BG_TOTAL_BLOCKS         (BG_COUNT * BG_BLOCKS_PER_GROUP)


#define JOURNAL_ZONE_START      (BG_ZONE_START + BG_TOTAL_BLOCKS)


#define DATA_ZONE_BLOCKS        BG_TOTAL_BLOCKS


#define FILE_DATA_BLOCKS        (BG_COUNT * BG_DATA_BLOCKS_PER_GROUP)


#define MAX_FREE_BLOCKS         50


#define DISK_INODE_SIZE         32


#define DIR_ENTRY_SIZE          16


#define MAX_OPEN_FILES          20


#define MAX_USERS               10


#define SYS_OPEN_FILE_MAX       40


#define EXTENTS_PER_LEAF        62




#define INODES_PER_CHUNK        (BLOCK_SIZE / DISK_INODE_SIZE)


#define INODE_CACHE_SIZE        2048


#define MAX_INODE_NUMBER        0xFFFFFFFEU


#define IMAP_FREE_SB_CAP        32


#define DIRS_PER_BLOCK          (BLOCK_SIZE / DIR_ENTRY_SIZE)


#define ADDRS_PER_BLOCK         (BLOCK_SIZE / (int)sizeof(uint16_t))


#define MAX_FILENAME_LEN        (DIR_ENTRY_SIZE - (int)sizeof(uint16_t))


#define TOTAL_DISK_BLOCKS       (2 + BG_TOTAL_BLOCKS + JOURNAL_ZONE_BLOCKS)


#define BOOT_BLOCKNO            0


#define SUPERBLOCK_BLOCKNO      1


#define DATA_ZONE_START         BG_ZONE_START


#define ROOT_INODE_NO           1U


#define ROOT_DIR_BLOCK          (BG_ZONE_START + 2)


#define VFS_MAGIC               0x55504653U


// 模式标志位
#define IFDIR                   0040000U 
#define IFREG                   0100000U
#define IEXEC                   0000100U
#define IWRITE                  0000200U
#define IREAD                   0000400U



#define O_RDONLY                0x0001U
#define O_WRONLY                0x0002U
#define O_RDWR                  0x0003U
#define O_APPEND                0x0008U



#define MINODE_DIRTY            0x01U  
#define MINODE_LOCKED           0x02U  




#define IMAP_FREE_CAP           128



#if defined(_MSC_VER)
#pragma pack(push, 1)
#else
#pragma pack(push, 1)
#endif










typedef struct BlockGroupDesc {
    uint16_t bgd_anchor_block;
    uint16_t bgd_data_start;
    uint16_t bgd_data_blocks;
    uint16_t bgd_pad;
} BlockGroupDesc;


typedef struct GroupDescTable {
    BlockGroupDesc bg[BG_COUNT];
    uint8_t        gdt_reserved[BLOCK_SIZE - sizeof(BlockGroupDesc) * BG_COUNT];
} GroupDescTable;


typedef struct SuperBlock {
    uint32_t         s_magic;
    uint32_t         s_inode_total;
    uint32_t         s_inode_free_count;
    uint32_t         s_inode_next;
    uint32_t         s_block_total;
    uint32_t         s_block_free_count;
    uint16_t         s_bg_count;
    uint16_t         s_imap_loc_root;
    uint16_t         s_imap_loc_level;
    uint16_t         s_imap_chk_root;
    uint16_t         s_imap_chk_level;
    uint16_t         s_imap_free_top;
    uint16_t         s_reserved16;
    uint32_t         s_imap_free_stack[IMAP_FREE_SB_CAP];
    BlockGroupDesc   s_bg_table[BG_COUNT];
    uint8_t          s_pad[BLOCK_SIZE - 56 - sizeof(BlockGroupDesc) * BG_COUNT
                            - IMAP_FREE_SB_CAP * (int)sizeof(uint32_t)];
} SuperBlock;













typedef struct Extent {
    uint32_t e_lblk;
    uint16_t e_pblk;
    uint16_t e_len;
} Extent;


typedef struct DiskINode {
    uint16_t d_mode;
    uint16_t d_nlink;
    uint32_t d_size;
    uint16_t d_uid;
    uint16_t d_gid;
    Extent   d_extent;
    uint16_t d_tree_root;
    uint16_t d_tree_level;
    uint32_t d_reserved;
    uint32_t d_pad;
} DiskINode;




typedef struct DirEntry {
    uint16_t de_inode;                  
    char     de_name[MAX_FILENAME_LEN]; 
} DirEntry;

#pragma pack(pop)



struct MemINode;


typedef struct SysOpenFile {
    uint32_t     f_offset;              
    uint16_t     f_mode;                
    uint16_t     f_flags;               
    uint16_t     f_count;               
    uint16_t     f_pad;                 
    struct MemINode *f_inode;           
} SysOpenFile;


typedef struct OpenFileTable {
    uint32_t     oft_read_pos;          
    uint32_t     oft_write_pos;         
    uint16_t     oft_mode;              
    uint16_t     oft_flags;             
    struct MemINode *oft_inode;         
} OpenFileTable;









typedef struct MemINode {
    DiskINode    m_dinode;              
    uint16_t     m_inode_no;            
    uint16_t     m_ref_count;           
    uint8_t      m_flags;               
    uint8_t      m_pad[3];              
} MemINode;





typedef struct User {
    char           u_name[32];          
    uint16_t       u_uid;               
    uint16_t       u_gid;               
    uint16_t       u_cdir;              
    uint8_t        u_active;            
    uint8_t        u_pad;               
    OpenFileTable  u_ofile[MAX_OPEN_FILES]; 
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
