#!/usr/bin/env bash

_utilsDir=$(cd "$(dirname "$0")" && pwd)
_statusbarDir=$(cd "$(dirname "$0")/../statusbar" && pwd)
_configHome="${XDG_CONFIG_HOME:-$HOME/.config}"

# 主题组件：都做存在性和重复进程检查，缺少某项不会阻止 dwm 启动。
if command -v picom >/dev/null 2>&1 && ! pgrep -x picom >/dev/null 2>&1; then
  picom --config "$_configHome/picom/picom.conf" -b
fi

if command -v dunst >/dev/null 2>&1 && ! pgrep -x dunst >/dev/null 2>&1; then
  dunst -config "$_configHome/dunst/dunstrc" >/dev/null 2>&1 &
fi

if command -v xsetroot >/dev/null 2>&1 &&
   ! pgrep -f "$_statusbarDir/status.sh" >/dev/null 2>&1; then
  "$_statusbarDir/status.sh" >/dev/null 2>&1 &
fi

# 原有的待办任务自动整理；目录不完整时安静跳过。
if [ -x "$_utilsDir/task/task_todo.sh" ]; then
  "$_utilsDir/task/task_todo.sh" analysis &
  "$_utilsDir/task/task_todo.sh" remind &
  "$_utilsDir/task/task_todo.sh" auto_finish &
fi
