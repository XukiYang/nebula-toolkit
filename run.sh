#!/bin/bash
mkdir -p build && cd build || exit 1

if cmake .. && make; then
    cd output && ./nebula-toolkit
else
    echo -e "\033[31m编译失败，请检查错误信息！\033[0m"
    exit 1
fi