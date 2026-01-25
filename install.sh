#!/usr/bin/env bash

set -euo pipefail

MODE="both"   # both|gui|text
KEY_ARG=""

usage() {
  cat <<'EOF'
Usage: ./install.sh [--gui|--text] [--key fw_api_xxx]

Installs to user-local paths:
  - Binaries:   ~/.local/bin
  - Menu entry: ~/.local/share/applications/firmware-quota.desktop
  - Icon:       ~/.local/share/icons/hicolor/48x48/apps/firmware-quota.png
  - Autostart:  ~/.config/autostart/firmware_quota.desktop (GUI installs only)
  - Env file:   ~/.config/firmware-quota/env (GUI installs only)

Defaults:
  - Builds/installs both text + GUI (if GUI dependencies are present)
  - Creates autostart entry for GUI install

Options:
  --gui         Install GUI (mixed) only
  --text        Install text only
  --key KEY     Provide FIRMWARE_API_KEY explicitly
  -h, --help    Show help
EOF
}

die() {
  echo "Error: $*" >&2
  exit 1
}

while [ $# -gt 0 ]; do
  case "$1" in
    --gui)
      MODE="gui"
      shift
      ;;
    --text)
      MODE="text"
      shift
      ;;
    --key)
      [ $# -ge 2 ] || die "--key requires a value"
      KEY_ARG="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "Unknown argument: $1"
      ;;
  esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

[ -f "Makefile" ] || die "Run from repo root (Makefile not found)"

HOME_DIR="${HOME:?HOME is not set}"

BIN_DIR="$HOME_DIR/.local/bin"
APP_DIR="$HOME_DIR/.local/share/applications"
ICON_DIR="$HOME_DIR/.local/share/icons/hicolor/48x48/apps"
STATE_DIR="$HOME_DIR/.local/share/firmware-quota"
CFG_DIR="$HOME_DIR/.config/firmware-quota"
AUTOSTART_DIR="$HOME_DIR/.config/autostart"

MANIFEST="$STATE_DIR/install-manifest.txt"

mkdir -p "$BIN_DIR" "$APP_DIR" "$ICON_DIR" "$STATE_DIR" "$CFG_DIR" "$AUTOSTART_DIR"

note_manifest=()
record() {
  note_manifest+=("$1")
}

install_file() {
  local src="$1"
  local dst="$2"
  local mode="$3"

  [ -f "$src" ] || die "Missing build artifact: $src"
  install -m "$mode" "$src" "$dst"
  record "$dst"
}

write_file() {
  local dst="$1"
  local mode="$2"
  umask 077
  cat >"$dst"
  chmod "$mode" "$dst"
  record "$dst"
}

extract_key_from_file() {
  local f="$1"
  [ -f "$f" ] || return 1

  local line
  while IFS= read -r line || [ -n "$line" ]; do
    # Strip trailing comments (simple best-effort)
    line="${line%%#*}"
    local re_dq='^[[:space:]]*(export[[:space:]]+)?FIRMWARE_API_KEY[[:space:]]*=[[:space:]]*"([^"]+)"[[:space:]]*$'
    local re_sq='^[[:space:]]*(export[[:space:]]+)?FIRMWARE_API_KEY[[:space:]]*=[[:space:]]*\x27([^\x27]+)\x27[[:space:]]*$'
    local re_bare='^[[:space:]]*(export[[:space:]]+)?FIRMWARE_API_KEY[[:space:]]*=[[:space:]]*([^[:space:]]+)[[:space:]]*$'

    if [[ "$line" =~ $re_dq ]]; then
      echo "${BASH_REMATCH[2]}"; return 0
    fi
    if [[ "$line" =~ $re_sq ]]; then
      echo "${BASH_REMATCH[2]}"; return 0
    fi
    if [[ "$line" =~ $re_bare ]]; then
      echo "${BASH_REMATCH[2]}"; return 0
    fi
  done <"$f"

  return 1
}

discover_key() {
  local env_file="$CFG_DIR/env"

  if [ -f "$env_file" ]; then
    local k
    k="$(extract_key_from_file "$env_file" || true)"
    if [ -n "$k" ]; then
      echo "$k"
      return 0
    fi
  fi

  if [ -n "${KEY_ARG}" ]; then
    echo "$KEY_ARG"
    return 0
  fi

  if [ -n "${FIRMWARE_API_KEY:-}" ]; then
    echo "$FIRMWARE_API_KEY"
    return 0
  fi

  local candidates=(
    "$HOME_DIR/.bashrc"
    "$HOME_DIR/.profile"
    "$HOME_DIR/.xprofile"
    "$HOME_DIR/.zshrc"
  )

  local cf
  for cf in "${candidates[@]}"; do
    local k
    k="$(extract_key_from_file "$cf" || true)"
    if [ -n "$k" ]; then
      echo "$k"
      return 0
    fi
  done

  # environment.d files
  if [ -d "$HOME_DIR/.config/environment.d" ]; then
    local envd
    for envd in "$HOME_DIR/.config/environment.d"/*.conf; do
      [ -f "$envd" ] || continue
      local k
      k="$(extract_key_from_file "$envd" || true)"
      if [ -n "$k" ]; then
        echo "$k"
        return 0
      fi
    done
  fi

  return 1
}

build_text() {
  make text
}

build_gui() {
  make mixed-gui
}

install_text() {
  install_file "$SCRIPT_DIR/show_quota_text" "$BIN_DIR/show_quota_text" 0755

  write_file "$BIN_DIR/firmware-quota-text" 0755 <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
BIN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$BIN_DIR/show_quota_text" "$@"
EOF
}

install_gui() {
  install_file "$SCRIPT_DIR/show_quota" "$BIN_DIR/show_quota" 0755

  # Tray icon: the app expects firmware-icon.{svg,png} next to the executable.
  if [ -f "$SCRIPT_DIR/firmware-icon.svg" ]; then
    install_file "$SCRIPT_DIR/firmware-icon.svg" "$BIN_DIR/firmware-icon.svg" 0644
  fi

  # Icon
  if [ -f "$SCRIPT_DIR/firmware-icon.png" ]; then
    install_file "$SCRIPT_DIR/firmware-icon.png" "$ICON_DIR/firmware-quota.png" 0644
  fi

  local key
  key="$(discover_key || true)"
  [ -n "$key" ] || die "FIRMWARE_API_KEY not found. Set it in your desktop session (e.g. ~/.profile) or re-run with: ./install.sh --gui --key fw_api_xxx"

  # Private env file (0600)
  umask 077
  {
    echo "# Managed by firmware-quota installer"
    echo "FIRMWARE_API_KEY=$key"
  } >"$CFG_DIR/env"
  chmod 0600 "$CFG_DIR/env"
  record "$CFG_DIR/env"

  # GUI launcher script
  write_file "$BIN_DIR/firmware-quota-gui" 0755 <<EOF
#!/usr/bin/env bash
set -euo pipefail
BIN_DIR="\$(cd "\$(dirname "\${BASH_SOURCE[0]}")" && pwd)"
ENV_FILE="$CFG_DIR/env"

if [ ! -f "$CFG_DIR/env" ]; then
  echo "Error: missing env file: $CFG_DIR/env" >&2
  exit 1
fi

set -a
. "$CFG_DIR/env"
set +a

exec "$BIN_DIR/show_quota" --gui
EOF

  # Menu entry
  write_file "$APP_DIR/firmware-quota.desktop" 0644 <<EOF
[Desktop Entry]
Type=Application
Name=Firmware Quota
Comment=Firmware API Quota Monitor
Exec=$BIN_DIR/firmware-quota-gui
Icon=$ICON_DIR/firmware-quota.png
Terminal=false
Categories=Utility;
EOF

  # Autostart entry
  write_file "$AUTOSTART_DIR/firmware_quota.desktop" 0644 <<EOF
[Desktop Entry]
Type=Application
Name=Firmware Quota
Comment=Firmware API Quota Monitor
Exec=$BIN_DIR/firmware-quota-gui
Icon=$ICON_DIR/firmware-quota.png
Terminal=false
X-GNOME-Autostart-enabled=true
EOF
}

rm -f "$MANIFEST"

case "$MODE" in
  both)
    build_text
    install_text
    # GUI is optional; attempt but do not fail the whole install if GUI deps missing.
    if build_gui; then
      install_gui
    else
      echo "Note: GUI build failed (missing GTK deps?). Installed text only." >&2
    fi
    ;;
  text)
    build_text
    install_text
    ;;
  gui)
    build_gui
    install_gui
    ;;
  *)
    die "Internal error: unknown MODE=$MODE"
    ;;
esac

# Write manifest last.
umask 077
{
  echo "# firmware-quota install manifest"
  echo "# created: $(date -Iseconds 2>/dev/null || date)"
  for p in "${note_manifest[@]}"; do
    echo "$p"
  done
} >"$MANIFEST"
chmod 0600 "$MANIFEST"

echo "Installed."
echo "- Binaries: $BIN_DIR"
if [ "$MODE" != "text" ]; then
  echo "- Menu:     $APP_DIR/firmware-quota.desktop"
  echo "- Autostart: $AUTOSTART_DIR/firmware_quota.desktop"
fi
echo "- Uninstall: ./uninstall.sh"
