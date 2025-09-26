#!/bin/sh
#
# This script is executed upon user login to restore desktop snapshot(s).
# It first checks if any restore task is armed. If not, it exits silently.
# It uses a lock file to ensure it only runs once per session.

# --- 步骤 1: 预检查，如果无需恢复则静默退出 ---

# 获取用户 HOME 目录，这是找到快照目录的前提
# 我们不能依赖 $HOME 变量，因为它可能尚未设置，但 C++ 程序需要它
# 使用 getent 是一个更可靠的方式
USER_HOME=$(getent passwd "$(id -un)" | cut -d: -f6)

# 定义基础快照目录
BASE_SNAPSHOT_DIR="$USER_HOME/.snapshot_manager"

# 检查基础目录是否存在，如果不存在，说明没有任何快照，直接退出
if [ ! -d "$BASE_SNAPSHOT_DIR" ]; then
    # 使用 return 优雅退出，不影响父进程
    return 0
fi

# 检查是否有任何一个 'restore_on_boot.flag' 文件存在
# find 命令会查找所有匹配的文件，-quit 会在找到第一个后立即退出，效率很高
if ! find "$BASE_SNAPSHOT_DIR" -name "restore_on_boot.flag" -print -quit | grep -q .; then
    # 如果 find 命令没有任何输出 (grep -q . 失败)，说明没有找到任何标志文件
    return 0
fi

# --- 步骤 2: 确认需要恢复，开始执行实际操作 ---

# 定义日志文件和锁文件路径
LOG_FILE="/tmp/desktop_restore.log"
USER_ID=$(id -u)
LOCK_DIR="/run/user/$USER_ID/desktop_snapshot"
LOCK_FILE="$LOCK_DIR/restored.lock"

# 检查锁文件，防止会话内重复执行
mkdir -p "$LOCK_DIR"
if [ -f "$LOCK_FILE" ]; then
    # 已经恢复过了，优雅退出
    return 0
fi

# 初始化日志文件 (现在我们确定真的需要它了)
echo "" > "$LOG_FILE"
echo "--- Log started at $(date) from $0 ---" >> "$LOG_FILE"
exec >> "$LOG_FILE" 2>&1

echo "Restore task(s) detected. Lock file not found. Proceeding with restore logic..."

# 等待桌面服务初始化
echo "Waiting for 2 seconds..."
sleep 2

HELPER_BIN="/usr/bin/autostart_helper"
echo "Checking for helper binary at '$HELPER_BIN'..."

if [ -x "$HELPER_BIN" ]; then
    echo "Executing helper in the background and creating lock file..."
    touch "$LOCK_FILE"
    "$HELPER_BIN" &
else
    echo "ERROR: Helper binary not found or is not executable."
fi

echo "--- Startup script finished (executed) ---"