#!/usr/bin/env bash
# Click action for the memory status block.

human_kib() {
  awk -v kib="${1:-0}" 'BEGIN {
    if (kib >= 1048576)      printf "%.1f GiB", kib / 1048576;
    else if (kib >= 1024)    printf "%.1f MiB", kib / 1024;
    else                     printf "%d KiB", kib;
  }'
}

show_summary() {
  local host kernel uptime_text cpu
  local mem_total mem_available mem_used
  local disk_total disk_used disk_available disk_percent
  local memory_text disk_text body

  host=$(hostname 2>/dev/null)
  kernel=$(uname -r 2>/dev/null)
  uptime_text=$(LC_ALL=C uptime -p 2>/dev/null | sed 's/^up //')
  cpu=$(LC_ALL=C lscpu 2>/dev/null |
    awk -F: '/^Model name:/ { sub(/^[ \t]+/, "", $2); print $2; exit }')
  [[ -n "$cpu" ]] || cpu=$(awk -F: '
    /^(model name|Hardware|Model)/ {
      sub(/^[ \t]+/, "", $2); print $2; exit
    }
  ' /proc/cpuinfo 2>/dev/null)

  read -r mem_total mem_available < <(awk '
    /^MemTotal:/     { total=$2 }
    /^MemAvailable:/ { available=$2 }
    END { print total+0, available+0 }
  ' /proc/meminfo)
  mem_used=$((mem_total - mem_available))
  memory_text="Used $(human_kib "$mem_used") · Available $(human_kib "$mem_available") · Total $(human_kib "$mem_total")"

  read -r disk_total disk_used disk_available disk_percent < <(
    LC_ALL=C df -Pk / 2>/dev/null | awk 'NR == 2 { print $2, $3, $4, $5 }'
  )
  if [[ -n "$disk_total" ]]; then
    disk_text="Used $(human_kib "$disk_used") · Free $(human_kib "$disk_available") · Total $(human_kib "$disk_total") · ${disk_percent} used"
  else
    disk_text="Unavailable"
  fi

  printf -v body '<b>Kernel</b>  %s\n<b>CPU</b>  %s\n<b>Memory</b>  %s\n<b>Disk</b>  %s\n<b>Uptime</b>  %s' \
    "${kernel:-unknown}" "${cpu:-unknown}" "$memory_text" "$disk_text" \
    "${uptime_text:-unknown}"
  notify-send -r 9527 "  ${host:-System}" "$body"
}

toggle_system_monitor() {
  local pids monitor
  pids=$(pgrep -f '(^|/)[s]t -t statusutil_system_monitor( |$)' 2>/dev/null)
  if [[ -n "$pids" ]]; then
    while IFS= read -r pid; do
      [[ "$pid" =~ ^[0-9]+$ ]] && kill "$pid" 2>/dev/null
    done <<< "$pids"
    return
  fi

  if command -v btop >/dev/null 2>&1; then
    monitor=btop
  elif command -v htop >/dev/null 2>&1; then
    monitor=htop
  else
    show_summary
    return
  fi

  # A stable title/instance makes the next right click find this exact window.
  st -t statusutil_system_monitor -n statusutil_system_monitor \
    -c float -e "$monitor" &
}

case "${BUTTON:-1}" in
  3) toggle_system_monitor ;;
  *) show_summary ;;
esac
