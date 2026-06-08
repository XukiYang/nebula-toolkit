#pragma once
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

#include <string>

namespace nebula {
namespace io {
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
    static int CreateTcpSocket(const std::string &ip, uint16_t port, bool non_block = true, int listen_backlog = 0) {
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

        if (bind(fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
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
    static int CreateUdpSocket(const std::string &ip, uint16_t port, bool non_block = true) {
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

        if (bind(fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
            close(fd);
            return -1;
        }

        return fd;
    }

    /// @brief 打开串口并配置 termios，返回 fd
    /// @param device_path 设备路径，如 "/dev/ttyS0"
    /// @param baud_rate   波特率，默认 115200
    /// @param non_block   是否非阻塞
    /// @return 成功返回 fd，失败返回 -1
    static int CreateSpFd(const std::string &device_path, int baud_rate = 115200, bool non_block = true) {
        // 打开串口设备：O_RDWR 读写，O_NOCTTY 不成为控制终端
        // O_NDELAY 仅在 non_block=true 时添加，否则以阻塞模式打开
        int open_flags = O_RDWR | O_NOCTTY;
        if (non_block) open_flags |= O_NDELAY;
        int fd = open(device_path.c_str(), open_flags);
        if (fd < 0) return -1;

        // 获取当前 termios 配置
        struct termios term;
        if (tcgetattr(fd, &term) != 0) {
            close(fd);
            return -1;
        }

        // 设置波特率
        speed_t speed = B115200;
        switch (baud_rate) {
            case 1200:   speed = B1200;   break;
            case 2400:   speed = B2400;   break;
            case 4800:   speed = B4800;   break;
            case 9600:   speed = B9600;   break;
            case 19200:  speed = B19200;  break;
            case 38400:  speed = B38400;  break;
            case 57600:  speed = B57600;  break;
            case 115200: speed = B115200; break;
            case 230400: speed = B230400; break;
            case 460800: speed = B460800; break;
            case 921600: speed = B921600; break;
            default: break;  // 未知波特率，使用默认值
        }
        if (cfsetispeed(&term, speed) < 0 || cfsetospeed(&term, speed) < 0) {
            close(fd);
            return -1;
        }

        // 8N1 配置：8 数据位，无校验，1 停止位，无流控
        term.c_cflag &= ~CSIZE;
        term.c_cflag |= CS8;                // 8 数据位
        term.c_cflag &= ~(PARENB | PARODD); // 无校验
        term.c_cflag &= ~CSTOPB;            // 1 停止位
        term.c_cflag &= ~CRTSCTS;           // 无硬件流控
        term.c_cflag |= CLOCAL | CREAD;     // 本地连接，启用接收

        // Raw 模式：禁用行缓冲、回显、信号等
        term.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
        term.c_iflag &= ~(IXON | IXOFF | IXANY | INLCR | ICRNL | IGNCR | PARMRK);
        term.c_oflag &= ~OPOST;

        // 超时配置：非阻塞模式下 VMIN=0, VTIME=0
        term.c_cc[VMIN]  = 0;
        term.c_cc[VTIME] = 0;

        // 应用配置
        if (tcsetattr(fd, TCSANOW, &term) != 0) {
            close(fd);
            return -1;
        }
        // 清空输入输出缓冲区
        tcflush(fd, TCIOFLUSH);

        // 设置非阻塞模式
        if (non_block) {
            int flags = fcntl(fd, F_GETFL);
            if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
                close(fd);
                return -1;
            }
        }

        return fd;
    }
};
}  // namespace transport
}  // namespace io
}  // namespace nebula