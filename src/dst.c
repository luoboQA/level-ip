#include "syshead.h"
#include "dst.h"
#include "ip.h"
#include "arp.h"

int dst_neigh_output(struct sk_buff *skb)
{
    struct iphdr *iphdr = ip_hdr(skb);
    struct netdev *netdev = skb->dev;
    struct rtentry *rt = skb->rt;
    uint32_t daddr = ntohl(iphdr->daddr);
    uint32_t saddr = ntohl(iphdr->saddr);

    uint8_t *dmac;

    if (rt->flags & RT_GATEWAY) {
        daddr = rt->gateway;
    }
    
    dmac = arp_get_hwaddr(daddr);
    
    if (dmac) {
        return netdev_transmit(skb, dmac, ETH_P_IP);
    } else {
        /* Keep ownership of skb until the ARP reply arrives. */
        if (arp_queue_skb(skb, daddr)) {
            arp_request(saddr, daddr, netdev);
        }

        /* The packet will be transmitted from arp_flush_pending(). Treat it
         * as accepted by the network layer; UDP sendto() must not fail just
         * because neighbor discovery is still in progress. */
        return 0;
    }
}
