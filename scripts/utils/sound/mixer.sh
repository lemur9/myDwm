#!/bin/bash
# 极简多 sink TUI 混音面板
# 乐观更新：按键后立即重绘，pamixer 后台异步写入

BAR_WIDTH=12
EVENT_PIPE=$(mktemp -u /tmp/mixer-events-XXXXXX)
BGPIDS=()

declare -a SINK_NAMES SINK_DESCS SINK_VOLS SINK_MUTED
SINK_COUNT=0
SEL=0
DEFAULT_SINK=""

cleanup() {
  tput rmcup
  tput cnorm
  rm -f "$EVENT_PIPE"
  for pid in "${BGPIDS[@]}"; do
    kill "$pid" 2>/dev/null
    pkill -P "$pid" 2>/dev/null
  done
}
trap cleanup EXIT INT TERM

load_sinks() {
  local dump default_name
  dump=$(pactl list sinks 2>/dev/null)
  # 兼容中文 locale：以冒号或中文冒号为分隔符取最后一段
  default_name=$(pactl info 2>/dev/null | awk -F'[：: \t]+' '/Default Sink|默认音频入口/{print $NF}')

  SINK_COUNT=0
  SINK_NAMES=(); SINK_DESCS=(); SINK_VOLS=(); SINK_MUTED=()

  local name desc vol muted
  while IFS= read -r line; do
    case "$line" in
      *"名称："*|*"Name:"*)
        name=$(printf '%s' "$line" | sed 's/.*[：:][[:space:]]*//' | xargs) ;;
      *"描述："*|*"Description:"*)
        desc=$(printf '%s' "$line" | sed 's/.*[：:][[:space:]]*//' | xargs) ;;
      *"静音：是"*|*"Mute: yes"*)
        muted=1 ;;
      *"静音：否"*|*"Mute: no"*)
        muted=0 ;;
      *"音量："*|*"Volume:"*)
        vol=$(printf '%s' "$line" | grep -oE '[0-9]+%' | head -1 | tr -d '%')
        if [ -n "$name" ]; then
          SINK_NAMES+=("$name")
          SINK_DESCS+=("${desc:-$name}")
          SINK_VOLS+=("${vol:-0}")
          SINK_MUTED+=("${muted:-0}")
          [ "$name" = "$default_name" ] && { DEFAULT_SINK="$name"; SEL=$SINK_COUNT; }
          SINK_COUNT=$(( SINK_COUNT + 1 ))
          name=""; desc=""; vol=""; muted=""
        fi ;;
    esac
  done <<< "$dump"

  [ "$SEL" -ge "$SINK_COUNT" ] && [ "$SINK_COUNT" -gt 0 ] && SEL=$(( SINK_COUNT - 1 ))
}

bar() {
  local filled=$(( $1 * BAR_WIDTH / 100 )) s="" i=0
  while [ $i -lt $BAR_WIDTH ]; do
    [ $i -lt $filled ] && s="${s}█" || s="${s}░"
    i=$(( i + 1 ))
  done
  printf '%s' "$s"
}

render() {
  local row=0
  tput cup $row 0; tput el; printf ' Mixer'
  row=1

  local i=0
  while [ $i -lt $SINK_COUNT ]; do
    local desc="${SINK_DESCS[$i]}"
    [ ${#desc} -gt 16 ] && desc="${desc:0:15}…"
    local cursor="  "
    [ "$i" -eq "$SEL" ] && cursor="▶ "
    local mute_tag=""
    [ "${SINK_MUTED[$i]}" = "1" ] && mute_tag=" M"
    local default_tag="  "
    [ "${SINK_NAMES[$i]}" = "$DEFAULT_SINK" ] && default_tag="* "
    tput cup $row 0; tput el
    printf '%s%s%-17s [%s] %3d%%%s' "$cursor" "$default_tag" "$desc" "$(bar "${SINK_VOLS[$i]}")" "${SINK_VOLS[$i]}" "$mute_tag"
    row=$(( row + 1 ))
    i=$(( i + 1 ))
  done

  tput cup $row 0; tput el
}

adjust() {
  local delta="$1" idx="$SEL"
  local vol=$(( ${SINK_VOLS[$idx]} + delta ))
  [ "$vol" -gt 100 ] && vol=100
  [ "$vol" -lt 0 ]   && vol=0
  SINK_VOLS[$idx]=$vol
  render
  pamixer --sink "${SINK_NAMES[$idx]}" --set-volume "$vol" &
  pkill -USR1 slstatus 2>/dev/null &
}

toggle_mute() {
  local idx="$SEL"
  if [ "${SINK_MUTED[$idx]}" = "1" ]; then
    SINK_MUTED[$idx]=0
    render
    pamixer --sink "${SINK_NAMES[$idx]}" --unmute &
  else
    SINK_MUTED[$idx]=1
    render
    pamixer --sink "${SINK_NAMES[$idx]}" --mute &
  fi
  pkill -USR1 slstatus 2>/dev/null &
}

set_default() {
  local name="${SINK_NAMES[$SEL]}"
  DEFAULT_SINK="$name"
  render
  pactl set-default-sink "$name" &
  pkill -USR1 slstatus 2>/dev/null &
}

read_key() {
  local k1 k2 k3
  IFS= read -r -s -t 0.05 -n 1 k1
  [ $? -ne 0 ] && { printf 'TIMEOUT'; return; }

  if [ "$k1" = $'\x1b' ]; then
    IFS= read -r -s -t 0.05 -n 1 k2
    IFS= read -r -s -t 0.05 -n 1 k3
    printf 'ESC_%s' "$k3"
  else
    printf '%s' "$k1"
  fi
}

main() {
  mkfifo "$EVENT_PIPE"

  pactl subscribe 2>/dev/null \
    | grep --line-buffered -E "change.*sink|change.*信宿" \
    > "$EVENT_PIPE" &
  BGPIDS+=($!)

  tput smcup
  tput civis
  load_sinks
  render

  local need_reload=0
  ( while IFS= read -r _; do kill -USR1 $$ 2>/dev/null; done < "$EVENT_PIPE" ) &
  BGPIDS+=($!)
  trap 'need_reload=1' USR1

  while true; do
    local key
    key=$(read_key)

    if [ "$need_reload" = "1" ]; then
      need_reload=0
      load_sinks
      render
    fi

    case "$key" in
      j|ESC_B)
        [ "$SEL" -lt $(( SINK_COUNT - 1 )) ] && { SEL=$(( SEL + 1 )); render; } ;;
      k|ESC_A)
        [ "$SEL" -gt 0 ] && { SEL=$(( SEL - 1 )); render; } ;;
      +|=|ESC_C)
        adjust +5 ;;
      -|ESC_D)
        adjust -5 ;;
      m)
        toggle_mute ;;
      s)
        set_default ;;
      q|ESC_)
        break ;;
    esac
  done
}

main
