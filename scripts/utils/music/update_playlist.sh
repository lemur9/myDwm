#!/usr/bin/env bash
# 根据 playlists.conf 同步网易云歌单并更新 MPD 数据库。

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PLAYLIST_CONFIG="${PLAYLIST_CONFIG:-$SCRIPT_DIR/playlists.conf}"
MUSIC_DIR="${MUSIC_DIR:-$HOME/Music}"
LOCK_FILE="/tmp/dwm-playlist-sync-${UID}.lock"
SUCCESS_STAMP="/tmp/dwm-playlist-sync-${UID}.stamp"
LOG_FILE="/tmp/dwm-music-sync-${UID}.log"
NOTIFY_ID=9530
COOLDOWN=${MUSIC_SYNC_COOLDOWN:-3600}
force=0
[[ "${1:-}" == "--force" ]] && force=1
backup_path=""
backup_target=""

notify_sync() {
  command -v notify-send >/dev/null 2>&1 || return
  notify-send -r "$NOTIFY_ID" -t "${3:-5000}" "${1:-歌单同步}" "${2:-}" -i folder-music
}

cleanup() {
  if [[ -n "$backup_path" && -d "$backup_path" ]]; then
    rm -rf "$backup_target"
    mv "$backup_path" "$backup_target" 2>/dev/null || true
  fi
  backup_path=""
  backup_target=""
}

abort_sync() {
  trap - INT TERM HUP EXIT
  cleanup
  exit 130
}
trap abort_sync INT TERM HUP
trap cleanup EXIT

mkdir -p "$MUSIC_DIR"

exec 9>"$LOCK_FILE"
if ! flock -n 9; then
  notify_sync "歌单同步" "同步进行中"
  exit 1
fi

if (( ! force )) && [[ -f "$SUCCESS_STAMP" ]]; then
  now=$(date +%s)
  last=$(stat -c %Y "$SUCCESS_STAMP" 2>/dev/null || printf '0')
  if (( now - last < COOLDOWN )); then
    notify_sync "歌单同步" "一小时内已同步"
    exit 0
  fi
fi

if [[ ! -f "$PLAYLIST_CONFIG" ]]; then
  notify_sync "同步失败" "未找到 playlists.conf" 8000
  exit 1
fi

mapfile -t playlists < <(
  sed -e '/^[[:space:]]*#/d' -e '/^[[:space:]]*$/d' "$PLAYLIST_CONFIG"
)

if (( ${#playlists[@]} == 0 )); then
  notify_sync "同步失败" "playlists.conf 没有歌单" 8000
  exit 1
fi

for item in "${playlists[@]}"; do
  IFS='|' read -r name url directory strategy extra <<< "$item"
  if [[ -z "$name" || -z "$url" || -z "$directory" || -n "$extra" ||
        "$directory" == */* || "$directory" == *..* ||
        ( "$strategy" != merge && "$strategy" != replace ) ]]; then
    notify_sync "同步失败" "playlists.conf 配置错误：${name:-未知歌单}" 8000
    exit 1
  fi
done

if ! command -v yun >/dev/null 2>&1; then
  notify_sync "同步失败" "未安装 yun" 8000
  exit 1
fi

count_audio() {
  find "$MUSIC_DIR" -type f \( \
    -iname '*.mp3' -o -iname '*.flac' -o -iname '*.ogg' -o \
    -iname '*.m4a' -o -iname '*.opus' -o -iname '*.wav' \
  \) -printf . 2>/dev/null | wc -c
}

start_time=$(date +%s)
before_count=$(count_audio)
: > "$LOG_FILE"
printf 'Playlist sync started: %s\nMusic directory: %s\n\n' \
  "$(date --iso-8601=seconds)" "$MUSIC_DIR" >> "$LOG_FILE"

results=()
success_count=0
total=${#playlists[@]}

cd "$MUSIC_DIR" || {
  notify_sync "同步失败" "无法访问音乐目录" 8000
  exit 1
}

for index in "${!playlists[@]}"; do
  IFS='|' read -r name url directory strategy <<< "${playlists[index]}"
  step=$((index + 1))
  target="$MUSIC_DIR/$directory"
  notify_sync "歌单同步" "${step}/${total} ${name}" 0
  printf '== %s ==\n' "$name" >> "$LOG_FILE"

  if [[ "$strategy" == replace && -d "$target" ]]; then
    backup_path="$MUSIC_DIR/.${directory}.backup.$$"
    backup_target="$target"
    rm -rf "$backup_path"
    mv "$target" "$backup_path"
  fi

  if yun "$url" -c 10 -p false -q 192 -s --skip-trial \
      -f "${directory}/:singer - :songName.:ext" >> "$LOG_FILE" 2>&1 &&
      { [[ "$strategy" == merge ]] || [[ -d "$target" ]]; }; then
    results+=("✓ ${name}")
    success_count=$((success_count + 1))
    if [[ -n "$backup_path" ]]; then
      rm -rf "$backup_path"
      backup_path=""
      backup_target=""
    fi
  else
    results+=("✗ ${name}")
    if [[ "$strategy" == replace ]]; then
      rm -rf "$target"
      if [[ -n "$backup_path" && -d "$backup_path" ]]; then
        mv "$backup_path" "$target"
      fi
      backup_path=""
      backup_target=""
    fi
  fi
  printf '\n' >> "$LOG_FILE"
done

after_count=$(count_audio)
delta=$((after_count - before_count))
database_result="不可用"
database_ok=0

if command -v mpc >/dev/null 2>&1; then
  if ! pgrep -x mpd >/dev/null 2>&1 && command -v mpd >/dev/null 2>&1; then
    mpd >> "$LOG_FILE" 2>&1 || true
    for _ in {1..20}; do
      mpc status >/dev/null 2>&1 && break
      sleep 0.1
    done
  fi
  if mpc update --wait >> "$LOG_FILE" 2>&1 || mpc update >> "$LOG_FILE" 2>&1; then
    database_result="已更新"
    database_ok=1
  else
    database_result="失败"
  fi
fi

elapsed=$(($(date +%s) - start_time))
result_lines=$(printf '%s\n' "${results[@]}")
if (( delta >= 0 )); then
  delta_text="+${delta}"
else
  delta_text="$delta"
fi

printf -v body '%s\n\n曲目 %s → %s (%s)\n数据库 %s\n耗时 %s 秒' \
  "$result_lines" "$before_count" "$after_count" "$delta_text" \
  "$database_result" "$elapsed"

if (( success_count == total && database_ok )); then
  printf 'Result: success\n' >> "$LOG_FILE"
  touch "$SUCCESS_STAMP"
  notify_sync "同步完成" "$body" 9000
  rm -f "$LOG_FILE"
  exit 0
else
  printf 'Result: partial failure\n' >> "$LOG_FILE"
  printf -v body '%s\n日志 %s' "$body" "$LOG_FILE"
  notify_sync "同步失败" "$body" 0
  exit 1
fi
