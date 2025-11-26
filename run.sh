#!/bin/bash
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
OUTPUT_DIR="$BUILD_DIR/output"
BINARY_NAME="nebula-toolkit"

USE_GDB=false
if [[ "$1" == "--gdb" ]]; then
    USE_GDB=true
    shift
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR" || {
    echo "无法进入 build 目录"
    exit 1
}

echo "正在配置 CMake（Debug 模式）..."
cmake -DCMAKE_BUILD_TYPE=Debug .. || {
    echo -e "\033[31mCMake 配置失败！\033[0m"
    exit 1
}

echo "正在编译..."
make || {
    echo -e "\033[31m编译失败，请检查错误信息！\033[0m"
    exit 1
}

if [[ ! -f "$OUTPUT_DIR/$BINARY_NAME" ]]; then
    echo -e "\033[31m错误：未找到可执行文件 $OUTPUT_DIR/$BINARY_NAME\033[0m"
    exit 1
fi

cd "$OUTPUT_DIR" || exit 1

if [[ "$USE_GDB" == true ]]; then
    echo -e "\033[32m启动 GDB 调试模式...\033[0m"
    echo "GDB 将加载程序并等待你的命令（例如输入 'run' 开始执行）"
    echo "----------------------------------------------------------"
    gdb --args ./"$BINARY_NAME" "$@"
else
    echo -e "\033[32m正常启动程序...\033[0m"
    exec ./"$BINARY_NAME" "$@"
fi
