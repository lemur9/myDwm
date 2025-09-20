#!/bin/bash

#toggle() {
#  notify-send Mouse$BUTTON
#    vol_text=$(pactl list sinks | grep alsa_output.usb-TTGK_Technology_Co._Ltd_SOMIC_3100220201201-00.analog-stereo -A 7 | sed -n '8p' | awk '{printf int($4)}')
#    vol_icon=" "
#   notify-send -r 9527 -h int:value:$vol_text -h string:hlcolor:#dddddd "$vol_icon Volume"
#   pkill -USR1 slstatus
#}

update() {
    # 获取默认音频入口
    sink=$(pactl info | grep 'Default Sink' | awk '{print $3}')
    if [ "$sink" = "" ]; then sink=$(pactl info | grep '默认音频入口' | awk -F'：' '{print $2}');fi

    # 获取音量
    volunmuted=$(pactl list sinks | grep $sink -A 6 | sed -n '7p' | grep '静音：否')
    vol_text=$(pactl list sinks | grep $sink -A 7 | sed -n '8p' | awk '{printf int($4)}')
    if [ "$LANG" != "zh_CN.UTF-8" ]; then
        volunmuted=$(pactl list sinks | grep $sink -A 6 | sed -n '7p' | grep 'Mute: no')
        vol_text=$(pactl list sinks | grep $sink -A 7 | sed -n '8p' | awk '{printf int($5)}')
    fi

    if [ ! "$volunmuted" ];      then vol_text=0; vol_icon="";
    elif [ "$vol_text" -eq 0 ];  then vol_text=0; vol_icon="";
    elif [ "$vol_text" -lt 10 ]; then vol_icon=""; vol_text=0$vol_text;
    elif [ "$vol_text" -le 50 ]; then vol_icon="";
    else vol_icon=""; fi

    notify-send -r 9527 -h int:value:$vol_text -h string:hlcolor:#dddddd "$vol_icon Volume"
}

setting() {
    pid=`ps aux | grep 'st -t statusutil_volume' | grep -v grep | awk '{print $2}'`
    mx=`xdotool getmouselocation --shell | grep X= | sed 's/X=//'`
    my=`xdotool getmouselocation --shell | grep Y= | sed 's/Y=//'`
    dunstctl close 9527
    [ -n "$pid" ] && kill $pid || st -t statusutil_volume -g 50x15+$((mx))+$((my + 20)) -c float -e alsamixer
}

change() {
    # 静音设置
    if [ "$1" -eq 0 ]; then
        pactl set-sink-mute @DEFAULT_SINK@ toggle
    else
        pactl set-sink-volume @DEFAULT_SINK@ $1%
    fi

    pkill -USR1 slstatus
    update
}

case $BUTTON in
#case "$1" in
    1) change 0 ;;
    3) setting ;;
    4) change +1 ;;
    5) change -1 ;;
    *) ;;
esac
