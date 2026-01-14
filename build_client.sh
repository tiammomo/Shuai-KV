#!/bin/bash
# 构建 shuaikv 客户端 (CMake)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 检查构建目录
if [ ! -d "build" ]; then
    echo "Creating build directory..."
    mkdir -p build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
fi

echo "Building shuaikv client..."
cd build
cmake --build . --target shuaikv_client -j$(nproc)

echo "Build successful!"
echo "Binary location: build/bin/shuaikv_client"
