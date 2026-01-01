#!/bin/bash

task_todo_sh=$(cd $(dirname $0);pwd)/task_todo.sh

work_sound="$(cd $(dirname $0);pwd)/sound/work.wav"
rest_sound="$(cd $(dirname $0);pwd)/sound/rest.wav"
work_interval=$((45 * 60))
rest_interval=$((15 * 60))

sync_todo=1

content=$(sed -n '2p' "$CONFIG_FILE")

settings() {
  echo "🍅 开启番茄钟 🍅"
  read -p "专注内容:" content
  read -p "专注时长(分钟):" focus_duration

  start_time=$(date '+%H:%M:%S')
  end_time=$(date -d "@$(( $(date +%s) + $focus_duration * 60 ))" '+%Y-%m-%d %H:%M:%S')

  echo "$start_time" > $CONFIG_FILE
  echo "$content" >> $CONFIG_FILE
  echo "$focus_duration" >> $CONFIG_FILE
  echo "$end_time" >> $CONFIG_FILE
}

start() {
  POMODORO_TIME=$(sed -n '3p' "$CONFIG_FILE")
  if [ -z "$POMODORO_TIME" ] || ! [[ "$POMODORO_TIME" =~ ^[0-9]+$ ]]; then
    echo "" > "$CONFIG_FILE"
    notify-send -r 9527 -t 5000 "🍅 番茄钟 🍅" "⚙️ 专注时间有误，请检查后重试！"
  else
    notify-send -r 9527 -t 5000 "🍅 番茄钟 🍅" "🚀 $(date '+%H:%M:%S') 开始专注...\n⏳ 专注时长 $POMODORO_TIME 分钟" -i dialog-information
    aplay "$work_sound" >/dev/null 2>&1
    add_todo

    sleep $(( $POMODORO_TIME * 60 ))

    notify-send -r 9527 -t 5000 "🍅 番茄钟 🍅" "🧘‍♂️ 专注结束，休息一会吧..." -i dialog-information
    aplay "$rest_sound" >/dev/null 2>&1
    echo "" > "$CONFIG_FILE"
    finish_todo
  fi
}

status() {
  start_time=$(sed -n '1p' "$CONFIG_FILE")
  end_time=$(sed -n '4p' "$CONFIG_FILE")
  while true; do
    focus_duration=$(sed -n '3p' "$CONFIG_FILE")
    if [ -n "$focus_duration" ]; then
      notify-send -r 9527 -t 0 "🍅 番茄钟 🍅" "🚀 $start_time 开始专注...\n⏳ 专注时长 $focus_duration 分钟 / 已专注 $((($(date +%s) - $(date -d "$start_time" +%s)) / 60)) 分钟\n🧑‍💻 专注内容:$content" -i dialog-information
      sleep 60
    else
      dunstctl close 9527
    fi
  done
}

cancel() {
  pid=`ps aux | grep 'pomodoro.sh start' | grep -v grep | awk '{print $2}'`
  [ -n "$pid" ] && kill $pid && echo "" > "$CONFIG_FILE" && bash "$task_todo_sh" delete $content && notify-send -r 9527 -t 5000 "🍅 番茄钟 🍅" "⚙️ 当前番茄钟任务已取消..."
}

health() {
  while true; do
    dunstctl close 9527

    notify-send -r 9527 -t 5000 "⏰ 健康提醒 ⏰" "🚀 $(date '+%H:%M:%S') 开始工作...\n⏳ 健康提醒已启动，工作时长45分钟" -i dialog-information
    aplay "$work_sound" >/dev/null 2>&1
    sleep "$work_interval"

    notify-send -r 9527 -t 5000 "⏰ 健康提醒 ⏰" "🧘‍♂️ 休息时间到，请休息15分钟..." -i dialog-information
    aplay "$rest_sound" >/dev/null 2>&1
    sleep "$rest_interval"

    notify-send -r 9527 -t 5000 "⏰ 健康提醒 ⏰" "💻 休息结束，继续工作吧！" -i dialog-information
    sleep 5
  done
}

add_todo() {
  if [ "$sync_todo" -eq 1 ]; then
    bash "$task_todo_sh" add $content
  fi
}

finish_todo() {
  if [ "$sync_todo" -eq 1 ]; then
    bash "$task_todo_sh" finish $content
  fi
}

case $1 in
  settings) settings ;;
  start) start ;;
  status) status ;;
  cancel) cancel ;;
  health) health ;;
  *) ;;
esac

