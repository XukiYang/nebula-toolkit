#pragma once

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <mutex>
#include <string>
#include <unordered_map>

#include "shmstore/change_event.hpp"

namespace nebula::shmstore {

// ────────────────────────────────────────────────────────────
// ChangeNotifier：UDP 组播发布者
//
// 将变更事件编码为 UDP 包，按 topic 发送到对应组播地址。
// 每个 topic 维护独立序列号。
//
// 注意：publish() 是同步 sendto，网络拥塞时可能阻塞调用线程。
//
// 用法：
//   ChangeNotifier notifier;
//   notifier.publish("order_book", {Op::Insert, key_bytes});
// ────────────────────────────────────────────────────────────
class ChangeNotifier {
public:
    explicit ChangeNotifier(uint16_t port = packet::kDefaultPort) : port_(port) {
        sock_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (sock_ < 0) {
            throw std::runtime_error("ChangeNotifier: socket() failed");
        }
    }

    ~ChangeNotifier() {
        if (sock_ >= 0) ::close(sock_);
    }

    ChangeNotifier(const ChangeNotifier&) = delete;
    ChangeNotifier& operator=(const ChangeNotifier&) = delete;

    // 发布变更事件
    void publish(std::string_view topic, const ChangeEvent& ev) {
        uint32_t seq = next_seq(topic);
        auto pkt = encode_packet(ev, seq);
        if (pkt.empty()) return;  // 编码失败（topic/key 超长等）

        std::string mcast_addr = packet::topic_to_mcast(topic);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        ::inet_pton(AF_INET, mcast_addr.c_str(), &addr.sin_addr);

        ::sendto(sock_, pkt.data(), pkt.size(), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    }

private:
    uint32_t next_seq(std::string_view topic) {
        std::lock_guard<std::mutex> lock(mu_);
        return seq_map_[std::string(topic)]++;
    }

    int sock_ = -1;
    uint16_t port_;
    std::mutex mu_;
    std::unordered_map<std::string, uint32_t> seq_map_;
};

}  // namespace nebula::shmstore
