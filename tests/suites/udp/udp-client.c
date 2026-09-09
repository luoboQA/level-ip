#include <arpa/inet.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
    const char message[] = "level-ip udp echo";
    char reply[sizeof(message)] = {0};
    struct sockaddr_in local = {0};
    struct sockaddr_in remote = {0};
    struct sockaddr_in source = {0};
    socklen_t source_len = sizeof(source);
    struct pollfd pfd;
    int fd;
    ssize_t sent;
    ssize_t received;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return 1;

    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(0);
    if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) return 2;

    remote.sin_family = AF_INET;
    remote.sin_port = htons(9000);
    if (inet_pton(AF_INET, "10.0.0.5", &remote.sin_addr) != 1) return 3;

    sent = sendto(fd, message, sizeof(message), 0,
                  (struct sockaddr *)&remote, sizeof(remote));
    if (sent != (ssize_t)sizeof(message)) return 4;

    pfd.fd = fd;
    pfd.events = POLLIN;
    if (poll(&pfd, 1, 3000) != 1) return 5;

    received = recvfrom(fd, reply, sizeof(reply), 0,
                        (struct sockaddr *)&source, &source_len);
    if (received != (ssize_t)sizeof(message)) return 6;
    if (memcmp(reply, message, sizeof(message)) != 0) return 7;
    if (source.sin_family != AF_INET || ntohs(source.sin_port) != 9000) return 8;

    close(fd);
    puts("UDP echo passed");
    return 0;
}
