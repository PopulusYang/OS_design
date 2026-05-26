CC     = cc
CFLAGS = -Wall -Wextra -Werror -std=c17 -D_GNU_SOURCE
LDLIBS = -lutil

all: websrv

websrv: websrv.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f websrv

.PHONY: all clean
