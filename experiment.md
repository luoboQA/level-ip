# 实验流程：
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
cd apps/curl-poll
./curl-poll example.com 80

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