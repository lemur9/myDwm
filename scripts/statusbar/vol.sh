#!/bin/bash

# todo 状态栏目前只显示默认音频入口的音量， 后续调整为类似alsamixer的多音频显示调节界面
sink_sh=$(cd $(dirname $0);cd ..;pwd)/utils/sink.sh

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

settings() {
    pid=`ps aux | grep 'st -t statusutil_sink_settings' | grep -v grep | awk '{print $2}'`
    mx=`xdotool getmouselocation --shell | grep X= | sed 's/X=//'`
    my=`xdotool getmouselocation --shell | grep Y= | sed 's/Y=//'`
    dunstctl close 9527
    [ -n "$pid" ] && kill $pid || st -t statusutil_sink_settings -g 50x15+$((mx))+$((my + 20)) -c float -e "$sink_sh" settings
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

sinks() {
    pid=`ps aux | grep 'st -t statusutil_sink' | grep -v grep | awk '{print $2}'`
    mx=`xdotool getmouselocation --shell | grep X= | sed 's/X=//'`
    my=`xdotool getmouselocation --shell | grep Y= | sed 's/Y=//'`
    dunstctl close 9527
    [ -n "$pid" ] && kill $pid || st -t statusutil_sink -g 50x15+$((mx))+$((my + 20)) -c float -e "$sink_sh" once
}

change_all() {
    # 获取所有音频输出设备
    sinks=$(pactl list short sinks | awk '{print $1}')

    # 静音设置
    if [ "$1" -eq 0 ]; then
        for sink in $sinks; do
            pactl set-sink-mute $sink toggle
        done
    else
        # 设置所有设备音量
        for sink in $sinks; do
            pactl set-sink-volume $sink $1%
        done
    fi

    pkill -USR1 slstatus
    update
}

if [ -n "$BUTTON" ]; then
   case $BUTTON in
   #case "$1" in
       1) change 0 ;;
       2) sinks ;;
       3) settings ;;
       4) change +1 ;;
       5) change -1 ;;
       *) ;;
   esac
else
    case $1 in
        all) change_all $2 ;;
    esac
fi
