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

# 一次性扫描当天定时任务，按时间顺序等待并播报，播完再处理下一条
remind() {
  local pid_file="${XDG_RUNTIME_DIR:-/tmp}/todo_remind.pid"
  if [ -f "$pid_file" ]; then
    local old_pid=$(cat "$pid_file")
    [ -n "$old_pid" ] && kill "$old_pid" 2>/dev/null
  fi
  echo $$ > "$pid_file"

  while IFS= read -r line; do
    local stime
    stime=$(echo "$line" | grep -oE 'S:[0-9]{4}-[0-9]{2}-[0-9]{2}_[0-9]{2}:[0-9]{2}:[0-9]{2}' | sed 's/S://')
    [ -z "$stime" ] && continue

    local stime_ts
    stime_ts=$(date -d "${stime/_/ }" +%s 2>/dev/null)
    [ -z "$stime_ts" ] && continue

    [ "$stime_ts" -le "$(date +%s)" ] && continue

    local task_text
    task_text=$(echo "$line" | sed 's/^- \[ \] //;s/S:[^ ]*//g;s/D:[^ ]*//g' | xargs)
    [ -z "$task_text" ] && continue

    local wait_secs=$(( stime_ts - $(date +%s) ))
    [ "$wait_secs" -gt 0 ] && sleep "$wait_secs"

    notify-send -t 8000 "⏰ 任务提醒" "$task_text" 2>/dev/null
    _speak "任务提醒"
    sleep 0.3
    _speak "${task_text}"

  done < <(grep "^\- \[ \]" "$TODO_FILE" 2>/dev/null \
    | awk 'match($0,/S:([0-9]{4}-[0-9]{2}-[0-9]{2}_[0-9]{2}:[0-9]{2}:[0-9]{2})/,a) {print a[1]" "$0}' \
    | sort | sed 's/^[^ ]* //')

  rm -f "$pid_file"
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
  *) clear_task ;;
esac

