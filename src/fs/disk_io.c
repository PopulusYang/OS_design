#include "fs/disk_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

static uint8_t *g_disk_mem = NULL; // 指向磁盘内存映射

static int g_disk_fd = -1; // 真实磁盘文件的文件描述符
static size_t g_disk_size = 0; // 内存映射的字节数

static char g_disk_path[512]; // 磁盘文件的完整路径

static int g_disk_dirty = 0; // 脏数据标志整型变量

// 检查磁盘块的合法性
static int disk_check_block(int block_no, const void *buf)
{
    if (g_disk_mem == NULL)// 磁盘是否初始化
    {
        return -1;
    }
    if (buf == NULL) // 缓冲区指针是否有效
    {
        return -1;
    }
    if (block_no < 0 || block_no >= TOTAL_DISK_BLOCKS) // 块号是否合法
    {
        return -1;
    }
    return 0;
}
//（重新）创建内存虚拟硬盘
int disk_create(void)
{
    //清理旧的文件描述符
    if (g_disk_fd >= 0)
    {
        close(g_disk_fd);
        g_disk_fd = -1; // 重置文件描述符
    }
    if (g_disk_mem != NULL) // 释放旧的映射
    {
        if (g_disk_size > 0) // mmap映射
            munmap(g_disk_mem, g_disk_size); 
        else // malloc分配
            free(g_disk_mem);
        g_disk_mem = NULL; // 清空指针
    }
    g_disk_size = 0; // 重置

    g_disk_mem = (uint8_t *)calloc((size_t)TOTAL_DISK_BLOCKS, (size_t)BLOCK_SIZE); // 分配新的内存
    if (g_disk_mem == NULL) // 分配失败
        return -1;

    g_disk_dirty = 0; // 标记为干净
    g_disk_path[0] = '\0'; // 清空路径，改为访问虚拟内存
    g_disk_fd = -1; // 重置文件描述符
    return 0;
}

//mmap教程： mmap(memory-mapped I/O)是POSIX系统调用，将磁盘文件的内容直接映射到进程的虚拟内存空间

// void *mmap(void *addr,    // 期望映射的起始地址（NULL = 由内核选择）
//            size_t length, // 映射的字节数
//            int prot,      // 访问权限
//            int flags,     // 映射选项
//            int fd,        // 文件描述符
//            off_t offset); // 文件的起始偏移量
// 返回映射内存的虚拟地址

// int munmap(void *addr, size_t length); // 取消映射

// 加载已有img
int disk_load(const char *disk_path)
{
    struct stat st; // POSIX 标准的文件元数据结构体，需要sys/stat.h,用于检查文件状态
    int fd; // 文件描述符
    void *map;
    size_t expect; // 总磁盘空间大小

    if (disk_path == NULL || disk_path[0] == '\0') // 检查路径是否为空
        return -1;

    expect = (size_t)TOTAL_DISK_BLOCKS * (size_t)BLOCK_SIZE;

    fd = open(disk_path, O_RDWR); // 打开文件，权限为读和写
    if (fd < 0) // 文件打开失败
    {
        fprintf(stderr, "[disk_io] open(%s) failed: %s\n", disk_path, strerror(errno)); // 输出标准错误
        return -1;
    }

    if (fstat(fd, &st) != 0 || (size_t)st.st_size != expect)// 检测文件描述是否可用（有效，权限，系统问题）
    {
        fprintf(stderr, "[disk_io] fstat size mismatch: have=%ld expect=%zu\n",
                (long)st.st_size, expect);
        close(fd);
        return -1;
    }

    map = mmap(NULL, expect, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0); // 将文件映射到虚拟内存
    if (map == MAP_FAILED) // 映射失败
    {
        fprintf(stderr, "[disk_io] mmap failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    if (g_disk_mem != NULL) // 清空原有映射
    {
        if (g_disk_fd >= 0)
            munmap(g_disk_mem, g_disk_size);
        else
            free(g_disk_mem);
    }

    g_disk_mem = (uint8_t *)map; // 让g_disk_mem指向新的映射
    g_disk_fd = fd; // 文件描述符
    g_disk_size = expect; // 大小
    g_disk_dirty = 0; // 设置为干净状态

    strncpy(g_disk_path, disk_path, sizeof(g_disk_path) - 1); //将路径存到全局
    g_disk_path[sizeof(g_disk_path) - 1] = '\0'; // 强制添加控制符终止符
    return 0;
}

// 确保目录存在，不存在则创建目录
static void ensure_parent_dir(const char *file_path)
{
    char buf[512];
    strncpy(buf, file_path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0'; // 将父目录拷贝到buf,添加字符串结束标识

    char *slash = strrchr(buf, '/'); //查找最后一个‘/’
    if (slash == NULL)
        return;
    *slash = '\0'; //剔除文件名只保留目录

    char *p = buf;
    // 创建目录，确保副目录存在
    if (*p == '/')
        p++;
    while (*p)
    {
        if (*p == '/')
        {
            *p = '\0';
            mkdir(buf, 0755);
            *p = '/';
        }
        p++;
    }
    mkdir(buf, 0755);
}

// 将内存中的虚拟磁盘持久化到镜像
int disk_save(const char *disk_path)
{
    size_t total_size = (size_t)TOTAL_DISK_BLOCKS * (size_t)BLOCK_SIZE;

    if (g_disk_mem == NULL)
        return -1;
    if (disk_path == NULL || disk_path[0] == '\0') // 路径不可为空
        return -1;

    if (g_disk_fd >= 0) // 直接同步mmap
    {
        ensure_parent_dir(disk_path); // 确保目录存在
        if (msync(g_disk_mem, g_disk_size, MS_SYNC) != 0) // 同步mmap和磁盘文件
            return -1;
    }
    else // 正常输入输出流
    {
        FILE *fp;
        ensure_parent_dir(disk_path);
        fp = fopen(disk_path, "wb");
        if (fp == NULL)
            return -1;
        if (fwrite(g_disk_mem, 1, total_size, fp) != total_size)
        {
            fclose(fp);
            return -1;
        }
        if (fflush(fp) != 0)
        {
            fclose(fp);
            return -1;
        }
        fclose(fp);
    }
    strncpy(g_disk_path, disk_path, sizeof(g_disk_path) - 1);
    g_disk_path[sizeof(g_disk_path) - 1] = '\0';
    g_disk_dirty = 0; // 同步成功，标记为干净
    return 0;
}

// 同步
int disk_sync(void)
{
    if (g_disk_mem == NULL)
        return -1;
    if (g_disk_path[0] == '\0')
        return -1;

    if (g_disk_fd >= 0)
    {
        return msync(g_disk_mem, g_disk_size, MS_SYNC) == 0 ? 0 : -1;
    return disk_save(g_disk_path);
}

// 取消镜像挂载，释放资源 
void disk_shutdown(void)
{
    if (g_disk_mem != NULL) // 已加载则同步mmap并且关闭（malloc直接释放内存）
    {
        if (g_disk_fd >= 0) 
        {
            msync(g_disk_mem, g_disk_size, MS_SYNC);
            munmap(g_disk_mem, g_disk_size);
            close(g_disk_fd);
            g_disk_fd = -1;
        }
        else
        {
            free(g_disk_mem);
        }
        g_disk_mem = NULL;
    }
    g_disk_size = 0;
    g_disk_dirty = 0;
    g_disk_path[0] = '\0';
}

// 读磁盘块
int disk_read_block(int block_no, void *buf)
{
    size_t offset; // 偏移量

    if (disk_check_block(block_no, buf) != 0) // 检查合法性
    {
        return -1;
    }

    offset = (size_t)block_no * (size_t)BLOCK_SIZE; // 偏移量=块号*块大小
    memcpy(buf, g_disk_mem + offset, (size_t)BLOCK_SIZE); // 将磁盘块读入缓冲区
    return 0;
}

// 写磁盘块，原理与读取相似
int disk_write_block(int block_no, const void *buf)
{
    size_t offset;

    if (disk_check_block(block_no, buf) != 0) // 检查合法
    {
        return -1;
    }

    offset = (size_t)block_no * (size_t)BLOCK_SIZE; // 计算偏移量
    memcpy(g_disk_mem + offset, buf, (size_t)BLOCK_SIZE); // 将缓冲区内容写入磁盘块（在内存中）
    g_disk_dirty = 1; // 标记为脏数据
    return 0;
}

// 返回mmap映射指针
void *disk_memory(void)
{
    return g_disk_mem;
}
// 返回磁盘大小
size_t disk_memory_size(void)
{
    return (size_t)TOTAL_DISK_BLOCKS * (size_t)BLOCK_SIZE;
}
