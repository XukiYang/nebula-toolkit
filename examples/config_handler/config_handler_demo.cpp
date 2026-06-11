// =============================================================================
// config_handler_demo.cpp -- 配置处理模块教学示例
// =============================================================================
//
// 核心思想:
//   配置处理模块提供两种配置格式的解析能力:
//   1. INI 格式 -- 简单的键值对，适合扁平配置
//   2. JSON 格式 -- 结构化数据，适合嵌套配置
//
// 模块包含三个类:
//   - IniConfigHandler: 带缓存的 INI 读写器，支持修改后保存
//   - IniReader: 轻量级无缓存 INI 读取器，每次读取都访问文件
//   - JsonReader: 基于 nlohmann/json 的 JSON 配置读取器，支持点分路径
//
// =============================================================================

#include <fmt/format.h>

#include <config_handler/ini_config_handler.hpp>
#include <config_handler/ini_reader.hpp>
#include <config_handler/json_reader.hpp>
#include <string>

int main() {
    fmt::println("========================================");
    fmt::println("  配置处理模块教学示例");
    fmt::println("========================================\n");

    // 配置文件路径 (尝试从项目根目录或 build 目录找到配置文件)
    auto find_path = [](const std::string& rel) -> std::string {
        // 尝试从项目根目录
        if (FILE* f = fopen(rel.c_str(), "r")) {
            fclose(f);
            return rel;
        }
        // 尝试从 build 目录 (../)
        std::string alt = "../" + rel;
        if (FILE* f = fopen(alt.c_str(), "r")) {
            fclose(f);
            return alt;
        }
        return rel;  // 返回原始路径，让后续报错
    };
    const std::string ini_path = find_path("examples/config_handler/config.ini");
    const std::string json_path = find_path("examples/config_handler/config.json");

    // 1. IniConfigHandler: 带缓存的 INI 读写器
    //    读取整个 INI 文件到内存缓存，后续查询从缓存读取
    //    支持修改缓存后保存回文件
    fmt::println("--- 1. IniConfigHandler: 带缓存的 INI 读写器 ---");
    {
        nebula::config_handler::IniConfigHandler handler(ini_path);

        if (!handler.ReadIniFile()) {
            fmt::println("  [ERROR] 无法读取配置文件");
            return 1;
        }

        // 查询配置值
        fmt::println("  server.host = {}", handler.GetVal("server", "host").GetString());
        fmt::println("  server.port = {}", handler.GetVal("server", "port").GetInt());
        fmt::println("  server.debug = {}", handler.GetVal("server", "debug").GetBool());
        fmt::println("  log.level = {}", handler.GetVal("log", "level").GetString());

        // 带默认值的查询
        fmt::println("  server.timeout (默认) = {}", handler.GetVal("server", "timeout", "30").GetString());

        // 检查 section/key 是否存在
        fmt::println("  HasSection(\"server\"): {}", handler.HasSection("server"));
        fmt::println("  HasKey(\"server\", \"host\"): {}", handler.HasKey("server", "host"));

        // 修改缓存
        handler.SetVal("server", "port", nebula::config_handler::Val("9090"));
        fmt::println("  修改后 server.port = {}", handler.GetVal("server", "port").GetInt());

        // 保存到新文件 (验证写入能力)
        std::string save_path = ini_path.substr(0, ini_path.rfind('/') + 1) + "config_modified.ini";
        handler.Save(save_path);
        fmt::println("  已保存到 {}", save_path);
    }

    // 2. IniReader: 轻量级无缓存 INI 读取器
    //    每次 GetValue 都从文件读取，适合配置不常变化的场景
    //    支持类型安全读取: bool / size_t / string
    fmt::println("\n--- 2. IniReader: 轻量级无缓存 INI 读取器 ---");
    {
        nebula::config_handler::IniReader reader(ini_path);

        std::string host;
        size_t port = 0;
        bool debug = false;

        reader.GetValue("server", "host", host);
        reader.GetValue("server", "port", port);
        reader.GetValue("server", "debug", debug);

        fmt::println("  host = {}", host);
        fmt::println("  port = {}", port);
        fmt::println("  debug = {}", debug);

        // 读取不存在的 key 返回 false
        std::string missing;
        bool found = reader.GetValue("server", "nonexistent", missing);
        fmt::println("  读取不存在的 key: {}", found ? "找到" : "未找到");
    }

    // 3. JsonReader: 基于 nlohmann/json 的 JSON 配置读取器
    //    支持点分路径查询嵌套字段，如 "log.global.max_file_size_kb"
    //    支持文件变更检测，适合热重载场景
    fmt::println("\n--- 3. JsonReader: JSON 配置读取器 ---");
    {
        nebula::config_handler::JsonReader json_reader(json_path);

        std::string app_name;
        int version = 0;
        std::string host;
        int port = 0;
        bool debug = false;
        size_t ring_buf_size = 0;

        // 点分路径查询嵌套字段
        json_reader.GetString("app.name", app_name);
        json_reader.GetInt("app.version", version);
        json_reader.GetString("server.host", host);
        json_reader.GetInt("server.port", port);
        json_reader.GetBool("server.debug", debug);
        json_reader.GetUInt("log.async.ring_buffer_size", ring_buf_size);

        fmt::println("  app.name = {}", app_name);
        fmt::println("  app.version = {}", version);
        fmt::println("  server.host = {}", host);
        fmt::println("  server.port = {}", port);
        fmt::println("  server.debug = {}", debug);
        fmt::println("  log.async.ring_buffer_size = {}", ring_buf_size);

        // 文件变更检测
        fmt::println("  IsModified: {} (文件未变更)", json_reader.IsModified());
    }

    fmt::println("\n========================================");
    fmt::println("  配置处理模块示例结束");
    fmt::println("========================================");

    return 0;
}
