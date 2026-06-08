#include "containers/lockfree_queue.hpp"
#include "net/core/reactor_core.hpp"
#include "net/transport/protocol_handler.hpp"
#include "net/transport/socket_creator.hpp"
#include "threading/timer_scheduler.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
class NoopHandler : public nebula::net::transport::ProtocolHandler {
public:
    void HandleEvent(const nebula::net::transport::EventContext&) override {}
};

bool WaitRecv(int fd, std::string& out, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    char       buffer[1024];
    while (std::chrono::steady_clock::now() < deadline) {
        const ssize_t n = recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
        if (n > 0) {
            out.assign(buffer, buffer + n);
            return true;
        }
        if (n == 0) {
            return false;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}
}  // namespace

int main() {
    using nebula::containers::LockFreeQueue;
    using nebula::containers::Result;
    using nebula::containers::TailKey;
    using nebula::containers::HeadKey;
    using nebula::net::core::ReactorCore;
    using nebula::net::transport::event_response::Frame;
    using nebula::net::transport::event_response::OptAction;
    using nebula::net::transport::event_response::ProtoType;

    constexpr uint16_t kPort = 19091;
    auto               queue = std::make_shared<LockFreeQueue<std::shared_ptr<Frame>>>(1024);
    auto               timer = std::make_shared<nebula::threading::TimerScheduler>(2);

    ReactorCore reactor(128);
    reactor.SetTimerScheduler(timer);
    reactor.SetEventResponseQueue(queue);

    HeadKey head_key = {'[', '['};
    TailKey tail_key = {']', ']'};

    reactor.SetConnHandlerParams(
        head_key, tail_key, {}, {},
        [queue](int fd, const std::vector<std::vector<uint8_t>>& packs) {
            // 业务回调线程：只负责组装响应Frame并投递回IO线程，不直接send。
            for (const auto& pack : packs) {
                std::string req(pack.begin(), pack.end());
                std::string resp = "echo:" + req;

                auto frame = std::make_shared<Frame>();
                frame->head.proto_type = ProtoType::kTcp;
                frame->head.fd         = fd;
                frame->head.conn_id    = 0;
                frame->head.msg_type   = 1;
                frame->head.opt_action = OptAction::kSend;
                frame->body.data_bytes_stream << resp;

                // IO线程会在Run()里消费该队列并执行真正send逻辑。
                if (queue->Push(frame) != Result::kSuccess) {
                    std::cerr << "queue push failed\n";
                }
            }
        },
        4096);

    const int listen_fd = nebula::net::transport::SocketCreator::CreateTcpSocket("127.0.0.1", kPort, true, 16);
    assert(listen_fd >= 0);
    reactor.RegisterProtocol(listen_fd, std::make_unique<NoopHandler>(), true);

    std::thread server_thread([&reactor]() { reactor.Run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(client_fd >= 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(kPort);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    assert(connect(client_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);

    const std::string req_payload = "hello-nebula";
    const std::string req_frame   = "[[" + req_payload + "]]";
    // 与UnPacker配置对应：[[payload]]。
    assert(send(client_fd, req_frame.data(), req_frame.size(), 0) == static_cast<ssize_t>(req_frame.size()));

    std::string recv_resp;
    const bool  ok = WaitRecv(client_fd, recv_resp, 2000);
    assert(ok);
    assert(recv_resp == ("echo:" + req_payload));

    std::cout << "reactor tcp unpack echo demo passed: " << recv_resp << std::endl;

    close(client_fd);
    reactor.Stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }
    return 0;
}
