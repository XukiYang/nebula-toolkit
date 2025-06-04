#pragma once
namespace net {
enum EventFlags {
  kReadable, // 对应EPOLLIN
  kWritable, // 对应EPOLLOUT
  kError,    // 对应EPOLLERR
  kHangUp    // 对应EPOLLHUP
};

enum TriggerMode { kEt, kLt };

struct Event {
  int fd;
  EventFlags event_flags;
};
} // namespace net
