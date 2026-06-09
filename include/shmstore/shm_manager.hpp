#pragma once

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace nebula::shmstore {

// ────────────────────────────────────────────────────────────
// ShmManager：共享内存段生命周期管理（单例）
//
// 用法：
//   auto& mgr = ShmManager::instance();
//   auto* seg = mgr.create_segment("order_book", 64 * 1024 * 1024);
//   // ... 使用 seg 构建 Store ...
//   mgr.destroy_segment("order_book");
// ────────────────────────────────────────────────────────────
class ShmManager {
public:
    static ShmManager& instance() {
        static ShmManager mgr;
        return mgr;
    }

    // 创建共享内存段（已存在则打开）
    // size: 段大小（字节），首次创建时生效
    boost::interprocess::managed_shared_memory* create_segment(const std::string& name, size_t size) {
        std::lock_guard<std::mutex> lock(mu_);

        auto it = segments_.find(name);
        if (it != segments_.end()) return it->second.get();

        auto seg = std::make_unique<boost::interprocess::managed_shared_memory>(
            boost::interprocess::open_or_create, name.c_str(), size);
        auto* ptr = seg.get();
        segments_.emplace(name, std::move(seg));
        return ptr;
    }

    // 打开已有的共享内存段（不存在返回 nullptr）
    boost::interprocess::managed_shared_memory* open_segment(const std::string& name) {
        std::lock_guard<std::mutex> lock(mu_);

        auto it = segments_.find(name);
        if (it != segments_.end()) return it->second.get();

        try {
            auto seg = std::make_unique<boost::interprocess::managed_shared_memory>(
                boost::interprocess::open_only, name.c_str());
            auto* ptr = seg.get();
            segments_.emplace(name, std::move(seg));
            return ptr;
        } catch (...) {
            return nullptr;
        }
    }

    // 销毁共享内存段（断开映射 + 删除系统对象）
    bool destroy_segment(const std::string& name) {
        std::lock_guard<std::mutex> lock(mu_);

        auto it = segments_.find(name);
        if (it != segments_.end()) {
            segments_.erase(it);
        }
        return boost::interprocess::shared_memory_object::remove(name.c_str());
    }

    // 列出当前管理的所有段名
    std::vector<std::string> list_segments() const {
        std::lock_guard<std::mutex> lock(mu_);
        std::vector<std::string> names;
        names.reserve(segments_.size());
        for (const auto& [name, _] : segments_) {
            names.push_back(name);
        }
        return names;
    }

    ShmManager(const ShmManager&) = delete;
    ShmManager& operator=(const ShmManager&) = delete;

private:
    ShmManager() = default;

    mutable std::mutex mu_;
    std::unordered_map<std::string, std::unique_ptr<boost::interprocess::managed_shared_memory>>
        segments_;
};

}  // namespace nebula::shmstore
