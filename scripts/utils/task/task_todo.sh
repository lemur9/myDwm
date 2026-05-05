#!/bin/zsh

# 获取当前日期（格式：YYYYMMDD）
TODAY=$(date +%Y%m%d)

# 获取当前日期（格式：YYYY-MM-DD）
TASK_TIME=$(date +%Y-%m-%d)

# 定义文件路径（默认用户目录，可自定义）
TODO_FILE="$HOME/work/todo/todo_${TODAY}.md"

create_task() {
  # 若文件不存在则创建
  if [ ! -f "$TODO_FILE" ]; then
    touch "$TODO_FILE"
    echo "# ${TASK_TIME} 待办清单" > "$TODO_FILE"
    echo "- [ ] S:${TASK_TIME} D:${TASK_TIME}" >> "$TODO_FILE"
  else
    nvim --noplugin -c "set autowriteall" -c "set updatetime=3000" -c "autocmd CursorHold,CursorHoldI * silent wa" "$TODO_FILE"
  fi
}

add_task() {
  echo "- [ ] $1 S:${TASK_TIME} D:${TASK_TIME}" >> "$TODO_FILE"
}

finish_task() {
  sed -i "/- \[ \].*$1/{s/- \[ \]/- [x]/}" "$TODO_FILE"
}

delete_task() {
  sed -i "/- \[ \].*$1/d" "$TODO_FILE"
}

_speak() {
  echo "$1" | piper-tts -m /opt/piper/zh_CN-huayan-medium.onnx --output-raw \
    | aplay -r 22050 -f S16_LE -c 1 -q
}

remind() {
  local pid_file="${XDG_RUNTIME_DIR:-/tmp}/todo_remind.pid"

  # 单实例锁（防重复启动）
  exec 9>"$pid_file"
  flock -n 9 || exit 0

  # 读取所有任务
  grep "^\- \[ \]" "$TODO_FILE" 2>/dev/null | while IFS= read -r line; do

    local stime
    stime=$(echo "$line" | grep -oE 'S:[0-9]{4}-[0-9]{2}-[0-9]{2}_[0-9]{2}:[0-9]{2}:[0-9]{2}' | sed 's/S://')
    [ -z "$stime" ] && continue

    local stime_ts
    stime_ts=$(date -d "${stime/_/ }" +%s 2>/dev/null)
    [ -z "$stime_ts" ] && continue

    local now_ts
    now_ts=$(date +%s)

    # 已过期跳过
    [ "$stime_ts" -le "$now_ts" ] && continue

    local task_text
    task_text=$(echo "$line" | sed 's/^- \[ \] //;s/S:[^ ]*//g;s/D:[^ ]*//g' | xargs)

    local key="${stime}|${task_text}"

    # ===== 核心：事件驱动调度 =====
    (
      sleep $(( stime_ts - $(date +%s) ))
      notify-send "⏰ 任务提醒" "$task_text" 2>/dev/null
      _speak "任务提醒"
      sleep 0.2
      _speak "$task_text"

    ) &

  done
}

auto_finish() {
  local pid_file="${XDG_RUNTIME_DIR:-/tmp}/todo_auto_finish.pid"

  # 单实例锁
  exec 9>"$pid_file"
  flock -n 9 || exit 0

  local now_ts
  now_ts=$(date +%s)

  grep "^\- \[ \]" "$TODO_FILE" 2>/dev/null | while IFS= read -r line; do

    local dtime
    dtime=$(echo "$line" | grep -oE 'D:[0-9]{4}-[0-9]{2}-[0-9]{2}_[0-9]{2}:[0-9]{2}:[0-9]{2}' | sed 's/D://')
    [ -z "$dtime" ] && continue

    local dtime_ts
    dtime_ts=$(date -d "${dtime/_/ }" +%s 2>/dev/null)
    [ -z "$dtime_ts" ] && continue

    # 已过期直接执行（不 sleep）
    if [ "$dtime_ts" -le "$now_ts" ]; then
      _do_finish "$line"
      continue
    fi

    local task_line="$line"

    # ===== 事件驱动核心 =====
    (
      sleep $(( dtime_ts - $(date +%s) ))

      # 二次校验（防重复执行）
      grep -Fqx "$task_line" "$TODO_FILE" || exit 0

      _do_finish "$task_line"

    ) &

  done
}

_do_finish() {
  local line="$1"

  local dtime
  dtime=$(echo "$line" | grep -oE 'D:[0-9]{4}-[0-9]{2}-[0-9]{2}_[0-9]{2}:[0-9]{2}:[0-9]{2}' | sed 's/D://')

  local escaped
  escaped=$(printf '%s' "$dtime" | sed 's/[]\/$*.^[]/\\&/g')

  sed -i "s/^- \[ \]\(.*D:${escaped}.*\)$/- [x]\1/" "$TODO_FILE"
}

analysis() {
  [ ! -f "$TODO_FILE" ] && create_task

  for file in ~/work/todo/todo_*.md; do
    if [ "$file" = "$TODO_FILE" ]; then continue; fi

    _expired=$(grep "\- \[ \]" "$file" | grep -v "\- \[ \]\sS" | awk -v today="$TASK_TIME" '{match($0,/D:([0-9-]+)/,a);if (a[1] <= today) print $0;}')

    _future=$(grep "\- \[ \]" "$file" | grep -v "\- \[ \]\sS" | awk -v today="$TASK_TIME" '{match($0,/S:([0-9-]+)/,a);if (a[1] >= today) print $0;}')

    if [ -n "$_expired" ]; then
      printf '%s\n' "$_expired" | sed -i '1r /dev/stdin' "$TODO_FILE"
      printf '%s\n' "$_expired" | grep -F -x -v -f - "$file" > tmp && mv tmp "$file"
    fi

    if [ -n "$_future" ]; then
      echo "$_future" >> "$TODO_FILE"
      printf '%s\n' "$_future" | grep -F -x -v -f - "$file" > tmp && mv tmp "$file"
    fi
  done
}

clear_task() {
  # 清除超过7天的历史任务
  find ~/work/todo -name "todo_*.md" -type f -mtime +7 -delete 2>/dev/null
}

case $1 in
  create) create_task ;;
  add) add_task $2 ;;
  finish) finish_task $2 ;;
  delete) delete_task $2 ;;
  analysis) clear_task && analysis ;;
  remind) remind ;;
  auto_finish) auto_finish ;;
  *) clear_task ;;
esac

