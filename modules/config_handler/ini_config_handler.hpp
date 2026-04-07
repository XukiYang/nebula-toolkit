#pragma once
#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <unordered_map>

namespace nebula {
namespace config_handler {
/* 数据值包装类 */
struct Val {
    std::string raw_;

    explicit Val(const std::string& raw = "") : raw_(raw) {}

    /* 获取整型 */
    int GetInt() const {
        try {
            return std::stoi(raw_);
        } catch (...) {
            return 0;
        }
    }

    /* 获取浮点型 */
    float GetFloat() const {
        try {
            return std::stof(raw_);
        } catch (...) {
            return 0.0f;
        }
    }

    /* 获取布尔型 */
    bool GetBool() const {
        std::string lower = raw_;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        return (lower == "true" || lower == "1" || lower == "t" || lower == "T" || lower == "yes" || lower == "YES"
                || lower == "on" || lower == "ON");
    }

    /* 获取字符串 */
    const std::string& GetString() const {
        return raw_;
    }

    /* 重载赋值操作符 */
    Val& operator=(const Val& other) {
        if (this != &other) {
            raw_ = other.raw_;
        }
        return *this;
    }
    /* 当定义了拷贝构造函数、拷贝赋值操作符、析构函数之一，就都需要定义 */
    /* 移动赋值 */
    // Val& operator=(Val&& other) noexcept {
    //     if (this != &other) {
    //         raw_ = std::move(other.raw_);
    //     }
    //     return *this;
    // }
};

class IniConfigHandler {
private:
    std::string                                                           file_path_;
    std::unordered_map<std::string, std::unordered_map<std::string, Val>> config_;
    std::string                                                           current_section_ = "DEFAULT";

private:
    /* 去除字符串首尾空白 */
    std::string trim(const std::string& str) const {
        size_t start = 0, end = str.length();
        while (start < end && std::isspace(str[start])) ++start;
        while (end > start && std::isspace(str[end - 1])) --end;
        return str.substr(start, end - start);
    }

    /* 判断是否为纯注释行 */
    bool IsComment(const std::string& line) const {
        std::string trimmed = trim(line);
        return !trimmed.empty() && (trimmed[0] == ';' || trimmed[0] == '#');
    }

    /* 判断是否为节名行 */
    bool IsSection(const std::string& line) const {
        std::string trimmed = trim(line);
        return trimmed.length() >= 2 && trimmed[0] == '[' && trimmed.back() == ']';
    }

    /* 提取节名 */
    std::string ExtractSection(const std::string& line) const {
        std::string trimmed = trim(line);
        return trimmed.substr(1, trimmed.length() - 2);
    }

    /* 解析键值对 */
    bool ParseKeyValue(const std::string& line, std::string& key, std::string& value) const {
        size_t pos = line.find('=');
        if (pos == std::string::npos) return false;

        key   = trim(line.substr(0, pos));
        value = trim(line.substr(pos + 1));

        /* 去除值两端的引号 */
        if (value.length() >= 2
            && ((value[0] == '"' && value.back() == '"') || (value[0] == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.length() - 2);
        }

        /* 去除末尾可能存在的;或# */
        pos = value.find(';');
        if (pos != std::string::npos) {
            value = trim(value.substr(0, pos));
        }
        pos = value.find('#');
        if (pos != std::string::npos) {
            value = trim(value.substr(0, pos));
        }

        return !key.empty();
    }

public:
    explicit IniConfigHandler(const std::string& file_path = "") : file_path_(file_path) {}

    /* 读取INI文件到缓存 */
    bool ReadIniFile(const std::string& file_path = "") {
        std::string path = file_path.empty() ? file_path_ : file_path;
        if (path.empty()) return false;
        if (file_path_.empty()) file_path_ = file_path;

        std::ifstream file(path);
        if (!file.is_open()) {
            fmt::print("connot open file: {}\n", path);
            return false;
        }

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || IsComment(line)) {
                continue;
            }

            if (IsSection(line)) {
                current_section_ = ExtractSection(line);
                continue;
            }

            std::string key, value;
            if (ParseKeyValue(line, key, value)) {
                config_[current_section_][key] = Val(value);
            } else {
                fmt::print("warning: section {} has invalid value: {}\n", current_section_, line);
            }
        }
        return true;
    }

    /* 刷新缓存 */
    bool FlushCache(const std::string& file_path = "") {
        return ReadIniFile(file_path);
    }

    /* 获取值,基于引用 */
    bool GetVal(const std::string& section, const std::string& key, Val& out_val) const {
        auto section_it = config_.find(section);
        if (section_it != config_.end()) {
            auto key_it = section_it->second.find(key);
            if (key_it != section_it->second.end()) {
                out_val = key_it->second;
                return true;
            }
        }
        return false;
    }

    /* 获取值,基于返回值 */
    Val GetVal(const std::string& section, const std::string& key, const std::string& default_val = "") const {
        auto section_it = config_.find(section);
        if (section_it != config_.end()) {
            auto key_it = section_it->second.find(key);
            if (key_it != section_it->second.end()) {
                return key_it->second;
            }
        }
        return Val(default_val);
    }

    /* 设置值 */
    void SetVal(const std::string& section, const std::string& key, const Val& val) {
        config_[section][key] = val;
    }

    /* 保存到文件 */
    bool Save(const std::string& file_path = "") const {
        std::string path = file_path.empty() ? file_path_ : file_path;
        if (path.empty()) return false;

        std::ofstream file(path);
        if (!file.is_open()) {
            fmt::print("connot create file: {}\n", path);
            return false;
        }

        for (const auto& [section_name, section_data] : config_) {
            if (section_name != "DEFAULT") {
                file << "[" << section_name << "]\n";
            }

            for (const auto& [key, val] : section_data) {
                file << key << " = " << val.raw_ << "\n";
            }
            file << "\n";
        }
        return true;
    }

    /* 检查键是否存在 */
    bool HasKey(const std::string& section, const std::string& key) const {
        auto section_it = config_.find(section);
        if (section_it != config_.end()) {
            return section_it->second.find(key) != section_it->second.end();
        }
        return false;
    }

    /* 检查节是否存在 */
    bool HasSection(const std::string& section) const {
        return config_.find(section) != config_.end();
    }

    /* 清空配置 */
    void Clear() {
        config_.clear();
        current_section_ = "DEFAULT";
    }

    ~IniConfigHandler() = default;
};

}  // namespace config_handler
}  // namespace nebula
