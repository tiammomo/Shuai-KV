#!/bin/bash
# 初始化单节点数据目录

# 获取当前目录名称作为节点 ID
NODE_ID=$(basename "$(pwd)")
DATA_DIR="data_${NODE_ID}"

echo "Initializing node ${NODE_ID}..."
mkdir -p "${DATA_DIR}"

# 初始化 Manifest
echo "{}" > "${DATA_DIR}/manifest.json"

echo "Node ${NODE_ID} initialized at ${DATA_DIR}/"
