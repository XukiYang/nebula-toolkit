# Nebula-Toolkit

Nebula-Toolkit 是一个轻量级、高性能的 C++ 工具库，提供了网络编程、数据处理、线程管理等常用功能模块，适用于各种 C++ 项目开发。

## 支持的 C++ 版本

- **最低要求**: C++11
- **推荐版本**: C++14 及以上

## 模块概览

### 数据容器库 (`nebula::containers`)

- **CircularBuffer**
  - 线程安全的环形缓冲区
  - 支持线性 IO、动态扩容、迭代器访问、零拷贝操作
  - 适用于网络数据缓存、数据流处理等场景
  - **C++ 版本要求**: C++11

- **UnPacker**
  - 基于环形缓冲区的数据包解包器
  - 支持头定位符、尾定位符、头尾定位符等多种分包模式
  - 提供数据大小回调和校验回调，支持自定义解析逻辑
  - **C++ 版本要求**: C++11

- **BitUtils**
  - 位操作工具集
  - 支持位设置、清除、翻转、计数等操作
  - 提供 32 位和 64 位整数的位操作方法
  - **C++ 版本要求**: C++11

### 日志库 (`nebula::logger`)

- **Logger**
  - 基于懒汉单例模式的日志实现
  - 支持日志分级（INFO、WARN、DEBUG、ERROR 等）
  - 提供文件输出、控制台输出、异步写入等功能
  - 支持配置动态更新，包括日志目录、文件大小限制等
  - **C++ 版本要求**: C++17
  - **备注**: 使用了 `if constexpr`、折叠表达式和 `std::byte` 等 C++17 特性

### 线程库 (`nebula::threading`)

- **ThreadPool**
  - 基于任务队列的异步线程池
  - 支持单任务和批量任务提交
  - 通过条件变量实现线程休眠与唤醒，减少资源消耗
  - **C++ 版本要求**: C++11

- **TimerScheduler**
  - 基于线程池和优先级队列的定时调度器
  - 支持毫秒级精度的定时任务
  - 支持取消未执行的任务，自动管理任务提交和线程池执行
  - **C++ 版本要求**: C++14
  - **备注**: 使用了 `std::make_unique` 等 C++14 特性

### 内存库 (`nebula::memory`)

- **BasicMemoryPool**
  - 线程安全的内存池实现
  - 支持内存块的分配和释放
  - 适用于频繁内存操作的场景，减少内存碎片
  - **C++ 版本要求**: C++11

### 配置处理库 (`nebula::config_handler`)

- **IniConfigHandler**
  - INI 配置文件处理器
  - 支持读取、解析、修改 INI 配置文件
  - 提供多种数据类型的获取方法（布尔值、整数、字符串等）
  - **C++ 版本要求**: C++11

- **IniReader**
  - 轻量级 INI 文件读取器
  - 提供简洁的接口读取 INI 文件中的配置项
  - **C++ 版本要求**: C++11

### 网络库 (`nebula::net`)

- **ReactorCore**
  - 基于 epoll 的事件反应器核心
  - 支持 TCP 连接的管理和事件处理
  - 提供非阻塞 IO 和边缘触发模式
  - **C++ 版本要求**: C++14
  - **备注**: 使用了 `std::make_unique` 等 C++14 特性

- **ProtocolHandler**
  - 协议处理器基类
  - 提供 TCP、UDP 等协议的处理接口
  - 支持事件驱动的网络编程模型
  - **C++ 版本要求**: C++14
  - **备注**: 使用了 `std::make_unique` 等 C++14 特性

### 串口处理库 (`nebula::serialport_handler`)

- **SerialClient**
  - 串口客户端实现
  - 支持串口的打开、关闭、读写操作
  - 适用于串口通信场景
  - **C++ 版本要求**: C++11

## 构建与安装

### 依赖项

- C++17 兼容的编译器
- fmt 库（用于日志格式化）

### 构建方法

1. 克隆仓库
2. 使用 CMake 或直接编译源文件

## 工程结构约定

- `include/`：头文件区（所有模块实现与对外 API）
- `lib/`：第三方依赖（git 子模块）
- `examples/`：示例程序
- `tests/`：测试代码

详细说明见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。

## 接入方式

### 方式一：直接 include（头文件导出层）

保持使用 `include/<module>/*.hpp`，示例：

```cpp
#include "containers/circular_buffer.hpp"
#include "threading/thread_pool.hpp"
```

### 方式二：CMake 子项目接入

```cmake
add_subdirectory(third_party/nebula-toolkit)
target_link_libraries(your_target PRIVATE nebula::all)
```

也可以按模块链接：

```cmake
target_link_libraries(your_target PRIVATE nebula::containers nebula::threading)
```

### 方式三：安装后 find_package

```cmake
find_package(nebula-toolkit CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE nebula::all)
```

## 兼容性说明

- 外部 `#include` 路径保持稳定：`include/<module>/*.hpp`
- 对外 CMake target 保持稳定：`nebula::containers`、`nebula::logger`、`nebula::net`、`nebula::all`

## 代码风格

使用 Google C++ 代码规范

## 分支管理

- 新功能: `feat-<模块名>`
- 功能更新: `update-<模块名>`
- 主分支: `main`
