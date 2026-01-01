#!/bin/bash

DEBOUNCE=$(cd $(dirname $0);cd ..;pwd)/utils/debounce.sh

UPDATE_PLAYLIST=$(cd $(dirname $0);cd ..;pwd)/utils/update_playlist.sh

update() {
  $DEBOUNCE "st_music_update" 3600
  [ $? -eq 1 ] && notify-send -r 9527 -t 2000 "🎵 歌单同步 🎵" "🔔 一小时内已同步..." && exit 1
  notify-send -r 9527 -t 2000 "🎵 歌单同步 🎵" "🔔 开始同步歌单..." -i dialog-information
  bash $UPDATE_PLAYLIST
  pgrep -x mpd && mpc update || (mpd && mpc update && killall mpd)
  notify-send -r 9527 -t 2000 "🎵 歌单同步 🎵" "🔔 歌单同步结束..." -i dialog-information
}

playlist() {
  pid=`ps aux | grep 'st -t statusutil_ncmpcpp' | grep -v grep | awk '{print $2}'`
  mpd_pid=$(pgrep -x mpd)
  mx=`xdotool getmouselocation --shell | grep X= | sed 's/X=//'`
  my=`xdotool getmouselocation --shell | grep Y= | sed 's/Y=//'`
  [ -n "$pid" ] && kill $pid || ([ -n "$mpd_pid" ] && st -t statusutil_ncmpcpp -g 30x20+$((mx))+$((my + 20)) -c float -e ncmpcpp)
}

prev() {
  $DEBOUNCE "st_music_prev" 1
  [ $? -eq 0 ] && mpc prev
}

next() {
  $DEBOUNCE "st_music_next" 1
  [ $? -eq 0 ] && mpc next
}

if [ -n "$BUTTON" ]; then
  case $BUTTON in
    2) update ;;
    3) playlist ;;
    4) prev ;;
    5) next ;;
    *) ;;
  esac
else
  case $1 in
    lyrics) lyrics_change ;;
    update) update ;;
    *) ;;
  esac
fi
