#ifndef UDP_H_
#define UDP_H_

#include "syshead.h"
#include "ip.h"
#include "sock.h"

#define UDP_HDR_LEN sizeof(struct udphdr)

struct udphdr {
    uint16_t source;
    uint16_t dest;
    uint16_t len;
    uint16_t check;
} __attribute__((packed));

struct udp_sock { struct sock sk; };
#define udp_sk(sk) ((struct udp_sock *)(sk))

extern struct net_ops udp_ops;
void udp_in(struct sk_buff *skb);
struct sock *udp_alloc_sock(int protocol);
int udp_init_sock(struct sock *sk);
int udp_bind(struct sock *sk, const struct sockaddr *addr, int addrlen);
int udp_connect(struct sock *sk, const struct sockaddr *addr, int addrlen, int flags);
int udp_sendto(struct sock *sk, const void *buf, int len,
               const struct sockaddr *addr, int addrlen, int flags);
int udp_disconnect(struct sock *sk, int flags);
int udp_write(struct sock *sk, const void *buf, int len);
int udp_read(struct sock *sk, void *buf, int len);
int udp_recvfrom(struct sock *sk, void *buf, int len, int flags,
                 struct sockaddr *addr, socklen_t *addrlen);
int udp_close(struct sock *sk);
int udp_abort(struct sock *sk);
int udp_recv_notify(struct sock *sk);

#endif
