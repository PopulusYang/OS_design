# UPFS 模拟 UNIX 文件系统 —— 构建脚本

CC      ?= cc
CFLAGS  ?= -Wall -Wextra -Werror -std=c17 -D_GNU_SOURCE -Iinclude -Isrc
LDFLAGS ?= -pthread

SRCS = src/disk_io.c src/format.c src/allocator.c src/dir_sys.c src/file_sys.c src/main.c
OBJS = $(SRCS:.c=.o)
TARGET = upfs

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) vfs_disk.img

run: $(TARGET)
	./$(TARGET)
