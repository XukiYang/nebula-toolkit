#pragma once
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "logger/logger.hpp"

namespace nebula {
namespace net {
namespace core {

/// @brief TCP 写缓冲管理器
/// 负责 TCP 连接的数据缓冲、EPOLLOUT 驱动发送、连接 ID 追踪。
/// 从 ReactorCore 中拆分出来，使 ReactorCore 只关注事件循环与 fd 生命周期。
class TcpWriteManager {
public:
    /// @brief 初始化 epoll 实例 fd（应在注册任何连接前调用一次）
    void Init(int epoll_fd) {
        epoll_fd_ = epoll_fd;
    }

    /// @brief 注册新连接，分配 conn_id 并初始化 epoll 事件掩码
    /// @param fd  连接 fd
    void RegisterFd(int fd) {
        conn_ids_[fd]       = ++next_conn_id_;
        fd_event_masks_[fd] = EPOLLIN | EPOLLET;
        LOGP_MSG("TcpWriteManager: registered fd:%d, conn_id:%lu", fd, conn_ids_[fd]);
    }

    /// @brief 获取连接的 conn_id，不存在返回 0
    uint64_t GetConnId(int fd) const {
        auto it = conn_ids_.find(fd);
        return it != conn_ids_.end() ? it->second : 0;
    }

    /// @brief 将待发送数据追加到写缓冲并尝试冲刷
    /// @param fd   目标连接 fd
    /// @param data 数据指针
    /// @param size 数据长度
    void EnqueueWrite(int fd, const void *data, size_t size) {
        auto &state = tcp_write_states_[fd];
        if (state.conn_id == 0) {
            // 首次写入时关联 conn_id
            auto id_it = conn_ids_.find(fd);
            if (id_it != conn_ids_.end()) {
                state.conn_id = id_it->second;
            }
        }

        if (size > 0) {
            const auto *bytes = reinterpret_cast<const uint8_t *>(data);
            state.buffer.insert(state.buffer.end(), bytes, bytes + size);
        }
        Flush(fd);
    }

    /// @brief 校验回包的 conn_id 是否与当前连接匹配，防止 fd 复用导致误发
    /// @return true 表示 conn_id 已过期，应丢弃该帧
    bool IsStaleConn(int fd, uint64_t frame_conn_id) const {
        if (frame_conn_id == 0) return false;
        auto it = conn_ids_.find(fd);
        return it != conn_ids_.end() && frame_conn_id != it->second;
    }

    /// @brief 尝试冲刷指定 fd 的待发数据
    /// 供 ReactorCore 在 EPOLLOUT 事件和入队后调用
    void Flush(int fd) {
        auto it = tcp_write_states_.find(fd);
        if (it == tcp_write_states_.end()) return;

        auto &state = it->second;
        while (state.sent_offset < state.buffer.size()) {
            const uint8_t *base = state.buffer.data() + state.sent_offset;
            const size_t   left = state.buffer.size() - state.sent_offset;
            const ssize_t  n    = send(fd, base, left, MSG_NOSIGNAL);

            if (n > 0) {
                state.sent_offset += static_cast<size_t>(n);
                continue;
            }
            if (n < 0 && errno == EINTR) {
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                // 当前不可写，打开 EPOLLOUT 等待下次可写继续冲刷
                UpdateWriteEvent(fd, true);
                return;
            }

            // 发送失败，清理写缓冲状态并关闭 EPOLLOUT
            tcp_write_states_.erase(it);
            UpdateWriteEvent(fd, false);
            return;
        }

        // 全部发送完成，清理缓冲区并关闭 EPOLLOUT 避免空转
        tcp_write_states_.erase(it);
        UpdateWriteEvent(fd, false);
    }

    /// @brief 清理连接的写缓冲状态
    void UnregisterFd(int fd) {
        tcp_write_states_.erase(fd);
        conn_ids_.erase(fd);
        fd_event_masks_.erase(fd);
    }

private:
    /// @brief TCP 写缓冲状态，每个连接独立
    struct TcpWriteState {
        uint64_t             conn_id = 0;
        std::vector<uint8_t> buffer;
        size_t               sent_offset = 0;
    };

    /// @brief 动态开关 EPOLLOUT
    /// @param fd     目标 fd
    /// @param enable true 开启 EPOLLOUT，false 关闭
    void UpdateWriteEvent(int fd, bool enable) {
        auto it = fd_event_masks_.find(fd);
        if (it == fd_event_masks_.end()) return;

        uint32_t new_mask = it->second;
        if (enable) {
            new_mask |= EPOLLOUT;
        } else {
            new_mask &= ~EPOLLOUT;
        }

        if (new_mask == it->second) return;

        epoll_event ev{};
        ev.events  = new_mask;
        ev.data.fd = fd;

        if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == -1) {
            perror("epoll_ctl mod");
            return;
        }
        it->second = new_mask;
    }

    // 连接 ID 管理（用于 fd 复用防护）
    std::unordered_map<int, uint64_t> conn_ids_;
    std::atomic<uint64_t>             next_conn_id_{0};

    // 每个 fd 的 epoll 事件掩码（用于 EPOLLOUT 动态开关）
    std::unordered_map<int, uint32_t> fd_event_masks_;

    // 每个 fd 的写缓冲状态
    std::unordered_map<int, TcpWriteState> tcp_write_states_;

    // epoll 实例 fd，RegisterFd 时缓存
    int epoll_fd_ = -1;
};

}  // namespace core
}  // namespace net
}  // namespace nebula
