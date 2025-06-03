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
    /// @brief 注册协议处理器
    /// @param epoll_fd
    /// @param protocol_handler
    void RegisterProtocol(int epoll_fd, std::unique_ptr<ProtocolHandler> protocol_handler) {};
    /// @brief 主事件循环
    void Run() {};

  private:
    /// @brief
    void DispatchEvents(const Event &event) {};

  private:
    std::unordered_map<int, std::unique_ptr<ProtocolHandler>> protocol_handlers_;
  };
}; // namespace net