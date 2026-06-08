#pragma once
#include <sys/socket.h>

#include <cstdint>
#include <memory>
#include <string>

#include "containers/bytes_stream.hpp"
#include "threading/timer_scheduler.hpp"
namespace nebula {
namespace io {
namespace transport {

enum EventFlags {
    kNone     = 0,  // 无事件标志
    kReadable = 1,  // 可读事件
    kWritable = 2,  // 可写事件
    kError    = 4,  // 错误事件
    kHangUp   = 8   // 连接挂起
};

/// @brief 事件上下文，聚合了 IO 事件所需的全部信息
/// 将 epoll_fd、fd、事件标志、连接 ID、定时器统一传递给协议处理器，
/// 避免 HandleEvent 参数散落，handler 需要什么直接从 ctx 取。
struct EventContext {
    int                                              epoll_fd        = -1;
    int                                              fd              = -1;
    EventFlags                                       event_flags     = kNone;
    uint64_t                                         conn_id         = 0;
    std::shared_ptr<threading::TimerScheduler>       timer_scheduler;
};

namespace event_response {

enum class ProtoType { kNone = -1, kTcp, kUdp, kSp /* 串口协议，预留 */ };
enum class OptAction { kNone = -1, kSend, kClose };

struct Head {
    ProtoType        proto_type = ProtoType::kNone;
    int              fd         = -1;
    uint64_t         conn_id    = 0;
    sockaddr_storage peer_addr{};
    socklen_t        peer_addr_len{};

    uint32_t  msg_type   = 0;                 // 业务消息类型
    OptAction opt_action = OptAction::kNone;  // 操作选项
};

struct Body {
    containers::BytesStream data_bytes_stream;
};

struct Frame {
    Head head;
    Body body;
};
}  // namespace event_response

}  // namespace transport
}  // namespace io
}  // namespace nebula
