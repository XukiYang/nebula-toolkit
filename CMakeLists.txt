# 本cmake使用使用模板构建
cmake_minimum_required(VERSION 3.14) # 最低 CMake 版本（fmt 9.1.0 建议 ≥3.14）
project(nebula-toolkit # 项目名称
    VERSION 1.0.0 # 版本号
    LANGUAGES CXX # 语言：CXX(C++) 或 C
)

#----------------------------------------------------------------
# 1. 基础配置
#----------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17) # C++标准 (11/14/17/20)
set(CMAKE_CXX_STANDARD_REQUIRED ON) # 强制使用指定标准
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # 生成 compile_commands.json

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/output)

#----------------------------------------------------------------
# 2. 添加头文件目录
#----------------------------------------------------------------
include_directories(include) # 头文件搜索路径

#----------------------------------------------------------------
# 3. 添加源文件并生成目标
#----------------------------------------------------------------
# file(GLOB SOURCES "src/*.cpp") # 自动收集源文件
# 或显式列出源文件（推荐）：
set(SOURCES
    tests/unit/ring_buffer_test.cpp
    # tests/unit/byte_stream_test.cpp
    # tests/unit/thread_pool_test.cpp
    # tests/unit/timer_scheduler.cpp
    # tests/unit/unpacker_test.cpp
    # tests/unit/net_reactor_test.cpp
    # tests/unit/logger_test.cpp

    src/containers/ring_buffer.cpp
    src/containers/byte_stream.cpp
)
# add_library(${PROJECT_NAME} STATIC ${SOURCES}) # 静态库
# 或生成可执行文件：
add_executable(${PROJECT_NAME} ${SOURCES})

#----------------------------------------------------------------
# 4. 依赖管理（示例：查找第三方库）
#----------------------------------------------------------------
# find_package(Boost 1.70 REQUIRED COMPONENTS filesystem system)
# target_link_libraries(${PROJECT_NAME} PUBLIC Boost::filesystem)
# find_package(FMT REQUIRED)  # 改用源码集成
# target_link_libraries(${PROJECT_NAME} fmt::fmt) 

#----------------------------------------------------------------
# 4.1 集成 fmt 源码（替换 find_package）
#----------------------------------------------------------------
# 禁用 fmt 的测试和文档
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_DOC OFF CACHE BOOL "" FORCE)
# 将 fmt 作为子项目引入
add_subdirectory(lib/fmt-10.2.1)
# 链接 fmt 提供的 target
target_link_libraries(${PROJECT_NAME} PRIVATE fmt::fmt)

#----------------------------------------------------------------
# 4.2 链接 Threads
#----------------------------------------------------------------
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

#----------------------------------------------------------------
# 5. 静态文件复制
#----------------------------------------------------------------
# 创建存放目录
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/configs
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/logs
    COMMENT "Makeing some directory"
)
# 拷贝静态文件
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${PROJECT_SOURCE_DIR}/configs/*
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/configs
    COMMENT "Copying configs/*.ini to output/configs directory"
)

#----------------------------------------------------------------
# 6. 安装规则（可选）
#----------------------------------------------------------------
install(DIRECTORY include/ DESTINATION include) # 安装头文件
install(TARGETS ${PROJECT_NAME} DESTINATION lib) # 安装库

#----------------------------------------------------------------
# 7. 单元测试支持（可选）,暂时不集成测试框架
#----------------------------------------------------------------
# if(BUILD_TESTING)
#     enable_testing()
#     add_subdirectory(tests) # 假设测试代码在 tests/ 目录
# endif()