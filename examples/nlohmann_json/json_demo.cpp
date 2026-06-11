// =============================================================================
// json_demo.cpp -- nlohmann/json 教学示例
// =============================================================================
//
// 核心思想:
//   nlohmann/json 是 C++ 中最流行的 JSON 库，设计哲学是让 JSON 值与 C++
//   类型之间实现自然映射。json 对象同时可以是对象、数组、字符串、数字、
//   布尔或 null，类型在运行时动态确定。
//
// nlohmann/json 的关键特性:
//   1. 直观的 API -- 像操作 STL 容器一样操作 JSON
//   2. 三种构造风格 -- json::parse() / json::object() / UDL _json
//   3. 类型安全 -- is_xxx() 检查 + at() 异常访问 + value() 默认值
//   4. ADL 序列化 -- 通过 to_json/from_json 自动支持自定义类型
//
// =============================================================================

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <string>
#include <vector>

// 使用 nlohmann::json 简化命名
using json = nlohmann::json;

// 自定义类型 -- 用于演示 ADL 序列化
struct User {
    std::string name;
    int age;
    std::vector<std::string> tags;
};

// ADL (Argument-Dependent Lookup) 序列化函数
// nlohmann/json 会自动发现同一命名空间下的 to_json/from_json
void to_json(json& j, const User& u) {
    j = json{{"name", u.name}, {"age", u.age}, {"tags", u.tags}};
}

void from_json(const json& j, User& u) {
    j.at("name").get_to(u.name);
    j.at("age").get_to(u.age);
    j.at("tags").get_to(u.tags);
}

int main() {
    fmt::println("========================================");
    fmt::println("  nlohmann/json 教学示例");
    fmt::println("========================================\n");

    // 1. 构造 JSON
    //    json 对象可以像 STL 容器一样使用初始化列表构造
    //    嵌套结构通过嵌套的 {} 自然表达
    fmt::println("--- 1. 构造 JSON ---");
    json config = {
        {"app", "nebula-toolkit"},
        {"version", 2},
        {"debug", true},
        {"modules", json::array({"containers", "logger", "io"})},
        {"server", {{"host", "127.0.0.1"}, {"port", 8080}}}
    };
    fmt::println("  {}", config.dump(2));  // dump(2) 美化输出，缩进2空格

    // 2. 访问与修改
    //    operator[] -- 不存在时自动创建（写入模式）
    //    at() -- 不存在时抛异常（安全读取）
    //    value() -- 不存在时返回默认值（防御性编程）
    fmt::println("\n--- 2. 访问与修改 ---");
    fmt::println("  app:    {}", config["app"].get<std::string>());
    fmt::println("  port:   {}", config["server"]["port"].get<int>());

    // at() 会做边界检查，key 不存在时抛出 json::out_of_range
    fmt::println("  debug:  {}", config.at("debug").get<bool>());

    // value() 提供默认值，key 不存在时不抛异常
    std::string log_level = config.value("log_level", "info");
    fmt::println("  log_level (默认值): {}", log_level);

    // 修改现有值
    config["version"] = 3;
    // 添加新字段
    config["log_level"] = "debug";
    fmt::println("  修改后: {}", config.dump());

    // 3. 序列化与反序列化
    //    dump() 将 JSON 对象序列化为字符串
    //    parse() 将字符串反序列化为 JSON 对象
    fmt::println("\n--- 3. 序列化与反序列化 ---");
    std::string json_str = R"({"name": "alice", "scores": [95, 87, 92]})";
    auto parsed = json::parse(json_str);
    fmt::println("  解析结果: name={}, scores={}",
                 parsed["name"].get<std::string>(),
                 parsed["scores"].dump());
    fmt::println("  重新序列化: {}", parsed.dump());

    // 4. 类型检查
    //    is_xxx() 系列方法用于运行时类型判断
    fmt::println("\n--- 4. 类型检查 ---");
    fmt::println("  config[\"app\"] is_string:  {}", config["app"].is_string());
    fmt::println("  config[\"version\"] is_number:  {}", config["version"].is_number());
    fmt::println("  config[\"modules\"] is_array:  {}", config["modules"].is_array());
    fmt::println("  config[\"server\"] is_object:  {}", config["server"].is_object());

    json null_val = nullptr;
    fmt::println("  null_val is_null:  {}", null_val.is_null());

    // 5. STL 兼容遍历
    //    items() 返回 key-value 对的迭代器，支持结构化绑定
    fmt::println("\n--- 5. STL 兼容遍历 ---");
    fmt::println("  server 配置:");
    for (auto& [key, val] : config["server"].items()) {
        fmt::println("    {} = {}", key, val.dump());
    }

    // 数组遍历
    fmt::println("  modules:");
    for (size_t i = 0; i < config["modules"].size(); ++i) {
        fmt::println("    [{}] {}", i, config["modules"][i].get<std::string>());
    }

    // 6. 异常与非异常访问对比
    //    at()     -- 安全，key 不存在抛 json::out_of_range
    //    []       -- 不安全，key 不存在时静默创建 null 值
    //    value()  -- 安全，key 不存在时返回默认值
    fmt::println("\n--- 6. 异常与非异常访问 ---");
    try {
        config.at("nonexistent_key");
    } catch (const json::out_of_range& e) {
        fmt::println("  at() 异常: {}", e.what());
    }

    // value() 不会抛异常，返回默认值
    int timeout = config.value("timeout", 30);
    fmt::println("  value() 默认值: timeout={}", timeout);

    // 7. 自定义类型 ADL 序列化
    //    通过定义同命名空间下的 to_json/from_json，nlohmann/json 自动支持
    fmt::println("\n--- 7. 自定义类型 ADL 序列化 ---");
    User user{"bob", 25, {"admin", "developer"}};
    json user_json = user;  // 自动调用 to_json
    fmt::println("  序列化: {}", user_json.dump(2));

    User restored = user_json.get<User>();  // 自动调用 from_json
    fmt::println("  反序列化: name={}, age={}, tags={}",
                 restored.name, restored.age,
                 fmt::join(restored.tags, ", "));

    fmt::println("\n========================================");
    fmt::println("  nlohmann/json 示例结束");
    fmt::println("========================================");

    return 0;
}
