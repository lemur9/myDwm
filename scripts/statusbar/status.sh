#!/usr/bin/env bash
# Lightweight clickable status loop for this dwm build.
# Control bytes 1/4/6/7/8 select both click actions and color schemes.

interval="${STATUS_INTERVAL:-1}"
VOL_VALUE="--"
VOL_ICON=""
MUSIC_TEXT=""
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
      [[ "$VOL_VALUE" =~ ^[0-9]+$ ]] && (( VOL_VALUE < 35 )) && VOL_ICON=""
    fi
  fi
}

music_status() {
  local output state track
  MUSIC_TEXT=""
  command -v mpc >/dev/null 2>&1 || return

  # One mpc process supplies both metadata and player state. Keep the title
  # hidden while paused/stopped; the music icon itself remains permanently.
  output=$(mpc -f '%artist% — %title%' status 2>/dev/null) || return
  state=$(printf '%s\n' "$output" | grep -m1 '^\[')
  [[ "$state" == *'[playing]'* ]] || return

  track=$(printf '%s\n' "$output" | sed -n '1p')
  if [[ -z "$track" || "$track" == \[* ]]; then
    track=$(mpc current -f '%file%' 2>/dev/null)
  fi
  [[ -z "$track" ]] && return
  ((${#track} > 32)) && track="${track:0:31}…"
  MUSIC_TEXT=" $track"
}

while :; do
  memory=$(memory_usage)
  volume_status
  music_status

  # Memory stays on the bar; CPU details are available from system.sh.
  printf -v status '\001󰍛 %3d%%\004%s' "$memory" "$MUSIC_TEXT"

  if [[ "$VOL_VALUE" =~ ^[0-9]+$ ]]; then
    printf -v volume_label '%3d%%' "$VOL_VALUE"
  else
    printf -v volume_label '%4s' "$VOL_VALUE"
  fi

  read -r weekday_num clock_text date_text <<< "$(date '+%u %H:%M %m-%d')"
  weekdays=(一 二 三 四 五 六 日)
  weekday_text=${weekdays[weekday_num - 1]}
  printf -v status '%s\006%s %s\007 %s\010 %s %s' \
    "$status" "$VOL_ICON" "$volume_label" "$clock_text" \
    "$weekday_text" "$date_text"

  # Do not emit redundant X property events when every visible value is equal.
  if [[ "$status" != "$last_status" ]]; then
    xsetroot -name "$status"
    last_status="$status"
  fi
  sleep "$interval"
done
