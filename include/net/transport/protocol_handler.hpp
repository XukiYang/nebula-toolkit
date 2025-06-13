#pragma once
#include "../../containers/unpacker.hpp"
#include "enums.hpp"
#include <functional>
#include <memory>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
namespace net {

/// @brief 业务执行回调类型定义
/// @param packs 解析后的数据包
using ExecCb = std::function<void(std::vector<std::vector<uint8_t>> &packs)>;

/// @brief 协议处理器基类
/// 处理不同协议的事件，提供统一接口
/// 处理器可以是TCP、UDP等协议的具体实现
/// 通过继承此类实现具体协议的处理逻辑
class ProtocolHandler {
public:
  virtual void
  HandleEvent(int epoll_fd, const Event &event,
              std::shared_ptr<threading::TimerScheduler> timer_shceduler) = 0;
  virtual bool ShouldClose() const { return false; }
  virtual ~ProtocolHandler() = default;
};

/// @brief TCP协议处理器
class TcpHandler : public ProtocolHandler {
public:
  TcpHandler(int fd, std::unique_ptr<containers::UnPacker> unpacker)
      : fd_(fd), unpacker_(std::move(unpacker)), should_close_(false) {}

  void SetCallback(ExecCb cb) { cb_ = std::move(cb); }
  bool ShouldClose() const override { return should_close_; }

  void HandleEvent(
      int epoll_fd, const Event &event,
      std::shared_ptr<threading::TimerScheduler> timer_shceduler) override {

    timer_shceduler_ = std::move(timer_shceduler);

    if (event.fd != fd_)
      return;

    // 处理错误事件
    if (event.event_flags & EventFlags::kError) {
      LOGP_MSG("Connection error on fd:%d", fd_);
      should_close_ = true;
      return;
    }

    // 处理连接挂起
    if (event.event_flags & EventFlags::kHangUp) {
      LOGP_MSG("Connection closed by peer on fd:%d", fd_);
      should_close_ = true;
      return;
    }

    // 处理可读事件（边缘触发模式）
    if (event.event_flags & EventFlags::kReadable) {
      ProcessReadableEvent();
    }
  }

private:
  const int fd_;
  bool should_close_;
  ExecCb cb_;
  std::unique_ptr<containers::UnPacker> unpacker_;
  std::vector<std::vector<uint8_t>> packs_;
  std::shared_ptr<threading::TimerScheduler> timer_shceduler_;

  void ProcessReadableEvent() {
    while (true) {
      auto [buffer, capacity] = unpacker_->GetLinearWriteSpace();
      if (capacity == 0) {
        LOGP_MSG("Buffer full on fd:%d,wirte space:%d,read space:%d", fd_,
                 unpacker_->AvailableToWrite(), unpacker_->AvailableToRead());
        break;
      }

      ssize_t n = read(fd_, buffer, capacity);

      if (n > 0) {
        // 提交写入数据
        unpacker_->CommitWriteSize(n);

        // 解析数据包
        unpacker_->Get(packs_);

        if (!packs_.empty() && cb_) {
          auto timer_task = [this]() {
            cb_(packs_);
            return 0;
          };
          timer_shceduler_->ScheduleOnce(5000, timer_task);
        }
      } else if (n == 0) { // 对端关闭连接
        should_close_ = true;
        break;
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break; // 没有更多数据可读
      } else {
        perror("read");
        should_close_ = true;
        break;
      }
    }
  }
};

class UdpHandler : public ProtocolHandler {
public:
  UdpHandler(int fd, std::unique_ptr<containers::UnPacker> unpacker)
      : fd_(fd), unpacker_(std::move(unpacker)), should_close_(false) {}

  void HandleEvent(
      int epoll_fd, const Event &event,
      std::shared_ptr<threading::TimerScheduler> timer_shceduler) override {
    timer_shceduler_ = std::move(timer_shceduler);

    if (event.event_flags & EventFlags::kError) {
      should_close_ = true;
      return;
    }
    if (event.event_flags & EventFlags::kReadable) {
      auto [buffer, capacity] = unpacker_->GetLinearWriteSpace();
      ssize_t len = recvfrom(event.fd, buffer, capacity, 0, nullptr, 0);
      if (len == -1 || errno == EAGAIN || errno == EWOULDBLOCK) {
        should_close_ = true;
        LOGP_MSG("udp error on fd:%d", fd_);
        return;
      }
      if (len == -1 || errno == ECONNREFUSED) {
        should_close_ = true;
        LOGP_MSG("udp error ECONNREFUSED on fd:%d", fd_);
        return;
      }
      unpacker_->CommitWriteSize(len);
      unpacker_->Get(packs_);

      auto timer_task = [this]() {
        cb_(packs_);
        return 0;
      };
      timer_shceduler_->ScheduleOnce(5000, timer_task);
    }
  };
  bool ShouldClose() const override { return should_close_; }
  void SetCallback(ExecCb cb) { cb_ = std::move(cb); }

private:
  const int fd_;
  bool should_close_;
  ExecCb cb_;
  std::unique_ptr<containers::UnPacker> unpacker_;
  std::vector<std::vector<uint8_t>> packs_;
  std::shared_ptr<threading::TimerScheduler> timer_shceduler_;
};

} // namespace net