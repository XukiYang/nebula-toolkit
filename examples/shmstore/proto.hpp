// ────────────────────────────────────────────────────────────
// 共享协议定义 —— 发送者 / 接收者进程共用
// ────────────────────────────────────────────────────────────
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>

// ── 协议常量 ──
constexpr uint16_t kPackMagic = 0xEB01;  // 包头魔数
constexpr uint16_t kTailMagic = 0xED02;  // 包尾魔数
constexpr uint8_t kVersion = 0x01;

// ── 消息类型 ──
enum class MsgType : uint8_t {
    Heartbeat = 0x01,     // 心跳
    SensorReport = 0x02,  // 传感器上报
    ControlCmd = 0x03,    // 控制指令
};

// ── 传感器状态 ──
enum class SensorStatus : uint8_t { Ok = 0, Warn = 1, Error = 2 };

// ── 控制指令码 ──
enum class CmdCode : uint8_t { Start = 1, Stop = 2, Reset = 3 };

// ────────────────────────────────────────────────────────────
// 包头 PackHead（24 字节）
// ────────────────────────────────────────────────────────────
struct PackHead {
    uint16_t magic;      // kPackMagic
    uint8_t version;     // kVersion
    uint8_t msg_type;    // MsgType
    uint32_t seq;        // 序列号
    uint32_t src_id;     // 发送方设备 ID
    uint32_t dst_id;     // 目标设备 ID
    uint32_t body_len;   // body 长度
    uint32_t timestamp;  // 秒级时间戳
};
static_assert(sizeof(PackHead) == 24, "PackHead must be 24 bytes");

// ────────────────────────────────────────────────────────────
// 消息体（固定 16 字节，按最大类型对齐）
// ────────────────────────────────────────────────────────────

// 心跳
struct HeartbeatBody {
    uint32_t uptime_sec;  // 运行时长（秒）
    uint8_t cpu_load;     // CPU 负载百分比
    uint8_t reserved[11];
};
static_assert(sizeof(HeartbeatBody) == 16);

// 传感器上报
struct SensorReportBody {
    uint32_t sensor_id;
    float temperature;  // 温度（℃）
    float humidity;     // 湿度（%）
    uint8_t status;     // SensorStatus
    uint8_t reserved[3];
};
static_assert(sizeof(SensorReportBody) == 16);

// 控制指令
struct ControlCmdBody {
    uint32_t target_id;
    uint8_t cmd_code;  // CmdCode
    uint8_t param;     // 附带参数
    uint8_t reserved[10];
};
static_assert(sizeof(ControlCmdBody) == 16);

// ────────────────────────────────────────────────────────────
// 包尾 PackTail（8 字节）
// ────────────────────────────────────────────────────────────
struct PackTail {
    uint32_t crc32;       // 校验（此处简化为 0）
    uint16_t tail_magic;  // kTailMagic
    uint16_t reserved;
};
static_assert(sizeof(PackTail) == 8, "PackTail must be 8 bytes");

// ────────────────────────────────────────────────────────────
// 完整消息 —— 共享内存中的存储单元
//   布局: [PackHead][Body(16B)][PackTail]
//   sizeof = 24 + 16 + 8 = 48 字节
// ────────────────────────────────────────────────────────────
struct MsgRecord {
    PackHead head;
    union Body {
        HeartbeatBody heartbeat;
        SensorReportBody sensor;
        ControlCmdBody cmd;
        std::array<uint8_t, 16> raw;
        Body() { raw.fill(0); }
    } body;
    PackTail tail;

    // 便捷: 获取 body 中的设备 ID（用于索引）
    uint32_t device_id() const {
        switch (static_cast<MsgType>(head.msg_type)) {
        case MsgType::Heartbeat:
            return head.src_id;
        case MsgType::SensorReport:
            return body.sensor.sensor_id;
        case MsgType::ControlCmd:
            return body.cmd.target_id;
        default:
            return 0;
        }
    }
};
static_assert(sizeof(MsgRecord) == 48, "MsgRecord must be 48 bytes");

// ────────────────────────────────────────────────────────────
// 辅助: 构造消息
// ────────────────────────────────────────────────────────────
inline MsgRecord make_heartbeat(uint32_t seq, uint32_t src_id, uint32_t uptime, uint8_t cpu_load) {
    MsgRecord m{};
    m.head.magic = kPackMagic;
    m.head.version = kVersion;
    m.head.msg_type = static_cast<uint8_t>(MsgType::Heartbeat);
    m.head.seq = seq;
    m.head.src_id = src_id;
    m.head.dst_id = 0;  // 广播
    m.head.body_len = sizeof(HeartbeatBody);
    m.head.timestamp = static_cast<uint32_t>(time(nullptr));
    m.body.heartbeat.uptime_sec = uptime;
    m.body.heartbeat.cpu_load = cpu_load;
    m.tail.tail_magic = kTailMagic;
    return m;
}

inline MsgRecord make_sensor_report(
    uint32_t seq, uint32_t src_id, uint32_t sensor_id, float temp, float hum, SensorStatus st) {
    MsgRecord m{};
    m.head.magic = kPackMagic;
    m.head.version = kVersion;
    m.head.msg_type = static_cast<uint8_t>(MsgType::SensorReport);
    m.head.seq = seq;
    m.head.src_id = src_id;
    m.head.dst_id = 0;
    m.head.body_len = sizeof(SensorReportBody);
    m.head.timestamp = static_cast<uint32_t>(time(nullptr));
    m.body.sensor.sensor_id = sensor_id;
    m.body.sensor.temperature = temp;
    m.body.sensor.humidity = hum;
    m.body.sensor.status = static_cast<uint8_t>(st);
    m.tail.tail_magic = kTailMagic;
    return m;
}

inline MsgRecord make_control_cmd(uint32_t seq, uint32_t src_id, uint32_t target_id, CmdCode cmd, uint8_t param) {
    MsgRecord m{};
    m.head.magic = kPackMagic;
    m.head.version = kVersion;
    m.head.msg_type = static_cast<uint8_t>(MsgType::ControlCmd);
    m.head.seq = seq;
    m.head.src_id = src_id;
    m.head.dst_id = target_id;
    m.head.body_len = sizeof(ControlCmdBody);
    m.head.timestamp = static_cast<uint32_t>(time(nullptr));
    m.body.cmd.target_id = target_id;
    m.body.cmd.cmd_code = static_cast<uint8_t>(cmd);
    m.body.cmd.param = param;
    m.tail.tail_magic = kTailMagic;
    return m;
}

// ────────────────────────────────────────────────────────────
// 辅助: 打印消息
// ────────────────────────────────────────────────────────────
inline const char *msg_type_str(uint8_t t) {
    switch (static_cast<MsgType>(t)) {
    case MsgType::Heartbeat:
        return "HEARTBEAT";
    case MsgType::SensorReport:
        return "SENSOR";
    case MsgType::ControlCmd:
        return "CONTROL";
    default:
        return "UNKNOWN";
    }
}

inline void print_msg(const char *prefix, const MsgRecord &m) {
    std::cout << prefix << " [seq=" << m.head.seq << " " << msg_type_str(m.head.msg_type) << " src=" << m.head.src_id
              << " dst=" << m.head.dst_id << "]";
    switch (static_cast<MsgType>(m.head.msg_type)) {
    case MsgType::Heartbeat:
        std::cout << " uptime=" << m.body.heartbeat.uptime_sec << "s cpu=" << (int)m.body.heartbeat.cpu_load << "%";
        break;
    case MsgType::SensorReport:
        std::cout << " sensor=" << m.body.sensor.sensor_id << " temp=" << std::fixed << std::setprecision(1)
                  << m.body.sensor.temperature << "℃ hum=" << m.body.sensor.humidity << "%";
        break;
    case MsgType::ControlCmd:
        std::cout << " target=" << m.body.cmd.target_id << " cmd=" << (int)m.body.cmd.cmd_code
                  << " param=" << (int)m.body.cmd.param;
        break;
    default:
        break;
    }
    std::cout << "\n";
}
