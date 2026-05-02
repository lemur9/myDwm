#!/bin/bash

# 检查依赖
command -v aplay >/dev/null 2>&1 || { echo "❌ 请先安装 aplay (alsa-utils)"; exit 1; }
command -v notify-send >/dev/null 2>&1 || { echo "❌ 请先安装 notify-send (libnotify)"; exit 1; }

pomodoro_sh=$(cd $(dirname $0);cd ..;pwd)/utils/pomodoro/pomodoro.sh

export CONFIG_FILE="$(cd $(dirname $0);cd ..;pwd)/utils/pomodoro/.config"
export POMODORO_TIME=$(sed -n '3p' "$CONFIG_FILE")

pomodoro() {
  end_time=$(sed -n '4p' "$CONFIG_FILE")
  [ -n "$end_time" ] && [ "$(date +%s)" -gt "$(date -d "$end_time" +%s)" ] && echo "" > "$CONFIG_FILE"

  if [ -n "$POMODORO_TIME" ]; then
    pid=`ps aux | grep 'pomodoro.sh status' | grep -v grep | awk '{print $2}'`
    [ -n "$pid" ] && kill $pid && dunstctl close 9527 || bash "$pomodoro_sh" status
  else
    pid=`ps aux | grep 'st -t statusutil_pomodoro' | grep -v grep | awk '{print $2}'`
    mx=`xdotool getmouselocation --shell | grep X= | sed 's/X=//'`
    my=`xdotool getmouselocation --shell | grep Y= | sed 's/Y=//'`
    dunstctl close 9527
    [ -n "$pid" ] && kill $pid || (st -t statusutil_pomodoro -g 40x3+$((mx))+$((my + 20)) -c float -e "$pomodoro_sh" settings && bash "$pomodoro_sh" start)
  fi
}

cancel_pomodoro() {
  pid=`ps aux | grep 'pomodoro.sh status' | grep -v grep | awk '{print $2}'`
  [ -n "$pid" ] && kill $pid && dunstctl close 9527
  bash "$pomodoro_sh" cancel
}

health_notify() {
  pid=`ps aux | grep 'pomodoro.sh health' | grep -v grep | awk '{print $2}'`
  [ -n "$pid" ] && kill $pid && notify-send -r 9527 -t 5000 "⏰ 健康提醒 ⏰" "🔔 健康提醒已停止..." -i dialog-information || bash "$pomodoro_sh" health
}

if [ -n "$BUTTON" ]; then
  case $BUTTON in
    1) pomodoro ;;
    2) health_notify ;;
    3) cancel_pomodoro ;;
    *) ;;
  esac
else
  case $1 in
    health) health_notify ;;
    pomodoro) pomodoro ;;
    *) ;;
  esac
fi
