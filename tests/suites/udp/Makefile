CC ?= cc
CFLAGS ?= -Wall -Wextra -Werror

all: udp-client

udp-client: udp-client.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f udp-client
