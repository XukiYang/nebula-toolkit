#pragma once
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>

namespace nebula {
namespace net {
namespace transport {

class SocketCreator {
public:
    SocketCreator() = delete;

    /// @brief 创建TCP套接字
    /// @param ip        绑定IP，空字符串表示 INADDR_ANY
    /// @param port      绑定端口
    /// @param non_block 是否非阻塞
    /// @param listen_backlog  listen 队列长度，0 表示不调用 listen
    /// @return 成功返回 fd，失败返回 -1
    static int CreateTcpSocket(const std::string& ip, uint16_t port, bool non_block = true, int listen_backlog = 0) {
        int flags = SOCK_STREAM;
        if (non_block) flags |= SOCK_NONBLOCK;

        int fd = socket(AF_INET, flags, 0);
        if (fd == -1) return -1;

        int opt = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
            close(fd);
            return -1;
        }

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(port);
        addr.sin_addr.s_addr = ip.empty() ? INADDR_ANY : inet_addr(ip.c_str());

        if (bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
            close(fd);
            return -1;
        }

        if (listen_backlog > 0 && listen(fd, listen_backlog) < 0) {
            close(fd);
            return -1;
        }

        return fd;
    }

    /// @brief 创建UDP套接字
    /// @param ip        绑定IP，空字符串表示 INADDR_ANY
    /// @param port      绑定端口
    /// @param non_block 是否非阻塞
    /// @return 成功返回 fd，失败返回 -1
    static int CreateUdpSocket(const std::string& ip, uint16_t port, bool non_block = true) {
        int flags = SOCK_DGRAM;
        if (non_block) flags |= SOCK_NONBLOCK;

        int fd = socket(AF_INET, flags, 0);
        if (fd == -1) return -1;

        int opt = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
            close(fd);
            return -1;
        }

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(port);
        addr.sin_addr.s_addr = ip.empty() ? INADDR_ANY : inet_addr(ip.c_str());

        if (bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
            close(fd);
            return -1;
        }

        return fd;
    }
};
}  // namespace transport
}  // namespace net
}  // namespace nebula