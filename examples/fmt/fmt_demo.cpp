// =============================================================================
// fmt_demo.cpp -- fmt 格式化库教学示例
// =============================================================================
//
// 核心思想:
//   fmt 是 Python str.format() 的 C++ 实现，使用 {} 占位符语法。
//   相比 printf，fmt 在编译期检查格式字符串，避免运行时未定义行为；
//   相比 iostream，fmt 性能快 5-10 倍，且语法更直观。
//
// fmt 的关键特性:
//   1. 类型安全 -- 编译期检查参数类型与格式说明符是否匹配
//   2. 高性能 -- 内部使用编译期格式解析 + 高效缓冲区管理
//   3. 可扩展 -- 通过特化 fmt::formatter<T> 支持自定义类型
//   4. 兼容性 -- 支持 C++11/14/17/20，跨平台
//
// =============================================================================

#include <fmt/color.h>
#include <fmt/compile.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <string>
#include <vector>

// 自定义类型示例
struct Point {
    double x;
    double y;
};

// 特化 fmt::formatter 以支持自定义类型的格式化
// fmt 的扩展机制: 只需特化 fmt::formatter<T>，实现 parse() 和 format() 即可
template <>
struct fmt::formatter<Point> : formatter<double> {
    // parse() 解析格式说明符，这里直接透传给 double 的 formatter
    auto parse(format_parse_context& ctx) { return formatter<double>::parse(ctx); }

    // format() 定义输出格式
    auto format(const Point& p, format_context& ctx) const {
        return format_to(ctx.out(), "({}, {})", p.x, p.y);
    }
};

int main() {
    fmt::println("========================================");
    fmt::println("  fmt 格式化库教学示例");
    fmt::println("========================================\n");

    // 1. 基本格式化
    //    {} 是占位符，按顺序匹配参数
    //    fmt::format() 返回 std::string，fmt::print() 直接输出到 stdout
    fmt::println("--- 1. 基本格式化 ---");
    std::string name = "nebula";
    int version = 2;
    fmt::println("  Hello, {}!", name);
    fmt::println("  {} toolkit v{}", name, version);
    fmt::println("  格式化结果: {}", fmt::format("{}-{}", name, version));

    // 2. 数值格式化
    //    格式说明符语法: [[fill]align][sign][#][0][width][.precision][type]
    //    align: < 左对齐, > 右对齐, ^ 居中
    //    type: d 十进制, x 十六进制, o 八进制, b 二进制, f 定点, e 科学计数
    fmt::println("\n--- 2. 数值格式化 ---");
    int val = 255;
    fmt::println("  十进制:    {:>10}", val);       // 右对齐，宽度10
    fmt::println("  十六进制:  {:08x}", val);       // 前导零填充，宽度8
    fmt::println("  八进制:    {:o}", val);
    fmt::println("  二进制:    {:b}", val);
    fmt::println("  浮点精度:  {:.4f}", 3.14159265); // 4位小数
    fmt::println("  科学计数:  {:e}", 123456.789);
    fmt::println("  填充居中:  {:*^20}", "center");  // * 填充，居中，宽度20

    // 3. 容器格式化
    //    fmt::join() 可以直接格式化任意范围，指定分隔符
    //    需要 #include <fmt/ranges.h>
    fmt::println("\n--- 3. 容器格式化 ---");
    std::vector<int> nums = {1, 2, 3, 4, 5};
    fmt::println("  vector:    [{}]", fmt::join(nums, ", "));

    std::vector<std::string> words = {"hello", "world", "fmt"};
    fmt::println("  strings:   [{}]", fmt::join(words, " | "));

    // 4. 自定义类型格式化
    //    通过特化 fmt::formatter<Point>，Point 可以直接用于格式化
    fmt::println("\n--- 4. 自定义类型格式化 ---");
    Point p{3.14, 2.718};
    fmt::println("  Point:     {}", p);
    fmt::println("  带精度:    {:.1f}", p);  // 透传精度给内部 double

    // 5. 颜色输出
    //    fmt::fg() 设置前景色，fmt::bg() 设置背景色
    //    fmt::emphasis 设置样式（bold, italic, underline 等）
    //    需要 #include <fmt/color.h>
    fmt::println("\n--- 5. 颜色输出 ---");
    fmt::print(fg(fmt::color::green) | fmt::emphasis::bold, "  [OK] ");
    fmt::println("绿色加粗文本");
    fmt::print(fg(fmt::color::red), "  [ERROR] ");
    fmt::println("红色错误文本");
    fmt::print(fg(fmt::color::cyan), "  [INFO] ");
    fmt::println("青色信息文本");

    // 6. 打印函数
    //    fmt::print()    -- 输出到 stdout，不换行
    //    fmt::println()  -- 输出到 stdout，自动换行
    //    fmt::format_to() -- 写入迭代器（如 back_inserter）
    fmt::println("\n--- 6. 打印函数 ---");
    std::string buf;
    fmt::format_to(std::back_inserter(buf), "写入buffer: {}+{}={}", 3, 4, 3 + 4);
    fmt::println("  {}", buf);

    // 7. 编译期格式检查
    //    fmt::format_string<Args...> 在编译期验证格式字符串与参数类型的匹配
    //    如果格式说明符与参数类型不匹配，编译器会报错
    //    例如: fmt::format("{:d}", "string") 会导致编译错误
    fmt::println("\n--- 7. 编译期格式检查 ---");
    // 正确用法 -- 编译通过
    auto result = fmt::format(FMT_COMPILE("编译期检查: {}"), 42);
    fmt::println("  {}", result);
    fmt::println("  FMT_COMPILE 宏将格式字符串预编译，进一步提升性能");

    fmt::println("\n========================================");
    fmt::println("  fmt 示例结束");
    fmt::println("========================================");

    return 0;
}
