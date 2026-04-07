#pragma once
#include <bits/socket.h>

#include <string>

#include "../../modules/containers/bytes_stream.hpp"
#include "../../modules/containers/lockfree_queue.hpp"
namespace nebula {
namespace net {
namespace transport {

enum EventFlags {
    kNone     = 0,  // 无事件标志
    kReadable = 1,  // 可读事件
    kWritable = 2,  // 可写事件
    kError    = 4,  // 错误事件
    kHangUp   = 8   // 连接挂起
};

enum TriggerMode { kEt, kLt };

// 事件结构
struct Event {
    int        fd;
    EventFlags event_flags;
};

namespace event_response {

enum class ProtoType { kNone = -1, kTcp, kUdp, kSp };
enum class OptAction { kNone = -1, kSend, kClose };

struct Head {
    ProtoType        proto_type = ProtoType::kNone;
    int              fd         = -1;
    uint64_t         conn_id    = -1;
    sockaddr_storage peer_addr{};
    socklen_t        peer_addr_len{};

    uint32_t  msg_type   = -1;                // 业务消息类型
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
}  // namespace net
}  // namespace nebula