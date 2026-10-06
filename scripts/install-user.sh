#!/usr/bin/env bash
set -euo pipefail
xtw_source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cmake --install "$xtw_source_dir/build" --prefix "$HOME/.local"
# Desktop launchers do not always inherit the interactive shell's PATH.
sed "s|^Exec=xtw5-linux$|Exec=$HOME/.local/bin/xtw5-linux|" \
  "$xtw_source_dir/resources/xtw5-linux.desktop" > "$HOME/.local/share/applications/xtw5-linux.desktop"
if command -v update-desktop-database >/dev/null; then
  update-desktop-database "$HOME/.local/share/applications"
fi
printf 'Installed: %s\n' "$HOME/.local/bin/xtw5-linux"
