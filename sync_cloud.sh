#!/bin/bash
# 同步 xrtc-server2.0 源码到云服务器（只同步代码，编译/重启/联调由用户负责）
# 用法: ./sync_cloud.sh
set -e

SERVER="ydqun@ydqun.top"
REMOTE_SFU_DIR="/home/ydqun/workspace/lessons/xrtc1.0/cpp/xrtc-server2.0"
# 本地路径用脚本所在目录推导，换机器/换路径不用改
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOCAL_SRC_DIR="$SCRIPT_DIR/src"

echo "== rsync src/ 到云端 =="
rsync -avz --exclude '.git/' -e ssh \
    "$LOCAL_SRC_DIR/" \
    "$SERVER:$REMOTE_SFU_DIR/src/"

echo "同步完成。云端编译/重启/联调请自行处理。"
