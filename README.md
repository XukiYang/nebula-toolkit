# Nebula-Toolkit

Nebula-Toolkit 是一个轻量级、高性能的 C++17 头文件工具库，提供了网络编程、数据处理、线程管理、共享内存等常用功能模块。

## 支持的 C++ 版本

- **要求**: C++17（使用了 `if constexpr`、折叠表达式、`std::byte` 等特性）

## 模块概览

### 数据容器库 (`nebula::containers`)

- **CircularBuffer** — 线程安全环形缓冲区，支持线性 IO、动态扩容、迭代器访问
- **UnPacker** — 基于 CircularBuffer 的数据包解包器，支持多种分包模式
- **BytesStream** — 字节流读写封装
- **LockFreeQueue** — 无锁队列

### 配置处理库 (`nebula::config`)

- **IniConfigHandler** — INI 配置文件读写，支持动态更新
- **IniReader** — 轻量级 INI 文件读取器
- **JsonReader** — JSON 配置读取（基于 nlohmann/json）

### 加密库 (`nebula::crypto`)

- **CRC32** — CRC32 校验计算

### 日志库 (`nebula::logger`)

- **Logger** — 异步日志实现，支持分级、文件输出、配置热更新
- **CrashCoreLogger** — 信号安全的日志器，适用于崩溃场景

### 内存库 (`nebula::memory`)

- **MemoryPool** — 线程安全内存池，减少频繁分配的开销

### IO 库 (`nebula::io`)

- **ReactorCore** — 基于 epoll 的事件反应器，支持 TCP/UDP/串口
- **TcpWriteManager** — TCP 写管理器
- **ProtocolHandler** — 协议处理器基类
- **SpHandler** — 串口事件驱动协议处理器
- **SocketCreator** — 套接字/串口设备创建工厂
- **SerialClient** — 阻塞式串口客户端

### 线程库 (`nebula::threading`)

- **TimerScheduler** — 基于优先级队列的定时调度器，毫秒级精度

### 共享内存库 (`nebula::shmstore`)

- **ShmManager** — 共享内存管理器
- **ShmAllocator** — 共享内存分配器
- **Store** — 共享内存存储
- **ChangeWatcher / ChangeNotifier** — 变更监听与通知

## 依赖项

- C++17 编译器
- [fmt](https://github.com/fmtlib/fmt) — 日志格式化（`lib/fmt` 子模块）
- [nlohmann/json](https://github.com/nlohmann/json) — JSON 处理（CMake FetchContent 自动拉取）
- Boost headers — 共享内存模块使用（interprocess / multi_index）
- pthreads

## 构建

```bash
# 默认构建（Debug，含测试和示例）
cmake --preset default
cmake --build build

# Release 构建（不含测试）
cmake --preset release
cmake --build build-release

# 快速迭代（不含测试）
cmake --preset notests
cmake --build build-notests

# 便捷脚本
./script/run.sh              # 构建并运行
./script/run.sh --build-only # 仅构建
./script/run.sh -c -v        # 清理构建 + 详细输出
./script/run.sh --gdb        # GDB 调试运行
```

### 运行测试

```bash
cmake --preset default && cmake --build build
./build/output/test_unit_io_reactor
```

测试是普通的 `main()` 可执行文件，无测试框架。添加新测试使用 `nebula_add_unit_test()`。

## 接入方式

### CMake 子项目（推荐）

```cmake
add_subdirectory(third_party/nebula-toolkit)
target_link_libraries(your_target PRIVATE nebula::all)
```

按模块链接：

```cmake
target_link_libraries(your_target PRIVATE nebula::containers nebula::threading)
```

### 直接 include

```cpp
#include "containers/circular_buffer.hpp"
#include "threading/timer_scheduler.hpp"
```

### find_package

```cmake
find_package(nebula-toolkit CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE nebula::all)
```

## CMake Targets

| Target | 模块 |
|---|---|
| `nebula::containers` | 数据容器 |
| `nebula::config` | 配置处理 |
| `nebula::json` | JSON 封装 |
| `nebula::memory` | 内存池 |
| `nebula::threading` | 线程/定时器 |
| `nebula::logger` | 日志 |
| `nebula::io` | 网络 IO |
| `nebula::shmstore` | 共享内存 |
| `nebula::all` | 全部模块 |

## 工程结构

```
include/          # 头文件（所有库代码）
├── containers/   # 数据容器
├── config_handler/ # 配置处理
├── crypto/       # 加密工具
├── io/           # 网络 IO
│   ├── core/     #   Reactor 核心
│   └── transport/#   协议传输层
├── logger/       # 日志
├── memory/       # 内存池
├── shmstore/     # 共享内存
└── threading/    # 线程/定时器
lib/              # 第三方依赖（git 子模块）
tests/            # 测试
examples/         # 示例
```

## 代码风格

Google C++ 规范，4 空格缩进，120 列限制。

```bash
python3 script/clang-format.py           # 格式化
python3 script/clang-format.py --dry-run # 检查
```

## 分支管理

- 新功能: `feat-<模块名>`
- 功能更新: `update-<模块名>`
- 主分支: `main`
