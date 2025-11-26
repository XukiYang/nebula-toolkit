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

constexpr uint8_t HEAD[] = {0xE, 0xD};
constexpr uint8_t TAIL   = 0xA;
std::atomic<bool> stop_flag{false};

std::vector<uint8_t> make_packet(const std::string& payload) {
    std::vector<uint8_t> pkt;
    pkt.insert(pkt.end(), HEAD, HEAD + 2);
    pkt.insert(pkt.end(), payload.begin(), payload.end());
    pkt.push_back(TAIL);
    return pkt;
}

/* TCP 压力测试函数 */
void tcp_benchmark(const std::string& server_ip, int port,
                   int    concurrency,        // 并发连接数
                   int    requests_per_conn,  // 每连接发送多少次
                   size_t payload_size,       // payload 字节数
                   int    duration_sec        // 最大运行时间（秒）
) {
    std::atomic<uint64_t>    total_bytes_sent{0};
    std::atomic<int>         active_connections{0};
    std::vector<std::thread> threads;
    auto                     start_time = std::chrono::steady_clock::now();

    std::string payload(payload_size, 'X');  // 固定内容

    for (int i = 0; i < concurrency; ++i) {
        threads.emplace_back([&, i]() {
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

            auto pkt = make_packet(payload);
            for (int req = 0; req < requests_per_conn && !stop_flag; ++req) {
                if (send(sockfd, pkt.data(), pkt.size(), 0) <= 0) break;
                total_bytes_sent += pkt.size();
            }

            active_connections--;
            close(sockfd);
        });
    }

    // 等待或超时
    if (duration_sec > 0) {
        std::this_thread::sleep_for(std::chrono::seconds(duration_sec));
        stop_flag = true;
    }

    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
        / 1000.0;

    double throughput_mbps   = (total_bytes_sent * 8.0) / (elapsed * 1e6);
    double throughput_mbps_s = total_bytes_sent / (1024.0 * 1024.0) / elapsed;

    std::cout << "\n=== TCP Benchmark Result ===\n";
    std::cout << "Server: " << server_ip << ":" << port << "\n";
    std::cout << "Concurrency: " << concurrency << " connections\n";
    std::cout << "Payload size: " << payload_size << " bytes\n";
    std::cout << "Total data sent: " << total_bytes_sent << " bytes\n";
    std::cout << "Elapsed time: " << elapsed << " seconds\n";
    std::cout << "Throughput: " << throughput_mbps << " Mbps (" << throughput_mbps_s << " MB/s)\n";
    std::cout << "Active connections at end: " << active_connections.load() << "\n\n";
}

/* UDP 压力测试函数 */
void udp_benchmark(const std::string& server_ip, int port,
                   int    thread_count,   // 发送线程数（模拟并发）
                   int    total_packets,  // 总包数（若为0，则按 duration_sec 发送）
                   size_t payload_size, int duration_sec) {
    std::atomic<uint64_t>    total_bytes_sent{0};
    std::vector<std::thread> threads;
    auto                     start_time = std::chrono::steady_clock::now();

    std::string payload(payload_size, 'X');
    auto        pkt = make_packet(payload);

    bool use_duration       = (total_packets == 0 && duration_sec > 0);
    int  packets_per_thread = use_duration ? 0 : (total_packets / thread_count);

    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&, i]() {
            int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
            if (sockfd < 0) {
                std::cerr << "UDP: Failed to create socket #" << i << "\n";
                return;
            }

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port   = htons(port);
            inet_pton(AF_INET, server_ip.c_str(), &addr.sin_addr);

            if (use_duration) {
                while (!stop_flag) {
                    if (sendto(sockfd, pkt.data(), pkt.size(), 0, (struct sockaddr*)&addr, sizeof(addr)) <= 0) {
                        break;
                    }
                    total_bytes_sent += pkt.size();
                }
            } else {
                for (int j = 0; j < packets_per_thread; ++j) {
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

    double throughput_mbps = (total_bytes_sent * 8.0) / (elapsed * 1e6);
    double throughput_mps  = total_bytes_sent / (1024.0 * 1024.0) / elapsed;

    std::cout << "\n=== UDP Benchmark Result ===\n";
    std::cout << "Server: " << server_ip << ":" << port << "\n";
    std::cout << "Threads: " << thread_count << "\n";
    std::cout << "Payload size: " << payload_size << " bytes\n";
    std::cout << "Total data sent: " << total_bytes_sent << " bytes\n";
    std::cout << "Elapsed time: " << elapsed << " seconds\n";
    std::cout << "Throughput: " << throughput_mbps << " Mbps (" << throughput_mps << " MB/s)\n\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage:\n"
                  << "  " << argv[0] << " tcp [concurrency=100] [payload=100] [requests=1000] [duration=0]\n"
                  << "  " << argv[0] << " udp [threads=4] [payload=100] [packets=0] [duration=10]\n";
        return 1;
    }

    std::string mode      = argv[1];
    std::string server_ip = "127.0.0.1";

    if (mode == "tcp") {
        int    concurrency  = (argc > 2) ? std::stoi(argv[2]) : 100;
        size_t payload_size = (argc > 3) ? std::stoul(argv[3]) : 100;
        int    requests     = (argc > 4) ? std::stoi(argv[4]) : 1000;
        int    duration     = (argc > 5) ? std::stoi(argv[5]) : 0;

        std::cout << "Starting TCP benchmark...\n";
        tcp_benchmark(server_ip, 8080, concurrency, requests, payload_size, duration);

    } else if (mode == "udp") {
        int    threads      = (argc > 2) ? std::stoi(argv[2]) : 4;
        size_t payload_size = (argc > 3) ? std::stoul(argv[3]) : 100;
        int    packets      = (argc > 4) ? std::stoi(argv[4]) : 0;
        int    duration     = (argc > 5) ? std::stoi(argv[5]) : 10;

        std::cout << "Starting UDP benchmark...\n";
        udp_benchmark(server_ip, 9090, threads, packets, payload_size, duration);

    } else {
        std::cerr << "Unknown mode: " << mode << "\n";
        return 1;
    }

    return 0;
}

// g++ -o net_benchmark_test net_benchmark_test.cpp -pthread