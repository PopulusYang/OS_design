# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

> **Maintenance rule**: After any feature implementation that changes the architecture, adds/removes modules, or modifies the shell command set, this file MUST be updated accordingly.

## Project Overview

UPFS — a UNIX file system + OS kernel simulator for an OS course design project. Implements a virtual file system on a disk image, plus an OS kernel layer with process management, memory management, VM execution engine, time-slice round-robin scheduler, system call interface, multi-user authentication, and environment variables.

Language: C17, compiled with GCC/Clang. Target: Linux/macOS (POSIX). No external dependencies beyond libc and pthreads.

## Build & Run

```bash
# CMake (preferred)
cmake -B build -S . -DCMAKE_C_FLAGS="-D_GNU_SOURCE"
cmake --build build
./build/bin/OS_design              # interactive shell
./build/bin/OS_design --serve      # TCP multi-terminal server (:8080 HTTP, :4096 raw TCP)
./build/bin/OS_design --serve 9999 # custom TCP port

# Makefile (alternative)
make && ./upfs
```

### TCP Server Mode (`--serve`)

In `--serve` mode, UPFS acts as a multi-terminal daemon:
- **Port 8080**: HTTP server serving the terminal web page + WebSocket upgrade at `/ws/*`
- **Port 4096** (or custom): Raw TCP terminal — connect with `nc localhost 4096` or `telnet`
- Each connection forks a child process running `upfs_session()`; the parent polls and forwards I/O


## Directory Structure

```
src/
  main.c              # Interactive shell entry point
  binaries.h/c        # Pre-built demo program binaries (.upx format)
  assembler.h/c       # Two-pass assembler: .s source → .upx binary
  serve.h/c           # TCP multi-terminal server (HTTP + WebSocket + raw TCP)
  fs/                 # File system layer
    disk_io.h/c       # Block-level read/write, disk persistence
    format.h/c        # mkfs: superblock, free blocks, root dir
    allocator.h/c     # Block/inode allocation, inode cache, mount/umount
    dir_sys.h/c       # Path resolution (namei), mkdir, chdir, ls
    file_sys.h/c      # File create/open/read/write/close/delete
  kernel/             # OS kernel layer
    memory.h/c        # 128MB physical memory, page allocator (4KB pages)
    cpu.h/c           # 32-bit RISC VM: 18 instructions, fetch-decode-execute
    process.h/c       # PCB, process table, fork/exec/wait/exit, UPX loader
    scheduler.h/c     # Round-robin scheduler with time slices (100 instr)
    syscall.h/c       # 20 system calls bridging VM to FS
  user/               # User management layer
    user_mgmt.h/c     # Multi-user accounts, password hashing, /etc/passwd
    env.h/c            # Environment variables: system + per-user, file-persisted
include/
  vfs_core.h          # Shared constants, disk structures, runtime types
```

## Architecture Layers

```
Shell (main.c)  ───  run, ps, env, export, unset, + all FS commands
    │
    ├── serve.c ─── TCP multi-terminal server (--serve mode)
    │
Process Mgmt ─── Scheduler ─── Syscall Interface
    │               │               │
CPU/VM ───────── Memory Mgmt ── Env Vars ── User Mgmt
    │               │               │           │
    └───────────────┴───────────────┴───────────┘
                    │
        File System Layer (fs/)
                    │
              Disk I/O (fs/disk_io)
```

## VM Instruction Set (32-bit RISC)

18 instructions, fixed 32-bit encoding: `[opcode:8][rd:4][rs1:4][rs2:4][imm12:12]`
16 general registers (R15=SP), PC, FLAGS (ZF).

| Op | Mnemonic | Description |
|----|----------|-------------|
| 0x00 | HALT | Stop execution, trigger exit |
| 0x01 | MOVI | R[rd] = sign-extend(imm12) |
| 0x02 | MOV | R[rd] = R[rs1] |
| 0x03 | LD | R[rd] = mem[R[rs1] + imm12] |
| 0x04 | ST | mem[R[rs1] + imm12] = R[rs2] |
| 0x05 | ADD | R[rd] = R[rs1] + R[rs2] |
| 0x06 | SUB | R[rd] = R[rs1] - R[rs2] |
| 0x07 | MUL | R[rd] = R[rs1] * R[rs2] |
| 0x08 | DIV | R[rd] = R[rs1] / R[rs2] |
| 0x09 | AND | R[rd] = R[rs1] & R[rs2] |
| 0x0A | OR | R[rd] = R[rs1] \| R[rs2] |
| 0x0B | XOR | R[rd] = R[rs1] ^ R[rs2] |
| 0x0C | CMP | FLAGS = R[rs1] - R[rs2] |
| 0x0D | JMP | PC += sign-extend(imm12) |
| 0x0E | JZ | if ZF: PC += sign-extend(imm12) |
| 0x0F | JNZ | if !ZF: PC += sign-extend(imm12) |
| 0x10 | CALL | PUSH PC; PC += imm12 |
| 0x11 | RET | POP PC |
| 0x12 | PUSH | SP-=4; mem[SP] = R[rs1] |
| 0x13 | POP | R[rd] = mem[SP]; SP+=4 |
| 0x14 | SYSCALL | Trigger syscall n=imm12 |
| 0x15 | LUI | R[rd] = imm12 << 12 |

## Executable Format (.upx)

```
[magic:4] "UPX\0"
[entry:4]  instruction index of entry point
[text_size:4]  code segment size (bytes)
[data_size:4]  data segment size
[bss_size:4]   uninitialized data size
[stack_size:4] stack size
[text: text_size]  code
[data: data_size]  initialized data
```

## System Calls (20)

| # | Name | Description |
|---|------|-------------|
| 0 | EXIT | Exit process |
| 1 | FORK | Fork process |
| 2 | EXEC | Load and execute program |
| 3 | WAIT | Wait for child |
| 4 | GETPID | Get process ID |
| 5 | OPEN | Open file |
| 6 | CLOSE | Close file |
| 7 | READ | Read from file |
| 8 | WRITE | Write to file/terminal |
| 9 | SEEK | Seek file position |
| 10 | GETCWD | Get current directory |
| 11 | CHDIR | Change directory |
| 12 | SBRK | Extend heap |
| 13 | GETENV | Get environment variable |
| 14 | SETENV | Set environment variable |
| 15 | UNSETENV | Remove environment variable |
| 16 | STAT | Get file status |
| 17 | CREATE | Create file |
| 18 | DELETE | Delete file |
| 19 | MKDIR | Create directory |

## Shell Commands

**System**: `format`, `mount`, `umount`
**Directories**: `mkdir`, `cd`, `pwd`, `ls`
**Files**: `create`, `write`, `cat`, `rm`
**Users**: `useradd`, `login`, `logout`, `whoami`, `passwd`, `users`
**Process/Env**: `asm`, `run`, `ps`, `env`, `export`, `unset`
**Other**: `help`, `clear`, `exit`

## Key Implementation Notes

- Each `MemINode` has a `pthread_rwlock_t` for concurrency
- Memory: 128MB byte array, page allocator with bitmap (4096 pages = 16MB kernel reserved)
- Processes: max 64, per-process page table (4096 pages max = 16MB)
- Scheduling: round-robin, 100 instruction time slice
- Passwords: salted 256-bit iterative hash (10000 rounds), stored in /etc/passwd
- Environment: /etc/environment (system) + ~/.env (per-user)
- Demo binaries injected during format into /bin/
- Assembly language reference: `doc/asm-reference.md`
