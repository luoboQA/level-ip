CC ?= cc
CFLAGS ?= -Wall -Wextra -Werror

all: udp-send

udp-send: udp-send.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f udp-send
