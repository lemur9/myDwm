#!/usr/bin/env bash
# Download configured NetEase playlists, update MPD once, and report results.

MUSIC_DIR="${MUSIC_DIR:-$HOME/Music}"
CACHE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/mydwm"
RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp}"
[[ -d "$RUNTIME_DIR" && -w "$RUNTIME_DIR" ]] || RUNTIME_DIR=/tmp
LOCK_FILE="$RUNTIME_DIR/mydwm-playlist-sync.lock"
SUCCESS_STAMP="$CACHE_DIR/playlist-sync.success"
LOG_FILE="$CACHE_DIR/music-sync.log"
NOTIFY_ID=9530
COOLDOWN=${MUSIC_SYNC_COOLDOWN:-3600}
force=0
[[ "${1:-}" == "--force" ]] && force=1
lock_dir=""
hot_backup=""

notify_sync() {
  command -v notify-send >/dev/null 2>&1 || return
  notify-send -r "$NOTIFY_ID" -t "${3:-5000}" "${1:-Playlist sync}" "${2:-}" -i folder-music
}

cleanup() {
  if [[ -n "$hot_backup" && -d "$hot_backup" && ! -d "$MUSIC_DIR/热歌榜" ]]; then
    mv "$hot_backup" "$MUSIC_DIR/热歌榜" 2>/dev/null || true
  fi
  hot_backup=""
  [[ -n "$lock_dir" ]] && rmdir "$lock_dir" 2>/dev/null || true
  lock_dir=""
}

abort_sync() {
  trap - INT TERM HUP EXIT
  cleanup
  exit 130
}
trap abort_sync INT TERM HUP
trap cleanup EXIT

mkdir -p "$CACHE_DIR" "$MUSIC_DIR"

if command -v flock >/dev/null 2>&1; then
  exec 9>"$LOCK_FILE"
  if ! flock -n 9; then
    notify_sync "Playlist sync" "Another sync is already running."
    exit 1
  fi
else
  lock_dir="$LOCK_FILE.d"
  if ! mkdir "$lock_dir" 2>/dev/null; then
    notify_sync "Playlist sync" "Another sync is already running."
    exit 1
  fi
fi

if (( ! force )) && [[ -f "$SUCCESS_STAMP" ]]; then
  now=$(date +%s)
  last=$(stat -c %Y "$SUCCESS_STAMP" 2>/dev/null || printf '0')
  age=$((now - last))
  if (( age < COOLDOWN )); then
    remaining=$(((COOLDOWN - age + 59) / 60))
    notify_sync "Playlist sync" "The last sync succeeded recently. Try again in ${remaining} min."
    exit 0
  fi
fi

if ! command -v yun >/dev/null 2>&1; then
  notify_sync "Playlist sync failed" "The 'yun' downloader is not installed." 8000
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

names=("Hot chart" "Folk collection" "Personal playlist")
urls=(
  "https://music.163.com/#/playlist?id=3778678"
  "https://music.163.com/#/playlist?id=919939187"
  "https://music.163.com/#/my/m/music/playlist?id=2656984788"
)
results=()
success_count=0

cd "$MUSIC_DIR" || {
  notify_sync "Playlist sync failed" "Cannot access $MUSIC_DIR" 8000
  exit 1
}

for index in "${!urls[@]}"; do
  step=$((index + 1))
  name=${names[index]}
  notify_sync "Playlist sync" "(${step}/${#urls[@]}) Syncing ${name}…" 0
  printf '== %s ==\n' "$name" >> "$LOG_FILE"

  # Keep the previous hot chart until its replacement has downloaded.
  if (( index == 0 )) && [[ -d "$MUSIC_DIR/热歌榜" ]]; then
    hot_backup="$MUSIC_DIR/.热歌榜.backup.$$"
    rm -rf "$hot_backup"
    mv "$MUSIC_DIR/热歌榜" "$hot_backup"
  fi

  if yun "${urls[index]}" -c 10 -p false -q 192 -s >> "$LOG_FILE" 2>&1; then
    if (( index != 0 )) || [[ -d "$MUSIC_DIR/热歌榜" ]]; then
      results+=("✓ ${name}")
      success_count=$((success_count + 1))
      if (( index == 0 )) && [[ -n "$hot_backup" ]]; then
        rm -rf "$hot_backup"
        hot_backup=""
      fi
    else
      results+=("✗ ${name} (no output directory)")
      [[ -n "$hot_backup" && -d "$hot_backup" ]] && mv "$hot_backup" "$MUSIC_DIR/热歌榜"
      hot_backup=""
    fi
  else
    results+=("✗ ${name}")
    if (( index == 0 )); then
      rm -rf "$MUSIC_DIR/热歌榜"
      if [[ -n "$hot_backup" && -d "$hot_backup" ]]; then
        mv "$hot_backup" "$MUSIC_DIR/热歌榜"
      fi
      hot_backup=""
    fi
  fi
  printf '\n' >> "$LOG_FILE"
done

after_count=$(count_audio)
delta=$((after_count - before_count))
database_result="Unavailable"
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
    database_result="Updated"
    database_ok=1
  else
    database_result="Failed"
  fi
fi

elapsed=$(($(date +%s) - start_time))
result_lines=$(printf '%s\n' "${results[@]}")
if (( delta >= 0 )); then
  delta_text="+${delta}"
else
  delta_text="$delta"
fi

printf -v body '%s\n\nLibrary  %s → %s (%s)\nMPD database  %s\nElapsed  %ss\nLog  %s' \
  "$result_lines" "$before_count" "$after_count" "$delta_text" \
  "$database_result" "$elapsed" "$LOG_FILE"

if (( success_count == ${#urls[@]} && database_ok )); then
  touch "$SUCCESS_STAMP"
  notify_sync "Playlist sync complete" "$body" 9000
  printf 'Result: success\n' >> "$LOG_FILE"
  exit 0
else
  notify_sync "Playlist sync finished with errors" "$body" 0
  printf 'Result: partial failure\n' >> "$LOG_FILE"
  exit 1
fi
