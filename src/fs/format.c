

#include "fs/format.h"
#include "fs/disk_io.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

#define FORMAT_LOG(fmt, ...) fprintf(stderr, "[format] " fmt "\n", ##__VA_ARGS__)


static const char BOOT_MAGIC[] = "UPFSBOOT";


static int write_disk_inode(uint16_t ino, const DiskINode *inode)
{
    int blk_no;
    int slot;
    int byte_off;
    char block_buf[BLOCK_SIZE];

    if (inode == NULL) {
        return -1;
    }
    if (ino >= TOTAL_INODES) {
        return -1;
    }

    
    blk_no = INODE_ZONE_START + (int)(ino / INODES_PER_BLOCK);
    
    slot = (int)(ino % INODES_PER_BLOCK);
    byte_off = slot * DISK_INODE_SIZE;

    if (read_block(blk_no, block_buf) != 0) {
        return -1;
    }

    memcpy(block_buf + byte_off, inode, (size_t)DISK_INODE_SIZE);
    return write_block(blk_no, block_buf);
}


static int clear_inode_zone(void)
{
    char zero_block[BLOCK_SIZE];
    int i;

    memset(zero_block, 0, sizeof(zero_block));
    for (i = 0; i < INODE_ZONE_BLOCKS; i++) {
        if (write_block(INODE_ZONE_START + i, zero_block) != 0) {
            return -1;
        }
    }
    return 0;
}













static int init_group_linked_free_blocks(SuperBlock *sb,
                                         const uint16_t *blks,
                                         int cnt)
{
    int idx;
    int left;
    int first_group;
    int i;

    if (sb == NULL || blks == NULL || cnt <= 0) {
        return -1;
    }

    idx = 0;
    left = cnt;

    
    first_group = left > MAX_FREE_BLOCKS ? MAX_FREE_BLOCKS : left;
    sb->s_free_block_count = (uint16_t)first_group;
    for (i = 0; i < first_group; i++) {
        sb->s_free_block_stack[i] = blks[idx++];
    }
    left -= first_group;
    sb->s_free_block_chain = 0;
    sb->s_block_free_count = (uint32_t)cnt;

    
    while (left > 0) {
        int group_n;
        uint16_t reg_blk;
        uint16_t reg_buf[BLOCK_SIZE / (int)sizeof(uint16_t)];
        int j;

        group_n = left > MAX_FREE_BLOCKS ? MAX_FREE_BLOCKS : left;
        
        reg_blk = blks[idx + group_n - 1];

        memset(reg_buf, 0, sizeof(reg_buf));
        
        reg_buf[0] = (uint16_t)(group_n - 1);
        for (j = 0; j < group_n - 1; j++) {
            reg_buf[j + 1] = blks[idx + j];
        }
        
        reg_buf[group_n] = sb->s_free_block_chain;

        if (write_block((int)reg_blk, reg_buf) != 0) {
            return -1;
        }

        sb->s_free_block_chain = reg_blk;
        idx += group_n;
        left -= group_n;
    }

    return 0;
}





static void init_free_inode_stack(SuperBlock *sb)
{
    int i;
    int top;
    uint32_t free_count;

    free_count = (uint32_t)(TOTAL_INODES - 2);
    sb->s_inode_total = (uint32_t)TOTAL_INODES;
    sb->s_inode_free_count = free_count;

    
    
    top = free_count > (uint32_t)INODE_FREE_STACK_SIZE
              ? INODE_FREE_STACK_SIZE - 1
              : (int)free_count - 1;

    for (i = 0; i <= top; i++) {
        sb->s_inode_free_stack[i] = (uint16_t)(TOTAL_INODES - 1 - i);
    }
    sb->s_inode_stack_top = (uint16_t)top;
}


static int init_boot_block(void)
{
    char boot_buf[BLOCK_SIZE];
    uint32_t *u32;

    memset(boot_buf, 0, sizeof(boot_buf));
    memcpy(boot_buf, BOOT_MAGIC, sizeof(BOOT_MAGIC) - 1);

    
    u32 = (uint32_t *)(boot_buf + BLOCK_SIZE - 2 * (int)sizeof(uint32_t));
    u32[0] = (uint32_t)SUPERBLOCK_BLOCKNO;
    u32[1] = (uint32_t)DATA_ZONE_START;

    return write_block(BOOT_BLOCKNO, boot_buf);
}


static int init_root_directory(void)
{
    DiskINode root_inode;
    char dir_block[BLOCK_SIZE];
    DirEntry *entries;
    int i;

    memset(&root_inode, 0, sizeof(root_inode));
    root_inode.d_mode = (uint16_t)(IFDIR | IREAD | IWRITE | IEXEC);
    root_inode.d_nlink = 2;                 
    root_inode.d_size = 2 * DIR_ENTRY_SIZE; 
    root_inode.d_uid = 0;
    root_inode.d_gid = 0;
    root_inode.d_direct[0] = (uint16_t)ROOT_DIR_BLOCK;

    if (write_disk_inode(ROOT_INODE_NO, &root_inode) != 0) {
        return -1;
    }

    memset(dir_block, 0, sizeof(dir_block));
    entries = (DirEntry *)dir_block;

    entries[0].de_inode = ROOT_INODE_NO;
    strncpy(entries[0].de_name, ".", MAX_FILENAME_LEN);

    entries[1].de_inode = ROOT_INODE_NO;
    strncpy(entries[1].de_name, "..", MAX_FILENAME_LEN);

    
    for (i = 0; i < 2; i++) {
        entries[i].de_name[MAX_FILENAME_LEN - 1] = '\0';
    }

    return write_block(ROOT_DIR_BLOCK, dir_block);
}

int format(const char *disk_path)
{
    SuperBlock sb;
    uint16_t free_blks[DATA_ZONE_BLOCKS];
    int free_cnt;
    int i;

    if (disk_path == NULL || disk_path[0] == '\0') {
        FORMAT_LOG("错误：磁盘路径为空");
        return -1;
    }

    if (disk_create() != 0) {
        FORMAT_LOG("错误：内存分配失败");
        return -1;
    }

    if (init_boot_block() != 0) {
        FORMAT_LOG("错误：引导块写入失败");
        disk_shutdown();
        return -1;
    }

    if (clear_inode_zone() != 0) {
        FORMAT_LOG("错误：i节点区清零失败");
        disk_shutdown();
        return -1;
    }

    
    free_cnt = 0;
    for (i = 0; i < DATA_ZONE_BLOCKS; i++) {
        uint16_t blk = (uint16_t)(DATA_ZONE_START + i);
        if (blk == ROOT_DIR_BLOCK) {
            continue;
        }
        free_blks[free_cnt++] = blk;
    }

    memset(&sb, 0, sizeof(sb));
    sb.s_magic = VFS_MAGIC;
    sb.s_block_total = (uint32_t)DATA_ZONE_BLOCKS;

    if (init_group_linked_free_blocks(&sb, free_blks, free_cnt) != 0) {
        FORMAT_LOG("错误：空闲块栈初始化失败");
        disk_shutdown();
        return -1;
    }

    init_free_inode_stack(&sb);

    if (write_block(SUPERBLOCK_BLOCKNO, &sb) != 0) {
        FORMAT_LOG("错误：超级块写入失败");
        disk_shutdown();
        return -1;
    }

    if (init_root_directory() != 0) {
        FORMAT_LOG("错误：根目录初始化失败");
        disk_shutdown();
        return -1;
    }

    if (disk_save(disk_path) != 0) {
        FORMAT_LOG("错误：镜像文件保存失败 (path=%s, errno=%d: %s)",
                   disk_path, errno, strerror(errno));
        disk_shutdown();
        return -1;
    }

    disk_shutdown();
    return 0;
}
