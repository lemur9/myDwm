#!/bin/bash

# 检查依赖
# command -v mpd >/dev/null 2>&1 || { echo "❌ 请先安装 mpd"; exit 1; }
# command -v mpc >/dev/null 2>&1 || { echo "❌ 请先安装 mpc"; exit 1; }
# command -v ncmpcpp >/dev/null 2>&1 || { echo "❌ 请先安装 ncmpcpp"; exit 1; }
# command -v cava >/dev/null 2>&1 || { echo "❌ 请先安装 cava"; exit 1; }
# command -v bc >/dev/null 2>&1 || { echo "❌ 请先安装 bc"; exit 1; }

lyrics=$(cd $(dirname $0);cd ..;pwd)/utils/music/lyrics.sh

DEBOUNCE=$(cd $(dirname $0);cd ..;pwd)/utils/debounce.sh

UPDATE_PLAYLIST=$(cd $(dirname $0);cd ..;pwd)/utils/music/update_playlist.sh

update() {
  $DEBOUNCE "st_music_update" 3600
  [ $? -eq 1 ] && notify-send -r 9527 -t 2000 "🎵 歌单同步 🎵" "🔔 一小时内已同步..." && exit 1
  notify-send -r 9527 -t 2000 "🎵 歌单同步 🎵" "🔔 开始同步歌单..." -i dialog-information
  bash $UPDATE_PLAYLIST
  pgrep -x mpd && mpc update || (mpd && mpc update && killall mpd)
  notify-send -r 9527 -t 2000 "🎵 歌单同步 🎵" "🔔 歌单同步结束..." -i dialog-information
}

lyrics_change() {
  lyrics_pid=$(pgrep -f lyrics.sh)
  [ -n "$lyrics_pid" ] && kill $lyrics_pid && notify-send -r 9999 -t 2000 "🎵 悬浮歌词 🎵" "悬浮歌词已关闭..." || (notify-send -r 9999 -t 2000 "🎵 悬浮歌词 🎵" "悬浮歌词已启动..." && nohup $lyrics open >/dev/null 2>&1 &)
}

switch_change() {
  mpd_pid=$(pgrep -x mpd)
  [ -n "$mpd_pid" ] && switch_close || switch_open
}

switch_open() {
  nohup mpd > /dev/null 2>&1 &
}

switch_close() {
  lyrics_pid=$(pgrep -f lyrics.sh)
  [ -n "$lyrics_pid" ] && kill $lyrics_pid

  ncmpcpp_pid=`ps aux | grep 'st -t statusutil_ncmpcpp' | grep -v grep | awk '{print $2}'`
  [ -n "$ncmpcpp_pid" ] && kill $ncmpcpp_pid

  mpd_pid=$(pgrep -x mpd)
  [ -n "$mpd_pid" ] && kill $mpd_pid
}

if [ -n "$BUTTON" ]; then
  case $BUTTON in
    1) switch_change ;;
    2) update ;;
    3) lyrics_change ;;
    *) ;;
  esac
else
  case $1 in
    switch) switch_change ;;
    update) update ;;
    lyrics) lyrics_change ;;
    *) ;;
  esac
fi
