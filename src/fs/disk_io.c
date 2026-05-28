



#include "fs/disk_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>


static uint8_t *g_disk_mem = NULL;


static int    g_disk_fd   = -1;
static size_t g_disk_size = 0;


static char g_disk_path[512];


static int g_disk_dirty = 0;


static int disk_check_block(int block_no, const void *buf)
{
    if (g_disk_mem == NULL) {
        return -1;
    }
    if (buf == NULL) {
        return -1;
    }
    if (block_no < 0 || block_no >= TOTAL_DISK_BLOCKS) {
        return -1;
    }
    return 0;
}

int disk_create(void)
{
    
    if (g_disk_fd >= 0) { close(g_disk_fd); g_disk_fd = -1; }
    if (g_disk_mem != NULL) {
        if (g_disk_size > 0) munmap(g_disk_mem, g_disk_size);
        else free(g_disk_mem);
        g_disk_mem = NULL;
    }
    g_disk_size = 0;

    
    g_disk_mem = (uint8_t *)calloc((size_t)TOTAL_DISK_BLOCKS, (size_t)BLOCK_SIZE);
    if (g_disk_mem == NULL) return -1;

    g_disk_dirty = 0;
    g_disk_path[0] = '\0';
    g_disk_fd = -1;
    return 0;
}

int disk_load(const char *disk_path)
{
    struct stat st;
    int    fd;
    void  *map;
    size_t expect;

    if (disk_path == NULL || disk_path[0] == '\0') return -1;

    expect = (size_t)TOTAL_DISK_BLOCKS * (size_t)BLOCK_SIZE;

    fd = open(disk_path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "[disk_io] open(%s) failed: %s\n", disk_path, strerror(errno));
        return -1;
    }

    if (fstat(fd, &st) != 0 || (size_t)st.st_size != expect) {
        fprintf(stderr, "[disk_io] fstat size mismatch: have=%ld expect=%zu\n",
                (long)st.st_size, expect);
        close(fd);
        return -1;
    }

    map = mmap(NULL, expect, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        fprintf(stderr, "[disk_io] mmap failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    
    if (g_disk_mem != NULL) {
        if (g_disk_fd >= 0) munmap(g_disk_mem, g_disk_size);
        else free(g_disk_mem);
    }

    g_disk_mem  = (uint8_t *)map;
    g_disk_fd   = fd;
    g_disk_size = expect;
    g_disk_dirty = 0;

    strncpy(g_disk_path, disk_path, sizeof(g_disk_path) - 1);
    g_disk_path[sizeof(g_disk_path) - 1] = '\0';
    return 0;
}


static void ensure_parent_dir(const char *file_path)
{
    char buf[512];

    strncpy(buf, file_path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *slash = strrchr(buf, '/');
    if (slash == NULL) return;
    *slash = '\0';

    
    char *p = buf;
    if (*p == '/') p++; 
    while (*p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(buf, 0755);
            *p = '/';
        }
        p++;
    }
    mkdir(buf, 0755); 
}

int disk_save(const char *disk_path)
{
    size_t total_size = (size_t)TOTAL_DISK_BLOCKS * (size_t)BLOCK_SIZE;

    if (g_disk_mem == NULL) return -1;
    if (disk_path == NULL || disk_path[0] == '\0') return -1;

    
    if (g_disk_fd >= 0) {
        ensure_parent_dir(disk_path);
        if (msync(g_disk_mem, g_disk_size, MS_SYNC) != 0) return -1;
    } else {
        FILE *fp;
        ensure_parent_dir(disk_path);
        fp = fopen(disk_path, "wb");
        if (fp == NULL) return -1;
        if (fwrite(g_disk_mem, 1, total_size, fp) != total_size) { fclose(fp); return -1; }
        if (fflush(fp) != 0) { fclose(fp); return -1; }
        fclose(fp);
    }

    strncpy(g_disk_path, disk_path, sizeof(g_disk_path) - 1);
    g_disk_path[sizeof(g_disk_path) - 1] = '\0';
    g_disk_dirty = 0;
    return 0;
}

int disk_sync(void)
{
    if (g_disk_mem == NULL) return -1;
    if (g_disk_path[0] == '\0') return -1;
    
    if (g_disk_fd >= 0) {
        return msync(g_disk_mem, g_disk_size, MS_SYNC) == 0 ? 0 : -1;
    }
    return disk_save(g_disk_path);
}

void disk_shutdown(void)
{
    if (g_disk_mem != NULL) {
        if (g_disk_fd >= 0) {
            msync(g_disk_mem, g_disk_size, MS_SYNC);
            munmap(g_disk_mem, g_disk_size);
            close(g_disk_fd);
            g_disk_fd = -1;
        } else {
            free(g_disk_mem);
        }
        g_disk_mem = NULL;
    }
    g_disk_size = 0;
    g_disk_dirty = 0;
    g_disk_path[0] = '\0';
}

int read_block(int block_no, void *buf)
{
    size_t offset;

    if (disk_check_block(block_no, buf) != 0) {
        return -1;
    }

    
    offset = (size_t)block_no * (size_t)BLOCK_SIZE;
    memcpy(buf, g_disk_mem + offset, (size_t)BLOCK_SIZE);
    return 0;
}

int write_block(int block_no, const void *buf)
{
    size_t offset;

    if (disk_check_block(block_no, buf) != 0) {
        return -1;
    }

    offset = (size_t)block_no * (size_t)BLOCK_SIZE;
    memcpy(g_disk_mem + offset, buf, (size_t)BLOCK_SIZE);
    g_disk_dirty = 1;
    return 0;
}

void *disk_memory(void)
{
    return g_disk_mem;
}

size_t disk_memory_size(void)
{
    return (size_t)TOTAL_DISK_BLOCKS * (size_t)BLOCK_SIZE;
}
