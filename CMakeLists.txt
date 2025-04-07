
cmake_minimum_required(VERSION 3.12) # 最低 CMake 版本
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

#----------------------------------------------------------------
# 2. 添加头文件目录
#----------------------------------------------------------------
include_directories(include) # 头文件搜索路径
# 或更推荐的方式（CMake 3.10+）：
# target_include_directories(${PROJECT_NAME} PUBLIC include)

#----------------------------------------------------------------
# 3. 添加源文件并生成目标
#----------------------------------------------------------------
file(GLOB SOURCES "src/*.cpp") # 自动收集源文件
# 或显式列出源文件（推荐）：
# set(SOURCES src/main.cpp src/utils.cpp)
# add_library(${PROJECT_NAME} STATIC ${SOURCES}) # 静态库
# 或生成可执行文件：
add_executable(${PROJECT_NAME} ${SOURCES})

#----------------------------------------------------------------
# 4. 依赖管理（示例：查找第三方库）
#----------------------------------------------------------------
find_package(Boost 1.70 REQUIRED COMPONENTS filesystem system)
target_link_libraries(${PROJECT_NAME} PUBLIC Boost::filesystem)

#----------------------------------------------------------------
# 5. 安装规则（可选）
#----------------------------------------------------------------
install(DIRECTORY include/ DESTINATION include) # 安装头文件
install(TARGETS ${PROJECT_NAME} DESTINATION lib) # 安装库

#----------------------------------------------------------------
# 6. 单元测试支持（可选）
#----------------------------------------------------------------
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests) # 假设测试代码在 tests/ 目录
endif()
