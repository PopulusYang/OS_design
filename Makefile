# UPFS 模拟 UNIX 文件系统 —— 构建脚本

CC      ?= cc
CFLAGS  ?= -Wall -Wextra -Werror -std=c17 -I.
LDFLAGS ?= -pthread

SRCS = disk_io.c format.c allocator.c dir_sys.c file_sys.c main.c
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
