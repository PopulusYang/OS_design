// syscall.c —— 系统调用实现：连接 VM 与 UPFS 文件系统

#include "kernel/syscall.h"
#include "kernel/scheduler.h"
#include "kernel/memory.h"

// 前向声明 —— 文件系统函数（由 file_sys.h / dir_sys.h 提供）
// 注意：这些函数使用外部 User 上下文，在 syscall 中需要切换
#include "fs/file_sys.h"
#include "fs/dir_sys.h"
#include "user/env.h"

#include <string.h>
#include <stdio.h>

// 为 syscall 操作临时绑定用户上下文（使用当前 Shell User 的绑定）
// 注意：实际实现中所有文件操作通过 file_sys 接口，该接口内部使用 dir_get_user()
// syscall 处理期间，当前 shell 的 user 应仍然绑定

int syscall_read_str(const PCB *p, uint32_t virt_addr, char *buf, int max_len)
{
    if (p == NULL || buf == NULL || max_len <= 0) return -1;
    for (int i = 0; i < max_len - 1; i++) {
        uint32_t phys;
        if (cpu_virt_to_phys(&p->p_cpu, virt_addr + (uint32_t)i, &phys) != 0) return -1;
        buf[i] = (char)mem_read8(phys);
        if (buf[i] == '\0') return 0;
    }
    buf[max_len - 1] = '\0';
    return 0;
}

int syscall_write_str(PCB *p, uint32_t virt_addr, const char *str, int max_len)
{
    if (p == NULL || str == NULL) return -1;
    int i;
    for (i = 0; i < max_len && str[i] != '\0'; i++) {
        uint32_t phys;
        if (cpu_virt_to_phys(&p->p_cpu, virt_addr + (uint32_t)i, &phys) != 0) return -1;
        mem_write8(phys, (uint8_t)str[i]);
    }
    if (i < max_len) {
        uint32_t phys;
        if (cpu_virt_to_phys(&p->p_cpu, virt_addr + (uint32_t)i, &phys) == 0)
            mem_write8(phys, 0);
    }
    return 0;
}

void syscall_dispatch(PCB *p, uint32_t no)
{
    if (p == NULL) return;

    uint32_t arg1 = p->p_cpu.regs[1];
    uint32_t arg2 = p->p_cpu.regs[2];
    uint32_t arg3 = p->p_cpu.regs[3];
    uint32_t *ret = &p->p_cpu.regs[0];

    switch (no) {

    case SYSCALL_EXIT: // exit(code)
        proc_exit((int)arg1);
        *ret = 0;
        break;

    case SYSCALL_FORK: // fork() → child_pid
        *ret = (uint32_t)proc_fork();
        break;

    case SYSCALL_EXEC: { // exec(path) → 0 or -1
        char path[256];
        if (syscall_read_str(p, arg1, path, (int)sizeof(path)) != 0) { *ret = (uint32_t)-1; break; }
        *ret = proc_exec(p, path) == 0 ? 0 : (uint32_t)-1;
        break;
    }

    case SYSCALL_WAIT: { // wait(&status) → child_pid
        int status = 0;
        int cpid = proc_wait(&status);
        if (cpid >= 0 && arg1 != 0) {
            cpu_write32(&p->p_cpu, arg1, (uint32_t)status);
        }
        *ret = (uint32_t)cpid;
        break;
    }

    case SYSCALL_GETPID: // getpid()
        *ret = p->p_pid;
        break;

    case SYSCALL_OPEN: { // open(path, flags) → fd
        char path[256];
        if (syscall_read_str(p, arg1, path, (int)sizeof(path)) != 0) { *ret = (uint32_t)-1; break; }
        int fs_fd = open(path, (uint16_t)arg2);
        if (fs_fd < 0) { *ret = (uint32_t)-1; break; }
        int pfd = proc_alloc_fd(p);
        if (pfd < 0) { close(fs_fd); *ret = (uint32_t)-1; break; }
        p->p_ofile[pfd].fd_type = PROC_FD_FILE;
        p->p_ofile[pfd].fd_fs_fd = fs_fd;
        p->p_ofile[pfd].fd_mode = (int)arg2;
        p->p_ofile[pfd].fd_pos = 0;
        *ret = (uint32_t)pfd;
        break;
    }

    case SYSCALL_CLOSE: // close(fd)
        if ((int)arg1 >= 0 && (int)arg1 < PROC_MAX_FD && p->p_ofile[arg1].fd_type == PROC_FD_FILE) {
            close(p->p_ofile[arg1].fd_fs_fd);
        }
        proc_free_fd(p, (int)arg1);
        *ret = 0;
        break;

    case SYSCALL_READ: { // read(fd, buf, count) → n
        int fd = (int)arg1;
        if (fd < 0 || fd >= PROC_MAX_FD) { *ret = (uint32_t)-1; break; }
        ProcFD *pfd = &p->p_ofile[fd];
        if (pfd->fd_type == PROC_FD_FILE) {
            // 从文件读取到内核临时缓冲，再拷贝到用户空间
            char tmp[512];
            int chunk = (int)arg3 > 512 ? 512 : (int)arg3;
            int n = read(pfd->fd_fs_fd, tmp, chunk);
            if (n <= 0) { *ret = (uint32_t)-1; break; }
            for (int i = 0; i < n; i++) {
                uint32_t phys;
                if (cpu_virt_to_phys(&p->p_cpu, arg2 + (uint32_t)i, &phys) == 0)
                    mem_write8(phys, (uint8_t)tmp[i]);
            }
            *ret = (uint32_t)n;
        } else {
            *ret = (uint32_t)-1;
        }
        break;
    }

    case SYSCALL_WRITE: { // write(fd, buf, count) → n
        int fd = (int)arg1;
        if (fd < 0 || fd >= PROC_MAX_FD) { *ret = (uint32_t)-1; break; }
        ProcFD *pfd = &p->p_ofile[fd];
        if (pfd->fd_type == PROC_FD_FILE && pfd->fd_fs_fd >= 0) {
            char tmp[512];
            int chunk = (int)arg3 > 512 ? 512 : (int)arg3;
            for (int i = 0; i < chunk; i++) {
                uint32_t phys;
                if (cpu_virt_to_phys(&p->p_cpu, arg2 + (uint32_t)i, &phys) == 0)
                    tmp[i] = (char)mem_read8(phys);
                else { chunk = i; break; }
            }
            int n = write(pfd->fd_fs_fd, tmp, chunk);
            *ret = (uint32_t)(n > 0 ? n : -1);
        } else if (pfd->fd_type == 0 && fd == 1) {
            // stdout (fd=1): 直接写到终端
            char tmp[512];
            int chunk = (int)arg3 > 511 ? 511 : (int)arg3;
            for (int i = 0; i < chunk; i++) {
                uint32_t phys;
                if (cpu_virt_to_phys(&p->p_cpu, arg2 + (uint32_t)i, &phys) == 0)
                    tmp[i] = (char)mem_read8(phys);
                else { chunk = i; break; }
            }
            tmp[chunk] = '\0';
            fputs(tmp, stdout);
            fflush(stdout);
            *ret = (uint32_t)chunk;
        } else {
            *ret = (uint32_t)-1;
        }
        break;
    }

    case SYSCALL_SEEK: { // seek(fd, offset, whence)
        (void)arg1; (void)arg2; (void)arg3;
        // 简化：不支持 seek
        *ret = 0;
        break;
    }

    case SYSCALL_GETCWD: { // getcwd(buf, size)
        (void)arg2;
        const User *u = dir_get_user();
        *ret = (uint32_t)-1;
        if (u != NULL && arg1 != 0) {
            // 简单返回 "/"
            syscall_write_str(p, arg1, "/", 256);
            *ret = 0;
        }
        break;
    }

    case SYSCALL_CHDIR: { // chdir(path)
        char path[256];
        if (syscall_read_str(p, arg1, path, (int)sizeof(path)) != 0) { *ret = (uint32_t)-1; break; }
        *ret = chdir(path) == 0 ? 0 : (uint32_t)-1;
        break;
    }

    case SYSCALL_SBRK: // sbrk(increment) → old_brk
        *ret = proc_sbrk(p, (int32_t)arg1);
        break;

    case SYSCALL_GETENV: { // getenv(name, buf, size)
        char name[64];
        if (syscall_read_str(p, arg1, name, (int)sizeof(name)) != 0) { *ret = (uint32_t)-1; break; }
        const char *val = env_get(name);
        if (val == NULL) { *ret = (uint32_t)-1; break; }
        syscall_write_str(p, arg2, val, (int)arg3);
        *ret = 0;
        break;
    }

    case SYSCALL_SETENV: { // setenv(name, value)
        char name[64], value[256];
        if (syscall_read_str(p, arg1, name, (int)sizeof(name)) != 0) { *ret = (uint32_t)-1; break; }
        if (syscall_read_str(p, arg2, value, (int)sizeof(value)) != 0) { *ret = (uint32_t)-1; break; }
        *ret = env_set(name, value) == 0 ? 0 : (uint32_t)-1;
        break;
    }

    case SYSCALL_UNSETENV: { // unsetenv(name)
        char name[64];
        if (syscall_read_str(p, arg1, name, (int)sizeof(name)) != 0) { *ret = (uint32_t)-1; break; }
        *ret = env_unset(name) == 0 ? 0 : (uint32_t)-1;
        break;
    }

    case SYSCALL_STAT: { // stat(path, buf)
        (void)arg2;
        *ret = 0; // 简化
        break;
    }

    case SYSCALL_CREATE: { // create(path, mode)
        char path[256];
        if (syscall_read_str(p, arg1, path, (int)sizeof(path)) != 0) { *ret = (uint32_t)-1; break; }
        *ret = create(path, (uint16_t)arg2) == 0 ? 0 : (uint32_t)-1;
        break;
    }

    case SYSCALL_DELETE: { // delete(path)
        char path[256];
        if (syscall_read_str(p, arg1, path, (int)sizeof(path)) != 0) { *ret = (uint32_t)-1; break; }
        *ret = delete(path) == 0 ? 0 : (uint32_t)-1;
        break;
    }

    case SYSCALL_MKDIR: { // mkdir(path, mode)
        char path[256];
        if (syscall_read_str(p, arg1, path, (int)sizeof(path)) != 0) { *ret = (uint32_t)-1; break; }
        *ret = vfs_mkdir(path, (uint16_t)arg2) == 0 ? 0 : (uint32_t)-1;
        break;
    }

    default:
        *ret = (uint32_t)-1;
        break;
    }
}
