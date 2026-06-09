# shmstore 实现与原理——从零到透彻

> 本文从发送者/接收者的代码出发，逐层展开 shmstore 的每一个组件。
> 假设读者熟悉 C++ 但没用过 Boost.Interprocess 和 Boost.MultiIndex。

---

## 目录

1. [全局架构：两个进程怎么通信](#1-全局架构)
2. [ShmManager：共享内存段的生命周期管理](#2-shmmanager)
3. [Boost.Interprocess 入门：managed_shared_memory](#3-boost-interprocess-入门)
4. [ShmAllocator：为什么不能用 std::vector](#4-shmallocator)
5. [Boost.MultiIndex 入门：一个多容器多个索引](#5-boost-multiindex-入门)
6. [Store 核心：把 MultiIndex 放进共享内存](#6-store-核心)
7. [自定义 Key Extractor：从嵌套结构取字段](#7-key-extractor)
8. [数据操作：insert / modify / erase](#8-数据操作)
9. [变更通知：ChangeEvent 与 Op](#9-变更通知)
10. [ChangeNotifier：UDP 组播发布](#10-changenotifier)
11. [网络包协议：编码/解码/CRC32](#11-网络包协议)
12. [ChangeWatcher：UDP 组播订阅](#12-changewatcher)
13. [完整数据流：一次 insert 的全链路](#13-完整数据流)
14. [性能瓶颈与优化方向](#14-性能瓶颈)

---

## 1. 全局架构

两个独立进程，通过共享内存交换数据，通过 UDP 组播交换"数据变了"的通知：

```
┌─────────────────┐                         ┌─────────────────┐
│     Sender      │                         │    Receiver     │
│                 │                         │                 │
│  tx_store ──┐   │    共享内存 "tx_pipe"    │   ┌── tx_store  │
│  (多索引表)  ├───┼────────────────────────┼───┤  (多索引表)  │
│             │   │                         │   └──            │
│  rx_store ──┤   │    共享内存 "rx_pipe"    │   ┌── rx_store  │
│  (多索引表)  ├───┼────────────────────────┼───┤  (多索引表)  │
│             │   │                         │   └──            │
│  notifier ──┤   │    UDP 组播 239.0.0.x   │   ├── watcher    │
│  watcher ───┘   │ ←──────────────────────→│   └── notifier   │
└─────────────────┘                         └─────────────────┘
```

**关键设计**：
- **数据走共享内存**——零拷贝，没有系统调用开销
- **通知走 UDP 组播**——轻量级，不阻塞数据路径
- **通知丢了不影响数据**——接收者可以随时主动去共享内存里读

发送者和接收者**对称**：各自创建一个管道（tx_pipe / rx_pipe），各自写自己的、读对方的。

---

## 2. ShmManager

```cpp
// include/shmstore/shm_manager.hpp
```

### 2.1 它解决什么问题

操作系统提供的共享内存 API 非常原始——你需要知道名字、大小，手动映射、手动解映射、手动删除。`ShmManager` 把这些封装成三个操作：

| 方法 | 行为 | 类比 |
|---|---|---|
| `create_segment(name, size)` | 创建或打开，指定大小 | `open(path, O_CREAT)` |
| `open_segment(name)` | 只打开已存在的，不存在返回 nullptr | `open(path, O_RDONLY)` |
| `destroy_segment(name)` | 解映射 + 删除系统对象 | `unlink(path)` |

### 2.2 单例模式

```cpp
static ShmManager& instance() {
    static ShmManager mgr;   // C++11 保证线程安全的局部静态变量
    return mgr;
}
```

用 Meyer's Singleton——第一次调用时构造，程序结束时析构。全局只需一个管理器。

### 2.3 内部缓存

```cpp
std::unordered_map<std::string, std::unique_ptr<boost::interprocess::managed_shared_memory>> segments_;
```

已打开的段缓存在 map 里。`create_segment` 先查缓存，有就直接返回指针，避免重复映射。`std::mutex mu_` 保护这个 map 的并发访问（多个线程同时调 create/open）。

### 2.4 create 与 open 的区别

```cpp
// create: open_or_create —— 不存在就创建，已存在就打开
auto seg = std::make_unique<boost::interprocess::managed_shared_memory>(
    boost::interprocess::open_or_create, name.c_str(), size);

// open: open_only —— 必须已存在，否则抛异常（被 catch 返回 nullptr）
auto seg = std::make_unique<boost::interprocess::managed_shared_memory>(
    boost::interprocess::open_only, name.c_str());
```

`open_or_create` 时 `size` 参数只在**首次创建**时生效；如果段已存在，size 被忽略，使用已有大小。

---

## 3. Boost.Interprocess 入门

### 3.1 什么是 managed_shared_memory

普通共享内存（POSIX `shm_open` / `mmap`）只给你一块原始字节。你想在里面放一个 `int`？手动算偏移。想放一个 `vector`？做不到——`vector` 内部有指针，跨进程后指针无效。

`boost::interprocess::managed_shared_memory` 在这块原始内存上实现了一个**简易堆管理器**：

```
┌──────────────────────────────────────────────────┐
│              共享内存段 (64MB)                      │
│                                                    │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐        │
│  │ 管理元数据 │  │  对象 A   │  │  对象 B   │  ...   │
│  │ (段头信息) │  │ (48 字节) │  │(多索引容器)│        │
│  └──────────┘  └──────────┘  └──────────┘        │
│                                                    │
│  段管理器知道每个对象在哪、多大、是否空闲            │
└──────────────────────────────────────────────────┘
```

### 3.2 核心 API

```cpp
namespace bi = boost::interprocess;

// 创建/打开一个 64MB 的共享内存段
bi::managed_shared_memory segment(bi::open_or_create, "my_segment", 64 * 1024 * 1024);

// 在共享内存里构造一个 int，名字叫 "counter"
int *p = segment.construct<int>("counter")(42);   // 初始值 42

// 在共享内存里构造一个对象，传参数给构造函数
MyObj *obj = segment.construct<MyObj>("my_obj")(arg1, arg2);

// 查找已构造的对象
auto [ptr, count] = segment.find<MyObj>("my_obj");  // ptr 是指针，count 是找到的个数

// find_or_construct：有就找，没有就构造
MyObj *obj2 = segment.find_or_construct<MyObj>("my_obj")(arg1, arg2);

// 析构并释放
segment.destroy<MyObj>("my_obj");
```

### 3.3 为什么需要 ShmAllocator

`segment.construct<T>()` 只能构造**单个对象**。如果你想在共享内存里放一个 `vector` 或 `multi_index_container`（它们内部需要动态分配节点），就需要一个**从共享内存段里分配内存的分配器**：

```cpp
// 获取段管理器
auto *mgr = segment.get_segment_manager();

// 用段管理器作为后端的分配器
bi::allocator<int, bi::managed_shared_memory::segment_manager> alloc(mgr);

// 现在可以用这个分配器构造容器，容器的节点全部分配在共享内存里
```

这就是 `ShmAllocator` 的来源。

---

## 4. ShmAllocator

```cpp
// include/shmstore/shm_allocator.hpp
```

### 4.1 定义

```cpp
template <typename T>
using ShmAllocator = boost::interprocess::allocator<T, ShmSegment::segment_manager>;
```

`boost::interprocess::allocator<T, SegmentManager>` 是 Boost 提供的分配器，接口和 `std::allocator` 一样（`allocate` / `deallocate`），但背后的内存来自共享内存段管理器。

### 4.2 为什么不能用 std::vector

`std::vector<int>` 内部大概长这样：

```cpp
template <typename T, typename Alloc = std::allocator<T>>
class vector {
    T* data_;       // 指向堆内存的指针
    size_t size_;
    size_t capacity_;
    Alloc alloc_;
};
```

`std::allocator` 调用的是 `new/delete`，分配的是**本进程的堆内存**。把 `std::vector` 放进共享内存，`data_` 指向的是进程 A 的堆地址——进程 B 看到这个指针是无效的（虚拟地址空间不同）。

解决方案：用 `ShmAllocator` 替代 `std::allocator`，让 `data_` 指向共享内存里的地址。因为共享内存被两个进程映射到各自的虚拟地址空间，**段管理器内部会做地址转换**，所以同一块内存在两个进程里可以被正确访问。

```cpp
// 正确做法：用 ShmAllocator
using ShmVector = boost::container::vector<int, ShmAllocator<int>>;
// ShmVector 可以安全地放在共享内存里

// 错误做法：std::vector 放共享内存 → 跨进程后指针失效
```

### 4.3 ShmString 同理

```cpp
using ShmString = boost::interprocess::basic_string<char, std::char_traits<char>, ShmAllocator<char>>;
```

`std::string` 有 SSO（小字符串优化），内部可能用栈缓冲区也可能用堆指针，放进共享内存同样不安全。`ShmString` 强制所有数据都在共享内存里分配。

---

## 5. Boost.MultiIndex 入门

### 5.1 问题：一个容器多个查询维度

假设你有一个订单表：

```cpp
struct Order {
    uint64_t order_id;
    char symbol[8];    // 股票代码
    double price;
    uint32_t timestamp;
};
```

你需要三种查询：
- 按 `order_id` 精确查找（唯一键）
- 按 `symbol` 范围查询（拿到某只股票的所有订单）
- 按 `timestamp` 范围查询（拿到某个时间段的订单）

传统方案：维护三个容器（三个 `map`），数据存三份，修改时要同步三个容器，容易出错。

`boost::multi_index_container` 的方案：**数据只存一份，建三棵索引树**。

### 5.2 核心概念

```cpp
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/member.hpp>

namespace mi = boost::multi_index;

using OrderBook = mi::multi_index_container<
    Order,                          // 存储的元素类型
    mi::indexed_by<
        mi::ordered_unique<mi::member<Order, uint64_t, &Order::order_id>>,   // 索引0: order_id, 唯一
        mi::ordered_non_unique<mi::member<Order, char[8], &Order::symbol>>,   // 索引1: symbol, 可重复
        mi::ordered_non_unique<mi::member<Order, uint32_t, &Order::timestamp>> // 索引2: timestamp, 可重复
    >
>;
```

**模板参数解读**：

- `mi::indexed_by<...>` — 索引列表的包装
- `mi::ordered_unique<...>` — 有序唯一索引（类似 `std::map`，键不可重复）
- `mi::ordered_non_unique<...>` — 有序非唯一索引（类似 `std::multimap`，键可重复）
- `mi::member<Order, uint64_t, &Order::order_id>` — 键提取器，告诉 MultiIndex "用 `Order::order_id` 字段作为这个索引的键"

### 5.3 内部数据结构

```
                   OrderBook (multi_index_container)
                   ┌─────────────────────────────────┐
                   │  数据节点池（每个节点存一份 Order）│
                   │  ┌─────┐ ┌─────┐ ┌─────┐       │
                   │  │ O1  │ │ O2  │ │ O3  │ ...   │
                   │  └──┬──┘ └──┬──┘ └──┬──┘       │
                   │     │       │       │            │
                   ├─────┼───────┼───────┼────────────┤
                   │     │       │       │            │
  索引0 (order_id) │    RB-tree 按 order_id 排序      │
                   │     1001 → 1002 → 1003           │
                   │                                   │
  索引1 (symbol)   │    RB-tree 按 symbol 排序         │
                   │     AAPL → AAPL → GOOG            │
                   │                                   │
  索引2 (timestamp)│    RB-tree 按 timestamp 排序      │
                   │     100 → 200 → 300               │
                   └─────────────────────────────────┘
```

**每个索引是一棵独立的红黑树**，树节点里存的是指向数据节点的指针（或偏移量），不是数据本身。所以数据只存一份，三棵树共享。

### 5.4 基本操作

```cpp
OrderBook book;

// 插入 —— 同时更新三棵树
book.insert(Order{1001, "AAPL", 150.0, 100});
book.insert(Order{1002, "AAPL", 151.0, 200});
book.insert(Order{1003, "GOOG", 2800.0, 300});

// 获取第 0 个索引（order_id 树）的引用
auto& idx0 = book.get<0>();
auto it = idx0.find(1002);    // O(log n) 查找
// *it 就是那条 Order

// 获取第 1 个索引（symbol 树）的引用
auto& idx1 = book.get<1>();
auto range = idx1.equal_range("AAPL");  // 返回一对迭代器
for (auto i = range.first; i != range.second; ++i) {
    // 遍历所有 symbol=="AAPL" 的订单
}

// 修改 —— 通过任意索引修改，所有索引自动更新
auto& idx0 = book.get<0>();
auto it = idx0.find(1001);
idx0.modify(it, [](Order& o) { o.price = 200.0; });

// 删除 —— 从所有索引中移除
idx0.erase(it);
```

### 5.5 ordered vs hashed

MultiIndex 还有 `hashed_unique` / `hashed_non_unique`（哈希索引，O(1) 查找）和 `sequenced`（链表，保持插入顺序）。本库只用了 `ordered`（红黑树），因为需要范围查询（`equal_range`、`lower_bound`）。

---

## 6. Store 核心

```cpp
// include/shmstore/store.hpp
```

### 6.1 类模板定义

```cpp
template <typename Record, typename... Indexes>
class Store {
    static_assert(std::is_standard_layout_v<Record>, "Record must be a standard-layout type for shared memory");
    // ...
};
```

- `Record` — 存储的数据类型（如 `MsgRecord`）
- `Indexes...` — 可变参数，展开为 Boost.MultiIndex 的索引定义
- `static_assert` 要求 Record 是 standard-layout 类型（没有虚函数、没有指针成员），这样才能安全地放在共享内存里

### 6.2 container_type 的推导

```cpp
using container_type = boost::multi_index_container<
    Record,
    boost::multi_index::indexed_by<Indexes...>,   // 展开所有索引定义
    ShmAllocator<Record>                           // 用共享内存分配器
>;
```

当用户写：

```cpp
using MsgStore = Store<MsgRecord,
    mi::ordered_unique<SeqExtractor>,
    mi::ordered_non_unique<DeviceIdExtractor>,
    mi::ordered_non_unique<MsgTypeExtractor>
>;
```

编译器展开后 `container_type` 变成：

```cpp
boost::multi_index_container<
    MsgRecord,
    boost::multi_index::indexed_by<
        mi::ordered_unique<SeqExtractor>,
        mi::ordered_non_unique<DeviceIdExtractor>,
        mi::ordered_non_unique<MsgTypeExtractor>
    >,
    ShmAllocator<MsgRecord>
>;
```

**`ShmAllocator<MsgRecord>` 是关键**——MultiIndex 容器内部的红黑树节点全部从共享内存分配。

### 6.3 构造函数

```cpp
Store(boost::interprocess::managed_shared_memory* seg, const std::string& name)
    : seg_(seg), name_(name) {
    container_ = seg->find_or_construct<container_type>(name.c_str())(
        typename container_type::ctor_args_list(),
        seg->get_segment_manager()
    );
}
```

逐行拆解：

1. `seg->find_or_construct<container_type>("tx_msgs")` — 在共享内存段里找名为 "tx_msgs" 的 `container_type` 对象。找到了返回指针；找不到就在共享内存里构造一个。

2. `(typename container_type::ctor_args_list(), seg->get_segment_manager())` — 这是构造参数：
   - `ctor_args_list()` — 默认构造参数，MultiIndex 需要为空
   - `seg->get_segment_manager()` — **这是最重要的参数**：告诉 MultiIndex 容器"你的内存分配器后端是这个共享内存段管理器"

3. `container_` 是一个 `container_type*`，指向共享内存里的对象。两个进程各自的 `Store` 对象持有不同的 `container_` 指针，但它们**指向共享内存中的同一个对象**。

### 6.4 两个进程如何共享同一个 Store

```cpp
// 进程 A（sender）
auto *tx_seg = ShmManager::instance().create_segment("tx_pipe", 64MB);
MsgStore tx_store(tx_seg, "tx_msgs");  // 在共享内存中构造 "tx_msgs"

// 进程 B（receiver）
auto *tx_seg = ShmManager::instance().open_segment("tx_pipe");  // 打开同一个段
MsgStore tx_store(tx_seg, "tx_msgs");  // find_or_construct 找到已有的 "tx_msgs"
```

两个进程的 `tx_store.container_` 指针值不同（各自虚拟地址空间的映射地址不同），但指向**同一块物理内存**。Boost.Interprocess 的段管理器在内部处理了地址转换。

---

## 7. 自定义 Key Extractor

### 7.1 为什么需要自定义

Boost.MultiIndex 提供了内置的键提取器：

```cpp
// 提取顶层成员
mi::member<Order, uint64_t, &Order::order_id>

// 提取成员函数的返回值
mi::const_mem_fun<Order, uint32_t, &Order::device_id>
```

但 `MsgRecord` 的字段在嵌套结构里：

```cpp
struct MsgRecord {
    PackHead head;           // seq 在这里: head.seq
    union Body {
        HeartbeatBody heartbeat;
        SensorReportBody sensor;
        ControlCmdBody cmd;
    } body;                  // device_id 可能在 body.sensor.sensor_id 等
    PackTail tail;
};
```

`mi::member` 只能取**直接成员**，不能取 `head.seq` 这种两级嵌套。所以需要自定义 extractor。

### 7.2 写法

```cpp
struct SeqExtractor {
    using result_type = uint32_t;  // 必须声明返回类型
    result_type operator()(const MsgRecord &m) const {
        return m.head.seq;         // 从嵌套结构中提取
    }
};
```

这就是一个**仿函数**（functor）——重载了 `operator()` 的结构体。Boost.MultiIndex 内部用它来提取键：

```cpp
// MultiIndex 内部大致这样用：
SeqExtractor extract;
uint32_t key = extract(record);  // 等价于 record.head.seq
// 然后用 key 做红黑树的比较和排序
```

### 7.3 device_id 的特殊处理

```cpp
struct DeviceIdExtractor {
    using result_type = uint32_t;
    result_type operator()(const MsgRecord &m) const {
        return m.device_id();  // 调用成员函数
    }
};
```

`device_id()` 是 `MsgRecord` 的成员函数，根据消息类型从不同字段提取：

```cpp
uint32_t device_id() const {
    switch (static_cast<MsgType>(head.msg_type)) {
    case MsgType::Heartbeat:   return head.src_id;
    case MsgType::SensorReport: return body.sensor.sensor_id;
    case MsgType::ControlCmd:  return body.cmd.target_id;
    default: return 0;
    }
}
```

这意味着同一个索引里，不同类型的消息用不同字段作为键，但对使用者来说是透明的。

---

## 8. 数据操作

### 8.1 insert

```cpp
std::pair<iterator, bool> insert(const Record& rec) {
    auto [it, ok] = container_->insert(rec);      // 1. MultiIndex 插入
    if (ok && change_cb_) notify(Op::Insert, *it); // 2. 触发变更通知
    return {it, ok};                               // 3. 返回迭代器和是否成功
}
```

`container_->insert(rec)` 内部做的事：

```
1. 在共享内存中分配一个节点（存放 Record 的副本）
2. 在索引0（seq树）中插入 → O(log n)
3. 在索引1（device_id树）中插入 → O(log n)
4. 在索引2（msg_type树）中插入 → O(log n)
5. 如果任何一步失败（比如唯一索引冲突），回滚前面的插入
```

返回的 `iterator` 指向插入的元素，`bool` 表示是否成功（唯一索引冲突时为 false）。

### 8.2 modify

```cpp
template <size_t Idx, typename Modifier>
bool modify(iterator it, Modifier modifier) {
    auto& idx = container_->template get<Idx>();     // 获取第 Idx 个索引
    auto local_it = idx.iterator_to(*it);            // 通用迭代器 → 索引专用迭代器
    bool ok = idx.modify(local_it, modifier);        // 执行修改
    if (ok && change_cb_) notify(Op::Update, *local_it);
    return ok;
}
```

**为什么不直接改字段？** 因为如果改了索引键（比如把 `seq` 从 1 改成 99），红黑树的排序就乱了。`modify` 做的事：

```
1. 调用 modifier(record) 修改数据
2. 检查每个索引的键是否变了
3. 如果某个索引的键变了，先把节点从那棵树里摘出来，再重新插入
4. 所有索引保持一致
```

`iterator_to(*it)` 的作用：`it` 是通用迭代器（来自容器默认视图），`idx.iterator_to()` 将它转换为**特定索引的迭代器**，因为 `modify` 需要知道是在哪个索引上操作。

### 8.3 erase_at

```cpp
template <size_t Idx, typename Iter>
size_t erase_at(Iter it) {
    auto& idx = container_->template get<Idx>();
    if (it == idx.end()) return 0;
    if (change_cb_) notify(Op::Erase, *it);   // 先通知（记录还在，能读到 key）
    idx.erase(it);                              // 再删除（从所有索引中移除）
    return 1;
}
```

**注意顺序**：先 `notify` 再 `erase`。因为 `notify` 需要读记录的前 8 字节作为 key，删除后就读不到了。

### 8.4 for_each

```cpp
template <typename Func>
void for_each(Func&& func) const {
    for (const auto& rec : *container_) {
        func(rec);
    }
}
```

遍历的是容器的**默认视图**（索引0），按插入顺序或索引0的排序顺序。

---

## 9. 变更通知

```cpp
// include/shmstore/change_event.hpp
```

### 9.1 Op 枚举

```cpp
enum class Op : uint8_t {
    Insert = 0x01,
    Update = 0x02,
    Erase  = 0x03
};
```

### 9.2 ByteSpan — 轻量字节视图

```cpp
struct ByteSpan {
    const uint8_t* data = nullptr;
    size_t size = 0;
};
```

C++17 的 `std::span<const uint8_t>` 需要 `<span>` 头文件（C++20），这里自定义了一个最小版本，只用于通知包里携带"变了哪条记录"的标识。

### 9.3 ChangeEvent

```cpp
struct ChangeEvent {
    Op op;                // 什么操作
    std::string_view topic;  // 哪个表（"tx_msgs" / "rx_msgs"）
    ByteSpan key;         // 记录的前 8 字节作为标识
};
```

`topic` 用 `string_view` 而不是 `string`——指向 `Store::name_`，零拷贝。

### 9.4 Store::notify 内部

```cpp
void notify(Op op, const Record& rec) {
    constexpr size_t kKeyLen = sizeof(Record) < sizeof(uint64_t) ? sizeof(Record) : sizeof(uint64_t);
    ChangeEvent ev{op, name_, ByteSpan{reinterpret_cast<const uint8_t*>(&rec), kKeyLen}};
    change_cb_(ev);
}
```

取记录的前 `min(sizeof(Record), 8)` 字节作为 key。对于 `MsgRecord`（48 字节），取前 8 字节 = `PackHead::magic` + `version` + `msg_type` + `seq` 的一部分。这只是个粗略标识，不是精确主键——精确查询要靠共享内存里的索引。

---

## 10. ChangeNotifier

```cpp
// include/shmstore/change_notifier.hpp
```

### 10.1 构造函数

```cpp
explicit ChangeNotifier(uint16_t port = packet::kDefaultPort) : port_(port) {
    sock_ = ::socket(AF_INET, SOCK_DGRAM, 0);  // 创建 UDP socket
}
```

`SOCK_DGRAM` = UDP，无连接、不可靠、低开销。组播通知对可靠性要求不高（丢了可以再查共享内存），所以 UDP 合适。

### 10.2 publish

```cpp
void publish(std::string_view topic, const ChangeEvent& ev) {
    uint32_t seq = next_seq(topic);                     // 1. 递增序列号
    auto pkt = encode_packet(ev, seq);                  // 2. 编码成字节包
    if (pkt.empty()) return;

    std::string mcast_addr = packet::topic_to_mcast(topic);  // 3. topic → 组播地址

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    ::inet_pton(AF_INET, mcast_addr.c_str(), &addr.sin_addr);

    ::sendto(sock_, pkt.data(), pkt.size(), 0,         // 4. 发送 UDP 包
             reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
}
```

### 10.3 topic → 组播地址映射

```cpp
inline uint8_t topic_to_mcast_last(std::string_view topic) {
    uint32_t h = 5381;                    // DJB2 哈希初始值
    for (char c : topic) {
        h = ((h << 5) + h) + static_cast<uint8_t>(c);  // h * 33 + c
    }
    return static_cast<uint8_t>(h % 254 + 1);  // 映射到 1~254
}

inline std::string topic_to_mcast(std::string_view topic) {
    auto last = topic_to_mcast_last(topic);
    return "239.0.0." + std::to_string(last);   // 如 "239.0.0.42"
}
```

**为什么用 239.0.0.0/24？** 这是 RFC 2365 定义的**管理作用域组播地址范围**，只在本地网络内路由，不会跑到公网。不同 topic 映射到不同地址，互不干扰。

### 10.4 序列号管理

```cpp
uint32_t next_seq(std::string_view topic) {
    std::lock_guard<std::mutex> lock(mu_);
    return seq_map_[std::string(topic)]++;
}
```

每个 topic 独立维护一个递增序列号。`mutex` 保护并发访问（如果多个线程同时 insert 触发 publish）。

---

## 11. 网络包协议

### 11.1 包格式

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|       magic (0xEB01)          |    version    |      op       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         seq (小端)                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   topic_len   |                                               |
+-+-+-+-+-+-+-+-+          topic (变长)                          +
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   key_len     |                                               |
+-+-+-+-+-+-+-+-+            key (变长)                          +
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        crc32 (小端)                            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

总大小 ≤ 128 字节。

### 11.2 编码

```cpp
inline std::vector<uint8_t> encode_packet(const ChangeEvent& ev, uint32_t seq) {
    std::vector<uint8_t> buf;
    buf.reserve(packet::kMaxSize);

    // magic: 0xEB01
    buf.push_back(0xEB);
    buf.push_back(0x01);

    // version
    buf.push_back(packet::kVersion);

    // op
    buf.push_back(static_cast<uint8_t>(ev.op));

    // seq (小端写入: 低字节在前)
    uint8_t seq_bytes[4];
    detail::put_le32(seq_bytes, seq);
    buf.insert(buf.end(), seq_bytes, seq_bytes + 4);

    // topic (len + data)
    buf.push_back(static_cast<uint8_t>(ev.topic.size()));
    buf.insert(buf.end(), ev.topic.begin(), ev.topic.end());

    // key (len + data)
    buf.push_back(static_cast<uint8_t>(ev.key.size));
    buf.insert(buf.end(), ev.key.data, ev.key.data + ev.key.size);

    // crc32: 覆盖前面所有字节
    uint32_t crc = detail::crc32(buf.data(), buf.size());
    uint8_t crc_bytes[4];
    detail::put_le32(crc_bytes, crc);
    buf.insert(buf.end(), crc_bytes, crc_bytes + 4);

    return buf;
}
```

### 11.3 CRC32 算法

```cpp
inline uint32_t crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}
```

这是标准 CRC32（IEEE 802.3 多项式 `0xEDB88320`）的逐位实现。对每个字节的每一位做查表异或。效率不高但代码自包含，不依赖外部库。

**为什么用 CRC32？** UDP 不保证数据完整性（虽然以太网有自己的 FCS，但经过路由器/网桥时可能被篡改）。CRC32 让接收方能检测到损坏的包并丢弃。

### 11.4 解码

```cpp
inline DecodeResult decode_packet(const uint8_t* data, size_t len) {
    DecodeResult r;

    // 1. 长度校验
    if (len < packet::kMinSize || len > packet::kMaxSize) return r;

    // 2. magic 校验
    if (data[0] != 0xEB || data[1] != 0x01) return r;

    // 3. CRC32 校验（去掉最后 4 字节 crc，校验前面所有字节）
    uint32_t expected_crc = detail::get_le32(data + len - 4);
    uint32_t actual_crc = detail::crc32(data, len - 4);
    if (expected_crc != actual_crc) return r;

    // 4. version 校验
    if (data[2] != packet::kVersion) return r;

    // 5. 解析各字段
    r.op  = static_cast<Op>(data[3]);
    r.seq = detail::get_le32(data + 4);

    uint8_t topic_len = data[8];
    r.topic = std::string_view(reinterpret_cast<const char*>(data + 9), topic_len);

    size_t key_offset = 9 + topic_len;
    uint8_t key_len = data[key_offset];
    r.key = ByteSpan{data + key_offset + 1, key_len};

    r.ok = true;
    return r;
}
```

**小端（Little-Endian）**：低字节存低地址。`put_le32(buf, 0x12345678)` 写入 `78 56 34 12`。这样在任何架构的机器上编解码结果一致。

---

## 12. ChangeWatcher

```cpp
// include/shmstore/change_watcher.hpp
```

### 12.1 构造函数

```cpp
explicit ChangeWatcher(uint16_t port = packet::kDefaultPort) : port_(port) {
    sock_ = ::socket(AF_INET, SOCK_DGRAM, 0);

    int yes = 1;
    ::setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    timeval tv{1, 0};  // 1 秒接收超时
    ::setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);  // 绑定所有网卡
    ::bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
}
```

- `SO_REUSEADDR` — 允许多个进程绑定同一个端口（组播场景必须）
- `SO_RCVTIMEO` — `recvfrom` 最多阻塞 1 秒，避免 `stop()` 时线程卡死在 `recvfrom` 上
- `INADDR_ANY` — 接收所有网卡上的包

### 12.2 subscribe

```cpp
void subscribe(std::string_view topic, Callback cb) {
    std::string mcast = packet::topic_to_mcast(topic);  // 同样的哈希 → 同样的组播地址

    ip_mreq mreq{};
    ::inet_pton(AF_INET, mcast.c_str(), &mreq.imr_multiaddr);  // 组播地址
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);              // 通过哪个网卡加入
    ::setsockopt(sock_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

    subs_[t] = {std::move(cb), kInitialSeq};
}
```

**`IP_ADD_MEMBERSHIP`** 是关键操作——告诉内核"我要接收发往 239.0.0.x 的 UDP 组播包"。内核会向网络设备发 IGMP Join 消息，路由器/交换机知道了就把组播包转发过来。

多个进程可以同时加入同一个组播组，**所有加入者都能收到同一份数据**——这就是组播的语义。

### 12.3 on_readable — 收包主逻辑

```cpp
bool on_readable() {
    uint8_t buf[packet::kMaxSize];
    sockaddr_in src_addr{};
    socklen_t addr_len = sizeof(src_addr);

    // 1. 收包（阻塞，最多 1 秒超时）
    ssize_t n = ::recvfrom(sock_, buf, sizeof(buf), 0,
                           reinterpret_cast<sockaddr*>(&src_addr), &addr_len);
    if (n < 0) return false;  // 超时或错误，静默忽略

    // 2. 解码 + CRC 校验
    auto result = decode_packet(buf, static_cast<size_t>(n));
    if (!result.ok) return false;  // 校验失败，丢弃

    // 3. 查找对应的订阅
    std::lock_guard<std::mutex> lock(mu_);
    auto it = subs_.find(std::string(result.topic));
    if (it == subs_.end()) return true;  // 没人订阅这个 topic，忽略

    auto& entry = it->second;

    // 4. 序列号检查
    uint32_t expected_seq = entry.last_seq + 1;
    if (result.seq > expected_seq) {
        // seq 太大 → 中间丢了包
        if (gap_cb_) gap_cb_(result.topic, result.seq - expected_seq);
    } else if (result.seq < expected_seq) {
        return true;  // seq 太小 → 重复包，丢弃
    }

    // 5. 正常：更新序列号，触发回调
    entry.last_seq = result.seq;
    if (entry.cb) entry.cb(result);
    return true;
}
```

**序列号检查的精妙之处**：

`kInitialSeq = ~uint32_t(0) = 0xFFFFFFFF`。第一个包的 seq 是 0。

```
expected_seq = last_seq + 1 = 0xFFFFFFFF + 1 = 0  (uint32 溢出回绕)
result.seq == 0 == expected_seq  →  匹配！
```

所以首包不需要特殊处理，自然通过。

### 12.4 start / stop

```cpp
void start() {
    if (running_.exchange(true)) return;  // 已经在运行，不重复启动
    thread_ = std::thread([this]() {
        while (running_) {
            on_readable();  // 循环收包
        }
    });
}

void stop() {
    running_ = false;                    // 设置退出标志
    if (thread_.joinable()) {
        thread_.join();                  // 等待线程结束（最多 1 秒，因为 recvfrom 有超时）
    }
}
```

`running_` 是 `std::atomic<bool>`，保证线程间可见性。

### 12.5 fd() — 外部事件循环集成

```cpp
int fd() const { return sock_; }
```

如果不想用独立线程，可以拿到 socket fd，集成到 `epoll` / `select` / 自己的 Reactor 里。fd 可读时调 `on_readable()`。

---

## 13. 完整数据流

### 13.1 发送者写入一条消息的全链路

```
tx_store.insert(make_heartbeat(1, 1001, 3600, 45))
│
├─ [1] make_heartbeat 构造 MsgRecord（48 字节，栈上）
│       填充 PackHead（magic, version, msg_type, seq, src_id, ...）
│       填充 HeartbeatBody（uptime_sec, cpu_load）
│       填充 PackTail（tail_magic）
│
├─ [2] container_->insert(rec)
│       │
│       ├─ 在共享内存中分配节点（ShmAllocator → segment_manager）
│       ├─ 复制 MsgRecord 到节点
│       ├─ 在索引0（seq树）中插入节点 → O(log n)
│       ├─ 在索引1（device_id树）中插入节点 → O(log n)
│       └─ 在索引2（msg_type树）中插入节点 → O(log n)
│
├─ [3] notify(Op::Insert, record)
│       │
│       ├─ 取记录前 8 字节作为 key
│       └─ 调用 change_cb_(ChangeEvent{Insert, "tx_msgs", key})
│               │
│               └─ ChangeNotifier::publish("tx_msgs", ev)
│                       │
│                       ├─ next_seq("tx_msgs") → seq=0（首次）
│                       ├─ encode_packet(ev, 0) → [EB 01 01 01 00 00 00 00 06 tx_msgs 08 key_data ... crc32]
│                       ├─ topic_to_mcast("tx_msgs") → "239.0.0.42"
│                       └─ sendto(sock_, pkt, ..., 239.0.0.42:9000)
│                               │
│                               └─ 进入内核网络栈 → 组播到本地网络
│
└─ 返回 {iterator, true}
```

### 13.2 接收者收到通知的全链路

```
ChangeWatcher 后台线程
│
├─ recvfrom(sock_, buf, ...) ← 阻塞等待 UDP 包
│       │
│       └─ 内核把组播包投递到 socket 接收缓冲区
│
├─ decode_packet(buf, n)
│       ├─ 校验 magic (0xEB01)
│       ├─ 校验 CRC32
│       ├─ 校验 version
│       └─ 解析出 {op=Insert, seq=0, topic="tx_msgs", key=...}
│
├─ 序列号检查
│       ├─ expected_seq = 0xFFFFFFFF + 1 = 0（溢出回绕）
│       ├─ result.seq = 0 == expected_seq → 匹配
│       └─ 更新 last_seq = 0
│
└─ 触发回调
        │
        └─ msg_count.fetch_add(1); got_msg = true;
```

### 13.3 接收者读取数据

```
tx_store.for_each([](const MsgRecord& m) { print_msg(m); })
│
└─ 遍历 MultiIndex 容器的默认视图（索引0，按 seq 排序）
        │
        └─ 对每条记录调用 print_msg → 直接读共享内存中的字段
                │
                └─ 没有任何系统调用，纯内存访问
```

---

## 14. 性能瓶颈与优化方向

### 14.1 当前瓶颈

| 瓶颈 | 原因 | 量化 |
|---|---|---|
| RB-tree cache miss | 节点不连续，遍历时 CPU cache 命中率低 | 1M 条记录时 ~200-500ns/次查询 |
| UDP sendto 系统调用 | 每次 insert 都触发，内核态切换 | ~1-5μs/次 |
| encode_packet 堆分配 | 每次 `vector<uint8_t>` 重新分配 | ~100-200ns/次 |
| ChangeWatcher mutex | 回调在锁内执行，慢回调阻塞收包 | 取决于回调复杂度 |

### 14.2 优化方向

| 方向 | 做法 | 预期收益 |
|---|---|---|
| 替换 RB-tree | 平坦数组 + 哈希索引 | 查询 10x 提升 |
| 通知批量化 | 多次 insert 只发一次通知 | 系统调用开销大幅降低 |
| 换掉 UDP | 同机场景用 eventfd / futex | 通知延迟从 μs 降到 ns |
| 预分配通知缓冲区 | 复用 buffer，避免堆分配 | 减少 GC 压力 |
| 无锁化 | CAS 替代 mutex | 消除锁竞争 |

---

## 附录：Boost 关键类型速查

| Boost 类型 | 作用 | 对标 |
|---|---|---|
| `managed_shared_memory` | 共享内存段 + 内存管理器 | 无标准对标 |
| `allocator<T, SegmentManager>` | 从共享内存分配的分配器 | `std::allocator<T>` |
| `multi_index_container<T, indexed_by<...>>` | 多索引容器 | 无标准对标 |
| `ordered_unique<Extractor>` | 有序唯一索引 | `std::map` |
| `ordered_non_unique<Extractor>` | 有序非唯一索引 | `std::multimap` |
| `member<T, Type, &T::field>` | 字段键提取器 | — |
| `const_mem_fun<T, Ret, &T::method>` | 成员函数键提取器 | — |
| `container::get<N>()` | 获取第 N 个索引 | — |
| `index::find(key)` | 按键查找 | `std::map::find` |
| `index::equal_range(key)` | 按键范围查找 | `std::multimap::equal_range` |
| `index::modify(it, mod)` | 修改元素并更新所有索引 | — |
| `index::iterator_to(ref)` | 从元素引用获取迭代器 | — |
