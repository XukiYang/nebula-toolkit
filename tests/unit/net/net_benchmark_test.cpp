#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

constexpr uint8_t HEAD[] = {0xE, 0xD, 0xF};
constexpr uint8_t TAIL[] = {0xA, 0xE};
std::atomic<bool> stop_flag{false};

std::vector<uint8_t> make_packet(const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> pkt;
    pkt.insert(pkt.end(), HEAD, HEAD + 3);
    pkt.insert(pkt.end(), payload.begin(), payload.end());
    pkt.push_back(TAIL[0]);
    pkt.push_back(TAIL[1]);
    return pkt;
}

/* TCP 压力测试函数 */
void tcp_benchmark(const std::string& server_ip, int port,
                   int    concurrency,   // 并发连接数
                   size_t payload_size,  // payload 字节数
                   int    qps_per_conn,  // 每连接每秒发送次数
                   int    duration_sec) {   // 运行总时间（秒）

    if (concurrency <= 0 || payload_size == 0 || qps_per_conn <= 0 || duration_sec <= 0) {
        std::cerr << "Error: Invalid TCP parameters\n";
        return;
    }

    std::atomic<uint64_t>    total_bytes_sent{0};
    std::atomic<int>         active_connections{0};
    std::vector<std::thread> threads;
    auto                     start_time = std::chrono::steady_clock::now();

    // 构造二进制 payload
    std::vector<uint8_t> payload;
    printf("payload_size:%d\n", (int)payload_size);
    payload.reserve(payload_size);
    for (size_t i = 0; i < payload_size; ++i) {
        payload.push_back(static_cast<uint8_t>(i & 0xFF));
    }

    const auto interval_us = std::chrono::microseconds(1'000'000 / qps_per_conn);  // 每次发送间隔

    for (int i = 0; i < concurrency; ++i) {
        threads.emplace_back([&, i, interval_us, payload]() {  // 按值捕获 payload，避免引用问题
            int sockfd = socket(AF_INET, SOCK_STREAM, 0);
            if (sockfd < 0) {
                std::cerr << "TCP: Failed to create socket #" << i << "\n";
                return;
            }

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port   = htons(port);
            inet_pton(AF_INET, server_ip.c_str(), &addr.sin_addr);

            if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                close(sockfd);
                return;
            }

            active_connections++;

            // 创建数据包并打印一次
            auto pkt = make_packet(payload);

            // 打印发送的数据包（每个连接只打印一次）
            std::cout << "TCP Connection #" << i << " sending packet (size: " << pkt.size() << "): ";
            for (size_t j = 0; j < pkt.size(); ++j) {
                printf("%02X ", pkt[j]);
            }
            std::cout << std::endl;

            auto next_send            = std::chrono::steady_clock::now();
            bool first_packet_printed = false;  // 确保只打印一次

            while (!stop_flag) {
                auto now = std::chrono::steady_clock::now();
                if (now >= next_send) {
                    if (send(sockfd, pkt.data(), pkt.size(), 0) <= 0) break;
                    total_bytes_sent += pkt.size();
                    next_send += interval_us;

                    // 确保只打印一次发送的数据（即使循环中多次执行）
                    if (!first_packet_printed) {
                        first_packet_printed = true;
                    }
                } else {
                    // 忙等待或短 sleep（精度要求不高可 sleep）
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
            }

            active_connections--;
            close(sockfd);
        });
    }

    // 主线程等待指定时长
    std::this_thread::sleep_for(std::chrono::seconds(duration_sec));
    stop_flag = true;

    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
        / 1000.0;

    if (elapsed <= 0) elapsed = 1e-3;

    double throughput_mbps = (total_bytes_sent * 8.0) / (elapsed * 1e6);
    double throughput_mbs  = total_bytes_sent / (1024.0 * 1024.0) / elapsed;

    std::cout << "\n=== TCP Benchmark Result ===\n";
    std::cout << "Server: " << server_ip << ":" << port << "\n";
    std::cout << "Concurrency: " << concurrency << " connections\n";
    std::cout << "Payload size: " << payload_size << " bytes\n";
    std::cout << "QPS per connection: " << qps_per_conn << "\n";
    std::cout << "Duration: " << duration_sec << " seconds\n";
    std::cout << "Total data sent: " << total_bytes_sent << " bytes\n";
    std::cout << "Elapsed time: " << elapsed << " seconds\n";
    std::cout << "Throughput: " << throughput_mbps << " Mbps (" << throughput_mbs << " MB/s)\n";
    std::cout << "Active connections at end: " << active_connections.load() << "\n\n";
}

/* UDP 压力测试函数 */
void udp_benchmark(const std::string& server_ip, int port, int thread_count, size_t payload_size, int total_packets,
                   int duration_sec) {
    if (thread_count <= 0 || payload_size == 0 || (total_packets == 0 && duration_sec <= 0)) {
        std::cerr << "Error: Invalid UDP parameters\n";
        return;
    }

    std::atomic<uint64_t>    total_bytes_sent{0};
    std::vector<std::thread> threads;
    auto                     start_time = std::chrono::steady_clock::now();

    // 使用二进制 payload（与 TCP 一致）
    std::vector<uint8_t> payload;
    payload.reserve(payload_size);
    for (size_t i = 0; i < payload_size; ++i) {
        payload.push_back(static_cast<uint8_t>(i & 0xFF));
    }
    auto pkt = make_packet(payload);

    bool use_duration       = (total_packets == 0 && duration_sec > 0);
    int  packets_per_thread = use_duration ? 0 : (total_packets / thread_count);
    int  remainder          = use_duration ? 0 : (total_packets % thread_count);

    if (remainder > 0 && !use_duration) {
        std::cout << "[Note] Total packets not evenly divisible; " << remainder << " extra packets distributed.\n";
    }

    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&, i, packets_per_thread, remainder, use_duration, pkt]() {  // 按值捕获 pkt
            int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
            if (sockfd < 0) {
                std::cerr << "UDP: Failed to create socket #" << i << "\n";
                return;
            }

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port   = htons(port);
            inet_pton(AF_INET, server_ip.c_str(), &addr.sin_addr);

            // 打印发送的数据包（每个线程只打印一次）
            std::cout << "UDP Thread #" << i << " sending packet (size: " << pkt.size() << "): ";
            for (size_t j = 0; j < pkt.size(); ++j) {
                printf("%02X ", pkt[j]);
            }
            std::cout << std::endl;

            if (use_duration) {
                while (!stop_flag) {
                    if (sendto(sockfd, pkt.data(), pkt.size(), 0, (struct sockaddr*)&addr, sizeof(addr)) <= 0) {
                        break;
                    }
                    total_bytes_sent += pkt.size();
                    // 加微小延时避免打满 CPU
                    // std::this_thread::sleep_for(std::chrono::nanoseconds(1));
                }
            } else {
                int actual_packets = packets_per_thread + (i < remainder ? 1 : 0);
                for (int j = 0; j < actual_packets; ++j) {
                    if (sendto(sockfd, pkt.data(), pkt.size(), 0, (struct sockaddr*)&addr, sizeof(addr)) <= 0) {
                        break;
                    }
                    total_bytes_sent += pkt.size();
                }
            }
            close(sockfd);
        });
    }

    if (use_duration) {
        std::this_thread::sleep_for(std::chrono::seconds(duration_sec));
        stop_flag = true;
    }

    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
        / 1000.0;

    if (elapsed <= 0) elapsed = 1e-3;

    double throughput_mbps = (total_bytes_sent * 8.0) / (elapsed * 1e6);
    double throughput_mbs  = total_bytes_sent / (1024.0 * 1024.0) / elapsed;

    std::cout << "\n=== UDP Benchmark Result ===\n";
    std::cout << "Server: " << server_ip << ":" << port << "\n";
    std::cout << "Threads: " << thread_count << "\n";
    std::cout << "Payload size: " << payload_size << " bytes\n";
    if (!use_duration) {
        std::cout << "Total packets: " << total_packets << "\n";
    } else {
        std::cout << "Duration: " << duration_sec << " seconds\n";
    }
    std::cout << "Total data sent: " << total_bytes_sent << " bytes\n";
    std::cout << "Elapsed time: " << elapsed << " seconds\n";
    std::cout << "Throughput: " << throughput_mbps << " Mbps (" << throughput_mbs << " MB/s)\n\n";
}

// 信号处理器
void signal_handler(int signum) {
    std::cout << "\nReceived signal " << signum << ", stopping benchmark...\n";
    stop_flag = true;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (argc < 2) {
        std::cerr << "Usage:\n"
                  << "  " << argv[0] << " tcp <concurrency> <payload_size> <qps_per_conn> [duration=10]\n"
                  << "  " << argv[0] << " udp <threads> <payload_size> [total_packets=0] [duration=10]\n"
                  << "\nNote: In TCP mode, 'qps_per_conn' is requests per second per connection.\n";
        return 1;
    }

    std::string mode      = argv[1];
    std::string server_ip = "127.0.0.1";

    if (mode == "tcp") {
        int    concurrency  = (argc > 2) ? std::stoi(argv[2]) : 10;
        size_t payload_sz   = (argc > 3) ? std::stoul(argv[3]) : 100;
        int    qps_per_conn = (argc > 4) ? std::stoi(argv[4]) : 100;
        int    duration     = (argc > 5) ? std::stoi(argv[5]) : 10;

        std::cout << "Starting TCP benchmark...\n";
        tcp_benchmark(server_ip, 8080, concurrency, payload_sz, qps_per_conn, duration);

    } else if (mode == "udp") {
        int    threads       = (argc > 2) ? std::stoi(argv[2]) : 4;
        size_t payload_sz    = (argc > 3) ? std::stoul(argv[3]) : 100;
        int    total_packets = (argc > 4) ? std::stoi(argv[4]) : 0;
        int    duration      = (argc > 5) ? std::stoi(argv[5]) : 10;

        std::cout << "Starting UDP benchmark...\n";
        udp_benchmark(server_ip, 9090, threads, payload_sz, total_packets, duration);

    } else {
        std::cerr << "Unknown mode: " << mode << "\n";
        return 1;
    }

    return 0;
}