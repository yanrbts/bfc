CC = gcc
CFLAGS = -Wall -Wextra -g

# 默认目标
all: a.out

# 生成可执行文件
a.out: wkey.c wkey.h
	$(CC) $(CFLAGS) wkey.c -o a.out

# 清理生成的文件
clean:
	rm -f a.out
