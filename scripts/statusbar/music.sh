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
  notify-send -r "$NOTIFY_ID" -t "${3:-2500}" "${1:-音乐}" "${2:-}" -i audio-x-generic
}

ensure_mpd() {
  command -v mpc >/dev/null 2>&1 || {
    notify_music "音乐" "未安装 mpc。"
    return 1
  }
  if pgrep -x mpd >/dev/null 2>&1; then
    if mpc status >/dev/null 2>&1; then
      return 0
    fi
    notify_music "音乐" "MPD 正在运行，但无法连接。"
    return 1
  fi
  command -v mpd >/dev/null 2>&1 || {
    notify_music "音乐" "未安装 mpd。"
    return 1
  }
  mpd >/dev/null 2>&1 || {
    notify_music "音乐" "MPD 启动失败。"
    return 1
  }
  for _ in {1..20}; do
    mpc status >/dev/null 2>&1 && return 0
    sleep 0.1
  done
  notify_music "音乐" "MPD 启动超时。"
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
    notify_music "音乐" "未安装 ncmpcpp。"
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
    notify_music "歌词" "悬浮歌词已关闭。"
  elif [[ -x "$LYRICS" ]]; then
    nohup "$LYRICS" open >/dev/null 2>&1 &
    notify_music "歌词" "悬浮歌词已开启。"
  else
    notify_music "歌词" "未找到歌词脚本。"
  fi
}

update_database() {
  ensure_mpd || return
  if mpc update >/dev/null 2>&1; then
    notify_music "音乐数据库" "已请求更新 MPD 数据库。"
  else
    notify_music "音乐数据库" "MPD 数据库更新失败。"
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
  [[ "$quiet" == 1 ]] || notify_music "音乐" "MPD 已停止。"
}

restart_mpd() {
  stop_mpd 1
  if ensure_mpd; then
    notify_music "音乐" "MPD 已重启。"
  fi
}

sync_playlists() {
  if [[ -x "$UPDATE_PLAYLIST" ]]; then
    "$UPDATE_PLAYLIST"
  else
    notify_music "歌单同步" "未找到歌单更新脚本。"
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
    notify_music "音乐" "未安装 Rofi。"
    return
  }
  local choice
  choice=$(printf '%s\n' \
    "󰐊  播放 / 暂停" \
    "  打开 / 关闭播放器" \
    "󰓇  开关悬浮歌词" \
    "󰑐  同步歌单" \
    "󰒍  更新 MPD 数据库" \
    "󰜉  重启 MPD" \
    "󰓛  停止 MPD" |
    rofi -dmenu -i -p "音乐")

  case "$choice" in
    *"播放 / 暂停") player_toggle ;;
    *"打开 / 关闭播放器") player_window ;;
    *"开关悬浮歌词") lyrics_toggle ;;
    *"同步歌单") sync_playlists ;;
    *"更新 MPD 数据库") update_database ;;
    *"重启 MPD") restart_mpd ;;
    *"停止 MPD") stop_mpd ;;
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
