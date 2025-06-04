#pragma once
#include "../threading/timer_scheduler.hpp"
#include "../transport/strategy.hpp"
#include <arpa/inet.h>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>

namespace net
{
  enum EventFlag
  {
    kReadBit,
    kWriteBit,
    kErrorBit
  };

  enum TriggerMode
  {
    kEt,
    kLt
  };

  struct Event
  {
    int fd;
    EventFlag event_flag;
  };

  /// @brief 协议处理器统一接口
  class ProtocolHandler
  {
  public:
    explicit ProtocolHandler(containers::UnPacker *unpacker) : unpacker_(unpacker) {}
    virtual void HandleEvent(const Event &event) = 0;

  protected:
    /// @brief 通用数据提交路径
    void SubmitData(const uint8_t *submit_data) {}
    containers::UnPacker *unpacker_;
  };

  /// @brief Reactor引擎
  class ReactorCore
  {
  public:
    ReactorCore(uint64_t max_events)
    {
      epoll_fd_ = epoll_create1(0);
      if (epoll_fd_ == -1)
        throw std::runtime_error("epoll_create");
    }

    /// @brief 注册协议处理器
    /// @param scoket_fd
    /// @param protocol_handler
    /// @param mode
    void RegisterProtocol(int scoket_fd, std::unique_ptr<ProtocolHandler> protocol_handler, TriggerMode mode = TriggerMode::kEt) {

    };

    /// @brief 主事件循环
    void Run()
    {
      // 事件容器
      epoll_event events[max_events_];
      while (true)
      {
        // 阻塞等待所有epoll_fd_实例所有就绪事件
        int nfds = epoll_wait(epoll_fd_, events, max_events_, -1);
        if (nfds == -1)
        {
          perror("epoll_wait");
          break;
        }

        // 遍历处理就绪事件 转内部事件结构
        for (int i = 0; i < nfds; ++i)
        {
          Event ev;
          ev.fd = events[i].data.fd;
          if (events[i].events & EPOLLIN)
          {
            ev.event_flag = kReadBit;
          }
          else if (events[i].events & EPOLLOUT)
          {
            ev.event_flag = kWriteBit;
          }
          else if (events[i].events & EPOLLERR)
          {
            ev.event_flag = kErrorBit;
          }

          // 找到对应的处理器
          auto it = protocol_handlers_.find(ev.fd);
          if (it != protocol_handlers_.end())
          {
            it->second->HandleEvent(ev);
          }
        }
      }
    };

  private:
    /// @brief
    void DispatchEvents(const Event &event) {};

  private:
    int epoll_fd_ = -1;
    uint64_t max_events_ = 64;
    std::unordered_map<int, std::unique_ptr<ProtocolHandler>> protocol_handlers_;
  };
}; // namespace net