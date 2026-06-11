#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <thread>

#include "shmstore/change_event.hpp"

namespace nebula::shmstore {

// ────────────────────────────────────────────────────────────
// ChangeWatcher：UDP 组播订阅者
//
// 加入 topic 对应的组播组，接收并校验 UDP 包，检测丢包，触发回调。
//
// 线程模型：
//   - 默认独立线程运行（start/stop）
//   - 也可通过 fd() 获取 socket，集成到外部事件循环（如 ReactorCore）
//
// 用法：
//   ChangeWatcher watcher;
//   watcher.subscribe("order_book", [](const DecodeResult& pkt) { ... });
//   watcher.on_gap([](auto topic, auto gap) { ... });
//   watcher.start();
//   // ...
//   watcher.stop();
// ────────────────────────────────────────────────────────────
class ChangeWatcher {
public:
    using Callback = std::function<void(const DecodeResult&)>;
    using GapCallback = std::function<void(std::string_view topic, uint32_t gap)>;

    explicit ChangeWatcher(uint16_t port = packet::kDefaultPort) : port_(port) {
        sock_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (sock_ < 0) {
            throw std::runtime_error("ChangeWatcher: socket() failed");
        }

        int yes = 1;
        ::setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        timeval tv{1, 0};  // 1 秒接收超时
        ::setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (::bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(sock_);
            throw std::runtime_error("ChangeWatcher: bind() failed");
        }
    }

    ~ChangeWatcher() { stop(); }

    ChangeWatcher(const ChangeWatcher&) = delete;
    ChangeWatcher& operator=(const ChangeWatcher&) = delete;

    // ── 订阅管理 ──────────────────────────────────────────

    // 订阅 topic（加入组播组），重复订阅会覆盖回调
    void subscribe(std::string_view topic, Callback cb) {
        std::string t(topic);
        std::string mcast = packet::topic_to_mcast(topic);

        ip_mreq mreq{};
        ::inet_pton(AF_INET, mcast.c_str(), &mreq.imr_multiaddr);
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        ::setsockopt(sock_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

        std::lock_guard<std::mutex> lock(mu_);
        subs_[t] = {std::move(cb), kInitialSeq};
    }

    // 取消订阅（离开组播组）
    void unsubscribe(std::string_view topic) {
        std::string t(topic);
        std::string mcast = packet::topic_to_mcast(topic);

        ip_mreq mreq{};
        ::inet_pton(AF_INET, mcast.c_str(), &mreq.imr_multiaddr);
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        ::setsockopt(sock_, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));

        std::lock_guard<std::mutex> lock(mu_);
        subs_.erase(t);
    }

    void on_gap(GapCallback cb) { gap_cb_ = std::move(cb); }

    // 获取内部 socket fd，用于集成到外部事件循环
    int fd() const { return sock_; }

    // 处理一个可读事件（fd 可读时调用）
    bool on_readable() {
        uint8_t buf[packet::kMaxSize];
        sockaddr_in src_addr{};
        socklen_t addr_len = sizeof(src_addr);

        ssize_t n = ::recvfrom(sock_, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&src_addr),
                               &addr_len);
        if (n < 0) return false;  // EAGAIN/超时/错误均忽略

        auto result = decode_packet(buf, static_cast<size_t>(n));
        if (!result.ok) return false;

        std::lock_guard<std::mutex> lock(mu_);
        auto it = subs_.find(std::string(result.topic));
        if (it == subs_.end()) return true;

        auto& entry = it->second;

        // 序列号检查（last_seq 初始为 kInitialSeq = 0xFFFFFFFF，首包 seq=0 正常接收）
        uint32_t expected_seq = entry.last_seq + 1;
        if (result.seq > expected_seq) {
            if (gap_cb_) gap_cb_(result.topic, result.seq - expected_seq);
        } else if (result.seq < expected_seq) {
            return true;  // 重复包，忽略
        }

        entry.last_seq = result.seq;
        if (entry.cb) entry.cb(result);
        return true;
    }

    // ── 独立线程模式 ──────────────────────────────────────

    void start() {
        if (running_.exchange(true)) return;
        thread_ = std::thread([this]() {
            while (running_) {
                on_readable();
            }
        });
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    bool is_running() const { return running_; }

private:
    // 初始序列表示"尚未收到任何包"，配合 last_seq+1 使首包 seq=0 正常匹配
    static constexpr uint32_t kInitialSeq = ~uint32_t(0);

    struct SubEntry {
        Callback cb;
        uint32_t last_seq;
    };

    int sock_ = -1;
    uint16_t port_;
    std::mutex mu_;
    std::map<std::string, SubEntry> subs_;  // map 支持透明查找
    GapCallback gap_cb_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace nebula::shmstore
