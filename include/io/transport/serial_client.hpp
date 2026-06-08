#pragma once

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nebula {
namespace io {
namespace transport {

/// @brief 阻塞式串口客户端
/// 提供串口的打开、关闭、读写操作，适用于不需要 Reactor 事件驱动的简单场景。
/// 对于事件驱动场景，请使用 SpHandler + ReactorCore。
class SerialClient {
public:
    enum Parity { NO_PARITY, ODD_PARITY, EVEN_PARITY };

    enum StopBits { ONE_STOP_BIT, TWO_STOP_BITS };

    enum DataBits { FIVE_DATA_BITS, SIX_DATA_BITS, SEVEN_DATA_BITS, EIGHT_DATA_BITS };

    /// @brief 串口配置结构体
    struct Config {
        std::string port;             ///< 串口设备路径
        int         baud_rate;        ///< 波特率
        DataBits    data_bits;        ///< 数据位
        Parity      parity;           ///< 校验位
        StopBits    stop_bits;        ///< 停止位
        bool        flow_control;     ///< 流控
        int         read_timeout_ms;  ///< 读取超时(ms)
        int         buffer_size;      ///< 缓冲区大小

        /// @brief 默认构造函数
        Config()
            : port(""),
              baud_rate(115200),
              data_bits(EIGHT_DATA_BITS),
              parity(NO_PARITY),
              stop_bits(ONE_STOP_BIT),
              flow_control(false),
              read_timeout_ms(1000),
              buffer_size(1024) {}
    };

    SerialClient() : fd_(-1), is_open_(false) {
        ClearError();
    }

    ~SerialClient() {
        Close();
    }

    /// @brief 初始化串口
    /// @param config 串口配置
    /// @return 是否成功
    bool Init(const Config& config) {
        Close();
        ClearError();

        config_ = config;

        fd_ = open(config.port.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
        if (fd_ < 0) {
            SetError(std::string("failed to open serial port: ") + strerror(errno));
            return false;
        }

        if (fcntl(fd_, F_SETFL, O_NONBLOCK) < 0) {
            SetError(std::string("failed to set non-blocking mode: ") + strerror(errno));
            close(fd_);
            fd_ = -1;
            return false;
        }

        struct termios term;
        if (tcgetattr(fd_, &term) != 0) {
            SetError(std::string("failed to get termios attributes: ") + strerror(errno));
            close(fd_);
            fd_ = -1;
            return false;
        }

        // 设置波特率
        speed_t baud_rate = B115200;
        switch (config.baud_rate) {
        case 1200:
            baud_rate = B1200;
            break;
        case 2400:
            baud_rate = B2400;
            break;
        case 4800:
            baud_rate = B4800;
            break;
        case 9600:
            baud_rate = B9600;
            break;
        case 19200:
            baud_rate = B19200;
            break;
        case 38400:
            baud_rate = B38400;
            break;
        case 57600:
            baud_rate = B57600;
            break;
        case 115200:
            baud_rate = B115200;
            break;
        case 230400:
            baud_rate = B230400;
            break;
        case 460800:
            baud_rate = B460800;
            break;
        case 921600:
            baud_rate = B921600;
            break;
        default:
            break;
        }

        if (cfsetispeed(&term, baud_rate) < 0 || cfsetospeed(&term, baud_rate) < 0) {
            SetError(std::string("failed to set baud rate: ") + strerror(errno));
            close(fd_);
            fd_ = -1;
            return false;
        }

        // 设置数据位
        term.c_cflag &= ~CSIZE;
        switch (config.data_bits) {
        case FIVE_DATA_BITS:
            term.c_cflag |= CS5;
            break;
        case SIX_DATA_BITS:
            term.c_cflag |= CS6;
            break;
        case SEVEN_DATA_BITS:
            term.c_cflag |= CS7;
            break;
        case EIGHT_DATA_BITS:
            term.c_cflag |= CS8;
            break;
        default:
            term.c_cflag |= CS8;
            break;
        }

        // 设置校验位
        term.c_cflag &= ~(PARENB | PARODD);
        switch (config.parity) {
        case ODD_PARITY:
            term.c_cflag |= (PARENB | PARODD);
            break;
        case EVEN_PARITY:
            term.c_cflag |= PARENB;
            break;
        default:
            break;
        }

        // 设置停止位
        term.c_cflag &= ~CSTOPB;
        if (config.stop_bits == TWO_STOP_BITS) {
            term.c_cflag |= CSTOPB;
        }

        // 设置流控
        if (config.flow_control) {
            term.c_cflag |= CRTSCTS;
        } else {
            term.c_cflag &= ~CRTSCTS;
        }

        // 设置终端模式：Raw 模式
        term.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
        term.c_iflag &= ~(IXON | IXOFF | IXANY | INLCR | ICRNL | IGNCR | PARMRK);
        term.c_oflag &= ~OPOST;

        // 设置超时
        term.c_cc[VTIME] = config.read_timeout_ms / 100;
        term.c_cc[VMIN]  = 0;

        if (tcsetattr(fd_, TCSANOW, &term) != 0) {
            SetError(std::string("failed to set termios attributes: ") + strerror(errno));
            close(fd_);
            fd_ = -1;
            return false;
        }

        tcflush(fd_, TCIOFLUSH);
        is_open_ = true;
        return true;
    }

    /// @brief 关闭串口
    void Close() {
        if (is_open_) {
            std::lock_guard<std::mutex> lock(mutex_);
            ::close(fd_);
            fd_      = -1;
            is_open_ = false;
        }
    }

    /// @brief 发送数据
    /// @param data 数据向量
    /// @return 发送的字节数，-1表示失败
    int Send(const std::vector<char>& data) {
        return Send(data.data(), data.size());
    }

    /// @brief 发送数据
    /// @param data 数据指针
    /// @param len 数据长度
    /// @return 发送的字节数，-1表示失败
    int Send(const char* data, size_t len) {
        if (!IsOpen()) {
            SetError("serial port is not open");
            return -1;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        ssize_t                     bytes_written = write(fd_, data, len);
        if (bytes_written < 0) {
            SetError(std::string("write failed: ") + strerror(errno));
            return -1;
        }
        return bytes_written;
    }

    /// @brief 接收数据
    /// @param data 接收数据向量
    /// @param max_len 最大接收长度
    /// @return 接收的字节数，-1表示失败
    int Recv(std::vector<char>& data, size_t max_len) {
        data.resize(max_len);
        int result = Recv(data.data(), max_len);
        if (result > 0) {
            data.resize(result);
        } else {
            data.clear();
        }
        return result;
    }

    /// @brief 接收数据
    /// @param data 接收缓冲区
    /// @param max_len 最大接收长度
    /// @return 接收的字节数，-1表示失败
    int Recv(char* data, size_t max_len) {
        if (!IsOpen()) {
            SetError("serial port is not open");
            return -1;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        ssize_t                     bytes_read = read(fd_, data, max_len);
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }
            SetError(std::string("read failed: ") + strerror(errno));
            return -1;
        }
        return bytes_read;
    }

    /// @brief 精确接收指定长度数据
    /// @param data 接收缓冲区
    /// @param len 期望接收长度
    /// @param timeout_ms 超时时间(ms)，-1使用配置超时
    /// @return 接收的字节数，-1表示失败
    int RecvExact(char* data, size_t len, int timeout_ms = -1) {
        if (!IsOpen()) {
            SetError("serial port is not open");
            return -1;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        size_t                      bytes_read      = 0;
        int                         current_timeout = (timeout_ms >= 0) ? timeout_ms : config_.read_timeout_ms;
        auto                        start_time      = std::chrono::steady_clock::now();

        while (bytes_read < len) {
            auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time)
                    .count();

            if (elapsed >= current_timeout) {
                SetError("recvexact timed out");
                return -1;
            }

            ssize_t read_size = read(fd_, data + bytes_read, len - bytes_read);
            if (read_size < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                SetError(std::string("read failed: ") + strerror(errno));
                return -1;
            }

            if (read_size == 0) {
                SetError("recvexact connection closed");
                return -1;
            }

            bytes_read += read_size;
        }

        return bytes_read;
    }

    /// @brief 检查串口是否打开
    /// @return 是否打开
    bool IsOpen() const {
        return is_open_;
    }

    /// @brief 设置读取超时
    /// @param timeout_ms 超时时间(ms)
    void SetReadTimeout(int timeout_ms) {
        if (timeout_ms > 0) {
            config_.read_timeout_ms = timeout_ms;
            if (IsOpen()) {
                struct termios term;
                if (tcgetattr(fd_, &term) == 0) {
                    term.c_cc[VTIME] = timeout_ms / 100;
                    tcsetattr(fd_, TCSANOW, &term);
                }
            }
        }
    }

    /// @brief 获取最后错误信息
    /// @return 错误信息
    std::string GetLastError() const {
        std::lock_guard<std::mutex> lock(error_mutex_);
        return last_error_;
    }

private:
    int                fd_;           ///< 文件描述符
    Config             config_;       ///< 串口配置
    mutable std::mutex mutex_;        ///< 操作互斥锁
    std::string        last_error_;   ///< 最后错误信息
    mutable std::mutex error_mutex_;  ///< 错误信息互斥锁
    std::atomic<bool>  is_open_;      ///< 串口是否打开

    /// @brief 设置错误信息
    /// @param error 错误信息
    void SetError(const std::string& error) {
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_ = error;
    }

    /// @brief 清除错误信息
    void ClearError() {
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_.clear();
    }
};

}  // namespace transport
}  // namespace io
}  // namespace nebula
