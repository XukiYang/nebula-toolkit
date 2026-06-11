#include <fmt/format.h>

#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <iostream>
#include <string>

// ============================================================================
// Boost.MultiIndex 深入浅出示例
// ============================================================================
//
// 核心思想：一个容器，多套索引，数据只存一份。
//
// 索引类型一览：
//   ordered_unique        有序 + 唯一（红黑树，类似 std::map）
//   ordered_non_unique    有序 + 可重复（类似 std::multimap）
//   hashed_unique         哈希 + 唯一（类似 std::unordered_map）
//   hashed_non_unique     哈希 + 可重复
//   sequenced             双向链表（类似 std::list，保持插入顺序）
//   random_access         随机访问（类似 std::vector，支持下标）
//
// Key Extractor（从元素中提取索引键的方式）：
//   member<T, Type, &T::mem>  提取成员变量
//   identity<T>               元素本身作为键（元素类型就是键类型）
//   const_mem_fun<T,Ret,&T::f>  提取 const 成员函数的返回值
//   global_fun<T,Ret,&f>       提取全局函数的返回值
//   composite_key<T, ...>      组合键（多字段联合索引）
//
// ============================================================================

// === 示例 1：基础用法 —— 有序索引 + Tag 访问 ================================

struct Employee {
    int id;
    std::string name;
    int age;
    std::string department;

    Employee(int i, std::string n, int a, std::string d)
        : id(i), name(std::move(n)), age(a), department(std::move(d)) {}
};

// Tag 类型：空结构体，仅作为编译期的"名字标签"，零开销。
// 用途：通过 get<Tag>() 代替 get<0>() 访问索引，可读性更好、不怕调整顺序。
struct ById {};
struct ByName {};
struct ByAge {};
struct ByDepartment {};

using namespace boost::multi_index;

// 定义容器：数据只存一份 Employee，但挂了两套索引
using EmployeeSet =
    multi_index_container<Employee,
                          indexed_by<
                              // 索引 0：按 id 有序且唯一（主键）
                              ordered_unique<tag<ById>, member<Employee, int, &Employee::id>>,
                              // 索引 1：按 name 有序可重复（可能有重名）
                              ordered_non_unique<tag<ByName>, member<Employee, std::string, &Employee::name>>>>;

// === 示例 2：五种索引类型全覆盖 =================================================

// 一个简单的商品结构，演示全部五种索引
struct Product {
    int sku;               // 库存编号，唯一
    std::string category;  // 分类，可重复
    std::string name;
    double price;

    Product(int s, std::string c, std::string n, double p)
        : sku(s), category(std::move(c)), name(std::move(n)), price(p) {}
};

struct BySku {};
struct ByCategory {};
struct ByPrice {};

using ProductCatalog =
    multi_index_container<Product,
                          indexed_by<
                              // 1) ordered_unique —— 有序 + 唯一，按 sku 排序，类似 std::map<int, Product>
                              ordered_unique<tag<BySku>, member<Product, int, &Product::sku>>,
                              // 2) ordered_non_unique —— 有序 + 可重复，同一分类的商品聚在一起
                              ordered_non_unique<tag<ByCategory>, member<Product, std::string, &Product::category>>,
                              // 3) sequenced —— 保持插入顺序，类似 std::list，支持 push_back/push_front
                              sequenced<>,
                              // 4) random_access —— 支持下标访问 product[0]，类似 std::vector
                              random_access<>>>;

// === 示例 3：组合键（composite_key）===========================================

struct Score {
    std::string student;
    std::string subject;
    int score;

    Score(std::string s, std::string sub, int sc) : student(std::move(s)), subject(std::move(sub)), score(sc) {}
};

struct ByStudentSubject {};
struct BySubjectScore {};

using ScoreBoard = multi_index_container<
    Score,
    indexed_by<
        // 组合键：先按 student 排序，student 相同时按 subject 排序
        // 效果：同一个人的所有成绩紧挨着，同一人内部按科目排序
        ordered_unique<tag<ByStudentSubject>,
                       composite_key<Score,
                                     member<Score, std::string, &Score::student>,
                                     member<Score, std::string, &Score::subject>>>,
        // 组合键 + 排序方向：按 subject 正序，score 降序（highest_first）
        ordered_non_unique<
            tag<BySubjectScore>,
            composite_key<Score, member<Score, std::string, &Score::subject>, member<Score, int, &Score::score>>,
            composite_key_compare<std::less<std::string>, std::greater<int>>>>>;

// === 示例 4：const_mem_fun —— 用成员函数返回值作为键 ==========================

struct User {
    int id;
    std::string first_name;
    std::string last_name;

    User(int i, std::string f, std::string l) : id(i), first_name(std::move(f)), last_name(std::move(l)) {}

    // 完整姓名，作为索引键提取
    std::string full_name() const { return first_name + " " + last_name; }
};

struct ByFullName {};

using UserRegistry = multi_index_container<
    User,
    indexed_by<ordered_unique<member<User, int, &User::id>>,
               // const_mem_fun：调用 User::full_name() 的返回值作为索引键
               ordered_unique<tag<ByFullName>, const_mem_fun<User, std::string, &User::full_name>>>>;

// === 示例 5：identity —— 元素本身作为键 =======================================

// 当元素类型本身就是键类型时，直接用 identity
// 比如存储一组去重的字符串，同时需要有序和快速查找
using StringSet = multi_index_container<std::string,
                                        indexed_by<ordered_unique<identity<std::string>>,   // 按字典序排列
                                                   hashed_unique<identity<std::string>>>>;  // O(1) 查找

// === 辅助：打印分隔线 ==========================================================

void print_section(const char *title) {
    fmt::println("\n{:=<60}", "");
    fmt::println("  {}", title);
    fmt::println("{:=<60}", "");
}

// === main：依次演示 ===========================================================

int main() {
    // ---------------------------------------------------------------
    // 示例 1：基础 —— ordered_unique + ordered_non_unique + Tag 访问
    // ---------------------------------------------------------------
    print_section("示例 1: 基础索引 + Tag 访问");

    EmployeeSet employees;
    employees.insert(Employee(3, "Charlie", 35, "Engineering"));
    employees.insert(Employee(1, "Alice", 30, "Engineering"));
    employees.insert(Employee(2, "Bob", 25, "Marketing"));
    employees.insert(Employee(4, "Diana", 30, "Marketing"));
    employees.insert(Employee(5, "Eve", 28, "Engineering"));

    // 通过 Tag 访问有序索引（推荐写法）
    fmt::println("--- 按 ID 有序遍历（ordered_unique<ById>）---");
    const auto &id_index = employees.get<ById>();
    for (const auto &e : id_index) {
        fmt::println("  id={:<3} name={:<10} age={:<3} dept={}", e.id, e.name, e.age, e.department);
    }

    // 通过数字索引访问（效果一样，但维护性差）
    fmt::println("\n--- 按 Name 有序遍历（get<1>() 等价写法）---");
    const auto &name_index = employees.get<ByName>();
    for (const auto &e : name_index) { fmt::println("  name={:<10} id={}", e.name, e.id); }

    // 查找 + 范围查询
    fmt::println("\n--- 范围查询：age >= 28 且 age <= 30 的员工 ---");
    // 在 ByName 索引上无法按 age 查，需要另一个索引——见示例 2

    // ---------------------------------------------------------------
    // 示例 2：五种索引类型
    // ---------------------------------------------------------------
    print_section("示例 2: 五种索引类型");

    ProductCatalog catalog;
    // 注意：当同时有 sequenced 和 random_access 索引时，直接 push_back 会歧义，
    // 所以统一用 insert() 插入，所有索引自动维护。
    catalog.insert(Product(1003, "Electronics", "Keyboard", 79.9));
    catalog.insert(Product(1001, "Electronics", "Mouse", 29.9));
    catalog.insert(Product(2002, "Books", "C++ Primer", 59.9));
    catalog.insert(Product(1002, "Electronics", "Monitor", 299.9));
    catalog.insert(Product(2001, "Books", "TCP/IP Guide", 49.9));
    catalog.insert(Product(3001, "Food", "Coffee", 12.9));

    // ordered_unique<BySku> —— 按 SKU 有序唯一
    fmt::println("--- 按 SKU 排序（ordered_unique）---");
    const auto &sku_idx = catalog.get<BySku>();
    for (const auto &p : sku_idx) { fmt::println("  SKU={:<5} {:<15} ${:.1f}", p.sku, p.name, p.price); }

    // ordered_non_unique<ByCategory> —— 同分类聚在一起
    fmt::println("\n--- 按分类分组（ordered_non_unique）---");
    const auto &cat_idx = catalog.get<ByCategory>();
    // equal_range 返回某分类下的所有商品
    auto range = cat_idx.equal_range("Electronics");
    fmt::println("  Electronics:");
    for (auto it = range.first; it != range.second; ++it) { fmt::println("    {} ${:.1f}", it->name, it->price); }

    // sequenced —— 按插入顺序遍历（第 2 个索引）
    fmt::println("\n--- 插入顺序（sequenced, get<2>()）---");
    const auto &seq_idx = catalog.get<2>();  // sequenced 索引没有 tag，用数字访问
    for (const auto &p : seq_idx) { fmt::println("  {:<15}", p.name); }

    // random_access —— 支持下标访问（第 3 个索引）
    fmt::println("\n--- 随机访问（random_access, get<3>()）---");
    const auto &ra_idx = catalog.get<3>();
    fmt::println("  第 0 个商品: {}", ra_idx[0].name);
    fmt::println("  第 2 个商品: {}", ra_idx[2].name);
    fmt::println("  最后一个:    {}", ra_idx[ra_idx.size() - 1].name);

    // ---------------------------------------------------------------
    // 示例 3：组合键 + 自定义排序
    // ---------------------------------------------------------------
    print_section("示例 3: 组合键 (composite_key)");

    ScoreBoard scores;
    scores.insert(Score("Alice", "Math", 95));
    scores.insert(Score("Alice", "English", 88));
    scores.insert(Score("Bob", "Math", 72));
    scores.insert(Score("Alice", "Math", 95));  // 重复，插入失败（unique）
    scores.insert(Score("Bob", "English", 90));
    scores.insert(Score("Charlie", "Math", 85));
    scores.insert(Score("Charlie", "English", 92));

    // 按 (student, subject) 遍历：同一个人的成绩紧挨着
    fmt::println("--- 按 (student, subject) 排序 ---");
    const auto &ss_idx = scores.get<ByStudentSubject>();
    for (const auto &s : ss_idx) { fmt::println("  {:<10} {:<10} {}", s.student, s.subject, s.score); }

    // 按 (subject, score 降序) 遍历：每科最高分排最前
    fmt::println("\n--- 按 (subject, score DESC) 排序 ---");
    const auto &as_idx = scores.get<BySubjectScore>();
    for (const auto &s : as_idx) { fmt::println("  {:<10} {:<10} {}", s.subject, s.student, s.score); }

    // 组合键的部分匹配：只指定 student 前缀，查找该学生所有成绩
    fmt::println("\n--- 部分键查找：Alice 的所有成绩 ---");
    auto alice_range = ss_idx.equal_range(std::make_tuple(std::string("Alice")));
    for (auto it = alice_range.first; it != alice_range.second; ++it) {
        fmt::println("  {} -> {}", it->subject, it->score);
    }

    // ---------------------------------------------------------------
    // 示例 4：const_mem_fun —— 成员函数作键提取器
    // ---------------------------------------------------------------
    print_section("示例 4: const_mem_fun 成员函数作键");

    UserRegistry users;
    users.insert(User(1, "Alice", "Smith"));
    users.insert(User(2, "Bob", "Jones"));
    users.insert(User(3, "Charlie", "Brown"));

    fmt::println("--- 按 full_name() 排序 ---");
    const auto &name_idx = users.get<ByFullName>();
    for (const auto &u : name_idx) { fmt::println("  id={:<3} name={}", u.id, u.full_name()); }

    // 用 full_name() 查找
    auto it = name_idx.find("Bob Jones");
    if (it != name_idx.end()) { fmt::println("\n  查找 'Bob Jones': id={}", it->id); }

    // ---------------------------------------------------------------
    // 示例 5：identity —— 元素本身作为键
    // ---------------------------------------------------------------
    print_section("示例 5: identity 元素本身作键");

    StringSet words;
    words.insert("banana");
    words.insert("apple");
    words.insert("cherry");
    words.insert("apple");  // 重复，不会插入

    // ordered 索引：字典序
    fmt::println("--- 有序遍历 ---");
    const auto &ordered = words.get<0>();
    for (const auto &w : ordered) { fmt::println("  {}", w); }

    // hashed 索引：O(1) 查找
    fmt::println("\n--- 哈希查找 ---");
    const auto &hashed = words.get<1>();
    fmt::println("  'apple' 存在? {}", hashed.find("apple") != hashed.end());
    fmt::println("  'grape' 存在? {}", hashed.find("grape") != hashed.end());

    // ---------------------------------------------------------------
    // 示例 6：修改元素（replace / modify）
    // ---------------------------------------------------------------
    print_section("示例 6: 修改元素 (replace & modify)");

    // replace：用新元素替换旧元素（要求索引键可比较）
    fmt::println("--- replace: 修改 Bob 的部门 ---");
    EmployeeSet emps;
    emps.insert(Employee(1, "Alice", 30, "Engineering"));
    emps.insert(Employee(2, "Bob", 25, "Marketing"));

    // 找到 Bob，修改后 replace 回去
    auto &id_idx = emps.get<ById>();
    auto bob_it = id_idx.find(2);
    if (bob_it != id_idx.end()) {
        Employee updated = *bob_it;     // 拷贝一份
        updated.department = "Sales";   // 修改
        emps.replace(bob_it, updated);  // 替换，所有索引自动更新
    }

    fmt::println("  替换后按 ID 遍历:");
    for (const auto &e : emps.get<ById>()) {
        fmt::println("    id={:<3} name={:<10} dept={}", e.id, e.name, e.department);
    }

    // modify：就地修改，用 lambda 操作元素
    fmt::println("\n--- modify: 给所有人加薪（age+1 模拟）---");
    emps.get<ById>().modify(emps.get<ById>().find(1), [](Employee &e) { e.age += 1; });
    emps.get<ById>().modify(emps.get<ById>().find(2), [](Employee &e) { e.age += 1; });

    for (const auto &e : emps.get<ById>()) { fmt::println("    id={:<3} name={:<10} age={}", e.id, e.name, e.age); }

    // ---------------------------------------------------------------
    // 示例 7：project —— 跨索引迭代器转换
    // ---------------------------------------------------------------
    print_section("示例 7: project 跨索引迭代器转换");

    EmployeeSet team;
    team.insert(Employee(3, "Charlie", 35, "Engineering"));
    team.insert(Employee(1, "Alice", 30, "Marketing"));
    team.insert(Employee(2, "Bob", 28, "Engineering"));

    // 场景：在 name 索引中找到了 Alice，想拿到 id 索引中的迭代器
    const auto &by_name = team.get<ByName>();
    auto name_it = by_name.find("Alice");
    if (name_it != by_name.end()) {
        // project<N> 或 project<Tag>：把一个索引的迭代器转换为另一个索引的迭代器
        auto id_it = team.project<ById>(name_it);
        fmt::println("  在 name 索引找到 'Alice'，project 到 id 索引: id={}", id_it->id);
    }

    fmt::println("\n{:=<60}", "");
    fmt::println("  全部示例运行完毕");
    fmt::println("{:=<60}", "");

    return 0;
}
