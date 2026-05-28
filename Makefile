# UPFS 模拟 UNIX 文件系统 —— 构建脚本

CC      ?= cc
CFLAGS  ?= -Wall -Wextra -Werror -std=c17 -D_GNU_SOURCE -Iinclude -Isrc
LDFLAGS ?= -pthread

OUTDIR = out
SRCS   = src/fs/disk_io.c src/fs/format.c src/fs/allocator.c \
         src/fs/dir_sys.c src/fs/file_sys.c \
         src/kernel/memory.c src/kernel/cpu.c src/kernel/process.c \
         src/kernel/scheduler.c src/kernel/syscall.c src/kernel/kernel_shared.c \
         src/user/user_mgmt.c src/user/env.c \
         src/binaries.c src/main.c src/serve.c src/assembler.c src/editor.c \
         src/compiler/c2s.c src/compiler/lexer.c src/compiler/parser.c \
         src/compiler/codegen.c src/compiler/regalloc.c src/compiler/ast.c
OBJS   = $(SRCS:src/%.c=$(OUTDIR)/%.o)
TARGET = upfs

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

$(OUTDIR)/%.o: src/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OUTDIR) $(TARGET) testimg/

run: $(TARGET)
	./$(TARGET)
