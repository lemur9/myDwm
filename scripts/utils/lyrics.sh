#!/bin/bash
#
# ncmpcpp + MPD + dunst 同步歌词脚本（支持暂停清除）
# 作者：Lemur 专属定制版 🦥
# 功能：
#  - 自动解析 .lrc 歌词并按时间同步显示
#  - 支持暂停/切歌自动清除通知
#  - 使用 notify-send (dunst) 展示歌词
#

LYRICS_DIR="$HOME/.ncmpcpp/lyrics"
NOTIFY_ID=9999       # 保持同一个通知 ID，不堆叠
MPD_FORMAT="%artist% - %title%"
SLEEP_OFFSET=0.2     # 提前显示一点，单位：秒

# 将 [mm:ss.xx] 转为秒
time_to_sec() {
  local time=$1
  local min=${time%%:*}
  local sec=${time#*:}
  echo "$min * 60 + $sec" | bc
}

# 获取当前歌曲标题
get_current_song() {
  mpc current -f "$MPD_FORMAT"
}

# 清除歌词通知
clear_notification() {
  notify-send -r "$NOTIFY_ID" ""
}

# 播放歌词（同步 .lrc）
play_lyrics() {
  local song="$1"
  local file="$LYRICS_DIR/${song}.txt"

  [[ ! -f "$file" ]] && {
    notify-send -r "$NOTIFY_ID" "🎵 $song 🎵" "新歌曲，尝试加载歌词中..."
      sleep 1
      file="$LYRICS_DIR/${song}.txt"
      [[ ! -f "$file" ]] && notify-send -r "$NOTIFY_ID" -t 2000 "🎵 $song 🎵" "歌词文件不存在"
    }

  while IFS= read -r line; do
    # 获取时间戳和歌词内容
    timestamp=$(echo "$line" | grep -oP '\[\d+:\d+\.\d+\]')
    text=$(echo "$line" | sed 's/\[[0-9]\+\:[0-9]\+\.[0-9]\+\]//')

    # 转换时间戳为秒
    minutes=$(echo "$timestamp" | cut -d':' -f1 | tr -d '[]')
    seconds=$(echo "$timestamp" | cut -d':' -f2 | tr -d '[]')
    sec=$(echo "$minutes*60 + $seconds" | bc)

    local play_sec=$(mpc status | awk 'NR==2 {split($3,a,"/"); split(a[1],b,":"); print b[1]*60 + b[2]}')
    local wait=$(echo "$play_sec - $sec" | bc)

    if [ $(echo "$wait < 0" | bc) -ne 0 ]; then
      wait=$(echo "$sec - $play_sec + $SLEEP_OFFSET" | bc)
      sleep "$wait"

      # 检查是否暂停或停止
      local state=$(mpc status | awk 'NR==2 {print $1}' | tr -d '[]')
      local current_song="$(get_current_song)"
      if [[ "$state" != "playing" || "$current_song" != "$song" ]]; then
        clear_notification
        return
      fi

      notify-send -r "$NOTIFY_ID" -t 6000 "🎵 $song 🎵 " "\n$text" -i dialog-information
    fi

  done < "$file"
}

open() {
  mpd_pid=$(pgrep -x mpd)
  [ -z "$mpd_pid" ] && exit 1

  # 启动时立即检查当前状态
  state=$(mpc status | awk 'NR==2 {print $1}' | tr -d '[]')
  if [[ "$state" == "playing" ]]; then
    song="$(get_current_song)"
    [[ -n "$song" ]] && play_lyrics "$song" &
  fi

  # 主循环：监听 mpd 状态
  mpc idleloop player | while read -r event; do
  state=$(mpc status | awk 'NR==2 {print $1}' | tr -d '[]')

  case "$state" in
    playing)
      song="$(get_current_song)"
      [[ -n "$song" ]] && play_lyrics "$song" & ;;
    paused|stop)
      clear_notification ;;
  esac
done
}

close() {
  pkill lyrics.sh
}

case "$1" in
  open)
    open ;;
  *)
    close ;;
esac
