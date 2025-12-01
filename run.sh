#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
OUTPUT_DIR="$BUILD_DIR/output"
BINARY_NAME="nebula-toolkit"

# 颜色定义
RED='\033[31m'
GREEN='\033[32m'
YELLOW='\033[33m'
BLUE='\033[34m'
RESET='\033[0m'

# 默认配置
ACTION="build-run" # build-run | build-only | run-only
USE_GDB=false
CLEAN_BUILD=false
VERBOSE=false
CMAKE_BUILD_TYPE="Debug"
CMAKE_EXTRA_FLAGS=""

# 显示帮助信息
show_help() {
    cat <<EOF
用法: $0 [选项] [程序参数...]

构建和运行 ${BINARY_NAME} 的便捷脚本

选项:
  -b, --build-only     仅编译，不运行
  -r, --run-only       仅运行，不编译（需确保已编译）
  -g, --gdb            使用 GDB 调试运行
  -c, --clean          清理构建目录后重新编译
  -v, --verbose        显示详细输出
  -t, --type TYPE      设置构建类型 (Debug|Release|RelWithDebInfo) [默认: Debug]
  -h, --help          显示此帮助信息

程序参数:
  所有在选项之后的参数将传递给可执行文件

示例:
  $0                          # 编译并运行
  $0 --build-only             # 仅编译
  $0 --run-only --gdb         # 使用 GDB 运行已编译的程序
  $0 --clean --verbose        # 清理后重新编译并显示详细输出
  $0 --type Release arg1 arg2 # Release 编译并传递参数给程序
EOF
}

# 解析命令行参数
parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case $1 in
        -b | --build-only)
            ACTION="build-only"
            shift
            ;;
        -r | --run-only)
            ACTION="run-only"
            shift
            ;;
        -g | --gdb)
            USE_GDB=true
            shift
            ;;
        -c | --clean)
            CLEAN_BUILD=true
            shift
            ;;
        -v | --verbose)
            VERBOSE=true
            shift
            ;;
        -t | --type)
            if [[ -n "$2" && ! "$2" =~ ^- ]]; then
                CMAKE_BUILD_TYPE="$2"
                shift 2
            else
                echo -e "${RED}错误: --type 参数需要一个构建类型${RESET}"
                exit 1
            fi
            ;;
        -h | --help)
            show_help
            exit 0
            ;;
        --) # 结束选项解析
            shift
            break
            ;;
        -*)
            echo -e "${RED}未知选项: $1${RESET}"
            show_help
            exit 1
            ;;
        *) # 程序参数开始
            break
            ;;
        esac
    done

    # 剩余参数传递给程序
    PROGRAM_ARGS=("$@")
}

# 检查依赖
check_dependencies() {
    local missing_deps=()

    if ! command -v cmake &>/dev/null; then
        missing_deps+=("cmake")
    fi

    if ! command -v make &>/dev/null; then
        missing_deps+=("make")
    fi

    if [[ "$USE_GDB" == true ]] && ! command -v gdb &>/dev/null; then
        missing_deps+=("gdb")
    fi

    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        echo -e "${RED}错误: 缺少必要的依赖: ${missing_deps[*]}${RESET}"
        exit 1
    fi
}

# 清理构建目录
clean_build() {
    if [[ "$CLEAN_BUILD" == true ]]; then
        echo -e "${YELLOW}清理构建目录...${RESET}"
        if [[ -d "$BUILD_DIR" ]]; then
            rm -rf "$BUILD_DIR"/*
        fi
    fi
}

# 编译项目
build_project() {
    echo -e "${GREEN}配置 CMake ($CMAKE_BUILD_TYPE 模式)...${RESET}"

    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR" || exit 1

    local cmake_cmd="cmake -DCMAKE_BUILD_TYPE=$CMAKE_BUILD_TYPE $CMAKE_EXTRA_FLAGS .."

    if [[ "$VERBOSE" == true ]]; then
        echo -e "${BLUE}执行: $cmake_cmd${RESET}"
        $cmake_cmd
    else
        $cmake_cmd 2>&1 | grep -E --color=always 'error|warning|^' || true
    fi

    if [[ ${PIPESTATUS[0]} -ne 0 ]]; then
        echo -e "${RED}CMake 配置失败！${RESET}"
        exit 1
    fi

    echo -e "${GREEN}编译项目...${RESET}"
    local make_cmd="make"

    if [[ "$VERBOSE" == true ]]; then
        echo -e "${BLUE}执行: $make_cmd${RESET}"
        $make_cmd
    else
        $make_cmd 2>&1 | grep -E --color=always 'error|warning|^\[.*\]' || true
    fi

    if [[ ${PIPESTATUS[0]} -ne 0 ]]; then
        echo -e "${RED}编译失败！${RESET}"
        exit 1
    fi

    if [[ ! -f "$OUTPUT_DIR/$BINARY_NAME" ]]; then
        echo -e "${RED}错误：未找到可执行文件 $OUTPUT_DIR/$BINARY_NAME${RESET}"
        exit 1
    fi

    echo -e "${GREEN}编译完成！${RESET}"
}

# 运行程序
run_program() {
    if [[ ! -f "$OUTPUT_DIR/$BINARY_NAME" ]]; then
        echo -e "${RED}错误：可执行文件不存在，请先编译${RESET}"
        exit 1
    fi

    cd "$OUTPUT_DIR" || exit 1

    if [[ "$USE_GDB" == true ]]; then
        echo -e "${GREEN}启动 GDB 调试模式...${RESET}"
        echo -e "${BLUE}程序参数: ${PROGRAM_ARGS[*]}${RESET}"
        echo "GDB 提示: 输入 'run' 开始执行，'bt' 查看堆栈，'q' 退出"
        echo "----------------------------------------"
        gdb --args "./$BINARY_NAME" "${PROGRAM_ARGS[@]}"
    else
        echo -e "${GREEN}启动程序...${RESET}"
        echo -e "${BLUE}程序参数: ${PROGRAM_ARGS[*]}${RESET}"
        echo "----------------------------------------"
        exec "./$BINARY_NAME" "${PROGRAM_ARGS[@]}"
    fi
}

# 显示配置信息
show_config() {
    echo -e "${BLUE}=== 构建配置 ===${RESET}"
    echo -e "操作模式:    $ACTION"
    echo -e "构建类型:    $CMAKE_BUILD_TYPE"
    echo -e "GDB 调试:    $USE_GDB"
    echo -e "清理构建:    $CLEAN_BUILD"
    echo -e "详细输出:    $VERBOSE"
    echo -e "程序参数:    ${PROGRAM_ARGS[*]}"
    echo -e "构建目录:    $BUILD_DIR"
    echo -e "输出目录:    $OUTPUT_DIR"
    echo -e "可执行文件:  $BINARY_NAME"
    echo -e "${BLUE}===============${RESET}"
    echo
}

# 主函数
main() {
    parse_arguments "$@"
    check_dependencies
    show_config

    case "$ACTION" in
    "build-run")
        clean_build
        build_project
        run_program
        ;;
    "build-only")
        clean_build
        build_project
        ;;
    "run-only")
        run_program
        ;;
    *)
        echo -e "${RED}错误: 未知的操作模式: $ACTION${RESET}"
        exit 1
        ;;
    esac
}

# 脚本入口
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi
