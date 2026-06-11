// =============================================================================
// reactor_tcp_echo_demo.cpp -- Reactor 模式 TCP Echo Server 教学示例
// =============================================================================
//
// 核心思想: Reactor 模式
//   Reactor 模式是高性能网络服务器的核心设计模式:
//   1. 一个线程 (Reactor) 负责监听所有 IO 事件 (通过 epoll)
//   2. 事件就绪时分发给对应的 Handler 处理
//   3. 业务逻辑通过线程池 (TimerScheduler) 异步执行，不阻塞 IO 线程
//   4. 业务线程通过无锁队列 (LockFreeQueue) 将响应送回 IO 线程发送
//
// 数据流:
//   Client -> epoll (可读事件) -> UnPacker (解帧) -> TimerScheduler (业务回调)
//   业务回调 -> LockFreeQueue (响应队列) -> epoll 主循环消费 -> TcpWriteManager -> Client
//
// epoll 工作原理:
//   - epoll_create() 创建 epoll 实例
//   - epoll_ctl() 注册/修改/删除文件描述符的监听事件
//   - epoll_wait() 阻塞等待事件就绪，返回就绪的 fd 列表
//   - 边沿触发 (ET): 状态变化时只通知一次，必须读到 EAGAIN
//   - 水平触发 (LT): 只要可读就会持续通知
//
// UnPacker 解帧流程:
//   TCP 是字节流协议，没有消息边界。UnPacker 通过帧头帧尾标记提取完整消息:
//   原始数据: "xxx[[hello]][[world]]yyy"
//   解帧结果: ["hello", "world"]
//
// =============================================================================

#include "containers/lockfree_queue.hpp"
#include "io/core/reactor_core.hpp"
#include "threading/timer_scheduler.hpp"

#include <fmt/format.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

// 辅助函数: 带超时的非阻塞 recv
// 在测试客户端中使用，轮询读取直到收到数据或超时
bool WaitRecv(int fd, std::string& out, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    char buffer[1024];
    while (std::chrono::steady_clock::now() < deadline) {
        const ssize_t n = recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
        if (n > 0) {
            out.assign(buffer, buffer + n);
            return true;
        }
        if (n == 0) {
            return false;  // 对端关闭
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return false;  // 真正的错误
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;  // 超时
}

}  // namespace

int main() {
    using nebula::containers::LockFreeQueue;
    using nebula::containers::Result;
    using nebula::containers::HeadKey;
    using nebula::containers::TailKey;
    using nebula::io::core::ReactorCore;
    using nebula::io::transport::event_response::Frame;
    using nebula::io::transport::event_response::OptAction;
    using nebula::io::transport::event_response::ProtoType;

    fmt::println("========================================");
    fmt::println("  Reactor TCP Echo Server 教学示例");
    fmt::println("========================================\n");

    constexpr uint16_t kPort = 19091;

    // 1. 创建核心组件
    //    LockFreeQueue: 业务线程 -> IO 线程的响应通道
    //    TimerScheduler: 业务线程池，执行回调不阻塞 epoll
    //    ReactorCore: epoll 事件循环，管理所有连接
    fmt::println("--- 1. 初始化组件 ---");
    auto queue = std::make_shared<LockFreeQueue<std::shared_ptr<Frame>>>(1024);
    auto timer = std::make_shared<nebula::threading::TimerScheduler>(2);

    ReactorCore reactor(128);  // max_events = 128
    reactor.SetTimerScheduler(timer);
    reactor.SetEventResponseQueue(queue);
    fmt::println("  ReactorCore: epoll 实例已创建");
    fmt::println("  TimerScheduler: 2 个工作线程");
    fmt::println("  LockFreeQueue: 容量 1024 (响应队列)");

    // 2. 配置解帧参数
    //    HeadKey/TailKey 定义帧头帧尾标记
    //    DataSzCb/CheckValidCb 可选的自定义长度计算和校验回调
    //    ExecCb 是业务回调，在 TimerScheduler 线程中执行
    fmt::println("\n--- 2. 配置解帧参数 ---");
    HeadKey head_key = {'[', '['};
    TailKey tail_key = {']', ']'};

    reactor.SetConnHandlerParams(
        head_key,
        tail_key,
        {},    // DataSzCb: 使用默认的 HeadTail 模式
        {},    // CheckValidCb: 无校验
        // ExecCb: 业务回调
        // 注意: 此回调在 TimerScheduler 工作线程中执行，不在 IO 线程
        // 因此可以安全地进行耗时操作，不会阻塞 epoll 事件循环
        [queue](int fd, const std::vector<std::vector<uint8_t>>& packs) {
            for (const auto& pack : packs) {
                std::string req(pack.begin(), pack.end());
                std::string resp = "echo:" + req;

                // 组装响应 Frame，通过 LockFreeQueue 送回 IO 线程
                // IO 线程在 Run() 主循环中消费队列，通过 TcpWriteManager 发送
                auto frame = std::make_shared<Frame>();
                frame->head.proto_type = ProtoType::kTcp;
                frame->head.fd = fd;
                frame->head.conn_id = 0;
                frame->head.msg_type = 1;
                frame->head.opt_action = OptAction::kSend;
                // BytesStream::operator<<(string) 写入 4字节长度前缀 + 内容
                // 接收方需要跳过前4字节才能得到纯文本
                frame->body.data_bytes_stream << resp;

                if (queue->Push(frame) != Result::kSuccess) {
                    fmt::println("  [WARN] 响应队列已满，丢弃响应");
                }
            }
        },
        4096  // UnPacker 缓冲区大小
    );

    // 3. 启动 TCP 监听
    //    ListenTcp 内部:
    //    - 创建非阻塞 TCP socket
    //    - bind + listen
    //    - 注册 TcpListenerHandler 到 epoll
    //    - 新连接到达时自动创建 TcpHandler 并注册
    fmt::println("\n--- 3. 启动 TCP 监听 ---");
    const int listen_fd = reactor.ListenTcp("127.0.0.1", kPort, 16);
    if (listen_fd < 0) {
        fmt::println("  [ERROR] 监听失败");
        return 1;
    }
    fmt::println("  监听 {}:{}", "127.0.0.1", kPort);

    // 4. 启动 Reactor 事件循环 (独立线程)
    //    Run() 主循环:
    //    - 消费响应队列 (业务线程 -> IO 线程)
    //    - epoll_wait 等待事件 (10ms 超时)
    //    - 分发事件到对应 Handler
    //    - 处理连接关闭
    fmt::println("\n--- 4. 启动 Reactor 事件循环 ---");
    std::thread server_thread([&reactor]() { reactor.Run(); });
    fmt::println("  Reactor 已在后台线程启动");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 5. 测试客户端
    //    连接服务器，发送 [[hello-nebula]]，验证收到 echo:hello-nebula
    fmt::println("\n--- 5. 测试客户端 ---");
    const int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        fmt::println("  [ERROR] 创建 socket 失败");
        reactor.Stop();
        server_thread.join();
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kPort);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(client_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        fmt::println("  [ERROR] 连接失败: {}", strerror(errno));
        close(client_fd);
        reactor.Stop();
        server_thread.join();
        return 1;
    }

    // 发送带帧头帧尾的消息
    const std::string req_payload = "hello-nebula";
    const std::string req_frame = "[[" + req_payload + "]]";
    ssize_t sent = send(client_fd, req_frame.data(), req_frame.size(), 0);
    fmt::println("  发送: \"{}\" ({}字节)", req_frame, sent);

    // 接收响应
    // BytesStream 序列化 string 时会写入 4 字节长度前缀 + 内容
    // 因此实际收到的数据前 4 字节是长度信息，需要跳过
    std::string recv_raw;
    const bool ok = WaitRecv(client_fd, recv_raw, 2000);
    if (!ok) {
        fmt::println("  [ERROR] 接收超时或连接关闭");
    } else {
        // 跳过 BytesStream 的 4 字节长度前缀
        std::string recv_resp = (recv_raw.size() > 4) ? recv_raw.substr(4) : recv_raw;
        fmt::println("  接收: \"{}\" (原始 {} 字节, 跳过长度前缀)", recv_resp, recv_raw.size());
        std::string expected = "echo:" + req_payload;
        fmt::println("  验证: {}", (recv_resp == expected) ? "PASS" : "FAIL");
    }

    // 6. 清理
    fmt::println("\n--- 6. 清理 ---");
    close(client_fd);
    reactor.Stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }
    fmt::println("  已停止 Reactor 并回收线程");

    fmt::println("\n========================================");
    fmt::println("  Reactor TCP Echo Server 示例结束");
    fmt::println("========================================");

    return 0;
}
