#pragma once

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>

#include "shmstore/change_event.hpp"
#include "shmstore/shm_allocator.hpp"

namespace nebula::shmstore {

namespace mi = boost::multi_index;

// ────────────────────────────────────────────────────────────
// Store：基于 Boost.MultiIndex 的共享内存多索引表
//
// 模板参数：
//   Record  — POD 结构体（必须是标准布局类型）
//   Indexes — Boost.MultiIndex 索引定义
//
// 用法：
//   struct Order { uint64_t order_id; std::array<char,16> symbol; double price; };
//
//   using OrderStore = Store<Order,
//       mi::ordered_unique<mi::member<Order, uint64_t, &Order::order_id>>,
//       mi::ordered_non_unique<mi::member<Order, std::array<char,16>, &Order::symbol>>
//   >;
//
//   auto* seg = ShmManager::instance().create_segment("orders", 64*1024*1024);
//   OrderStore store(seg, "orders");
//   store.insert(Order{1, {"AAPL"}, 150.0});
//
// 索引访问：store.get_index<N>()，自带 find / equal_range / lower_bound 等接口
// ────────────────────────────────────────────────────────────
template <typename Record, typename... Indexes>
class Store {
    static_assert(std::is_standard_layout_v<Record>, "Record must be a standard-layout type for shared memory");

public:
    using container_type =
        boost::multi_index_container<Record, boost::multi_index::indexed_by<Indexes...>, ShmAllocator<Record>>;

    using iterator = typename container_type::iterator;
    using ChangeCallback = std::function<void(const ChangeEvent&)>;

    Store(boost::interprocess::managed_shared_memory* seg, const std::string& name)
        : seg_(seg), name_(name) {
        container_ = seg->find_or_construct<container_type>(name.c_str())(
            typename container_type::ctor_args_list(), seg->get_segment_manager());
    }

    ~Store() = default;
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    // ── 数据操作 ──────────────────────────────────────────

    std::pair<iterator, bool> insert(const Record& rec) {
        auto [it, ok] = container_->insert(rec);
        if (ok && change_cb_) notify(Op::Insert, *it);
        return {it, ok};
    }

    // 通过索引修改记录（维护所有索引一致性）
    template <size_t Idx, typename Modifier>
    bool modify(iterator it, Modifier modifier) {
        auto& idx = container_->template get<Idx>();
        auto local_it = idx.iterator_to(*it);
        bool ok = idx.modify(local_it, modifier);
        if (ok && change_cb_) notify(Op::Update, *local_it);
        return ok;
    }

    void erase(iterator it) {
        if (change_cb_) notify(Op::Erase, *it);
        container_->erase(it);
    }

    // 通过第 Idx 个索引的迭代器删除
    template <size_t Idx, typename Iter>
    size_t erase_at(Iter it) {
        auto& idx = container_->template get<Idx>();
        if (it == idx.end()) return 0;
        if (change_cb_) notify(Op::Erase, *it);
        idx.erase(it);
        return 1;
    }

    // 获取第 N 个索引的引用
    template <size_t N>
    auto& get_index() {
        return container_->template get<N>();
    }

    template <size_t N>
    const auto& get_index() const {
        return container_->template get<N>();
    }

    size_t size() const { return container_->size(); }
    bool empty() const { return container_->empty(); }

    template <typename Func>
    void for_each(Func&& func) const {
        for (const auto& rec : *container_) {
            func(rec);
        }
    }

    // ── 变更通知 ──────────────────────────────────────────

    void on_change(ChangeCallback cb) { change_cb_ = std::move(cb); }

    // 手动触发通知（用于批量更新后的刷新）
    void flush_notify(const Record& rec, Op op) {
        if (change_cb_) notify(op, rec);
    }

    const std::string& name() const { return name_; }

private:
    // 取记录前 min(sizeof(Record), 8) 字节作为 key
    void notify(Op op, const Record& rec) {
        constexpr size_t kKeyLen = sizeof(Record) < sizeof(uint64_t) ? sizeof(Record) : sizeof(uint64_t);
        ChangeEvent ev{op, name_, ByteSpan{reinterpret_cast<const uint8_t*>(&rec), kKeyLen}};
        change_cb_(ev);
    }

    boost::interprocess::managed_shared_memory* seg_;
    std::string name_;
    container_type* container_ = nullptr;
    ChangeCallback change_cb_;
};

}  // namespace nebula::shmstore
