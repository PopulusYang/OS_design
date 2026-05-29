

#include "kernel/syscall.h"
#include "kernel/scheduler.h"
#include "kernel/memory.h"
#include "kernel/pipe.h"
#include "kernel/ipc.h"
#include "vfs_core.h"



#include "fs/file_sys.h"
#include "fs/dir_sys.h"
#include "fs/allocator.h"
#include "user/env.h"
#include "assembler.h"
#include "editor.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>





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

    case SYSCALL_EXIT: 
        proc_exit((int)arg1);
        *ret = 0;
        break;

    case SYSCALL_FORK: 
        *ret = (uint32_t)proc_fork();
        break;

    case SYSCALL_EXEC: { 
        char path[256];
        if (syscall_read_str(p, arg1, path, (int)sizeof(path)) != 0) { *ret = (uint32_t)-1; break; }
        *ret = proc_exec(p, path) == 0 ? 0 : (uint32_t)-1;
        break;
    }

    case SYSCALL_WAIT: { 
        int status = 0;
        int cpid = proc_wait(&status);
        if (cpid >= 0 && arg1 != 0) {
            cpu_write32(&p->p_cpu, arg1, (uint32_t)status);
        }
        *ret = (uint32_t)cpid;
        break;
    }

    case SYSCALL_GETPID: 
        *ret = p->p_pid;
        break;

    case SYSCALL_OPEN: { 
        char path[256];
        if (syscall_read_str(p, arg1, path, (int)sizeof(path)) != 0) { *ret = (uint32_t)-1; break; }
        {
            int fifo_fd = ipc_fifo_open(p, path, (uint16_t)arg2);
            if (fifo_fd >= 0) { *ret = (uint32_t)fifo_fd; break; }
        }
        int fs_fd = vfs_open(path, (uint16_t)arg2);
        if (fs_fd < 0) { *ret = (uint32_t)-1; break; }
        int pfd = proc_alloc_fd(p);
        if (pfd < 0) { vfs_close(fs_fd); *ret = (uint32_t)-1; break; }
        p->p_ofile[pfd].fd_type = PROC_FD_FILE;
        p->p_ofile[pfd].fd_fs_fd = fs_fd;
        p->p_ofile[pfd].fd_mode = (int)arg2;
        p->p_ofile[pfd].fd_pos = 0;
        *ret = (uint32_t)pfd;
        break;
    }

    case SYSCALL_CLOSE: 
        if ((int)arg1 >= 0 && (int)arg1 < PROC_MAX_FD)
            proc_free_fd(p, (int)arg1);
        *ret = 0;
        break;

    case SYSCALL_READ: { 
        int fd = (int)arg1;
        if (fd < 0 || fd >= PROC_MAX_FD) { *ret = (uint32_t)-1; break; }
        ProcFD *pfd = &p->p_ofile[fd];
        if (PROC_FD_IS_PIPE_RD(pfd->fd_type)) {
            char tmp[512];
            int chunk = (int)arg3 > 512 ? 512 : (int)arg3;
            int n = pipe_read(pfd->fd_pipe_id, tmp, chunk);
            if (n < 0) { *ret = (uint32_t)-1; break; }
            for (int i = 0; i < n; i++) {
                uint32_t phys;
                if (cpu_virt_to_phys(&p->p_cpu, arg2 + (uint32_t)i, &phys) == 0)
                    mem_write8(phys, (uint8_t)tmp[i]);
            }
            *ret = (uint32_t)n;
        } else if (pfd->fd_type == PROC_FD_FILE) {
            char tmp[512];
            int chunk = (int)arg3 > 512 ? 512 : (int)arg3;
            int n = vfs_read(pfd->fd_fs_fd, tmp, chunk);
            if (n <= 0) { *ret = (uint32_t)-1; break; }
            for (int i = 0; i < n; i++) {
                uint32_t phys;
                if (cpu_virt_to_phys(&p->p_cpu, arg2 + (uint32_t)i, &phys) == 0)
                    mem_write8(phys, (uint8_t)tmp[i]);
            }
            *ret = (uint32_t)n;
        } else if (fd == 0 && pfd->fd_type == PROC_FD_TERM) {
            char tmp[512];
            int chunk = (int)arg3 > 512 ? 512 : (int)arg3;
            ssize_t n = read(STDIN_FILENO, tmp, (size_t)chunk);
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { *ret = 0; break; }
            if (n <= 0) { *ret = (uint32_t)-1; break; }
            for (ssize_t i = 0; i < n; i++) {
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

    case SYSCALL_WRITE: { 
        int fd = (int)arg1;
        if (fd < 0 || fd >= PROC_MAX_FD) { *ret = (uint32_t)-1; break; }
        ProcFD *pfd = &p->p_ofile[fd];
        if (PROC_FD_IS_PIPE_WR(pfd->fd_type)) {
            char tmp[512];
            int chunk = (int)arg3 > 512 ? 512 : (int)arg3;
            for (int i = 0; i < chunk; i++) {
                uint32_t phys;
                if (cpu_virt_to_phys(&p->p_cpu, arg2 + (uint32_t)i, &phys) == 0)
                    tmp[i] = (char)mem_read8(phys);
                else { chunk = i; break; }
            }
            int n = pipe_write(pfd->fd_pipe_id, tmp, chunk);
            *ret = (uint32_t)(n >= 0 ? n : -1);
        } else if (pfd->fd_type == PROC_FD_FILE && pfd->fd_fs_fd >= 0) {
            char tmp[512];
            int chunk = (int)arg3 > 512 ? 512 : (int)arg3;
            for (int i = 0; i < chunk; i++) {
                uint32_t phys;
                if (cpu_virt_to_phys(&p->p_cpu, arg2 + (uint32_t)i, &phys) == 0)
                    tmp[i] = (char)mem_read8(phys);
                else { chunk = i; break; }
            }
            int n = vfs_write(pfd->fd_fs_fd, tmp, chunk);
            *ret = (uint32_t)(n > 0 ? n : -1);
        } else if (pfd->fd_type == PROC_FD_TERM && fd == 1) {
            char tmp[512];
            int chunk = (int)arg3 > 511 ? 511 : (int)arg3;
            for (int i = 0; i < chunk; i++) {
                uint32_t phys;
                if (cpu_virt_to_phys(&p->p_cpu, arg2 + (uint32_t)i, &phys) == 0)
                    tmp[i] = (char)mem_read8(phys);
                else { chunk = i; break; }
            }
            tmp[chunk] = '\0';
            for (int i = 0; i < chunk; i++) {
                if (tmp[i] == '\n') fputc('\r', stdout);
                fputc(tmp[i], stdout);
            }
            fflush(stdout);
            *ret = (uint32_t)chunk;
        } else {
            *ret = (uint32_t)-1;
        }
        break;
    }

    case SYSCALL_SEEK: { 
        (void)arg1; (void)arg2; (void)arg3;
        
        *ret = 0;
        break;
    }

    case SYSCALL_GETCWD: { 
        (void)arg2;
        const User *u = dir_get_user();
        *ret = (uint32_t)-1;
        if (u != NULL && arg1 != 0) {
            
            syscall_write_str(p, arg1, "/", 256);
            *ret = 0;
        }
        break;
    }

    case SYSCALL_CHDIR: { 
        char path[256];
        if (syscall_read_str(p, arg1, path, (int)sizeof(path)) != 0) { *ret = (uint32_t)-1; break; }
        *ret = chdir(path) == 0 ? 0 : (uint32_t)-1;
        break;
    }

    case SYSCALL_SBRK: 
        *ret = proc_sbrk(p, (int32_t)arg1);
        break;

    case SYSCALL_GETENV: { 
        char name[64];
        if (syscall_read_str(p, arg1, name, (int)sizeof(name)) != 0) { *ret = (uint32_t)-1; break; }
        const char *val = env_get(name);
        if (val == NULL) { *ret = (uint32_t)-1; break; }
        syscall_write_str(p, arg2, val, (int)arg3);
        *ret = 0;
        break;
    }

    case SYSCALL_SETENV: { 
        char name[64], value[256];
        if (syscall_read_str(p, arg1, name, (int)sizeof(name)) != 0) { *ret = (uint32_t)-1; break; }
        if (syscall_read_str(p, arg2, value, (int)sizeof(value)) != 0) { *ret = (uint32_t)-1; break; }
        *ret = env_set(name, value) == 0 ? 0 : (uint32_t)-1;
        break;
    }

    case SYSCALL_UNSETENV: { 
        char name[64];
        if (syscall_read_str(p, arg1, name, (int)sizeof(name)) != 0) { *ret = (uint32_t)-1; break; }
        *ret = env_unset(name) == 0 ? 0 : (uint32_t)-1;
        break;
    }

    case SYSCALL_STAT: { 
        (void)arg2;
        *ret = 0; 
        break;
    }

    case SYSCALL_CREATE: { 
        char path[256];
        if (syscall_read_str(p, arg1, path, (int)sizeof(path)) != 0) { *ret = (uint32_t)-1; break; }
        *ret = vfs_create(path, (uint16_t)arg2) == 0 ? 0 : (uint32_t)-1;
        break;
    }

    case SYSCALL_DELETE: { 
        char path[256];
        if (syscall_read_str(p, arg1, path, (int)sizeof(path)) != 0) { *ret = (uint32_t)-1; break; }
        *ret = vfs_delete(path) == 0 ? 0 : (uint32_t)-1;
        break;
    }

    case SYSCALL_MKDIR: { 
        char path[256];
        if (syscall_read_str(p, arg1, path, (int)sizeof(path)) != 0) { *ret = (uint32_t)-1; break; }
        *ret = vfs_mkdir(path, (uint16_t)arg2) == 0 ? 0 : (uint32_t)-1;
        break;
    }

    case SYSCALL_HOST_EDIT: { 
        char path[512]; int i = 0;
        printf("File: "); fflush(stdout);
        while (i < 510) {
            char c;
            if (read(STDIN_FILENO, &c, 1) != 1) continue;
            if (c == '\n' || c == '\r') break;
            if (c == 127 || c == '\b') { if (i > 0) { i--; printf("\b \b"); } continue; }
            if (c >= 32 && c < 127) { path[i++] = c; printf("%c", c); }
            fflush(stdout);
        }
        path[i] = '\0'; printf("\r\n"); fflush(stdout);
        if (!path[0]) { *ret = 0; break; }
        
        char tmp[256];
        snprintf(tmp, sizeof(tmp), "/tmp/upfs_vim_sys_%d", getpid());
        MemINode *ip = namei(path);
        if (ip) {
            iput(ip);
            int fd = vfs_open(path, O_RDONLY);
            if (fd >= 0) {
                char buf[8192]; int total = 0, n;
                while ((n = vfs_read(fd, buf + total, (int)sizeof(buf) - total - 1)) > 0) total += n;
                vfs_close(fd);
                FILE *f = fopen(tmp, "w");
                if (f) { fwrite(buf, 1, (size_t)total, f); fclose(f); }
            }
        }
        printf("\033[2J\033[H"); fflush(stdout);
        editor_open(tmp);
        printf("\033[2J\033[H"); fflush(stdout);
        FILE *f = fopen(tmp, "r");
        if (f) {
            fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
            if (sz > 0) {
                char *buf = malloc((size_t)(sz + 1));
                if (buf) {
                    fread(buf, 1, (size_t)sz, f);
                    vfs_delete(path);
                    if (vfs_create(path, 0644) != 0) { vfs_delete(path); vfs_create(path, 0644); }
                    int fd = vfs_open(path, O_WRONLY);
                    if (fd >= 0) { vfs_write(fd, buf, (int)sz); vfs_close(fd); }
                    free(buf);
                }
            } else {
                vfs_delete(path); vfs_create(path, 0644);
            }
            fclose(f);
        }
        unlink(tmp);
        *ret = 0;
        break;
    }

    case SYSCALL_HOST_ASM: { 
        char src[512], out[512]; int si = 0, oi = 0;
        printf("Source: "); fflush(stdout);
        while (si < 510) {
            char c;
            if (read(STDIN_FILENO, &c, 1) != 1) continue;
            if (c == '\n' || c == '\r') break;
            if (c == 127 || c == '\b') { if (si > 0) { si--; printf("\b \b"); } } else if (c >= 32 && c < 127) { src[si++] = c; printf("%c", c); }
            fflush(stdout);
        }
        src[si] = '\0'; printf("\r\n"); fflush(stdout);
        printf("Output: "); fflush(stdout);
        while (oi < 510) {
            char c;
            if (read(STDIN_FILENO, &c, 1) != 1) continue;
            if (c == '\n' || c == '\r') break;
            if (c == 127 || c == '\b') { if (oi > 0) { oi--; printf("\b \b"); } } else if (c >= 32 && c < 127) { out[oi++] = c; printf("%c", c); }
            fflush(stdout);
        }
        out[oi] = '\0'; printf("\r\n"); fflush(stdout);
        char ts[256], to[256];
        snprintf(ts, sizeof(ts), "/tmp/upfs_asm_src_sys_%d", getpid());
        snprintf(to, sizeof(to), "/tmp/upfs_asm_out_sys_%d", getpid());
        
        { MemINode *ip = namei(src);
          if (!ip) { printf("Source not found\n"); *ret = 0; break; }
          iput(ip);
          int fd = vfs_open(src, O_RDONLY);
          if (fd >= 0) {
              char buf[8192]; int total = 0, n;
              while ((n = vfs_read(fd, buf + total, (int)sizeof(buf) - total - 1)) > 0) total += n;
              vfs_close(fd);
              FILE *f = fopen(ts, "w");
              if (f) { fwrite(buf, 1, (size_t)total, f); fclose(f); }
          } }
        assemble_file(ts, to);
        
        { FILE *f = fopen(to, "rb");
          if (f) {
              fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
              if (sz > 0) {
                  char *buf = malloc((size_t)sz);
                  if (buf) {
                      fread(buf, 1, (size_t)sz, f);
                      vfs_delete(out);
                      if (vfs_create(out, 0644) != 0) { vfs_delete(out); vfs_create(out, 0644); }
                      int fd = vfs_open(out, O_WRONLY);
                      if (fd >= 0) { vfs_write(fd, buf, (int)sz); vfs_close(fd); }
                      free(buf);
                  }
              }
              fclose(f);
          } }
        unlink(ts); unlink(to);
        *ret = 0;
        break;
    }

    case SYSCALL_PIPE: { 
        int fds[2];
        if (proc_pipe(fds) != 0) { *ret = (uint32_t)-1; break; }
        cpu_write32(&p->p_cpu, arg1, (uint32_t)fds[0]);
        cpu_write32(&p->p_cpu, arg1 + 4, (uint32_t)fds[1]);
        *ret = 0;
        break;
    }

    case SYSCALL_KILL: 
        *ret = ipc_kill(arg1, (int)arg2) == 0 ? 0 : (uint32_t)-1;
        break;

    case SYSCALL_SEMGET: 
        *ret = (uint32_t)ipc_semget((int)arg1, (int)arg2);
        break;

    case SYSCALL_SEMOP: 
        *ret = ipc_semop((int)arg1, (int)arg2) == 0 ? 0 : (uint32_t)-1;
        break;

    case SYSCALL_MSGGET: 
        *ret = (uint32_t)ipc_msgget((int)arg1);
        break;

    case SYSCALL_MSGSND: { 
        int type = (int)cpu_read32(&p->p_cpu, arg2);
        char tmp[IPC_MSG_SIZE];
        int chunk = (int)arg3 > IPC_MSG_SIZE ? IPC_MSG_SIZE : (int)arg3;
        if (chunk > 4) chunk -= 4;
        else chunk = 0;
        for (int i = 0; i < chunk; i++) {
            uint32_t phys;
            if (cpu_virt_to_phys(&p->p_cpu, arg2 + 4 + (uint32_t)i, &phys) == 0)
                tmp[i] = (char)mem_read8(phys);
        }
        int n = ipc_msgsnd((int)arg1, type, tmp, chunk);
        *ret = (uint32_t)(n >= 0 ? n : -1);
        break;
    }

    case SYSCALL_MSGRCV: { 
        char tmp[IPC_MSG_SIZE];
        int out_type = 0;
        int chunk = (int)arg3 > IPC_MSG_SIZE ? IPC_MSG_SIZE : (int)arg3;
        int n = ipc_msgrcv((int)arg1, (int)arg3, tmp, chunk > 4 ? chunk - 4 : 0, &out_type);
        if (n < 0) { *ret = (uint32_t)-1; break; }
        cpu_write32(&p->p_cpu, arg2, (uint32_t)out_type);
        for (int i = 0; i < n; i++) {
            uint32_t phys;
            if (cpu_virt_to_phys(&p->p_cpu, arg2 + 4 + (uint32_t)i, &phys) == 0)
                mem_write8(phys, (uint8_t)tmp[i]);
        }
        *ret = (uint32_t)n;
        break;
    }

    case SYSCALL_SHMGET: 
        *ret = (uint32_t)ipc_shmget((int)arg1, (int)arg2);
        break;

    case SYSCALL_SHMAT: 
        *ret = (uint32_t)ipc_shmat(p, (int)arg1, arg2);
        break;

    case SYSCALL_SHMDT: 
        *ret = ipc_shmdt(p, (int)arg1) == 0 ? 0 : (uint32_t)-1;
        break;

    case SYSCALL_MKFIFO: { 
        char path[256];
        if (syscall_read_str(p, arg1, path, (int)sizeof(path)) != 0) { *ret = (uint32_t)-1; break; }
        *ret = ipc_mkfifo(path) == 0 ? 0 : (uint32_t)-1;
        break;
    }

    case SYSCALL_GETSIG: 
        *ret = (uint32_t)p->p_sigusr1_count;
        p->p_sigusr1_count = 0;
        break;

    default:
        *ret = (uint32_t)-1;
        break;
    }
}
