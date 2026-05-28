





#ifndef VFS_CORE_H
#define VFS_CORE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif




#define BLOCK_SIZE              512


#define INODE_ZONE_BLOCKS       32


#define DATA_ZONE_BLOCKS        512


#define MAX_FREE_BLOCKS         50


#define DISK_INODE_SIZE         32


#define DIR_ENTRY_SIZE          16


#define MAX_OPEN_FILES          20


#define MAX_USERS               10




#define INODES_PER_BLOCK        (BLOCK_SIZE / DISK_INODE_SIZE)


#define TOTAL_INODES            (INODE_ZONE_BLOCKS * INODES_PER_BLOCK)


#define DIRS_PER_BLOCK          (BLOCK_SIZE / DIR_ENTRY_SIZE)


#define ADDRS_PER_BLOCK         (BLOCK_SIZE / (int)sizeof(uint16_t))


#define MAX_FILENAME_LEN        (DIR_ENTRY_SIZE - (int)sizeof(uint16_t))


#define TOTAL_DISK_BLOCKS       (1 + 1 + INODE_ZONE_BLOCKS + DATA_ZONE_BLOCKS)


#define BOOT_BLOCKNO            0


#define SUPERBLOCK_BLOCKNO      1


#define INODE_ZONE_START        2


#define DATA_ZONE_START         (INODE_ZONE_START + INODE_ZONE_BLOCKS)


#define ROOT_INODE_NO           1


#define ROOT_DIR_BLOCK          DATA_ZONE_START


#define VFS_MAGIC               0x55504653U



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




#define SUPERBLOCK_FIXED_SIZE   (                       \
    (int)sizeof(uint32_t) * 5 +                         \
    (int)sizeof(uint16_t) * 2 +                         \
    MAX_FREE_BLOCKS * (int)sizeof(uint16_t) +           \
    (int)sizeof(uint16_t)                                 \
)

#define INODE_FREE_STACK_SIZE   (                       \
    (BLOCK_SIZE - SUPERBLOCK_FIXED_SIZE) / (int)sizeof(uint16_t) \
)



#if defined(_MSC_VER)
#pragma pack(push, 1)
#else
#pragma pack(push, 1)
#endif










typedef struct SuperBlock {
    uint32_t s_magic;                               
    uint32_t s_inode_total;                         
    uint32_t s_inode_free_count;                    
    uint32_t s_block_total;                         
    uint32_t s_block_free_count;                    
    uint16_t s_free_block_count;                    
    uint16_t s_free_block_chain;                    
    uint16_t s_free_block_stack[MAX_FREE_BLOCKS];   
    uint16_t s_inode_stack_top;                     
    uint16_t s_inode_free_stack[INODE_FREE_STACK_SIZE]; 
} SuperBlock;













typedef struct DiskINode {
    uint16_t d_mode;                    
    uint16_t d_nlink;                   
    uint32_t d_size;                    
    uint16_t d_uid;                     
    uint16_t d_gid;                     
    uint16_t d_direct[8];               
    uint16_t d_sindirect;               
    uint16_t d_dindirect;               
} DiskINode;




typedef struct DirEntry {
    uint16_t de_inode;                  
    char     de_name[MAX_FILENAME_LEN]; 
} DirEntry;

#pragma pack(pop)



struct MemINode;


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
