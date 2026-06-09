// ────────────────────────────────────────────────────────────
// shmstore 示例 —— 接收者进程
//
// 编译：
//   cmake .. && cmake --build .
//   ./build/examples/example_shmstore_receiver
//
// 运行（先启动接收者，再启动发送者）：
//   终端 1: ./build/examples/example_shmstore_receiver
//   终端 2: ./build/examples/example_shmstore_sender
//
// 演示内容：
//   1. 创建共享内存管道 rx_pipe，等待发送者写入
//   2. 通过 UDP 组播监听发送者的 tx_pipe 变更
//   3. 读取并打印发送者写入的消息
//   4. 向 rx_pipe 写入响应（控制指令 + 传感器数据）
//   5. 多索引遍历与查询
// ────────────────────────────────────────────────────────────

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include "proto.hpp"
#include "shmstore/change_notifier.hpp"
#include "shmstore/change_watcher.hpp"
#include "shmstore/shm_manager.hpp"
#include "shmstore/store.hpp"

namespace bi = boost::interprocess;
namespace mi = boost::multi_index;
using namespace nebula::shmstore;

// ── 自定义 key extractor ──
struct SeqExtractor {
    using result_type = uint32_t;
    result_type operator()(const MsgRecord &m) const { return m.head.seq; }
};

struct MsgTypeExtractor {
    using result_type = uint8_t;
    result_type operator()(const MsgRecord &m) const { return m.head.msg_type; }
};

struct DeviceIdExtractor {
    using result_type = uint32_t;
    result_type operator()(const MsgRecord &m) const { return m.device_id(); }
};

// ── 多索引表（与发送者相同的 schema）──
using MsgStore = Store<MsgRecord,
                       mi::ordered_unique<SeqExtractor>,
                       mi::ordered_non_unique<DeviceIdExtractor>,
                       mi::ordered_non_unique<MsgTypeExtractor>>;

int main() {
    std::cout << "========== 接收者进程 ==========\n\n";

    constexpr size_t seg_size = 64 * 1024 * 1024;

    // ── 1. 创建接收管道 rx_pipe ──
    auto *rx_seg = ShmManager::instance().create_segment("rx_pipe", seg_size);
    MsgStore rx_store(rx_seg, "rx_msgs");

    ChangeNotifier notifier;
    rx_store.on_change([&notifier](const ChangeEvent &ev) { notifier.publish(ev.topic, ev); });

    // ── 2. 等待发送者创建 tx_pipe ──
    std::cout << "[*] 等待发送者创建 tx_pipe ...\n";
    bi::managed_shared_memory *tx_seg = nullptr;
    while (!(tx_seg = ShmManager::instance().open_segment("tx_pipe"))) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    MsgStore tx_store(tx_seg, "tx_msgs");

    // ── 3. 启动 UDP 组播监听（发送者变更通知）──
    std::atomic<bool> got_msg{false};
    std::atomic<int> msg_count{0};

    ChangeWatcher watcher;
    watcher.subscribe("tx_msgs", [&](const DecodeResult &pkt) {
        msg_count.fetch_add(1);
        got_msg = true;
    });
    watcher.on_gap([](std::string_view topic, uint32_t gap) {
        std::cout << "   [警告] topic=" << topic << " 丢包 " << gap << " 条\n";
    });
    watcher.start();

    // ── 4. 等待发送者写入数据 ──
    std::cout << "[*] 等待发送者写入消息 ...\n";
    for (int i = 0; i < 50 && !got_msg.load(); ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); }
    // 多等一轮确保全部写入
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // ── 5. 读取并打印发送者的全部消息 ──
    std::cout << "\n[1] 收到发送者 " << tx_store.size() << " 条消息:\n";
    tx_store.for_each([](const MsgRecord &m) { print_msg("   RX ←", m); });

    // ── 6. 按类型统计 ──
    std::cout << "\n[2] 按消息类型统计:\n";
    {
        auto &idx = tx_store.get_index<2>();
        {
            auto range = idx.equal_range(static_cast<uint8_t>(MsgType::Heartbeat));
            std::cout << "   Heartbeat:    " << std::distance(range.first, range.second) << " 条\n";
        }
        {
            auto range = idx.equal_range(static_cast<uint8_t>(MsgType::SensorReport));
            std::cout << "   SensorReport: " << std::distance(range.first, range.second) << " 条\n";
        }
        {
            auto range = idx.equal_range(static_cast<uint8_t>(MsgType::ControlCmd));
            std::cout << "   ControlCmd:   " << std::distance(range.first, range.second) << " 条\n";
        }
    }

    // ── 7. 查找并处理告警传感器 ──
    std::cout << "\n[3] 扫描告警传感器（status=Warn）:\n";
    tx_store.for_each([](const MsgRecord &m) {
        if (static_cast<MsgType>(m.head.msg_type) == MsgType::SensorReport
            && m.body.sensor.status == static_cast<uint8_t>(SensorStatus::Warn)) {
            print_msg("   ⚠ ", m);
        }
    });

    // ── 8. 写入响应到 rx_pipe ──
    std::cout << "\n[4] 向 rx_pipe 写入响应:\n";

    // 对告警传感器发 Reset 指令
    {
        auto m = make_control_cmd(101, 2001, 2002, CmdCode::Reset, 0);
        rx_store.insert(m);
        print_msg("   TX →", m);
    }

    // 回复心跳确认
    {
        auto m = make_heartbeat(102, 2001, 1800, 12);
        rx_store.insert(m);
        print_msg("   TX →", m);
    }

    // 上报自己的传感器数据
    {
        auto m = make_sensor_report(103, 2001, 4001, 18.3f, 55.0f, SensorStatus::Ok);
        rx_store.insert(m);
        print_msg("   TX →", m);
    }

    // ── 9. 验证 rx_pipe 中的数据 ──
    std::cout << "\n[5] rx_pipe 中共 " << rx_store.size() << " 条响应\n";

    // 按 device_id 查询
    {
        auto &idx = rx_store.get_index<1>();
        auto range = idx.equal_range(2002);
        std::cout << "   [device_id=2002] 共 " << std::distance(range.first, range.second) << " 条:\n";
        for (auto i = range.first; i != range.second; ++i) { print_msg("     ", *i); }
    }

    // ── 10. 等待发送者读取响应后退出 ──
    std::cout << "\n[*] 等待发送者读取响应（3 秒） ...\n";
    std::this_thread::sleep_for(std::chrono::seconds(3));

    watcher.stop();

    // 清理
    ShmManager::instance().destroy_segment("rx_pipe");
    ShmManager::instance().destroy_segment("tx_pipe");
    std::cout << "\n========== 接收者结束 ==========\n";
    return 0;
}
