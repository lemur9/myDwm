#!/bin/bash

DEBOUNCE=$(cd $(dirname $0);cd ..;pwd)/utils/debounce.sh

playlist() {
  pid=`ps aux | grep 'st -t statusutil_ncmpcpp' | grep -v grep | awk '{print $2}'`
  mpd_pid=$(pgrep -x mpd)
  mx=`xdotool getmouselocation --shell | grep X= | sed 's/X=//'`
  my=`xdotool getmouselocation --shell | grep Y= | sed 's/Y=//'`
  [ -n "$pid" ] && kill $pid || ([ -n "$mpd_pid" ] && st -t statusutil_ncmpcpp -g 30x20+$((mx))+$((my + 20)) -c float -e ncmpcpp -c ~/.config/ncmpcpp/config-classic)
}

play_change() {
  mpd_pid=$(pgrep -x mpd)
  [ -n "$mpd_pid" ] && mpc toggle
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
    2) play_change ;;
    3) playlist ;;
    4) prev ;;
    5) next ;;
    *) ;;
  esac
else
  case $1 in
    lyrics) lyrics_change ;;
    change) play_change ;;
    *) ;;
  esac
fi
