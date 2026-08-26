#!/usr/bin/env bash
# Lightweight clickable status loop for this dwm build.
# Control bytes 1/4/5/6/7/8 select both click actions and color schemes.

interval="${STATUS_INTERVAL:-1}"
prev_total=0
prev_idle=0
CPU_VALUE=0
VOL_VALUE="--"
VOL_ICON=""
TRACK=""
PLAY_ICON=""

command -v xsetroot >/dev/null 2>&1 || {
  printf 'status.sh: xsetroot is required\n' >&2
  exit 1
}

cpu_usage() {
  local _cpu user nice system idle iowait irq softirq steal guest guest_nice
  local idle_all non_idle total delta_total delta_idle
  read -r _cpu user nice system idle iowait irq softirq steal guest guest_nice < /proc/stat
  idle_all=$((idle + iowait))
  non_idle=$((user + nice + system + irq + softirq + steal))
  total=$((idle_all + non_idle))

  if (( prev_total > 0 && total > prev_total )); then
    delta_total=$((total - prev_total))
    delta_idle=$((idle_all - prev_idle))
    CPU_VALUE=$((100 * (delta_total - delta_idle) / delta_total))
  fi
  prev_total=$total
  prev_idle=$idle_all
}

memory_usage() {
  awk '
    /^MemTotal:/     { total=$2 }
    /^MemAvailable:/ { available=$2 }
    END {
      if (total > 0) printf "%d", 100 * (total - available) / total;
      else printf "0";
    }
  ' /proc/meminfo
}

volume_status() {
  local muted value
  VOL_VALUE="--"
  VOL_ICON=""

  if command -v pactl >/dev/null 2>&1; then
    muted=$(pactl get-sink-mute @DEFAULT_SINK@ 2>/dev/null)
    value=$(pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null |
      awk 'NR == 1 { for (i=1; i<=NF; i++) if ($i ~ /%$/) { gsub(/%/, "", $i); print $i; exit } }')
    VOL_VALUE="${value:---}"
    if printf '%s' "$muted" | grep -qiE 'yes|是'; then
      VOL_ICON=""
    elif [[ "$VOL_VALUE" =~ ^[0-9]+$ ]] && (( VOL_VALUE < 35 )); then
      VOL_ICON=""
    fi
  elif command -v wpctl >/dev/null 2>&1; then
    value=$(wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null)
    if printf '%s' "$value" | grep -q MUTED; then
      VOL_ICON=""
      VOL_VALUE=0
    else
      VOL_VALUE=$(awk '{ printf "%d", $2 * 100 }' <<< "$value")
      (( VOL_VALUE < 35 )) && VOL_ICON=""
    fi
  fi
}

music_status() {
  local state
  TRACK=""
  PLAY_ICON=""
  command -v mpc >/dev/null 2>&1 || return

  TRACK=$(mpc current -f '%artist% — %title%' 2>/dev/null)
  [[ -z "$TRACK" ]] && TRACK=$(mpc current 2>/dev/null)
  [[ -z "$TRACK" ]] && return
  ((${#TRACK} > 32)) && TRACK="${TRACK:0:31}…"

  state=$(mpc status 2>/dev/null | sed -n '2p')
  if [[ "$state" == *'[playing]'* ]]; then
    PLAY_ICON=""
  else
    PLAY_ICON=""
  fi
}

trap 'exit 0' INT TERM HUP

while :; do
  cpu_usage
  memory=$(memory_usage)
  volume_status
  music_status

  printf -v status '\001 %s%%  󰍛 %s%%' "$CPU_VALUE" "$memory"
  if [[ -n "$TRACK" ]]; then
    printf -v status '%s\004 %s\005%s' "$status" "$TRACK" "$PLAY_ICON"
  fi
  if [[ "$VOL_VALUE" =~ ^[0-9]+$ ]]; then
    volume_label="${VOL_VALUE}%"
  else
    volume_label="$VOL_VALUE"
  fi
  printf -v status '%s\006%s %s\007 %s\010 %s' \
    "$status" "$VOL_ICON" "$volume_label" "$(date '+%H:%M')" "$(date '+%a %m-%d')"

  xsetroot -name "$status"
  sleep "$interval"
done
