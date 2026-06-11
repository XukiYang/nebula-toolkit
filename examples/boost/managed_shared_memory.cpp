#include <fmt/format.h>

#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <string>

// ============================================================================
// Boost.Interprocess managed_shared_memory 深入浅出示例
// ============================================================================
//
// 核心思想：在进程间共享一块内存区域，让多个进程能直接读写同一份数据，
//          避免序列化/反序列化开销，实现零拷贝通信。
//
// managed_shared_memory 的关键能力：
//   1. 在共享内存中 构造/销毁 C++ 对象（placement new）
//   2. 在共享内存中 使用 STL 风格容器（vector, map, string ...）
//   3. 跨进程 的 命名对象 查找（通过名字获取共享对象）
//   4. 内置 分配器，管理共享内存的分配与回收
//
// 重要概念：
//   - 共享内存的生命周期独立于进程（除非显式销毁或系统重启）
//   - 共享内存中的指针必须用 offset_ptr，不能用原始指针（不同进程映射地址不同）
//   - Boost 容器（interprocess::vector 等）内部已使用 offset_ptr，可直接使用
//
// ============================================================================

namespace bip = boost::interprocess;

// === 辅助：打印分隔线 ==========================================================

void print_section(const char* title) {
    fmt::println("\n{:=<60}", "");
    fmt::println("  {}", title);
    fmt::println("{:=<60}", "");
}

// 每次运行前清理上次残留的共享内存（避免 name 冲突）
void cleanup(const char* name) {
    bip::shared_memory_object::remove(name);
    bip::named_mutex::remove("shm_demo_mutex");
}

// === 示例 1：基本读写 —— 在共享内存中存取原始数据 ==============================

void example_basic() {
    print_section("示例 1: 基本读写 —— 原始数据");

    const char* shm_name = "ShmBasic";
    cleanup(shm_name);

    // 创建一块 1024 字节的共享内存
    bip::managed_shared_memory segment(bip::create_only, shm_name, 1024);

    // 在共享内存中分配一个 int，初始值 42
    auto* p_val = segment.construct<int>("MyInteger")(42);
    fmt::println("  构造: *MyInteger = {}", *p_val);

    // 在共享内存中分配一个 int 数组，长度 5
    int init[] = {10, 20, 30, 40, 50};
    auto* p_arr = segment.construct<int>("MyArray")(5);  // 先分配 5 个
    for (int i = 0; i < 5; ++i) p_arr[i] = init[i];

    fmt::println("  构造: MyArray = [{}]",
                 fmt::join(p_arr, p_arr + 5, ", "));

    // 通过名字查找已存在的对象
    auto [found, count] = segment.find<int>("MyInteger");
    if (found) {
        fmt::println("  查找: MyInteger 存在, count={}, value={}", count, *found);
    }

    // 销毁命名对象
    segment.destroy<int>("MyInteger");
    auto [found2, count2] = segment.find<int>("MyInteger");
    fmt::println("  销毁后查找: MyInteger 存在? {}", found2 != nullptr);

    // 注意：共享内存段析构时会自动回收所有未销毁的对象
}

// === 示例 2：STL 风格容器 —— 在共享内存中使用 vector / map / string ==============

void example_containers() {
    print_section("示例 2: STL 风格容器（vector / map / string）");

    const char* shm_name = "ShmContainers";
    cleanup(shm_name);

    // 分配 64KB 共享内存
    bip::managed_shared_memory segment(bip::create_only, shm_name, 65536);

    // ---- vector：在共享内存中构造动态数组 ----
    // 注意：必须用 bip::allocator，不能用 std::allocator
    using ShmVector = bip::vector<int, bip::allocator<int, bip::managed_shared_memory::segment_manager>>;
    auto* vec = segment.construct<ShmVector>("IntVec")(segment.get_segment_manager());
    vec->push_back(1);
    vec->push_back(2);
    vec->push_back(3);

    fmt::println("  vector: [{}]", fmt::join(*vec, ", "));

    // ---- string：共享内存安全的字符串 ----
    // 注意：bip::basic_string 的构造需要通过 construct 传入 allocator
    using ShmString =
        bip::basic_string<char, std::char_traits<char>,
                          bip::allocator<char, bip::managed_shared_memory::segment_manager>>;
    // construct 的参数列表对应 basic_string(size_type, CharT, allocator) 构造函数
    auto* s = segment.construct<ShmString>("MyStr")(
        26u, 'X', bip::allocator<char, bip::managed_shared_memory::segment_manager>(segment.get_segment_manager()));
    // 用 assign 从 C 字符串赋值
    s->assign("Hello from shared memory!");
    fmt::println("  string: \"{}\"", s->c_str());

    // ---- map：共享内存中的有序映射 ----
    using ShmMap = bip::map<int, int, std::less<int>,
                            bip::allocator<std::pair<const int, int>,
                                           bip::managed_shared_memory::segment_manager>>;
    auto* m = segment.construct<ShmMap>("IntMap")(segment.get_segment_manager());
    (*m)[1] = 100;
    (*m)[2] = 200;
    (*m)[3] = 300;

    fmt::println("  map:");
    for (const auto& [k, v] : *m) {
        fmt::println("    {} -> {}", k, v);
    }
}

// === 示例 3：共享内存中的自定义对象 ============================================

// 要放入共享内存的自定义结构体
// 注意：不能包含指针、std::string、std::vector 等！
// 因为不同进程映射地址不同，原始指针会失效。
// 必须全部使用固定大小的类型，或 Boost.Interprocess 的容器/offset_ptr。
struct ShmRecord {
    int id;
    char name[32];   // 固定大小的字符数组，不用 std::string
    double score;

    ShmRecord() : id(0), name{}, score(0.0) {}
    ShmRecord(int i, const char* n, double s) : id(i), score(s) {
        std::strncpy(name, n, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }
};

void example_custom_object() {
    print_section("示例 3: 自定义对象");

    const char* shm_name = "ShmCustom";
    cleanup(shm_name);

    bip::managed_shared_memory segment(bip::create_only, shm_name, 4096);

    // 构造单个对象
    auto* rec = segment.construct<ShmRecord>("Record")(1, "Alice", 95.5);
    fmt::println("  Record: id={}, name=\"{}\", score={:.1f}", rec->id, rec->name, rec->score);

    // 构造对象数组（先分配 3 个默认构造的元素，再逐个赋值）
    auto* arr_ptr = segment.construct<ShmRecord>("RecordArray")[3]();
    arr_ptr[0] = ShmRecord(10, "Math", 88.0);
    arr_ptr[1] = ShmRecord(20, "English", 92.0);
    arr_ptr[2] = ShmRecord(30, "Physics", 76.5);
    auto [arr, count] = segment.find<ShmRecord>("RecordArray");
    fmt::println("  RecordArray ({} 个):", count);
    for (size_t i = 0; i < count; ++i) {
        fmt::println("    [{}] id={}, name=\"{}\", score={:.1f}", i, arr[i].id, arr[i].name, arr[i].score);
    }
}

// === 示例 4：多进程通信 —— 父子进程读写共享内存 ================================

// 跨进程共享的消息结构（固定大小，无指针）
struct ShmMessage {
    char text[64];
    int value;

    ShmMessage() : text{}, value(0) {}
};

void example_multiprocess() {
    print_section("示例 4: 多进程通信（fork 父子进程）");

    const char* shm_name = "ShmMultiProc";
    cleanup(shm_name);

    // 父进程创建共享内存
    bip::managed_shared_memory segment(bip::create_only, shm_name, 8192);

    // 在共享内存中写入一条消息
    auto* msg = segment.construct<ShmMessage>("Message")();
    std::strncpy(msg->text, "Parent says hello!", 63);
    msg->value = 42;

    // 同时写入一个计数器
    auto* counter = segment.construct<int>("Counter")(0);

    fmt::println("  [父进程] 写入: text=\"{}\", value={}, Counter={}", msg->text, msg->value, *counter);

    pid_t pid = fork();

    if (pid == 0) {
        // ---- 子进程 ----
        // attach 到已有的共享内存（名字相同即可打开同一块）
        bip::managed_shared_memory child_seg(bip::open_only, shm_name);

        // 读取父进程写入的数据
        auto [child_msg, _] = child_seg.find<ShmMessage>("Message");
        if (child_msg) {
            fmt::println("  [子进程] 读取: text=\"{}\", value={}", child_msg->text, child_msg->value);
        }

        // 修改数据
        std::strncpy(child_msg->text, "Child modified it!", 63);
        child_msg->value = 100;
        auto [child_cnt, __] = child_seg.find<int>("Counter");
        *child_cnt = 999;

        fmt::println("  [子进程] 修改: text=\"{}\", value={}, Counter={}", child_msg->text, child_msg->value, *child_cnt);

        std::fflush(stdout);  // 刷新输出，否则 _exit 会丢弃缓冲区
        _exit(0);             // 子进程退出，不清理共享内存（用 _exit 避免触发父进程的 atexit）
    } else {
        // ---- 父进程：等待子进程结束 ----
        int status;
        waitpid(pid, &status, 0);

        // 读取子进程修改后的数据
        auto [parent_msg, _] = segment.find<ShmMessage>("Message");
        auto [parent_cnt, __] = segment.find<int>("Counter");
        fmt::println("  [父进程] 读取子进程修改: text=\"{}\", value={}, Counter={}",
                     parent_msg->text, parent_msg->value, *parent_cnt);
    }
}

// === 示例 5：互斥同步 —— 多进程安全读写 =======================================

void example_mutex() {
    print_section("示例 5: 命名互斥锁（named_mutex）— 多进程同步");

    const char* shm_name = "ShmMutex";
    const char* mutex_name = "shm_demo_mutex";
    cleanup(shm_name);

    bip::managed_shared_memory segment(bip::create_only, shm_name, 4096);
    auto* counter = segment.construct<int>("SharedCounter")(0);
    fmt::println("  初始计数器: {}", *counter);

    // 创建命名互斥锁（独立于共享内存，系统级存在）
    bip::named_mutex mutex(bip::create_only, mutex_name);

    const int ITERATIONS = 10000;

    pid_t pid = fork();

    if (pid == 0) {
        // ---- 子进程：+1 操作 ----
        bip::managed_shared_memory child_seg(bip::open_only, shm_name);
        bip::named_mutex child_mutex(bip::open_only, mutex_name);
        auto [cnt, _] = child_seg.find<int>("SharedCounter");

        for (int i = 0; i < ITERATIONS; ++i) {
            bip::scoped_lock<bip::named_mutex> lock(child_mutex);
            (*cnt)++;
        }
        _exit(0);
    } else {
        // ---- 父进程：+1 操作 ----
        for (int i = 0; i < ITERATIONS; ++i) {
            bip::scoped_lock<bip::named_mutex> lock(mutex);
            (*counter)++;
        }

        int status;
        waitpid(pid, &status, 0);

        fmt::println("  两个进程各 +1 {} 次", ITERATIONS);
        fmt::println("  最终计数器: {} (应为 {})", *counter, ITERATIONS * 2);
        fmt::println("  结果: {}", *counter == ITERATIONS * 2 ? "✓ 正确" : "✗ 错误!");
    }

    // 清理
    bip::named_mutex::remove(mutex_name);
}

// === 示例 6：共享内存中的内存统计 + 动态扩展 ==================================

void example_nested() {
    print_section("示例 6: 内存统计 + 动态分配");

    const char* shm_name = "ShmNested";
    cleanup(shm_name);

    // 初始只分配 4KB
    bip::managed_shared_memory segment(bip::create_only, shm_name, 4096);
    auto* mgr = segment.get_segment_manager();

    fmt::println("  初始状态:");
    fmt::println("    总大小:  {} bytes", segment.get_size());
    fmt::println("    空闲:    {} bytes", segment.get_free_memory());

    // 用 vector 在共享内存中存一批数据
    using ShmVector = bip::vector<int, bip::allocator<int, bip::managed_shared_memory::segment_manager>>;
    auto* vec = segment.construct<ShmVector>("BigVec")(mgr);
    for (int i = 0; i < 100; ++i) {
        vec->push_back(i * i);
    }

    fmt::println("\n  插入 100 个 int 后:");
    fmt::println("    空闲:    {} bytes", segment.get_free_memory());
    fmt::println("    前 10 个: [{}, {}, {}, {}, {}, {}, {}, {}, {}, {}]",
                 (*vec)[0], (*vec)[1], (*vec)[2], (*vec)[3], (*vec)[4],
                 (*vec)[5], (*vec)[6], (*vec)[7], (*vec)[8], (*vec)[9]);

    // 构造更多对象，展示内存消耗
    using ShmMap = bip::map<int, int, std::less<int>,
                            bip::allocator<std::pair<const int, int>,
                                           bip::managed_shared_memory::segment_manager>>;
    auto* m = segment.construct<ShmMap>("IntMap")(mgr);
    for (int i = 0; i < 50; ++i) {
        (*m)[i] = i * 10;
    }

    fmt::println("\n  再插入 50 个 map 条目后:");
    fmt::println("    空闲:    {} bytes", segment.get_free_memory());
    fmt::println("    map 大小: {} 条", m->size());

    // 销毁对象，回收内存
    segment.destroy<ShmVector>("BigVec");
    fmt::println("\n  销毁 vector 后:");
    fmt::println("    空闲:    {} bytes", segment.get_free_memory());
}

// === main =====================================================================

int main() {
    fmt::println("Boost.Interprocess managed_shared_memory 示例合集");
    fmt::println("进程 PID: {}", getpid());

    example_basic();
    example_containers();
    example_custom_object();
    example_multiprocess();
    example_mutex();
    example_nested();

    // 最终清理所有示例的共享内存
    print_section("清理");
    const char* names[] = {"ShmBasic", "ShmContainers", "ShmCustom", "ShmMultiProc", "ShmMutex", "ShmNested"};
    for (const auto& name : names) {
        bool removed = bip::shared_memory_object::remove(name);
        fmt::println("  {} removed={}", name, removed);
    }

    return 0;
}
