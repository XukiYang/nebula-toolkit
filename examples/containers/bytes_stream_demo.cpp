// =============================================================================
// bytes_stream_demo.cpp -- BytesStream 二进制序列化教学示例
// =============================================================================
//
// 核心思想:
//   BytesStream 提供二进制数据的序列化/反序列化能力。
//   字符串使用 4 字节长度前缀（TLV 思想: Type-Length-Value），
//   确保反序列化时能正确边界。
//
// BytesStream 的关键设计:
//   1. 链式写入 -- operator<< 返回引用，支持 stream << a << b << c
//   2. 自动扩容 -- 内部 vector 自动增长，也可手动 Reserve()
//   3. 类型安全 -- 通过模板特化支持 struct、string、vector<T>
//
// =============================================================================

#include <fmt/format.h>

#include <containers/bytes_stream.hpp>
#include <string>
#include <vector>

// 示例结构体
struct PlayerInfo {
    uint32_t id;
    float x;
    float y;
    char name[16];  // 固定长度字段
};

int main() {
    using nebula::containers::BytesStream;

    fmt::println("========================================");
    fmt::println("  BytesStream 二进制序列化教学示例");
    fmt::println("========================================\n");

    // 1. 结构体序列化与反序列化
    //    operator<<(const T&) 将 POD 结构体按字节写入
    //    operator>>(T&) 按字节读出，要求类型大小一致
    fmt::println("--- 1. 结构体序列化与反序列化 ---");
    BytesStream stream;

    PlayerInfo player{1001, 3.14f, 2.71f, "alice"};
    stream << player;
    fmt::println("  序列化: id={}, pos=({:.2f}, {:.2f}), name=\"{}\"",
                 player.id, player.x, player.y, player.name);
    fmt::println("  流大小: {} 字节", stream.Size());

    // 反序列化
    PlayerInfo restored{};
    stream >> restored;
    fmt::println("  反序列化: id={}, pos=({:.2f}, {:.2f}), name=\"{}\"",
                 restored.id, restored.x, restored.y, restored.name);

    // 2. 字符串序列化
    //    string 使用 4 字节长度前缀 + 内容的格式
    //    反序列化时先读长度，再读对应字节数的内容
    fmt::println("\n--- 2. 字符串序列化 ---");
    BytesStream str_stream;

    std::string text = "hello bytes stream";
    str_stream << text;
    fmt::println("  写入字符串: \"{}\" (长度={})", text, text.size());
    fmt::println("  流大小: {} 字节 (4字节长度前缀 + {}字节内容)",
                 str_stream.Size(), text.size());

    std::string read_text;
    str_stream >> read_text;
    fmt::println("  读取字符串: \"{}\"", read_text);

    // 3. 向量序列化
    //    vector 直接按字节写入，不写长度前缀（与 string 不同）
    //    反序列化时需要预分配好目标 vector 的大小
    fmt::println("\n--- 3. 向量序列化 ---");
    BytesStream vec_stream;

    std::vector<int> nums = {10, 20, 30, 40, 50};
    vec_stream << nums;
    fmt::println("  写入 vector: [{}] ({}个元素, {}字节)",
                 fmt::join(nums, ", "), nums.size(), nums.size() * sizeof(int));

    // 反序列化: 必须预分配目标 vector
    std::vector<int> read_nums(nums.size());
    vec_stream >> read_nums;
    fmt::println("  读取 vector: [{}]", fmt::join(read_nums, ", "));

    // 4. 链式写入
    //    operator<< 返回 BytesStream&，支持连续写入多个字段
    //    这是流式序列化的常见模式
    fmt::println("\n--- 4. 链式写入 ---");
    BytesStream chain_stream;

    uint32_t cmd_id = 42;
    float value = 3.14f;
    std::string payload = "chain-write";

    // 链式写入: stream << a << b << c
    chain_stream << cmd_id << value << payload;
    fmt::println("  链式写入: cmd_id={}, value={:.2f}, payload=\"{}\"",
                 cmd_id, value, payload);
    fmt::println("  流大小: {} 字节", chain_stream.Size());

    // 链式读取
    uint32_t read_cmd;
    float read_value;
    std::string read_payload;
    chain_stream >> read_cmd >> read_value >> read_payload;
    fmt::println("  链式读取: cmd_id={}, value={:.2f}, payload=\"{}\"",
                 read_cmd, read_value, read_payload);

    // 5. PostRead / PostWrite 手动控制
    //    在特殊场景下（如跳过某些字段），可以手动推进读写位置
    fmt::println("\n--- 5. PostRead / PostWrite 手动控制 ---");
    BytesStream manual_stream;

    // 手动写入: PostWrite 推进写指针
    manual_stream.Reserve(32);
    auto* raw = const_cast<char*>(manual_stream.Data());
    memcpy(raw, "RAW", 3);
    manual_stream.PostWrite(3);
    fmt::println("  PostWrite 推进写指针 3 字节");
    fmt::println("  流大小: {} 字节", manual_stream.Size());

    // 手动读取: PostRead 推进读指针
    manual_stream.PostRead(3);
    fmt::println("  PostRead 推进读指针 3 字节");

    // 6. ReadBytes 读取原始字节
    //    ReadBytes(count) 返回指定数量的原始字节，不进行类型解析
    fmt::println("\n--- 6. ReadBytes 读取原始字节 ---");
    BytesStream raw_stream;
    raw_stream << std::string("raw-bytes-test");

    auto raw_bytes = raw_stream.ReadBytes(4);  // 读取前4字节（长度前缀）
    fmt::println("  ReadBytes(4) 返回 {} 字节", raw_bytes.size());
    fmt::println("  值: [{:02x}, {:02x}, {:02x}, {:02x}]",
                 static_cast<uint8_t>(raw_bytes[0]),
                 static_cast<uint8_t>(raw_bytes[1]),
                 static_cast<uint8_t>(raw_bytes[2]),
                 static_cast<uint8_t>(raw_bytes[3]));

    fmt::println("\n========================================");
    fmt::println("  BytesStream 示例结束");
    fmt::println("========================================");

    return 0;
}
