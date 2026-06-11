// ────────────────────────────────────────────────────────────
// shmstore 示例 —— 发送者进程
//
// 编译：
//   cmake .. && cmake --build .
//   ./build/examples/example_shmstore_sender
//
// 运行（先启动接收者，再启动发送者）：
//   终端 1: ./build/examples/example_shmstore_receiver
//   终端 2: ./build/examples/example_shmstore_sender
//
// 演示内容：
//   1. 创建共享内存管道 tx_pipe，写入自定义协议消息
//   2. 通过 UDP 组播通知接收者
//   3. 监听接收者的 rx_pipe，等待响应
//   4. 多索引查询：按 seq、msg_type、device_id
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

// ── 自定义 key extractor（跨嵌套 struct 提取字段）──
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

// ── 多索引表定义 ──
//   索引 0: seq（唯一主键）
//   索引 1: device_id（按设备 ID 查询）
//   索引 2: msg_type（按消息类型查询）
using MsgStore = Store<MsgRecord,
                       mi::ordered_unique<SeqExtractor>,
                       mi::ordered_non_unique<DeviceIdExtractor>,
                       mi::ordered_non_unique<MsgTypeExtractor>>;

int main() {
    std::cout << "========== 发送者进程 ==========\n\n";

    constexpr size_t seg_size = 64 * 1024 * 1024;

    // ── 1. 创建发送管道 tx_pipe ──
    auto *tx_seg = ShmManager::instance().create_segment("tx_pipe", seg_size);
    MsgStore tx_store(tx_seg, "tx_msgs");

    ChangeNotifier notifier;
    tx_store.on_change([&notifier](const ChangeEvent &ev) { notifier.publish(ev.topic, ev); });

    // ── 2. 等待接收者创建 rx_pipe ──
    std::cout << "[*] 等待接收者创建 rx_pipe ...\n";
    bi::managed_shared_memory *rx_seg = nullptr;
    while (!(rx_seg = ShmManager::instance().open_segment("rx_pipe"))) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    MsgStore rx_store(rx_seg, "rx_msgs");

    // ── 3. 启动 UDP 组播监听（接收者变更通知）──
    std::atomic<bool> got_response{false};
    ChangeWatcher watcher;
    watcher.subscribe("rx_msgs", [&got_response](const DecodeResult &pkt) {
        std::cout << "   [通知] 收到接收者变更通知, topic=" << pkt.topic << "\n";
        got_response = true;
    });
    watcher.on_gap([](std::string_view topic, uint32_t gap) {
        std::cout << "   [警告] topic=" << topic << " 丢包 " << gap << " 条\n";
    });
    watcher.start();

    // ── 4. 发送消息（写入 tx_pipe）──
    std::cout << "\n[1] 发送心跳消息\n";
    {
        auto m = make_heartbeat(1, 1001, 3600, 45);
        tx_store.insert(m);
        print_msg("   TX →", m);
    }

    std::cout << "[2] 发送传感器数据\n";
    {
        auto m1 = make_sensor_report(2, 1001, 2001, 23.5f, 65.2f, SensorStatus::Ok);
        tx_store.insert(m1);
        print_msg("   TX →", m1);

        auto m2 = make_sensor_report(3, 1001, 2002, 42.1f, 30.0f, SensorStatus::Warn);
        tx_store.insert(m2);
        print_msg("   TX →", m2);
    }

    std::cout << "[3] 发送控制指令\n";
    {
        auto m1 = make_control_cmd(4, 1001, 3001, CmdCode::Start, 0);
        tx_store.insert(m1);
        print_msg("   TX →", m1);

        auto m2 = make_control_cmd(5, 1001, 3002, CmdCode::Reset, 1);
        tx_store.insert(m2);
        print_msg("   TX →", m2);
    }

    // ── 5. 多索引查询演示 ──
    std::cout << "\n[4] 多索引查询 —— tx_pipe 中共 " << tx_store.size() << " 条消息\n";

    // 按 seq 查询
    {
        auto &idx = tx_store.get_index<0>();
        if (auto it = idx.find(3); it != idx.end()) {
            std::cout << "   [seq=3] ";
            print_msg("", *it);
        }
    }

    // 按 msg_type 查询传感器数据
    {
        auto &idx = tx_store.get_index<2>();
        auto range = idx.equal_range(static_cast<uint8_t>(MsgType::SensorReport));
        std::cout << "   [类型=SensorReport] 共 " << std::distance(range.first, range.second) << " 条:\n";
        for (auto i = range.first; i != range.second; ++i) { print_msg("     ", *i); }
    }

    // 按 device_id 查询
    {
        auto &idx = tx_store.get_index<1>();
        auto range = idx.equal_range(3001);
        std::cout << "   [device_id=3001] 共 " << std::distance(range.first, range.second) << " 条:\n";
        for (auto i = range.first; i != range.second; ++i) { print_msg("     ", *i); }
    }

    // ── 6. 等待接收者响应 ──
    std::cout << "\n[5] 等待接收者响应 ...\n";
    for (int i = 0; i < 80; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (got_response.load() && !rx_store.empty()) break;
    }

    // ── 7. 读取接收者的响应 ──
    if (!rx_store.empty()) {
        std::cout << "\n[6] 收到接收者 " << rx_store.size() << " 条响应:\n";
        rx_store.for_each([](const MsgRecord &m) { print_msg("   RX ←", m); });
    } else {
        std::cout << "\n[6] 接收者未响应（超时）\n";
    }

    // ── 8. 更新演示：修改 seq=1 的心跳 ──
    std::cout << "\n[7] 更新演示 —— 修改 seq=1 心跳的 cpu_load\n";
    {
        auto &idx = tx_store.get_index<0>();
        if (auto it = idx.find(1); it != idx.end()) {
            tx_store.modify<0>(it, [](MsgRecord &m) { m.body.heartbeat.cpu_load = 99; });
            std::cout << "   更新后: ";
            print_msg("", *it);
        }
    }

    // ── 9. 删除演示 ──
    std::cout << "\n[8] 删除演示 —— 删除 seq=5\n";
    {
        auto &idx = tx_store.get_index<0>();
        if (auto it = idx.find(5); it != idx.end()) { tx_store.erase_at<0>(it); }
        std::cout << "   剩余 " << tx_store.size() << " 条\n";
    }

    watcher.stop();
    std::cout << "\n========== 发送者结束 ==========\n";
    return 0;
}
