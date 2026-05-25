#!/bin/bash

# todo 状态栏目前只显示默认音频入口的音量， 后续调整为类似alsamixer的多音频显示调节界面
mixer_sh=$(cd $(dirname $0);cd ..;pwd)/utils/sound/mixer.sh

update() {
  local input_idx="$1"  # 可选：sink-input id，传入时显示该流的音量

  if [ -n "$input_idx" ]; then
    local dump
    dump=$(pactl list sink-inputs)
    vol_text=$(echo "$dump" | awk -v target="$input_idx" '
      /^(Sink Input|信宿输入) #/ { cur=$NF; gsub(/#/,"",cur) }
      cur==target && /音量：|Volume:/ { match($0, /([0-9]+)%/, m); print m[1]; exit }
    ')
    local muted
    muted=$(echo "$dump" | awk -v target="$input_idx" '
      /^(Sink Input|信宿输入) #/ { cur=$NF; gsub(/#/,"",cur) }
      cur==target && /静音：|Mute:/ { print $0; exit }
    ')
    local volunmuted=""
    echo "$muted" | grep -qE '静音：否|Mute: no' && volunmuted=1
  else
    sink=$(pactl info | grep 'Default Sink' | awk '{print $3}')
    if [ "$sink" = "" ]; then sink=$(pactl info | grep '默认音频入口' | awk -F'：' '{print $2}');fi

    volunmuted=$(pactl list sinks | grep $sink -A 6 | sed -n '7p' | grep '静音：否')
    vol_text=$(pactl list sinks | grep $sink -A 7 | sed -n '8p' | awk '{printf int($4)}')
    if [ "$LANG" != "zh_CN.UTF-8" ]; then
      volunmuted=$(pactl list sinks | grep $sink -A 6 | sed -n '7p' | grep 'Mute: no')
      vol_text=$(pactl list sinks | grep $sink -A 7 | sed -n '8p' | awk '{printf int($5)}')
    fi
  fi

  if [ ! "$volunmuted" ];      then vol_text=0; vol_icon="";
  elif [ "$vol_text" -eq 0 ];  then vol_text=0; vol_icon="";
  elif [ "$vol_text" -lt 10 ]; then vol_icon=""; vol_text=0$vol_text;
  elif [ "$vol_text" -le 50 ]; then vol_icon="";
  else vol_icon=""; fi

  local active_count extra_tag=""
  active_count=$(pactl list short sinks | awk '$5 != "SUSPENDED"' | wc -l)
  [ "$active_count" -gt 1 ] && extra_tag=" +$(( active_count - 1 ))"

  notify-send -r 9527 -h int:value:$vol_text -h string:hlcolor:#dddddd "$vol_icon Volume${extra_tag}"
}

settings() {
  pid=$(ps aux | grep 'st -t statusutil_sink_settings' | grep -v grep | awk '{print $2}')
  mx=$(xdotool getmouselocation --shell | grep X= | sed 's/X=//')
  my=$(xdotool getmouselocation --shell | grep Y= | sed 's/Y=//')
  dunstctl close 9527
  [ -n "$pid" ] && kill $pid || st -t statusutil_sink_settings -g 52x12+$((mx))+$((my + 20)) -c float -e "$mixer_sh"
}

change() {
  sink=@DEFAULT_SINK@
  [ -n "$2" ] && sink=$2

  if [ "$1" -eq 0 ]; then
    pactl set-sink-mute $sink toggle
  else
    cur=$(pactl get-sink-volume $sink | awk 'NR==1 {printf int($5)}')
    vol_text=$(( cur + $1 ))
    [ "$vol_text" -gt 100 ] && vol_text=100
    pactl set-sink-volume $sink $vol_text%
  fi

  pkill -USR1 slstatus
  update
}

# 构造 rofi 菜单中单条音频流的显示文本。
# 兼容中英文 locale（pactl 中文输出头行为"信宿输入 #N"，英文为"Sink Input #N"）。
# 用 match(/"([^"]+)"/) 取完整引号内容，避免 $NF 对含空格值（如"Google Chrome"）截断。
# 输出格式：app: media [#id, rateHz, Nch]；media 为"Playback"或与 app 相同时省略。
sink_input_label() {
  local id="$1"
  local dump="${2:-$(pactl list sink-inputs)}"  # 由调用方预取，避免循环内重复调用 pactl

  local meta
  meta=$(echo "$dump" | awk -v target="$id" '
    /^(Sink Input|信宿输入) #/ { cur = $NF; gsub(/#/, "", cur) }
    cur == target && /application\.name/ { match($0, /"([^"]+)"/, m); app = m[1] }
    cur == target && /media\.name/       { match($0, /"([^"]+)"/, m); media = m[1] }
    cur == target && /[0-9]+ch [0-9]+Hz/ {
      match($0, /([0-9]+)ch ([0-9]+)Hz/, m); channels = m[1]; rate = m[2]
    }
    END { printf "%s\t%s\t%s\t%s", app, media, rate, channels }
  ')

  local app media rate channels
  app=$(printf '%s' "$meta"     | cut -f1)
  media=$(printf '%s' "$meta"   | cut -f2)
  rate=$(printf '%s' "$meta"    | cut -f3)
  channels=$(printf '%s' "$meta"| cut -f4)

  # 括号内始终带 sink-input 编号，Chrome 等同名多流靠此区分
  local bracket="#${id}"
  [ -n "$rate" ]     && bracket="${bracket}, ${rate}Hz"
  [ -n "$channels" ] && bracket="${bracket}, ${channels}ch"

  if echo "$app" | grep -qi "chrome" || [ "$media" = "Playback" ] || [ -z "$media" ] || [ "$media" = "$app" ]; then
    printf '%s [%s]' "$app" "$bracket"
  else
    printf '%s: %s [%s]' "$app" "$media" "$bracket"
  fi
}

# 尝试用活跃窗口标题与 media.name 匹配，缩小候选范围。
# Chrome 的 media.name 固定为"Playback"，此函数对 Chrome 无效，仅对 mpv 等有意义。
match_by_window_title() {
  local inputs="$1"
  local win_title="$2"
  local sink_inputs_dump
  sink_inputs_dump=$(pactl list sink-inputs)

  while IFS= read -r id; do
    [ -z "$id" ] && continue
    local media_name
    media_name=$(echo "$sink_inputs_dump" | awk -v target="$id" '
      /^(Sink Input|信宿输入) #/ { cur = $NF; gsub(/#/,"",cur) }
      cur == target && /media\.name/ { match($0,/"([^"]+)"/,m); print m[1]; exit }
    ')
    [ -z "$media_name" ] && continue
    if echo "$win_title" | grep -qF "$media_name" 2>/dev/null || \
       echo "$media_name" | grep -qF "$win_title" 2>/dev/null; then
      echo "$id"
    fi
  done <<< "$inputs"
}

sinks() {
  dunstctl close 9527

  local active_inputs
  active_inputs=$(get_active_sink_inputs)

  local selected_input

  if [ -z "$active_inputs" ]; then
    active_inputs=$(pactl list short sink-inputs | awk '{print $1}')
    [ -z "$active_inputs" ] && return
  fi

  local input_count
  input_count=$(echo "$active_inputs" | grep -c .)

  if [ "$input_count" -eq 1 ]; then
    selected_input=$(echo "$active_inputs" | head -1)
  else
    local win_title
    win_title=$(xdotool getactivewindow getwindowname 2>/dev/null)
    local title_matches=""
    [ -n "$win_title" ] && title_matches=$(match_by_window_title "$active_inputs" "$win_title")

    local matched_count
    matched_count=$(echo "$title_matches" | grep -c .)

    if [ -n "$title_matches" ] && [ "$matched_count" -eq 1 ]; then
      selected_input=$(echo "$title_matches" | head -1)
    else
      local candidates="${title_matches:-$active_inputs}"

      # 预取一次 dump，供循环内 sink_input_label 复用
      local sink_inputs_dump
      sink_inputs_dump=$(pactl list sink-inputs)

      local menu_items=""
      while IFS= read -r id; do
        [ -z "$id" ] && continue
        label=$(sink_input_label "$id" "$sink_inputs_dump")
        menu_items="${menu_items}${id}: ${label}\n"
      done <<< "$candidates"

      local choice
      choice=$(printf "%b" "$menu_items" | rofi -dmenu -i -p "选择音频流")
      [ -z "$choice" ] && return
      selected_input=$(echo "$choice" | cut -d: -f1)
    fi
  fi

  local default_sink
  default_sink=$(pactl info | awk '/Default Sink/{print $3}')

  local selected_sink
  selected_sink=$(pactl list short sinks | awk '{print $2}' | \
    rofi -dmenu -i -p "切换到设备" -select "$default_sink")
  [ -z "$selected_sink" ] && return

  pactl move-sink-input "$selected_input" "$selected_sink"
  pkill -USR1 slstatus
  update
}

# 返回当前活跃窗口关联的 sink-input 编号列表（每行一个）。
# 用 BFS 遍历完整进程树，覆盖 Chrome 等多进程应用（音频子进程不是窗口 PID 的直接子进程）。
get_active_sink_inputs() {
  local win_pid
  win_pid=$(xdotool getactivewindow getwindowpid 2>/dev/null) || return 1

  local all_pids="$win_pid"
  local queue="$win_pid"
  while [ -n "$queue" ]; do
    local next=""
    for p in $queue; do
      local children
      children=$(pgrep -P "$p" 2>/dev/null | tr '\n' ' ')
      if [ -n "$children" ]; then
        next="$next $children"
        all_pids="$all_pids $children"
      fi
    done
    queue="${next# }"
  done

  local sink_pids
  sink_pids=$(pactl list sink-inputs | awk '/application\.process\.id/ {val=$NF; gsub(/"/,"",val); print val}' | tr '\n' ' ')

  pactl list sink-inputs | awk -v pids="$all_pids" '
    /^(Sink Input|信宿输入) #/ { idx = $NF; gsub(/#/, "", idx) }
    /application\.process\.id/ {
      val = $NF; gsub(/"/, "", val)
      n = split(pids, arr, " ")
      for (i = 1; i <= n; i++) if (arr[i] == val) { print idx; break }
    }
  '
}

# 滚轮调节音量：优先调节当前活跃窗口对应的 sink-input，无匹配时回退到全局音量。
smart_change() {
  local delta="$1"
  local sink_inputs
  sink_inputs=$(get_active_sink_inputs)

  local active_win
  active_win=$(xdotool getactivewindow 2>/dev/null)

  if [ -n "$sink_inputs" ]; then
    while IFS= read -r idx; do
      local cur
      cur=$(pactl list sink-inputs | awk -v target="$idx" '
        /^(Sink Input|信宿输入) #/ { cur=$NF; gsub(/#/,"",cur) }
        cur==target && /音量：|Volume:/ { match($0, /([0-9]+)%/, m); print m[1]; exit }
      ')
      [ -z "$cur" ] && continue
      local vol=$(( cur + delta ))
      [ "$vol" -gt 100 ] && vol=100
      [ "$vol" -lt 0 ]   && vol=0
      pactl set-sink-input-volume "$idx" "${vol}%"
    done <<< "$sink_inputs"
  else
    change "$delta"
    return
  fi

  pkill -USR1 slstatus
  local last_idx
  last_idx=$(echo "$sink_inputs" | tail -1)
  update "$last_idx"
}

if [ -n "$BUTTON" ]; then
  case $BUTTON in
    1) change 0 ;;
    2) sinks ;;
    3) settings ;;
    4) smart_change +1 ;;
    5) smart_change -1 ;;
  esac
else
  case $1 in
    change) smart_change $2 ;;
  esac
fi
