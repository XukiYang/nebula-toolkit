// =============================================================================
// unpacker_demo.cpp -- UnPacker 协议解包器教学示例
// =============================================================================
//
// 核心思想:
//   从字节流中提取完整数据包，处理 TCP 粘包/拆包问题。
//   UnPacker 继承自 CircularBuffer，内部维护一个环形缓冲区，
//   持续接收数据并按协议格式提取完整帧。
//
// UnPacker 的三种模式:
//   1. HeadOnly  -- 只有帧头标记，数据长度由回调计算
//   2. HeadTail  -- 帧头 + 帧尾标记，如 [[data]] 格式
//   3. HeadTailCb -- 帧头 + 帧尾 + 自定义长度计算 + 校验回调
//
// =============================================================================

#include <fmt/format.h>

#include <containers/unpacker.hpp>
#include <string>
#include <vector>

// 辅助函数: 将字节向量转为可打印字符串
std::string BytesToString(const std::vector<uint8_t>& data) {
    return std::string(data.begin(), data.end());
}

int main() {
    using nebula::containers::UnPacker;

    fmt::println("========================================");
    fmt::println("  UnPacker 协议解包器教学示例");
    fmt::println("========================================\n");

    // 1. HeadTail 模式: [[data]] 格式解包
    //    帧头 "[[" 和帧尾 "]]" 之间的内容被视为一个完整数据包
    //    默认输出不包含头尾标记
    fmt::println("--- 1. HeadTail 模式: [[data]] 格式 ---");
    {
        auto unpacker = UnPacker::CreateHeadTail(
            {'[', '['},  // 帧头
            {']', ']'},  // 帧尾
            4096         // 缓冲区大小
        );

        // 构造测试数据: "[[hello]][[world]]"
        std::string data = "[[hello]][[world]]";
        std::vector<std::vector<uint8_t>> packets;

        unpacker->PushAndGet(
            reinterpret_cast<const uint8_t*>(data.data()),
            data.size(),
            packets
        );

        fmt::println("  输入: \"{}\"", data);
        fmt::println("  解出 {} 个数据包:", packets.size());
        for (size_t i = 0; i < packets.size(); ++i) {
            fmt::println("    [{}] \"{}\"", i, BytesToString(packets[i]));
        }
    }

    // 2. 不同帧头帧尾: <<data>>>
    fmt::println("\n--- 2. 不同帧头帧尾: <<data>>> ---");
    {
        auto unpacker = UnPacker::CreateHeadTail(
            {'<', '<'},
            {'>', '>', '>'},
            4096
        );

        std::string data = "<<<payload>>>";
        std::vector<std::vector<uint8_t>> packets;
        unpacker->PushAndGet(
            reinterpret_cast<const uint8_t*>(data.data()),
            data.size(),
            packets
        );

        fmt::println("  输入: \"{}\"", data);
        for (size_t i = 0; i < packets.size(); ++i) {
            fmt::println("  解出: \"{}\"", BytesToString(packets[i]));
        }
    }

    // 3. HeadOnly 模式
    //    只有帧头标记，数据长度由协议约定（此处用分隔符模拟）
    fmt::println("\n--- 3. HeadOnly 模式 ---");
    {
        // 帧头 "$" 后面跟随数据直到下一个 "$"
        auto unpacker = UnPacker::CreateHeadOnly(
            {'$'},
            4096
        );

        std::string data = "$hello$world$test";
        std::vector<std::vector<uint8_t>> packets;
        unpacker->PushAndGet(
            reinterpret_cast<const uint8_t*>(data.data()),
            data.size(),
            packets
        );

        fmt::println("  输入: \"{}\"", data);
        fmt::println("  解出 {} 个数据包:", packets.size());
        for (size_t i = 0; i < packets.size(); ++i) {
            fmt::println("    [{}] \"{}\"", i, BytesToString(packets[i]));
        }
    }

    // 4. 回调模式: 自定义长度计算 + 校验
    //    DataSzCb 回调从帧头解析数据区长度
    //    CheckValidCb 回调校验数据有效性
    //    适用于二进制协议: [帧头][长度字段][数据][帧尾]
    fmt::println("\n--- 4. 回调模式: 自定义长度计算 ---");
    {
        // 自定义长度计算回调
        // 假设协议: [0xAA][0xBB][data_len(2字节, big-endian)][data...]
        auto data_sz_cb = [](
            const uint8_t* head_ptr,
            size_t& head_size,
            size_t& data_size,
            size_t& tail_size
        ) {
            head_size = 2;   // 帧头占 2 字节
            // 从第 2-3 字节读取数据长度 (big-endian)
            data_size = (static_cast<size_t>(head_ptr[2]) << 8) | head_ptr[3];
            tail_size = 0;   // 无帧尾
        };

        // 数据校验回调
        auto check_cb = [](const uint8_t* data_ptr) -> bool {
            // 简单校验: 数据非空
            return data_ptr != nullptr;
        };

        auto unpacker = UnPacker::CreateWithCallbacks(
            {0xAA, 0xBB},  // 帧头
            {},             // 无帧尾
            data_sz_cb,
            check_cb,
            4096
        );

        // 构造二进制数据: [0xAA][0xBB][0x00][0x05]["hello"]
        std::vector<uint8_t> bin_data = {
            0xAA, 0xBB,              // 帧头
            0x00, 0x05,              // 数据长度 = 5
            'h', 'e', 'l', 'l', 'o'  // 数据
        };

        std::vector<std::vector<uint8_t>> packets;
        unpacker->PushAndGet(bin_data.data(), bin_data.size(), packets);

        fmt::println("  输入: {} 字节二进制数据", bin_data.size());
        fmt::println("  解出 {} 个数据包:", packets.size());
        for (size_t i = 0; i < packets.size(); ++i) {
            fmt::println("    [{}] \"{}\" ({}字节)",
                         i, BytesToString(packets[i]), packets[i].size());
        }
    }

    // 5. 分片推送: 模拟 TCP 粘包
    //    实际网络中数据可能分多个 TCP 段到达
    //    UnPacker 内部缓冲区会累积数据直到凑齐完整帧
    fmt::println("\n--- 5. 分片推送: 模拟 TCP 粘包 ---");
    {
        auto unpacker = UnPacker::CreateHeadTail(
            {'[', '['},
            {']', ']'}
        );

        // 完整数据: [[AA]][[BB]][[CC]]
        // 模拟分 3 次到达，故意打散帧边界
        std::string chunk1 = "[[AA]][[";  // 完整包 "AA" + 下一帧头 "["
        std::string chunk2 = "BB";        // 帧体中间部分
        std::string chunk3 = "]][[CC]]";  // 帧尾 + 完整包 "CC"

        std::vector<std::vector<uint8_t>> all_packets;

        auto push_chunk = [&](const std::string& chunk, const std::string& label) {
            std::vector<std::vector<uint8_t>> packets;
            unpacker->PushAndGet(
                reinterpret_cast<const uint8_t*>(chunk.data()),
                chunk.size(),
                packets
            );
            fmt::println("  推送 \"{}\": 解出 {} 个包", label, packets.size());
            for (auto& p : packets) {
                all_packets.push_back(std::move(p));
            }
        };

        push_chunk(chunk1, chunk1);
        push_chunk(chunk2, chunk2);
        push_chunk(chunk3, chunk3);

        fmt::println("  总共解出 {} 个完整数据包:", all_packets.size());
        for (size_t i = 0; i < all_packets.size(); ++i) {
            fmt::println("    [{}] \"{}\"", i, BytesToString(all_packets[i]));
        }
    }

    fmt::println("\n========================================");
    fmt::println("  UnPacker 示例结束");
    fmt::println("========================================");

    return 0;
}
