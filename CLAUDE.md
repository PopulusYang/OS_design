# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

> **Maintenance rule**: After any feature implementation that changes the architecture, adds/removes modules, or modifies the shell command set, this file MUST be updated accordingly.

## Project Overview

UPFS — a UNIX file system simulator for an OS course design project. It implements a complete virtual file system on a single disk image file (`vfs_disk.img`), including a custom disk layout, inode-based file metadata, block allocation via group linking, directory management with path resolution, multi-user authentication with salted password hashing, and an interactive shell with ANSI-styled UI.

Language: C17, compiled with GCC/Clang. Target: Linux/macOS (POSIX).

## Build & Run

```bash
# CMake (preferred)
cmake -B build -S . -DCMAKE_C_FLAGS="-D_GNU_SOURCE"
cmake --build build
./build/bin/OS_design

# Makefile (alternative)
make
./upfs
```

No external dependencies beyond libc and pthreads.

## Architecture

The system is organized into six layers, each with a `.c`/`.h` pair in `src/`:

| Layer | File | Responsibility |
|-------|------|---------------|
| **Disk I/O** | `disk_io.c` | Block-level read/write on an in-memory byte array, persisted to/from the `.img` file. `read_block()`/`write_block()` operate on 512-byte logical blocks. |
| **Format** | `format.c` | `mkfs` — initializes superblock, boot block, free-block group linking stacks, and root directory. |
| **Allocator** | `allocator.c` | Block allocation via group linking (成组链接法, 50-block stacks). Inode allocation via superblock stack. In-memory inode cache with a 128-bucket hash table, per-inode `pthread_rwlock_t`, and ref-counted `iget`/`iput`. Also handles mount/umount. |
| **Directory** | `dir_sys.c` | Path resolution (`namei` — resolves absolute/relative paths to MemINode), `mkdir`, `chdir`, `ls`, directory entry link/unlink. |
| **File** | `file_sys.c` | File create/open/read/write/close/delete. Uses a per-process open file table. The hybrid index scheme supports up to ~32 MiB files (8 direct + 1 single-indirect + 1 double-indirect). |
| **User Mgmt** | `user_mgmt.c` | Multi-user account management: add/delete users, salted iterative-hash password verification, `/etc/passwd` persistence, POSIX directory creation (`/home`, `/root`, `/etc`). Sits above `file_sys`/`dir_sys`. |

Shared constants and on-disk struct layouts (`SuperBlock`, `DiskINode`, `DirEntry`) plus runtime structs (`MemINode`, `OpenFileTable`, `User`) are defined in `include/vfs_core.h`.

The entry point `src/main.c` provides an interactive shell. It auto-detects existing disk images on startup and supports auto-save on exit.

## Shell Commands

**System**: `format [path]`, `mount [path]`, `umount`
**Directories**: `mkdir <path> [mode]`, `cd <path>`, `pwd`, `ls [path]`
**Files**: `create <path> [mode]`, `write <path> <data>`, `cat <path>`, `rm <path>`
**Users**: `useradd <name> <password>`, `login <name> <password>`, `logout`, `whoami`, `passwd <name> <newpass>`, `users`
**Other**: `help`, `clear`, `exit`

Prompt format: `username:display_path ›` (home directory shown as `~`).

## Multi-User System

- Supports 1–8 users (configurable via `USER_MAX_COUNT` in `user_mgmt.h`).
- Normal user UIDs start at 1000; root is uid 0.
- Passwords are hashed with a 256-bit iterative mixing function (10000 rounds) and a random 8-byte salt, stored as hex in `/etc/passwd`.
- `/etc/passwd` format (one line per user): `username:uid:password_hash_hex:salt_hex:home_dir`
- On `format`: creates POSIX dirs (`/home`, `/root`, `/etc`), prompts for first username/password, auto-login.
- On `mount`: loads user DB from `/etc/passwd`, prompts for login.
- Legacy images (no `/etc/passwd`) are handled gracefully — root login without password.

## Disk Layout

546 blocks of 512 bytes each:
- Block 0: Boot block (reserved)
- Block 1: SuperBlock (magic 0x55504653 "UPFS", free block/inode stacks)
- Blocks 2–33: Inode zone (32 blocks × 16 inodes/block = 512 inodes)
- Blocks 34–545: Data zone (512 blocks)

Inode 0 is reserved. Inode 1 is the root directory (data block 34).

## Key Concurrency Pattern

Each `MemINode` in the hash cache has its own `pthread_rwlock_t`. File operations acquire read or write locks via `inode_rdlock()`/`inode_wrlock()` and release with `inode_unlock()`. The open file table tracks lock type per fd (`OF_RDLOCKED`/`OF_WRLOCKED` flags) so `close()` can release the correct lock.

## Notes

- `tests/` is currently empty (only `.gitkeep`).
- The `vfs_disk.img` at repo root is a pre-built demo image. It's in `.gitignore` now but exists from prior commits.
- `#pragma pack(1)` is used for all on-disk structs to ensure cross-platform layout consistency.
- `_Static_assert` validates struct sizes at compile time.
