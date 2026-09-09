#include "syshead.h"
#include "udp.h"
#include "tcp.h"
#include "skbuff.h"
#include "ip.h"
#include "utils.h"
#include "wait.h"

static uint16_t udp_next_port = 41000;

struct net_ops udp_ops = {
    .alloc_sock = &udp_alloc_sock,
    .init = &udp_init_sock,
    .bind = &udp_bind,
    .connect = &udp_connect,
    .sendto = &udp_sendto,
    .disconnect = &udp_disconnect,
    .write = &udp_write,
    .read = &udp_read,
    .recvfrom = &udp_recvfrom,
    .recv_notify = &udp_recv_notify,
    .close = &udp_close,
    .abort = &udp_abort,
};

static uint16_t udp_alloc_port(void) { return ++udp_next_port; }

static uint16_t udp_checksum(uint8_t *data, uint32_t saddr,
                             uint32_t daddr, uint16_t len)
{
    struct udp_pseudo {
        uint32_t saddr;
        uint32_t daddr;
        uint8_t zero;
        uint8_t proto;
        uint16_t len;
    } __attribute__((packed)) pseudo;

    pseudo.saddr = htonl(saddr);
    pseudo.daddr = htonl(daddr);
    pseudo.zero = 0;
    pseudo.proto = IP_UDP;
    pseudo.len = htons(len);
    return checksum(data, len, checksum(&pseudo, sizeof(pseudo), 0));
}

struct sock *udp_alloc_sock(int protocol)
{
    struct udp_sock *usk = calloc(1, sizeof(*usk));
    (void)protocol;
    return &usk->sk;
}

int udp_init_sock(struct sock *sk)
{
    sk->state = TCP_CLOSE;
    return 0;
}

int udp_bind(struct sock *sk, const struct sockaddr *addr, int addrlen)
{
    const struct sockaddr_in *in = (const struct sockaddr_in *)addr;
    uint16_t port;

    if (!addr || addrlen < (int)sizeof(*in) || in->sin_family != AF_INET)
        return -EINVAL;

    port = ntohs(in->sin_port);
    sk->sport = port ? port : udp_alloc_port();
    sk->saddr = ntohl(in->sin_addr.s_addr);
    if (sk->saddr == INADDR_ANY) sk->saddr = parse_ipv4_string("10.0.0.4");
    return 0;
}

int udp_connect(struct sock *sk, const struct sockaddr *addr, int addrlen, int flags)
{
    const struct sockaddr_in *in = (const struct sockaddr_in *)addr;
    (void)addrlen;
    (void)flags;
    if (!addr || in->sin_family != AF_INET) return -EAFNOSUPPORT;

    if (!sk->sport) sk->sport = udp_alloc_port();
    sk->dport = ntohs(in->sin_port);
    if (!sk->saddr) sk->saddr = parse_ipv4_string("10.0.0.4");
    sk->daddr = ntohl(in->sin_addr.s_addr);
    sk->state = TCP_ESTABLISHED;
    sock_connected(sk);
    return 0;
}

int udp_disconnect(struct sock *sk, int flags)
{
    (void)flags;
    sk->state = TCP_CLOSE;
    return 0;
}

int udp_write(struct sock *sk, const void *buf, int len)
{
    return udp_sendto(sk, buf, len, NULL, 0, 0);
}

int udp_sendto(struct sock *sk, const void *buf, int len,
               const struct sockaddr *addr, int addrlen, int flags)
{
    struct sk_buff *skb;
    struct udphdr *uh;
    int total, rc;
    struct sock output;

    struct sockaddr_in destination;
    uint32_t daddr = sk->daddr;
    uint16_t dport = sk->dport;
    uint32_t saddr = sk->saddr;

    (void)flags;
    if (addr) {
        if (addrlen < (int)sizeof(destination) ||
            addr->sa_family != AF_INET) return -EAFNOSUPPORT;
        memcpy(&destination, addr, sizeof(destination));
        daddr = ntohl(destination.sin_addr.s_addr);
        dport = ntohs(destination.sin_port);
    }
    if (!dport) return -EDESTADDRREQ;
    if (!sk->sport) sk->sport = udp_alloc_port();
    if (!saddr) sk->saddr = saddr = parse_ipv4_string("10.0.0.4");
    if (len < 0 || len > 65507) return -EMSGSIZE;

    total = ETH_HDR_LEN + IP_HDR_LEN + UDP_HDR_LEN + len;
    skb = alloc_skb(total);
    if (!skb) return -ENOMEM;
    skb_reserve(skb, total);
    skb->protocol = IP_UDP;
    skb->dlen = len;
    skb_push(skb, len);
    memcpy(skb->data, buf, len);

    uh = (struct udphdr *)skb_push(skb, UDP_HDR_LEN);
    uh->source = htons(sk->sport);
    uh->dest = htons(dport);
    uh->len = htons(UDP_HDR_LEN + len);
    uh->check = 0;
    uh->check = udp_checksum(skb->data, saddr, daddr, UDP_HDR_LEN + len);
    if (uh->check == 0) uh->check = 0xffff;

    output = *sk;
    output.saddr = saddr;
    output.daddr = daddr;
    output.dport = dport;
    rc = ip_output(&output, skb);
    free_skb(skb);
    return rc < 0 ? rc : len;
}

void udp_in(struct sk_buff *skb)
{
    struct iphdr *ih = ip_hdr(skb);
    struct udphdr *uh = (struct udphdr *)ih->data;
    uint16_t len = ntohs(uh->len);
    struct socket *sock;
    struct sock *sk;

    if (len < UDP_HDR_LEN || len > ip_len(ih)) goto drop;
    if (uh->check != 0 && udp_checksum((uint8_t *)uh, ih->saddr, ih->daddr, len) != 0)
        goto drop;

    sock = socket_lookup(ntohs(uh->source), ntohs(uh->dest));
    if (!sock) goto drop;
    sk = sock->sk;

    skb->payload = (uint8_t *)(uh + 1);
    skb->dlen = len - UDP_HDR_LEN;
    skb->seq = ih->saddr;
    skb->end_seq = ntohs(uh->source);
    skb->refcnt++;
    skb_queue_tail(&sk->receive_queue, skb);
    sk->poll_events |= (POLLIN | POLLPRI | POLLRDNORM | POLLRDBAND);
    sk->ops->recv_notify(sk);
    return;

drop:
    free_skb(skb);
}

int udp_read(struct sock *sk, void *buf, int len)
{
    return udp_recvfrom(sk, buf, len, 0, NULL, NULL);
}

int udp_recvfrom(struct sock *sk, void *buf, int len, int flags,
                 struct sockaddr *addr, socklen_t *addrlen)
{
    struct sk_buff *skb;
    int copied;

    (void)flags;
    for (;;) {
        skb = skb_peek(&sk->receive_queue);
        if (skb) break;
        if (sk->sock->flags & O_NONBLOCK) return -EAGAIN;
        pthread_mutex_lock(&sk->recv_wait.lock);
        socket_release(sk->sock);
        wait_sleep(&sk->recv_wait);
        pthread_mutex_unlock(&sk->recv_wait.lock);
        socket_wr_acquire(sk->sock);
    }

    copied = skb->dlen > len ? len : skb->dlen;
    memcpy(buf, skb->payload, copied);
    if (addr && addrlen && *addrlen >= sizeof(struct sockaddr_in)) {
        struct sockaddr_in *source = (struct sockaddr_in *)addr;
        memset(source, 0, sizeof(*source));
        source->sin_family = AF_INET;
        source->sin_port = htons((uint16_t)skb->end_seq);
        source->sin_addr.s_addr = htonl(skb->seq);
        *addrlen = sizeof(*source);
    }
    skb_dequeue(&sk->receive_queue);
    skb->refcnt--;
    free_skb(skb);
    if (skb_queue_empty(&sk->receive_queue)) sk->poll_events &= ~POLLIN;
    return copied;
}

int udp_recv_notify(struct sock *sk) { return wait_wakeup(&sk->recv_wait); }
int udp_close(struct sock *sk) { sk->state = TCP_CLOSE; return 0; }
int udp_abort(struct sock *sk) { return udp_close(sk); }
