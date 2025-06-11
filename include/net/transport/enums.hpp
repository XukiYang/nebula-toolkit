#pragma once
namespace net {

enum EventFlags {
  kNone = 0,     // 无事件标志
  kReadable = 1, // 可读事件
  kWritable = 2, // 可写事件
  kError = 4,    // 错误事件
  kHangUp = 8    // 连接挂起
};

enum TriggerMode { kEt, kLt };

// 事件结构
struct Event {
  int fd;
  EventFlags event_flags;
};

} // namespace net