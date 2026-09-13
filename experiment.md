# 主线
主线顺序：APP->liblevelip->IPC->socket->TCP/UDP->IP->DST/Route/ARP->Ethernet->TUN/TAP
反：TUN/TAP -> Ethernet -> IP/ARP -> TCP/UDP -> socket -> IPC -> liblevelip -> APP
正向是你主动调函数一层层push头构造send下去；反向是CORE线程read(TAP)一层层pull头上来，最后靠wakeup把等在IPC里的APP叫醒
宿主机内核网络栈  ←→  TAP 设备 (10.0.0.5)
                          ↑↓ 以太网帧
本程序协议栈     ←→  netdev (10.0.0.4)
内核和 Level-IP 各有一套完整的协议栈,网卡（TAP）管到 L2，只搬运以太网帧，不解析。

两个 IP 要同网段，直接通过 ARP + 以太网帧通信，无需网关：
10.0.0.4（程序）和 10.0.0.5（TAP）在 10.0.0.0/24。
程序发的帧，源 IP 10.0.0.4，经 TAP 到宿主机。
本程序自己实现 ARP，维护自己的 IP↔MAC 映射。
但宿主机内核也有自己的邻居表（ARP 缓存）。
当内核要转发帧时，如果它的 ARP 表里没有某 IP 的 MAC，就会广播 ARP 询问。

`
方向 A：Level-IP 发送 → 内核处理
Level-IP 构造以太网帧
    │  src IP = 10.0.0.4
    │  dst IP = ?（决定去向）
    ▼
tun_write(fd, frame, len)
    │
    ▼
写入 /dev/net/tap
    │
    ▼
内核从 tap0 收到帧
    │
    ▼
【链路层】检查以太头
    ├─ 目的 MAC 是 tap0 / 广播 / 组播？
    │   否 → 丢弃
    ├─ ethertype = 0x0800 (IP)？
    │   0x0806 (ARP) → 内核 ARP 处理
    │   其他        → 丢弃 / 按协议
    ▼
【网络层】检查 IP 头
    ├─ 校验和、版本、长度正确？
    │   否 → 丢弃
    ├─ 目的 IP 是谁？
    │
    ├─ ① = 10.0.0.5（tap0 本机地址）
    │      → 内核自己处理
    │      → 传输层 TCP/UDP/ICMP
    │      → 交给 socket / 回包
    │      （帧不回到 Level-IP）
    │
    ├─ ② = 10.0.0.4（同网段邻居）
    │      → 内核转发
    │      → 查邻居表（ARP 缓存）
    │          ├─ 有 MAC → 从 tap0 发出
    │          └─ 无 MAC → 发 ARP 请求
    │      → 帧从 tap0 发出
    │      → Level-IP 的 tun_read 读到
    │      → Level-IP 继续拆（ip_rcv → TCP/UDP）
    │
    └─ ③ = 其他网段（如 8.8.8.8）
           → 查路由表
               ├─ 有网关 → 从网关接口发出（如 eth0）→ 物理网络
               └─ 无网关 → 丢弃（ICMP 不可达）

方向 B：外部 → 内核 → Level-IP（接收）
外部帧 (dst IP = 10.0.0.4, dst MAC = tap0 的 MAC)
    │
    ▼
内核收到帧（从物理网卡 / 路由转发 / 本机进程发出）
    │
    ▼
【链路层】目的 MAC 是 tap0 的 MAC？
    │  是
    ▼
内核把帧"交给 tap0"
    │
    ▼
tap0 的另一端是 /dev/net/tap
    │
    ▼
帧被放进 TAP 的读队列
    │
    ▼
Level-IP 的 tun_read 读到
    │
    ▼
【Level-IP 链路层】netdev_receive 看 ethertype
    ├─ ARP → arp_rcv
    ├─ IP  → ip_rcv
    └─ 其他 → 丢弃
    │
    ▼
【Level-IP 网络层】ip_rcv 看目的 IP
    ├─ = 10.0.0.4（netdev->addr）？
    │    是 → 继续拆 → TCP/UDP
    │    否 → 丢弃（或转发）

方向 C：Level-IP 发送 → 外部（经内核转发） 这是方向 A 的情况 ③单列出来
Level-IP 构造帧 (src IP = 10.0.0.4, dst IP = 8.8.8.8)
    │
    ▼
tun_write → /dev/net/tap
    │
    ▼
内核从 tap0 收到帧
    │
    ▼
【链路层】检查以太头 ✓
    │
    ▼
【网络层】检查 IP 头
    │  目的 IP = 8.8.8.8
    │  不在同网段
    ▼
查路由表 → 找默认网关
    │
    ▼
从网关接口发出（如 eth0）
    │
    ▼
物理网络 → 到达 8.8.8.8

tap0 网卡
   │
   ├─ 发送方向：内核把帧写入 tap0 的发送队列
   │              → 队列里的帧等待被"另一端"读走
   │
   └─ 接收方向：内核从 tap0 收到帧
                  → 帧来自"另一端"写入
`
# TCP实验流程：
开启 IP 转发
sudo sysctl -w net.ipv4.ip_forward=1

配置 iptables 规则（使用 eth2 接口）
sudo iptables -I INPUT --source 10.0.0.0/24 -j ACCEPT
sudo iptables -t nat -I POSTROUTING --out-interface eth2 -j MASQUERADE
sudo iptables -I FORWARD --in-interface eth2 --out-interface tap0 -j ACCEPT
sudo iptables -I FORWARD --in-interface tap0 --out-interface eth2 -j ACCEPT

验证规则
sudo iptables -t nat -L -n -v | grep 10.0
sudo iptables -L FORWARD -n -v | grep tap0

给 ip 命令设置 capabilities
sudo setcap cap_net_admin=ep /usr/bin/ip

验证 lvl-ip 的 capabilities
getcap ./lvl-ip
应显示: ./lvl-ip = cap_setpcap,cap_net_admin=ep

terminal1: 启动协议栈
sudo ./lvl-ip

terminal2: 功能测试
1. Ping 测试
ping -c 4 10.0.0.4

2. HTTP 请求测试
cd tools
./level-ip ../apps/curl/curl example.com 80
./level-ip ../apps/curl/curl baidu.com 80

3. HTTPS 测试
cd tools
./level-ip ../apps/curl/curl example.com 443

4. 使用 curl-poll 工具
cd tools
./level-ip ../apps/curl-poll/curl-poll example.com 80

terminal3: 抓包分析
1. 监控 tap0 接口所有流量
sudo tcpdump -i tap0 -n -vv
2. 监控特定 IP 的流量
sudo tcpdump -i any host 10.0.0.4 -n
3. 只监控 HTTP 流量（端口 80）
sudo tcpdump -i tap0 -n port 80
4. 保存抓包文件供 Wireshark 分析
sudo tcpdump -i tap0 -w level-ip.pcap -n
5. 读取保存的抓包文件
tcpdump -r level-ip.pcap -n

编译调试版本（日志保存到debug-xxxx.log）
make debug
# 项目结构
  一次 HTTP 请求大致经过下面这条路径：
  curl
    │ socket/connect/write/read
    ▼
  liblevelip.so
    │ 拦截 libc（内核态） 的 socket API
    ▼
  Unix Domain Socket
    │ IPC
    ▼
  lvl-ip 进程
    │
    ├─ TCP
    ├─ IPv4
    ├─ ARP
    └─ Ethernet
    │
    ▼
  TAP 虚拟网卡
    │
    ▼
  Linux 主机转发 / NAT
    │
    ▼
  真实网络

  协议栈可以理解成一组层层嵌套的包装。

  发送数据时：

  应用数据
    ↓ TCP 加 TCP 头
  TCP 段
    ↓ IPv4 加 IP 头
  IP 数据报
    ↓ Ethernet 加以太网头
  以太网帧

  例如发送字符串：

  GET / HTTP/1.1

  最终在网线上大致是：

  Ethernet Header
    IPv4 Header
      TCP Header
        HTTP Data

  接收时顺序相反：

  Ethernet
    → IPv4
      → TCP
        → 应用程序

  在本项目中，接收入口是：

  src/netdev.c

  # TAP 和 TUN 的区别

  本项目使用的是 TAP：

  ifr.ifr_flags = IFF_TAP | IFF_NO_PI;

  TAP 提供的是完整的二层 Ethernet 帧：

  目标 MAC
  源 MAC
  EtherType
  数据

  所以项目需要自己处理：

  - Ethernet
  - ARP
  - IPv4
  - TCP

  如果使用 TUN，则通常只会得到三层 IP 数据包，Ethernet 和 ARP 会由系统处理。

  这里的 TAP 网卡相当于给用户态程序一根“虚拟网线”。

  项目设置了：

  TAP IP: 10.0.0.5
  Level-IP IP: 10.0.0.4
  网段: 10.0.0.0/24

  其中：

  - 10.0.0.5 是 Linux 主机上的 TAP 接口地址
  - 10.0.0.4 是 Level-IP 自己认为拥有的地址

  # 第一层：Ethernet

  Ethernet 帧头定义在：

  include/ethernet.h

  struct eth_hdr
  {
      uint8_t  dmac[6];
      uint8_t  smac[6];
      uint16_t ethertype;
      uint8_t  payload[];
  };

  含义是：

  dmac       目标 MAC 地址
  smac       源 MAC 地址
  ethertype  上层协议类型
  payload    上层数据

  常见 EtherType：

  0x0800  IPv4
  0x0806  ARP
  0x86dd  IPv6

  发送时，netdev_transmit() 会添加 Ethernet 头：

  skb_push(skb, ETH_HDR_LEN);

  memcpy(hdr->dmac, dst_hw, dev->addr_len);
  memcpy(hdr->smac, dev->hwaddr, dev->addr_len);

  hdr->ethertype = htons(ethertype);
  tun_write((char *)skb->data, skb->len);

  这里的 skb_push() 很重要。它会把数据指针向前移动，为当前协议层预留头部空间。

  可以把 sk_buff 想象成一块可不断向前扩展的报文内存：

  [预留空间][应用数据]
            ↑ data

  添加 TCP、IP、Ethernet 头时：

  [TCP][应用数据]
  [IP][TCP][应用数据]
  [ETH][IP][TCP][应用数据]

  ———

  # 第二层：ARP

  IP 地址是逻辑地址，例如：

  10.0.0.4

  MAC 地址是二层硬件地址，例如：

  00:0c:29:6d:50:25

  发送 Ethernet 帧时必须知道目标 MAC，因此需要 ARP：

  谁拥有 10.0.0.5？
  请告诉 00:0c:29:6d:50:25

  ARP 请求是广播：

  目标 MAC = ff:ff:ff:ff:ff:ff

  ARP 回复则告诉发送方：

  10.0.0.5 对应 aa:bb:cc:dd:ee:ff

  项目的 ARP 处理主要在：

  src/arp.c

  接收 ARP：

  void arp_rcv(struct sk_buff *skb)

  发送 ARP 请求：

  int arp_request(uint32_t sip, uint32_t dip, struct netdev *netdev)

  ARP 缓存函数：

  unsigned char* arp_get_hwaddr(uint32_t sip)

  注意一个重要概念：

  IP 决定“发给哪台机器”
  MAC 决定“在当前二层网络发给哪个网卡”

  IP 可以跨路由器，而 MAC 地址只在当前链路有效。

  ———

  # 第三层：IPv4

  IPv4 头定义在：

  include/ip.h

  重要字段：

  version       IPv4 版本
  ihl           IP 头长度
  len           整个 IP 数据报长度
  ttl           生存时间
  proto         上层协议
  csum          IP 头校验和
  saddr         源 IP
  daddr         目标 IP

  接收入口：

  int ip_rcv(struct sk_buff *skb)

  它做几件事：

  if (ih->version != IPV4)
      丢弃;

  if (ih->ihl < 5)
      丢弃;

  if (ih->ttl == 0)
      丢弃;

  检查 IP 校验和;

  根据 proto 分发;

  根据 proto：

  switch (ih->proto) {
  case ICMPV4:
      icmpv4_incoming(skb);
      break;
  case IP_TCP:
      tcp_in(skb);
      break;
  }

  常见 IPv4 协议号：

  1   ICMP
  6   TCP
  17  UDP

  发送 IP 数据包的代码在：

  src/ip_output.c

  核心流程：

  route_lookup(sk->daddr);
  skb_push(skb, IP_HDR_LEN);
  填写 IPv4 头;
  计算校验和;
  dst_neigh_output(skb);

  IPv4 层主要解决：

  1. 目标 IP 是谁？
  2. 应该从哪个网卡发送？
  3. 是否需要经过网关？
  4. 上层协议是什么？
  5. 数据包是否有效？

  ———

  # 路由

  路由表决定：

  目标 IP → 下一步从哪个接口发送

  项目中路由相关代码：

  src/route.c
  include/route.h

  调用方式：

  rt = route_lookup(sk->daddr);

  可以把路由理解为：

  目标地址 10.0.0.5
  匹配 10.0.0.0/24
  从 tap0 发送

  现实中的 Linux 路由器还会决定：

  目标地址 8.8.8.8
  匹配默认路由 0.0.0.0/0
  交给网关 192.168.1.1
  从 eth0 发送

  本项目为了简化，很多地方仍然是实验性质的，例如 TCP 源地址硬编码为：

  sk->saddr = parse_ipv4_string("10.0.0.4");

  ———

  # 第四层：ICMP 和 Ping

  Ping 并不使用 TCP，而是使用 ICMP Echo：

  ICMP Echo Request
  ICMP Echo Reply

  代码：

  src/icmpv4.c

  收到 Ping：

  case ICMP_V4_ECHO:
      icmpv4_reply(skb);

  回复时：

  icmp->type = ICMP_V4_REPLY;
  skb->protocol = ICMPV4;
  sk.daddr = iphdr->saddr;
  ip_output(&sk, skb);

  因此一次 Ping 的路径是：

  Ethernet
    → IPv4
      → ICMP Echo Request
      ← ICMP Echo Reply

  Ping 可以验证：

  - TAP 是否工作
  - Ethernet 是否工作
  - ARP 是否工作
  - IPv4 是否工作
  - ICMP 是否工作

  但 Ping 成功并不代表 TCP 一定正确。

  ———

  # 第五层：TCP

  TCP 是这个项目最重要、也最复杂的部分。

  TCP 主要提供：

  - 面向连接
  - 可靠传输
  - 按序交付
  - 丢包重传
  - 流量控制
  - 半关闭和连接终止

  TCP 头定义在：

  include/tcp.h

  关键字段：

  sport       源端口
  dport       目标端口
  seq         序列号
  ack_seq     确认号
  syn         建立连接
  ack         确认数据
  fin         关闭连接
  rst         重置连接
  win         接收窗口
  csum        TCP 校验和

  ## TCP 三次握手

  客户端首先发送：

  SYN
  seq = x

  服务器回复：

  SYN + ACK
  seq = y
  ack = x + 1

  客户端再回复：

  ACK
  ack = y + 1

  代码中的 TCP 状态包括：

  TCP_CLOSE
  TCP_SYN_SENT
  TCP_SYN_RECEIVED
  TCP_ESTABLISHED
  TCP_FIN_WAIT_1
  TCP_FIN_WAIT_2
  TCP_CLOSE_WAIT
  TCP_LAST_ACK
  TCP_TIME_WAIT

  客户端连接大致是：

  TCP_CLOSE
    ↓ connect()
  TCP_SYN_SENT
    ↓ 收到 SYN+ACK
  TCP_ESTABLISHED

  服务端收到连接请求时则通常是：

  TCP_LISTEN
    ↓ 收到 SYN
  TCP_SYN_RECEIVED
    ↓ 收到 ACK
  TCP_ESTABLISHED

  接收 TCP 数据的入口：

  void tcp_in(struct sk_buff *skb)

  发送 TCP 数据的主要代码：

  src/tcp_output.c

  处理 TCP 状态和数据的主要代码：

  src/tcp_input.c

  ———

  # TCP 为什么需要序列号和确认号

  假设应用发送：

  ABCDE

  TCP 不直接认为“整段数据都成功了”，而是给字节编号：

  A: 1000
  B: 1001
  C: 1002
  D: 1003
  E: 1004

  接收方回复：

  ACK = 1005

  意思是：

  1005 之前的字节我都收到了，
  下一个希望收到 1005。

  项目中的传输控制块：

  struct tcb {
      uint32_t snd_una;
      uint32_t snd_nxt;
      uint32_t snd_wnd;
      uint32_t iss;
      uint32_t rcv_nxt;
      uint32_t rcv_wnd;
      uint32_t irs;
  };

  这些变量非常重要：

  snd_una  最早尚未确认的序列号
  snd_nxt  下一个准备发送的序列号
  rcv_nxt  下一个期待接收的序列号
  snd_wnd  对方允许我发送多少
  rcv_wnd  我允许对方发送多少

  TCP 的发送队列：

  sk->write_queue

  TCP 的接收队列：

  sk->receive_queue

  如果发送的数据没有及时收到 ACK，就会进入重传逻辑。

  项目中重传相关代码：

  src/tcp_output.c
  src/timer.c

  相关函数：

  tcp_rearm_rto_timer()
  tcp_retransmission_timeout()
  tcp_stop_rto_timer()

  其中 RTO 是 Retransmission Timeout，即重传超时时间。

  ———

  # Socket API 是如何接入协议栈的

  应用程序调用的是普通 Linux API：

  socket()
  connect()
  write()
  read()
  close()

  但是 liblevelip.so 重写了这些函数。

  例如：

  int socket(int domain, int type, int protocol)

  它会判断：

  AF_INET + SOCK_STREAM + TCP

  如果是支持的 TCP socket，就不调用真正的 Linux socket，而是：

  1. 创建一个 Unix Domain Socket
  2. 连接 /tmp/lvlip.socket
  3. 把 socket() 请求发送给 lvl-ip
  4. 等待 Level-IP 返回结果

  因此：

  应用程序的 socket()
          ↓
  liblevelip.so 的 socket()
          ↓
  Unix Domain Socket
          ↓
  lvl-ip 的 _socket()
          ↓
  创建 Level-IP 内部 socket

  IPC 消息类型在：

  include/ipc.h

  例如：

  IPC_SOCKET
  IPC_CONNECT
  IPC_WRITE
  IPC_READ
  IPC_CLOSE
  IPC_POLL

  这是非常值得学习的地方：它把“应用程序接口”和“网络协议实现”分离开了。
 
  # 从 curl 到真实网络的完整过程

  假设运行：

  ./level-ip ../apps/curl/curl example.com 80

  大致过程如下：

  1. curl 调用 socket(AF_INET, SOCK_STREAM, TCP)

  2. liblevelip.so 拦截 socket()

  3. liblevelip.so 通过 Unix Socket 把 IPC_SOCKET 发给 lvl-ip

  4. lvl-ip 创建内部 socket

  5. curl 调用 connect()

  6. Level-IP 创建 TCP SYN

  7. TCP 加 TCP 头

  8. IPv4 加 IP 头

  9. ARP 查询下一跳 MAC

  10. Ethernet 加二层头

  11. 数据写入 TAP

  12. Linux 主机转发并 NAT

  13. 远端服务器回复 SYN+ACK

  14. TAP 收到 Ethernet 帧

  15. netdev_receive() 分发给 ip_rcv()

  16. ip_rcv() 分发给 tcp_in()

  17. TCP 状态变成 ESTABLISHED

  18. curl 调用 write()

  19. HTTP 数据通过 TCP 发送

  20. curl 调用 read()

  21. TCP 接收并按序重组数据

  22. 数据通过 IPC 返回给 curl

  这就是整个项目的主线。
  # UDP 实验

  UDP 现在支持完整的数据报接口：

  - `socket(AF_INET, SOCK_DGRAM, 0)`
  - `bind()` 绑定本地地址和端口
  - `sendto()` 指定目标地址发送一个 UDP 数据报
  - `recvfrom()` 接收数据并获得发送方地址

  构建 UDP 测试程序：

  ```bash
  make udp-test
  ```

  运行 UDP Echo 测试。测试程序会启动一个绑定在 `10.0.0.5:9000` 的 Linux UDP 服务端，Level-IP 通过 TAP 接口向它发送数据，再接收 Echo 回复：

  ```bash
  terminal1: sudo ./lvl-ip
  terminal2: cd tests && sudo ./suites/udp/suite-udp
  ```

  运行全部测试时，UDP 测试已由 `tests/test-run-all` 自动执行：

  ```bash
  make test
  ```

  UDP 报文抓包：

  ```bash
  sudo tcpdump -i tap0 -n -vv udp
  sudo tcpdump -i tap0 -n -vv udp port 9000
  ```

  一次 UDP 发送的协议路径是：

  ```text
  sendto()
    → IPC_SENDTO
    → udp_sendto()
    → UDP 头
    → IPv4 头
    → ARP 查询下一跳 MAC
    → Ethernet 帧
    → TAP
  ```

  如果 ARP 缓存中没有下一跳的 MAC 地址，Level-IP 现在会：

  1. 把待发送的 IP 数据包放入 ARP pending 队列；
  2. 只发送一次 ARP Request，避免重复广播；
  3. 收到 ARP Reply 后学习 MAC 地址；
  4. 自动重发队列中的所有数据包。

  相关实现位于 `src/dst.c` 和 `src/arp.c`。

  ## 单向 UDP 发送工具

  项目还提供一个只发送、不等待回复的 UDP 应用：

  启动服务器  
  ```python3 tests/suites/udp/udp-server.py  ```
  
  ```bash
  make apps
  ./tools/level-ip ./apps/udp/udp-send 10.0.0.5 9000 "hello udp"
  ```

  程序调用 `sendto()` 后立即退出。配合抓包观察：

  ```bash
  sudo tcpdump -i tap0 -n -vv udp port 9000
  ```

  这个工具用于验证 UDP 发送路径；网页内容仍使用 TCP 应用和 curl 获取。
