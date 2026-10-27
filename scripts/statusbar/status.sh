#!/usr/bin/env bash
# Lightweight clickable status loop for this dwm build.
# Control bytes 1/4/6/7/8 select both click actions and color schemes.

interval="${STATUS_INTERVAL:-1}"
VOLUME_TEXT=" 音频离线"
audioctl="$(cd -- "$(dirname -- "$0")/../utils/sound" && pwd)/audioctl"
MUSIC_TEXT=""
last_status=""

command -v xsetroot >/dev/null 2>&1 || {
  printf 'status.sh: xsetroot is required\n' >&2
  exit 1
}

# Only one bundled status process may own the root-window title.
LOCK_FILE="/tmp/dwm-status-${UID}.lock"
exec 9>"$LOCK_FILE"
flock -n 9 || exit 0

# The themed bar is the selected provider by default. Stop the two common
# root-name writers; otherwise they race with this loop and the bar alternates.
if [[ "${DWM_STATUS_TAKEOVER:-0}" == 1 ]]; then
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
  # No pactl/wpctl fallback: displaying a different target from the one being
  # controlled is worse than explicitly showing that the shared service is off.
  VOLUME_TEXT=$("$audioctl" status 2>/dev/null) || VOLUME_TEXT=" 音频离线"
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

  read -r weekday_num clock_text date_text <<< "$(date '+%u %H:%M %m-%d')"
  weekdays=(一 二 三 四 五 六 日)
  weekday_text=${weekdays[weekday_num - 1]}
  printf -v status '%s\006%s\007 %s\010 %s %s' \
    "$status" "$VOLUME_TEXT" "$clock_text" \
    "$weekday_text" "$date_text"

  # Do not emit redundant X property events when every visible value is equal.
  if [[ "$status" != "$last_status" ]]; then
    xsetroot -name "$status"
    last_status="$status"
  fi
  sleep "$interval"
done
