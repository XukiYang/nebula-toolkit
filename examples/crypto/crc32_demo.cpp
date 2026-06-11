// =============================================================================
// crc32_demo.cpp -- CRC32 校验教学示例
// =============================================================================
//
// 核心思想:
//   CRC32 (Cyclic Redundancy Check) 基于多项式除法计算校验值。
//   发送方计算数据的 CRC32 值并附加到数据末尾，接收方重新计算并比对，
//   若不一致则说明数据在传输过程中被篡改或损坏。
//
// CRC32 的关键特性:
//   1. 一次性计算 -- Crc32::Calculate() 适合完整数据
//   2. 增量计算 -- Crc32::Update() 适合流式数据，分块追加
//   3. 数据完整性验证 -- Crc32::Verify() 比对预期校验值
//
// =============================================================================

#include <fmt/format.h>

#include <crypto/crc32.hpp>
#include <string>
#include <vector>

int main() {
    using nebula::crypto::Crc32;

    fmt::println("========================================");
    fmt::println("  CRC32 校验教学示例");
    fmt::println("========================================\n");

    // 1. 一次性计算
    //    Crc32::Calculate() 对完整数据计算 CRC32
    //    支持 string、vector<uint8_t>、void*+size 等多种输入
    fmt::println("--- 1. 一次性计算 ---");
    std::string text = "Hello, CRC32!";
    uint32_t crc = Crc32::Calculate(text);
    fmt::println("  数据:   \"{}\"", text);
    fmt::println("  CRC32:  0x{:08X}", crc);

    // 不同数据类型
    std::vector<uint8_t> bytes = {0x01, 0x02, 0x03, 0x04};
    uint32_t bytes_crc = Crc32::Calculate(bytes);
    fmt::println("  bytes:  [01, 02, 03, 04] -> CRC32=0x{:08X}", bytes_crc);

    const char* raw = "raw data";
    uint32_t raw_crc = Crc32::Calculate(raw, strlen(raw));
    fmt::println("  raw:    \"{}\" -> CRC32=0x{:08X}", raw, raw_crc);

    // 2. 增量计算
    //    Crc32 实例支持分块追加数据，适用于流式场景
    //    每次调用 Update() 追加数据，最后调用 GetValue() 获取结果
    fmt::println("\n--- 2. 增量计算 ---");
    Crc32 incremental;
    incremental.Update("Hello, ");
    incremental.Update("CRC32!");
    uint32_t inc_crc = incremental.GetValue();
    fmt::println("  分块写入: \"Hello, \" + \"CRC32!\"");
    fmt::println("  增量CRC32: 0x{:08X}", inc_crc);
    fmt::println("  与一次性计算一致: {}", (inc_crc == crc) ? "PASS" : "FAIL");

    // 增量计算也可以分更多块
    Crc32 multi;
    multi.Update("He");
    multi.Update("ll");
    multi.Update("o, ");
    multi.Update("CR");
    multi.Update("C3");
    multi.Update("2!");
    uint32_t multi_crc = multi.GetValue();
    fmt::println("  6块分片CRC32: 0x{:08X} (应一致: {})",
                 multi_crc, (multi_crc == crc) ? "PASS" : "FAIL");

    // Reset 重置状态，可复用同一个 Crc32 对象
    incremental.Reset();
    incremental.Update("different data");
    fmt::println("  Reset后计算: 0x{:08X}", incremental.GetValue());

    // 3. 数据完整性验证
    //    Crc32::Verify() 将计算结果与预期值比对
    //    典型用法: 发送方计算 CRC，接收方验证
    fmt::println("\n--- 3. 数据完整性验证 ---");
    std::string packet = "important data packet";
    uint32_t expected_crc = Crc32::Calculate(packet);

    fmt::println("  发送方: 数据=\"{}\", CRC=0x{:08X}", packet, expected_crc);

    // 接收方验证 -- 数据完整
    bool valid = Crc32::Verify(packet, expected_crc);
    fmt::println("  接收方验证 (完整): {}", valid ? "PASS" : "FAIL");

    // 模拟数据损坏
    std::string corrupted = "important data packeT";  // 最后一个字符被篡改
    bool invalid = Crc32::Verify(corrupted, expected_crc);
    fmt::println("  接收方验证 (损坏): {}", invalid ? "PASS -- 不应通过" : "FAIL -- 检测到篡改");

    // 4. 便捷函数
    //    GenerateChecksum / VerifyChecksum 是自由函数，接口更简洁
    fmt::println("\n--- 4. 便捷函数 ---");
    using nebula::crypto::GenerateChecksum;
    using nebula::crypto::VerifyChecksum;

    const char* msg = "nebula toolkit";
    uint32_t checksum = GenerateChecksum(msg, strlen(msg));
    fmt::println("  GenerateChecksum: 0x{:08X}", checksum);
    fmt::println("  VerifyChecksum:   {}", VerifyChecksum(msg, strlen(msg), checksum) ? "PASS" : "FAIL");

    fmt::println("\n========================================");
    fmt::println("  CRC32 示例结束");
    fmt::println("========================================");

    return 0;
}
