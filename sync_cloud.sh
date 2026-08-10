#!/bin/bash
# 同步 xrtc-server2.0 源码和配置到云服务器（编译/重启/联调由用户负责）
# 用法: ./sync_cloud.sh
set -e

SERVER="ydqun@ydqun.top"
REMOTE_SFU_DIR="/home/ydqun/workspace/lessons/xrtc1.0/cpp/xrtc-server2.0"
# 本地路径用脚本所在目录推导，换机器/换路径不用改
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOCAL_SRC_DIR="$SCRIPT_DIR/src"
LOCAL_CONF_DIR="$SCRIPT_DIR/conf"

echo "== rsync CMakeLists.txt 到云端 =="
# 必须带上: CMakeLists 用 file(GLOB) 收源文件, 新增目录(如 src/video)不同步会漏编译
rsync -avz -e ssh \
    "$SCRIPT_DIR/CMakeLists.txt" \
    "$SERVER:$REMOTE_SFU_DIR/CMakeLists.txt"

echo "== rsync src/ 到云端 =="
rsync -avz --exclude '.git/' -e ssh \
    "$LOCAL_SRC_DIR/" \
    "$SERVER:$REMOTE_SFU_DIR/src/"

echo "== rsync conf/ 到云端 =="
rsync -avz --exclude '.git/' -e ssh \
    "$LOCAL_CONF_DIR/" \
    "$SERVER:$REMOTE_SFU_DIR/conf/"

echo "同步完成。云端编译/重启/联调请自行处理。"
