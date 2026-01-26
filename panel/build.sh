#!/usr/bin/env bash

set -euo pipefail

die() {
  echo "Error: $*" >&2
  exit 1
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "Missing command: $1"
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

need_cmd make
need_cmd g++
need_cmd pkg-config

if ! pkg-config --exists libmatepanelapplet-4.0 2>/dev/null; then
  cat >&2 <<'EOF'
Error: Missing pkg-config module: libmatepanelapplet-4.0

This means the MATE panel applet development files are not installed.

Install the MATE panel applet dev package (examples):
  - Debian/Ubuntu:  sudo apt-get install libmate-panel-applet-dev
  - Fedora:         sudo dnf install mate-panel-applet-devel
  - Arch:           sudo pacman -S mate-panel-applet
  - openSUSE:       sudo zypper install mate-panel-applet-devel

Also ensure pkg-config is installed:
  - Debian/Ubuntu:  sudo apt-get install pkg-config

If you installed the package but pkg-config still can't find it, check PKG_CONFIG_PATH.
EOF
  exit 1
fi

cd "$SCRIPT_DIR"
make

echo "Built: firmware-quota-applet"
