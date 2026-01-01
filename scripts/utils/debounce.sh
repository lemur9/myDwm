#!/bin/bash
# debounce.sh —— 防抖脚本
# 用法示例：
#   debounce "volume" 1 pactl set-sink-mute @DEFAULT_SINK@ toggle

# 参数：
#   $1 = 锁名称（建议与功能名一致）
#   $2 = 冷却时间（秒）
#   $3... = 要执行的命令

NAME="$1"
COOLDOWN="$2"

LOCKDIR="${XDG_RUNTIME_DIR:-/tmp}"
LOCKFILE="$LOCKDIR/debounce_$NAME.lock"

# 检查是否在冷却期
if [ -f "$LOCKFILE" ]; then
  last=$(stat -c %Y "$LOCKFILE")
  now=$(date +%s)
  if (( now - last < COOLDOWN )); then
    # 仍在冷却期
    exit 1
  fi
fi

# 更新时间戳
touch "$LOCKFILE"
exit 0
