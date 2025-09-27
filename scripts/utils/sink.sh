#!/bin/bash

# 操作选项列表
operation_list="1:禁音切换
2:音量+5%
3:音量-5%"

change() {
    echo "音频切换"
    count=1
    while [ $count -ne 0 ]; do
        count=$1
        app_selection=$(pactl list short sink-inputs | while read id sink pid format; do
            echo "$id: $(pactl list short clients | awk -v c=$pid '$1==c {print $3}')"
        done | fzf --prompt="选择应用> " --height=40% --layout=reverse --border)

        if [ -z "$app_selection" ]; then
            exit 1
        fi

        app_id=$(echo "$app_selection" | cut -d: -f1)

        sink_selection=$(pactl list sinks | grep -E "\sName:|\s名称：" | awk -F '[:：]' '{print $2}' | nl -w1 -s': ' | fzf --prompt="选择音频设备> " --height=40% --layout=reverse --border)

        if [ -z "$sink_selection" ]; then
            exit 1
        fi

        sink_name=$(echo "$sink_selection" | awk '{print $2}')

        pactl move-sink-input "$app_id" "$sink_name"
    done
}

change_multiple() {
    echo "多音频音量调节"
    sink_selection=$(pactl list sinks | grep -E "\sName:|\s名称：" | awk -F '[:：]' '{print $2}' | nl -w1 -s': ' | fzf --prompt="选择音频设备> " --height=40% --layout=reverse --border)

    if [ -z "$sink_selection" ]; then
        exit 1
    fi

    operation_choice=$(printf '%s\n' "$operation_list" | awk -F '[:：]' '{print $2}' | nl -w1 -s': ' | fzf --prompt="选择操作> " --height=40% --layout=reverse --border)

    if [ -z "$operation_choice" ]; then
        exit 1
    fi

    sink_name=$(echo "$sink_selection" | awk '{print $2}')
    operation=$(echo "$operation_choice" | cut -d: -f1)

    case "$operation" in
        1)
            pactl set-sink-mute "$sink_name" toggle
            ;;
        2)
            pactl set-sink-volume "$sink_name" +5%
            ;;
        3)
            pactl set-sink-volume "$sink_name" -5%
            ;;
    esac

    # 更新状态栏
    pkill -USR1 slstatus 2>/dev/null
}


case $1 in
    settings) change_multiple ;;
    once) change 0 ;;
    forever) change 1 ;;
    *) change 0 ;;
esac