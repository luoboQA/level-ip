#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s <ipv4> <port> <message>\n", program);
}

int main(int argc, char **argv)
{
    struct sockaddr_in destination = {0};
    char *end;
    long port;
    int fd;
    ssize_t sent;

    if (argc != 4) {
        usage(argv[0]);
        return 1;
    }

    port = strtol(argv[2], &end, 10);
    if (*argv[2] == '\0' || *end != '\0' || port < 1 || port > 65535) {
        fprintf(stderr, "Invalid UDP port: %s\n", argv[2]);
        return 1;
    }

    destination.sin_family = AF_INET;
    destination.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, argv[1], &destination.sin_addr) != 1) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", argv[1]);
        return 1;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    sent = sendto(fd, argv[3], strlen(argv[3]), 0,
                  (struct sockaddr *)&destination, sizeof(destination));
    if (sent < 0) {
        perror("sendto");
        close(fd);
        return 1;
    }

    printf("sent %zd bytes to %s:%ld\n", sent, argv[1], port);
    close(fd);
    return 0;
}
