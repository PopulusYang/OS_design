# UPFS 模拟 UNIX 文件系统 —— 构建脚本

CC      ?= cc
CFLAGS  ?= -Wall -Wextra -Werror -std=c17 -D_GNU_SOURCE -Iinclude -Isrc
LDFLAGS ?= -pthread

OUTDIR = out
SRCS   = src/disk_io.c src/format.c src/allocator.c src/dir_sys.c src/file_sys.c src/user_mgmt.c src/main.c
OBJS   = $(SRCS:src/%.c=$(OUTDIR)/%.o)
TARGET = upfs

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

$(OUTDIR)/%.o: src/%.c | $(OUTDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR):
	mkdir -p $(OUTDIR)

clean:
	rm -rf $(OUTDIR) $(TARGET) vfs_disk.img

run: $(TARGET)
	./$(TARGET)
