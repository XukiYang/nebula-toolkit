// =============================================================================
// circular_buffer_demo.cpp -- CircularBuffer 环形缓冲区教学示例
// =============================================================================
//
// 核心思想:
//   环形缓冲区使用固定大小数组 + 读写指针环回实现 FIFO 队列。
//   当指针到达数组末尾时自动回绕到开头，避免内存搬移。
//   适用于生产者-消费者场景、网络 IO 缓冲等高频读写场景。
//
// CircularBuffer 的关键设计:
//   1. 线程安全 -- 内部使用 mutex 保护读写操作
//   2. 零拷贝 IO -- GetLinearWriteSpace/GetLinearReadSpace 返回连续内存区间
//      可直接用于 recv()/send() 等系统调用，避免额外拷贝
//   3. 多种数据类型支持 -- uint8_t*、void*、string、vector<T>
//
// =============================================================================

#include <fmt/format.h>

#include <containers/circular_buffer.hpp>
#include <string>
#include <vector>

int main() {
    using nebula::containers::CircularBuffer;

    fmt::println("========================================");
    fmt::println("  CircularBuffer 环形缓冲区教学示例");
    fmt::println("========================================\n");

    // 1. 基本读写
    //    Write/Read 支持多种数据类型: uint8_t*、void*、string、vector<T>
    fmt::println("--- 1. 基本读写 ---");
    CircularBuffer buf(64);  // 64 字节容量

    // 写入 string
    std::string msg = "hello nebula";
    buf.Write(msg);
    fmt::println("  写入: \"{}\"", msg);

    // 读取 string
    std::string out;
    buf.Read(out, msg.size());
    fmt::println("  读取: \"{}\"", out);

    // 写入 vector<uint8_t>
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04, 0x05};
    buf.Write(data);
    fmt::println("  写入 vector: [{}]", fmt::join(data, ", "));

    // 读取 vector -- ReadAutoResize 会自动调整 vector 大小
    std::vector<uint8_t> read_data;
    buf.ReadAutoResize(read_data, data.size());
    fmt::println("  读取 vector: [{}]", fmt::join(read_data, ", "));

    // 2. 环回写入验证
    //    当写入数据超过缓冲区末尾时，数据会从开头继续写入
    //    这是环形缓冲区的核心特性 -- 不需要搬移已有数据
    fmt::println("\n--- 2. 环回写入验证 ---");
    CircularBuffer wrap_buf(16);  // 小缓冲区，便于演示环回

    // 先写入 12 字节
    std::string first = "123456789ABC";
    wrap_buf.Write(first);
    fmt::println("  写入 \"{}\" (12字节)", first);
    fmt::println("  容量={}, 已用={}, 可写={}",
                 wrap_buf.Capacity(), wrap_buf.Length(), wrap_buf.AvailableToWrite());

    // 读取 8 字节，腾出空间
    std::string consumed;
    wrap_buf.Read(consumed, 8);
    fmt::println("  读取 \"{}\" (8字节)", consumed);
    fmt::println("  已用={}, 可写={}", wrap_buf.Length(), wrap_buf.AvailableToWrite());

    // 再写入 8 字节 -- 触发环回
    std::string second = "DEFGHIJK";
    wrap_buf.Write(second);
    fmt::println("  写入 \"{}\" (8字节，环回写入)", second);
    fmt::println("  已用={}, 可写={}", wrap_buf.Length(), wrap_buf.AvailableToWrite());

    // 读取剩余数据验证完整性
    std::string remaining;
    wrap_buf.Read(remaining, wrap_buf.Length());
    fmt::println("  剩余数据: \"{}\"", remaining);

    // 3. 零拷贝 IO
    //    GetLinearWriteSpace() 返回一段连续的可写内存区间
    //    写入数据后调用 CommitWriteSize() 提交写入量
    //    这样可以直接把 recv() 的数据写入缓冲区，无需中间拷贝
    fmt::println("\n--- 3. 零拷贝 IO ---");
    CircularBuffer zc_buf(32);

    // 获取线性可写空间
    auto [write_ptr, write_len] = zc_buf.GetLinearWriteSpace();
    fmt::println("  可写空间: {} 字节", write_len);

    // 模拟直接写入（比如从 recv() 获取数据）
    const char* recv_data = "zero-copy-data";
    size_t recv_len = strlen(recv_data);
    memcpy(write_ptr, recv_data, recv_len);
    zc_buf.CommitWriteSize(recv_len);
    fmt::println("  零拷贝写入: \"{}\"", recv_data);

    // 获取线性可读空间
    auto [read_ptr, read_len] = zc_buf.GetLinearReadSpace();
    fmt::println("  可读空间: {} 字节", read_len);
    fmt::println("  读取内容: \"{}\"", std::string(reinterpret_cast<const char*>(read_ptr), read_len));
    zc_buf.CommitReadSize(read_len);

    // 4. Peek 预览不出队
    //    Peek 与 Read 类似，但不移动读指针，数据仍然保留在缓冲区中
    //    适用于"先检查再决定是否消费"的场景
    fmt::println("\n--- 4. Peek 预览不出队 ---");
    CircularBuffer peek_buf(32);
    peek_buf.Write(std::string("peek-test"));

    std::vector<uint8_t> peek_data;
    peek_buf.Peek(peek_data, 4);
    fmt::println("  Peek 4字节: \"{}\"",
                 std::string(peek_data.begin(), peek_data.end()));
    fmt::println("  Peek 后已用: {} (数据未消费)", peek_buf.Length());

    // 5. 状态查询
    //    Capacity/Length/Usage/IsEmpty/IsFull 提供缓冲区状态信息
    fmt::println("\n--- 5. 状态查询 ---");
    CircularBuffer stat_buf(128);
    stat_buf.Write(std::string("status check"));

    fmt::println("  Capacity:       {} 字节", stat_buf.Capacity());
    fmt::println("  Length (已用):   {} 字节", stat_buf.Length());
    fmt::println("  AvailableRead:  {} 字节", stat_buf.AvailableToRead());
    fmt::println("  AvailableWrite: {} 字节", stat_buf.AvailableToWrite());
    fmt::println("  Usage:          {:.1f}%", stat_buf.Usage() * 100);
    fmt::println("  IsEmpty:        {}", stat_buf.IsEmpty());
    fmt::println("  IsFull:         {}", stat_buf.IsFull());

    fmt::println("\n========================================");
    fmt::println("  CircularBuffer 示例结束");
    fmt::println("========================================");

    return 0;
}
