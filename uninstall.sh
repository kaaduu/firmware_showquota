#!/usr/bin/env bash

set -euo pipefail

usage() {
  cat <<'EOF'
Usage: ./uninstall.sh

Removes files installed by ./install.sh (user-local install).
Also purges user data:
  - ~/.firmware_quota_gui.conf
  - ~/show_quota.log
EOF
}

die() {
  echo "Error: $*" >&2
  exit 1
}

case "${1:-}" in
  "" ) ;;
  -h|--help) usage; exit 0 ;;
  *) die "Unknown argument: $1" ;;
esac

HOME_DIR="${HOME:?HOME is not set}"
MANIFEST="$HOME_DIR/.local/share/firmware-quota/install-manifest.txt"

if [ ! -f "$MANIFEST" ]; then
  die "Manifest not found: $MANIFEST (nothing to uninstall, or it was installed differently)"
fi

paths=()
while IFS= read -r line || [ -n "$line" ]; do
  [[ "$line" =~ ^# ]] && continue
  [ -n "$line" ] || continue
  paths+=("$line")
done <"$MANIFEST"

for p in "${paths[@]}"; do
  if [ -e "$p" ] || [ -L "$p" ]; then
    rm -f "$p" || true
  fi
done

# Remove manifest itself.
rm -f "$MANIFEST" || true

# Purge user data (requested).
rm -f "$HOME_DIR/.firmware_quota_gui.conf" || true
rm -f "$HOME_DIR/show_quota.log" || true

# Best-effort cleanup of empty dirs created by installer.
rmdir "$HOME_DIR/.local/share/firmware-quota" 2>/dev/null || true
rmdir "$HOME_DIR/.config/firmware-quota" 2>/dev/null || true
rmdir "$HOME_DIR/.local/share/icons/hicolor/48x48/apps" 2>/dev/null || true
rmdir "$HOME_DIR/.local/share/icons/hicolor/48x48" 2>/dev/null || true
rmdir "$HOME_DIR/.local/share/icons/hicolor" 2>/dev/null || true
rmdir "$HOME_DIR/.local/share/icons" 2>/dev/null || true

echo "Uninstalled (and purged user data)."
