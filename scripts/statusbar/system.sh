#!/usr/bin/env bash
# Click action for the CPU / memory status block.

show_summary() {
  local host kernel uptime_text cpu memory root_disk
  host=$(hostname 2>/dev/null)
  kernel=$(uname -r)
  uptime_text=$(uptime -p 2>/dev/null | sed 's/^up //')
  cpu=$(awk -F: '/model name/ { gsub(/^[ \t]+/, "", $2); print $2; exit }' /proc/cpuinfo)
  memory=$(free -h 2>/dev/null | awk '/^Mem:/ { print $3 " / " $2 }')
  root_disk=$(df -h / 2>/dev/null | awk 'NR == 2 { print $3 " / " $2 " (" $5 ")" }')

  notify-send -r 9527 "  ${host:-System}" \
    "<b>CPU</b>  ${cpu:-unknown}\n<b>Memory</b>  ${memory:-unknown}\n<b>Disk</b>  ${root_disk:-unknown}\n<b>Kernel</b>  $kernel\n<b>Uptime</b>  ${uptime_text:-unknown}"
}

case "${BUTTON:-1}" in
  3)
    if command -v btop >/dev/null 2>&1; then
      st -c float -e btop
    elif command -v htop >/dev/null 2>&1; then
      st -c float -e htop
    else
      show_summary
    fi
    ;;
  *) show_summary ;;
esac
