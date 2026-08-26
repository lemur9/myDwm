#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
config_home="${XDG_CONFIG_HOME:-$HOME/.config}"

install_config() {
  local component=$1 source=$2 target=$3
  mkdir -p "$(dirname "$target")"
  if [[ -e "$target" && ! -L "$target" ]]; then
    cp -a "$target" "$target.backup.$(date +%Y%m%d-%H%M%S)"
    printf 'Backed up %s\n' "$target"
  fi
  cp "$source" "$target"
  printf 'Installed %-6s -> %s\n' "$component" "$target"
}

install_config picom "$repo_dir/rice/picom/picom.conf" "$config_home/picom/picom.conf"
install_config rofi  "$repo_dir/rice/rofi/config.rasi"   "$config_home/rofi/config.rasi"
install_config dunst "$repo_dir/rice/dunst/dunstrc"      "$config_home/dunst/dunstrc"

cat <<EOF

Theme configs installed.

Next steps:
  1. Merge rice/st/theme.h into your st source config.h and rebuild st.
  2. In this repo's config.h, make workspace point to this checkout:
       $repo_dir
  3. Rebuild/reinstall dwm.
  4. Stop old instances before testing: pkill picom; pkill dunst; pkill slstatus
  5. Restart dwm. The bundled autostart script launches Picom, Dunst and status.sh.

See: $repo_dir/rice/README.md
EOF
