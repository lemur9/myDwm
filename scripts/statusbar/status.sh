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
last_status=""
lock_dir=""

command -v xsetroot >/dev/null 2>&1 || {
  printf 'status.sh: xsetroot is required\n' >&2
  exit 1
}

# Only one bundled status process may own the root-window title.
runtime_dir="${XDG_RUNTIME_DIR:-/tmp}"
[[ -d "$runtime_dir" && -w "$runtime_dir" ]] || runtime_dir=/tmp
if command -v flock >/dev/null 2>&1; then
  exec 9>"$runtime_dir/mydwm-status.lock"
  flock -n 9 || exit 0
else
  lock_dir="$runtime_dir/mydwm-status.lock.d"
  mkdir "$lock_dir" 2>/dev/null || exit 0
fi

cleanup() {
  trap - INT TERM HUP EXIT
  [[ -n "$lock_dir" ]] && rmdir "$lock_dir" 2>/dev/null
  exit 0
}
trap cleanup INT TERM HUP EXIT

# The themed bar is the selected provider by default. Stop the two common
# root-name writers; otherwise they race with this loop and the bar alternates.
if [[ "${MYDWM_STATUS_TAKEOVER:-0}" == 1 ]]; then
  pkill -x slstatus 2>/dev/null || true
  pkill -x dwmblocks 2>/dev/null || true
fi

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

while :; do
  cpu_usage
  memory=$(memory_usage)
  volume_status
  music_status

  # Fixed-width numeric fields keep module geometry stable at 9/10/100%.
  printf -v status '\001 %3d%%  󰍛 %3d%%' "$CPU_VALUE" "$memory"
  if [[ -n "$TRACK" ]]; then
    printf -v status '%s\004 %s\005%s' "$status" "$TRACK" "$PLAY_ICON"
  fi
  if [[ "$VOL_VALUE" =~ ^[0-9]+$ ]]; then
    printf -v volume_label '%3d%%' "$VOL_VALUE"
  else
    printf -v volume_label '%4s' "$VOL_VALUE"
  fi
  printf -v status '%s\006%s %s\007 %s\010 %s' \
    "$status" "$VOL_ICON" "$volume_label" \
    "$(date '+%H:%M')" "$(LC_TIME=C date '+%a %m-%d')"

  # Do not emit redundant X property events when every visible value is equal.
  if [[ "$status" != "$last_status" ]]; then
    xsetroot -name "$status"
    last_status="$status"
  fi
  sleep "$interval"
done
