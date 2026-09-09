CFLAGS = -Wall -Wextra -Werror -Wno-address-of-packed-member -Wno-sign-compare -Wno-unused-parameter -I include

src = $(wildcard src/*.c)
obj = $(patsubst src/%.c, build/%.o, $(src))
headers = $(wildcard include/*.h)
apps = apps/curl/curl

lvl-ip: $(obj)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(obj) -o lvl-ip
	@# Creating TUN/TAP devices (if they exist, ignore errors)
	@echo "Checking TUN/TAP devices..."
	@sudo sh -c 'mkdir -p /dev/net'
	@sudo sh -c 'mknod /dev/net/tap c 10 200 2>/dev/null || true'
	@sudo sh -c 'mknod /dev/net/tun c 10 200 2>/dev/null || true'
	@sudo chmod 0666 /dev/net/tap 2>/dev/null || true
	@sudo chmod 0666 /dev/net/tun 2>/dev/null || true
	@sudo modprobe tun
	@echo "Device setup complete."
	@echo
	@echo "lvl-ip needs CAP_NET_ADMIN:"
	sudo setcap cap_setpcap,cap_net_admin=ep lvl-ip

build/%.o: src/%.c ${headers}
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

debug: CFLAGS+= -DDEBUG_SOCKET -DDEBUG_TCP -g 
debug: lvl-ip
	sudo ./lvl-ip 2>&1 | tee debug-$(shell date +%Y%m%d_%H%M%S).log

apps: $(apps)
	$(MAKE) -C tools
	$(MAKE) -C apps/curl
	$(MAKE) -C apps/curl-poll

all: lvl-ip apps

test: CFLAGS += -DDEBUG_SOCKET -DDEBUG_TCP -g
test: lvl-ip apps
	@echo
	@echo "Networking capabilites are required for test dependencies:"
	which arping | sudo xargs setcap cap_net_raw=ep
	which tc | sudo xargs setcap cap_net_admin=ep
	@echo
	cd tests && sudo ./test-run-all

clean:
	rm -f build/*.o lvl-ip
	rm apps/curl/curl
	rm apps/curl-poll/curl-poll
	rm -f debug-*
	rm -f lvl-ip-test.log
	@echo "Cleaned."
