#pragma once
#include <nlohmann/json.hpp>
#include <sys/stat.h>

#include <fstream>
#include <sstream>
#include <string>

namespace nebula {
namespace config_handler {

class JsonReader {
public:
    explicit JsonReader(const std::string& file_path) : file_path_(file_path) {
        LoadFile(file_path);
    }

    JsonReader() = default;

    bool GetString(const std::string& dot_path, std::string& out) const {
        const auto* node = NavigateToNode(dot_path);
        if (!node || !node->is_string()) return false;
        out = node->get<std::string>();
        return true;
    }

    bool GetInt(const std::string& dot_path, int& out) const {
        const auto* node = NavigateToNode(dot_path);
        if (!node || !node->is_number_integer()) return false;
        out = node->get<int>();
        return true;
    }

    bool GetUInt(const std::string& dot_path, size_t& out) const {
        const auto* node = NavigateToNode(dot_path);
        if (!node || !node->is_number_unsigned()) return false;
        out = node->get<size_t>();
        return true;
    }

    bool GetBool(const std::string& dot_path, bool& out) const {
        const auto* node = NavigateToNode(dot_path);
        if (!node || !node->is_boolean()) return false;
        out = node->get<bool>();
        return true;
    }

    bool GetFloat(const std::string& dot_path, float& out) const {
        const auto* node = NavigateToNode(dot_path);
        if (!node || !node->is_number()) return false;
        out = node->get<float>();
        return true;
    }

    bool IsModified() {
        struct stat st {};
        if (stat(file_path_.c_str(), &st) != 0) return false;
        if (st.st_mtime != last_mtime_) {
            last_mtime_ = st.st_mtime;
            return true;
        }
        return false;
    }

    bool LoadFile(const std::string& file_path) {
        file_path_ = file_path;
        std::ifstream file(file_path);
        if (!file.is_open()) return false;

        try {
            doc_ = nlohmann::json::parse(file);
        } catch (...) {
            return false;
        }

        struct stat st {};
        if (stat(file_path.c_str(), &st) == 0) {
            last_mtime_ = st.st_mtime;
        }
        return true;
    }

private:
    const nlohmann::json* NavigateToNode(const std::string& dot_path) const {
        const auto* current = &doc_;
        std::string segment;
        std::istringstream stream(dot_path);

        while (std::getline(stream, segment, '.')) {
            if (!current->is_object() || !current->contains(segment)) {
                return nullptr;
            }
            current = &(*current)[segment];
        }
        return current;
    }

    std::string file_path_;
    nlohmann::json doc_;
    time_t last_mtime_ = 0;
};

}  // namespace config_handler
}  // namespace nebula
