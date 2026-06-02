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

In `--serve` mode, UPFS runs as a multi-terminal daemon with a desktop-style Web UI:
- **Port 8080**: HTTP + WebSocket endpoint used by the browser desktop
- **Port 4096** (or custom): Raw TCP terminal (`nc localhost 4096`)
- Each terminal WebSocket connection is served by a child process with an interactive shell session
- Each API WebSocket connection is served by a child process that handles file browser, text read/write, and dashboard requests
- The parent process keeps a `poll` loop and forwards WebSocket payloads to child socketpairs

### Web UI Architecture

```
Browser → GET /          → HTTP → desktop single-page UI (embedded in web_page.h)
Browser → /ws/N          → WS   → terminal child session
Browser → /api           → WS   → API child session
```

Current UI behavior:
- Startup transition screen is shown first, then desktop is revealed
- Desktop includes menu bar, dock, draggable windows, and theme toggle
- Main apps: Terminal, Finder-style file browser, Activity Monitor, and TextEdit
- TextEdit can open, edit, and save files in the mounted image through the API socket


## Directory Structure

```
src/
  main.c              # Interactive shell entry point
  binaries.h/c        # Pre-built demo program binaries (.upx format)
  assembler.h/c       # Two-pass assembler: .s source → .upx binary
  serve.h/c           # TCP multi-terminal server (HTTP + WebSocket + raw TCP)
  web_api.c           # JSON API session: parse requests, call VFS/kernel, serialize JSON responses
  web_page.h          # Embedded desktop-style Web UI (startup screen + windows + dock)
  fs/                 # File system layer
    disk_io.h/c       # Block-level read/write, disk persistence
    format.h/c        # mkfs: superblock, block groups, root dir
    bg.h/c            # ext2-style block groups: per-group data + anchor free lists
    inomap.h/c        # Dynamic inode chunks + loc/chk B+ tree maps
    extent.h/c        # Extent tree: (lblk, pblk, len) mapping, merge adjacent runs
    buf.h/c           # Global block cache (LRU + hash), bread/bwrite/bdwrite
    journal.h/c       # Metadata journaling + mount replay
    vfs.c + vfs.h     # VFS op tables dispatching to UPFS
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
**Files**: `create`, `write`, `cat`, `rm`, `cp`, `ln`, `stat`, `chmod`
**Users**: `useradd`, `login`, `logout`, `whoami`, `passwd`, `users`
**Process/Env**: `asm`, `run`, `ps`, `env`, `export`, `unset`
**Debug**: `design_debug` (`super`, `inodes`, `blocks`, `bg`, `sof`, `memory`, `process`, `all`)
**Other**: `help`, `clear`, `exit`

## Key Implementation Notes

- Each `MemINode` has a `pthread_rwlock_t` for concurrency
- **Two-level open file table**: User open file table (20 per user) → System open file table (40 global entries) → Active inode table (hash-indexed cache)
- **Block groups (ext2-style)**: 8 groups × 64 blocks (1 anchor + 63 data blocks each); per-group 成组链接法 free lists in anchor blocks; `balloc_for(ino_hint)` prefers parent inode's block group
- **Dynamic inodes (XFS-style chunks)**: no fixed inode zone; each **Inode Chunk** = one data block holding 16 inodes; `inomap.c` maintains B+ trees for inode→(chunk,slot) and chunk free bitmaps; new chunks allocated via `bg_balloc_for()` on demand; max ~504×16 inodes
- **Format + anchors**: after `inomap_format_init()`, `format()` must call `bg_sync()` so on-disk anchor free lists exclude btree/chunk blocks (otherwise mount may reallocate and corrupt the imap)
- Disk layout: boot(0) + super(1) + 8 block groups(2–513) + journal(514–545) = 546 blocks; **re-format required** after layout change
- **Extent mapping**: inode holds inline `Extent` + optional B+ tree root (`d_tree_root`); each extent is `(e_lblk, e_pblk, e_len)`; adjacent logical/physical runs merge on allocate; leaf blocks hold up to 62 extents, index blocks for larger files
- **Permission enforcement**: `vfs_access()` checks owner/group/other rwx bits; root bypasses
- **Serve-mode consistency**: both terminal sessions and API sessions reload metadata and invalidate/flush cache paths so Web edits and shell commands stay in sync across processes
- Memory: 128MB byte array, page allocator with bitmap (4096 pages = 16MB kernel reserved)
- Processes: max 64, per-process page table (4096 pages max = 16MB)
- Scheduling: round-robin, 100 instruction time slice
- Passwords: salted 256-bit iterative hash (10000 rounds), stored in /etc/passwd
- Environment: /etc/environment (system) + ~/.env (per-user)
- Demo binaries injected during format into /bin/
- Assembly language reference: `doc/asm-reference.md`
