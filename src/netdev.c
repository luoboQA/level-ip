#include "syshead.h"
#include "utils.h"
#include "skbuff.h"
#include "netdev.h"
#include "ethernet.h"
#include "arp.h"
#include "ip.h"
#include "tuntap_if.h"
#include "basic.h"

#include <linux/netlink.h>
#include <linux/rtnetlink.h>

struct netdev *loop;
struct netdev *netdev;
extern int running;

static struct netdev *netdev_alloc(char *addr, char *hwaddr, uint32_t mtu)
{
    struct netdev *dev = malloc(sizeof(struct netdev));

    dev->addr = ip_parse(addr);

    sscanf(hwaddr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &dev->hwaddr[0],
                                                    &dev->hwaddr[1],
                                                    &dev->hwaddr[2],
                                                    &dev->hwaddr[3],
                                                    &dev->hwaddr[4],
                                                    &dev->hwaddr[5]);

    dev->addr_len = 6;
    dev->mtu = mtu;

    return dev;
}

void netdev_init(char *addr, char *hwaddr)
{
    loop = netdev_alloc("127.0.0.1", "00:00:00:00:00:00", 1500);
    netdev = netdev_alloc("10.0.0.4", "00:0c:29:6d:50:25", 1500);
}

int netdev_transmit(struct sk_buff *skb, uint8_t *dst_hw, uint16_t ethertype)
{
    struct netdev *dev;
    struct eth_hdr *hdr;
    int ret = 0;

    dev = skb->dev;

    skb_push(skb, ETH_HDR_LEN);

    hdr = (struct eth_hdr *)skb->data;

    memcpy(hdr->dmac, dst_hw, dev->addr_len);
    memcpy(hdr->smac, dev->hwaddr, dev->addr_len);

    hdr->ethertype = htons(ethertype);
    eth_dbg("out", hdr);

    ret = tun_write((char *)skb->data, skb->len);

    return ret;
}

static int netlink_addattr(struct nlmsghdr *nlh, size_t maxlen,
                           uint16_t type, const void *data, size_t len)
{
    size_t attr_len = RTA_LENGTH(len);
    size_t aligned_len = RTA_ALIGN(attr_len);
    struct rtattr *rta;

    if (NLMSG_ALIGN(nlh->nlmsg_len) + aligned_len > maxlen) return -1;

    rta = (struct rtattr *)((char *)nlh + NLMSG_ALIGN(nlh->nlmsg_len));
    rta->rta_type = type;
    rta->rta_len = attr_len;
    memcpy(RTA_DATA(rta), data, len);
    memset((char *)rta + attr_len, 0, aligned_len - attr_len);
    nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + aligned_len;
    return 0;
}

int netdev_update_neigh(uint32_t addr, const uint8_t *hwaddr)
{
    struct {
        struct nlmsghdr nlh;
        struct ndmsg ndm;
        char attrs[128];
    } req = {0};
    struct {
        struct nlmsghdr nlh;
        struct nlmsgerr error;
    } response = {0};
    struct ifreq ifr = {0};
    struct sockaddr_nl kernel = {0};
    int fd;

    strncpy(ifr.ifr_name, tun_name(), IFNAMSIZ - 1);
    fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd < 0) return -1;

    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        close(fd);
        return -1;
    }

    req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(req.ndm));
    req.nlh.nlmsg_type = RTM_NEWNEIGH;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_REPLACE;
    req.ndm.ndm_family = AF_INET;
    req.ndm.ndm_ifindex = ifr.ifr_ifindex;
    req.ndm.ndm_state = NUD_STALE;
    req.ndm.ndm_type = RTN_UNICAST;

    addr = htonl(addr);
    if (netlink_addattr(&req.nlh, sizeof(req), NDA_DST, &addr,
                        sizeof(addr)) < 0 ||
        netlink_addattr(&req.nlh, sizeof(req), NDA_LLADDR, hwaddr, 6) < 0) {
        close(fd);
        return -1;
    }

    kernel.nl_family = AF_NETLINK;
    if (sendto(fd, &req, req.nlh.nlmsg_len, 0,
               (struct sockaddr *)&kernel, sizeof(kernel)) < 0 ||
        recv(fd, &response, sizeof(response), 0) < 0) {
        close(fd);
        return -1;
    }

    close(fd);
    return response.error.error == 0 ? 0 : -1;
}

static int netdev_receive(struct sk_buff *skb)
{
    struct eth_hdr *hdr = eth_hdr(skb);

    eth_dbg("in", hdr);

    switch (hdr->ethertype) {
        case ETH_P_ARP:
            arp_rcv(skb);
            break;
        case ETH_P_IP:
            ip_rcv(skb);
            break;
        case ETH_P_IPV6:
        default:
            printf("Unsupported ethertype %x\n", hdr->ethertype);
            free_skb(skb);
            break;
    }

    return 0;
}

void *netdev_rx_loop()
{
    while (running) {
        struct sk_buff *skb = alloc_skb(BUFLEN);
        
        if (tun_read((char *)skb->data, BUFLEN) < 0) { 
            perror("ERR: Read from tun_fd");
            free_skb(skb);
            return NULL;
        }

        netdev_receive(skb);
    }

    return NULL;
}

struct netdev* netdev_get(uint32_t sip)
{
    if (netdev->addr == sip) {
        return netdev;
    } else {
        return NULL;
    }
}

void free_netdev()
{
    free(loop);
    free(netdev);
}
