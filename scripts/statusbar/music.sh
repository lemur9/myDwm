#!/usr/bin/env bash
# Unified mouse actions for the complete music status module.
# Left: play/pause, middle: ncmpcpp, right: Rofi menu, wheel: prev/next.

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
UTILS_DIR=$(cd "$SCRIPT_DIR/../utils" && pwd)
LYRICS="$UTILS_DIR/music/lyrics.sh"
UPDATE_PLAYLIST="$UTILS_DIR/music/update_playlist.sh"
DEBOUNCE="$UTILS_DIR/debounce.sh"
NOTIFY_ID=9531

notify_music() {
  command -v notify-send >/dev/null 2>&1 || return
  notify-send -r "$NOTIFY_ID" -t "${3:-2500}" "${1:-Music}" "${2:-}" -i audio-x-generic
}

ensure_mpd() {
  command -v mpc >/dev/null 2>&1 || {
    notify_music "Music" "mpc is not installed."
    return 1
  }
  if pgrep -x mpd >/dev/null 2>&1; then
    if mpc status >/dev/null 2>&1; then
      return 0
    fi
    notify_music "Music" "MPD is running but cannot be reached."
    return 1
  fi
  command -v mpd >/dev/null 2>&1 || {
    notify_music "Music" "mpd is not installed."
    return 1
  }
  mpd >/dev/null 2>&1 || {
    notify_music "Music" "Failed to start MPD."
    return 1
  }
  for _ in {1..20}; do
    mpc status >/dev/null 2>&1 && return 0
    sleep 0.1
  done
  notify_music "Music" "MPD did not become ready."
  return 1
}

player_toggle() {
  ensure_mpd || return
  local state
  state=$(mpc status 2>/dev/null | sed -n '2p')
  if [[ "$state" == *'[playing]'* || "$state" == *'[paused]'* ]]; then
    mpc toggle >/dev/null 2>&1
  else
    mpc play >/dev/null 2>&1
  fi
}

player_window() {
  local pids mx my
  pids=$(pgrep -f 'st -t statusutil_ncmpcpp' 2>/dev/null)
  if [[ -n "$pids" ]]; then
    kill $pids 2>/dev/null
    return
  fi
  ensure_mpd || return
  command -v ncmpcpp >/dev/null 2>&1 || {
    notify_music "Music" "ncmpcpp is not installed."
    return
  }

  if command -v xdotool >/dev/null 2>&1; then
    mx=$(xdotool getmouselocation --shell 2>/dev/null | awk -F= '/^X=/{print $2}')
    my=$(xdotool getmouselocation --shell 2>/dev/null | awk -F= '/^Y=/{print $2}')
  fi
  if [[ "$mx" =~ ^[0-9]+$ && "$my" =~ ^[0-9]+$ ]]; then
    st -t statusutil_ncmpcpp -g "30x20+${mx}+$((my + 30))" -c float \
      -e ncmpcpp -c "$HOME/.config/ncmpcpp/config-classic" &
  else
    st -t statusutil_ncmpcpp -c float \
      -e ncmpcpp -c "$HOME/.config/ncmpcpp/config-classic" &
  fi
}

lyrics_toggle() {
  local pids
  pids=$(pgrep -f '[l]yrics.sh' 2>/dev/null)
  if [[ -n "$pids" ]]; then
    kill $pids 2>/dev/null
    notify_music "Lyrics" "Floating lyrics disabled."
  elif [[ -x "$LYRICS" ]]; then
    nohup "$LYRICS" open >/dev/null 2>&1 &
    notify_music "Lyrics" "Floating lyrics enabled."
  else
    notify_music "Lyrics" "Lyrics helper was not found."
  fi
}

update_database() {
  ensure_mpd || return
  if mpc update >/dev/null 2>&1; then
    notify_music "Music database" "MPD database update requested."
  else
    notify_music "Music database" "Failed to update the MPD database."
  fi
}

stop_mpd() {
  local quiet=${1:-0} pids
  pids=$(pgrep -f 'st -t statusutil_ncmpcpp' 2>/dev/null)
  [[ -n "$pids" ]] && kill $pids 2>/dev/null
  pids=$(pgrep -f '[l]yrics.sh' 2>/dev/null)
  [[ -n "$pids" ]] && kill $pids 2>/dev/null
  command -v mpc >/dev/null 2>&1 && mpc stop >/dev/null 2>&1
  pkill -x mpd 2>/dev/null || true
  [[ "$quiet" == 1 ]] || notify_music "Music" "MPD stopped."
}

restart_mpd() {
  stop_mpd 1
  if ensure_mpd; then
    notify_music "Music" "MPD restarted."
  fi
}

sync_playlists() {
  if [[ -x "$UPDATE_PLAYLIST" ]]; then
    "$UPDATE_PLAYLIST"
  else
    notify_music "Playlist sync" "Playlist updater was not found."
  fi
}

previous_track() {
  "$DEBOUNCE" "st_music_prev" 1 >/dev/null 2>&1 || return
  ensure_mpd && mpc prev >/dev/null 2>&1
}

next_track() {
  "$DEBOUNCE" "st_music_next" 1 >/dev/null 2>&1 || return
  ensure_mpd && mpc next >/dev/null 2>&1
}

music_menu() {
  command -v rofi >/dev/null 2>&1 || {
    notify_music "Music" "Rofi is not installed."
    return
  }
  local choice
  choice=$(printf '%s\n' \
    "󰐊  Play / Pause" \
    "  Open / Close player" \
    "󰓇  Toggle lyrics" \
    "󰑐  Sync playlists" \
    "󰒍  Update MPD database" \
    "󰜉  Restart MPD" \
    "󰓛  Stop MPD" |
    rofi -dmenu -i -p "Music")

  case "$choice" in
    *"Play / Pause") player_toggle ;;
    *"Open / Close player") player_window ;;
    *"Toggle lyrics") lyrics_toggle ;;
    *"Sync playlists") sync_playlists ;;
    *"Update MPD database") update_database ;;
    *"Restart MPD") restart_mpd ;;
    *"Stop MPD") stop_mpd ;;
  esac
}

if [[ -n "${BUTTON:-}" ]]; then
  case "$BUTTON" in
    1) player_toggle ;;
    2) player_window ;;
    3) music_menu ;;
    4) previous_track ;;
    5) next_track ;;
  esac
else
  case "${1:-menu}" in
    toggle|play) player_toggle ;;
    player) player_window ;;
    lyrics) lyrics_toggle ;;
    sync) sync_playlists ;;
    update) update_database ;;
    restart) restart_mpd ;;
    stop) stop_mpd ;;
    prev) previous_track ;;
    next) next_track ;;
    menu|*) music_menu ;;
  esac
fi
