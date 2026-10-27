#!/usr/bin/env bash
# Compatibility entry for dwm's clickable status module and existing bindings.
# Every operation goes through the same session-local audio controller.
audioctl="$(cd -- "$(dirname -- "$0")/../utils/sound" && pwd)/audioctl"

if [[ -n "${BUTTON:-}" ]]; then
  case "$BUTTON" in
    1) exec "$audioctl" mute --pointer ;;
    2) exec "$audioctl" panel apps ;;
    3) exec "$audioctl" panel devices ;;
    4) exec "$audioctl" adjust +1 --pointer ;;
    5) exec "$audioctl" adjust -1 --pointer ;;
    *) exit 0 ;;
  esac
fi

case "${1:-}" in
  change|device)
    delta="${2:-0}"
    scope=()
    [[ "$1" == device ]] && scope=(--device)
    if [[ "$delta" == 0 ]]; then
      exec "$audioctl" mute "${scope[@]}"
    fi
    exec "$audioctl" adjust "$delta" "${scope[@]}"
    ;;
  apps|sinks) exec "$audioctl" panel apps ;;
  devices|settings) exec "$audioctl" panel devices ;;
  *) exec "$audioctl" "$@" ;;
esac
