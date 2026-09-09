import socket

server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
server.bind(("10.0.0.5", 9000))
data, address = server.recvfrom(65535)
server.sendto(data, address)
server.close()
