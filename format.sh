#!/bin/bash
# 格式化代码脚本

set -e

echo "格式化 shuaikv 代码..."

# 检查 clang-format 是否安装
if ! command -v clang-format &> /dev/null; then
    echo "Error: clang-format not found"
    echo "Install with: sudo apt install clang-format"
    exit 1
fi

# 格式化所有头文件和源文件
find shuaikv -name "*.hpp" -o -name "*.cpp" -o -name "*.h" | xargs clang-format-18 -i -style=file

# 格式化 tests 目录
find tests -name "*.cpp" -o -name "*.hpp" | xargs clang-format-18 -i -style=file

# 格式化根目录脚本
for file in build.sh build_client.sh init.sh; do
    if [ -f "$file" ]; then
        clang-format-18 -i -style=file "$file"
    fi
done

echo "格式化完成!"
