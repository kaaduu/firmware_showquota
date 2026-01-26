#!/usr/bin/env bash

set -euo pipefail

usage() {
  cat <<'EOF'
Usage: packaging/deb/build.sh [--out DIR] [--version VERSION] [--arch ARCH]
                              [--no-gui]

Builds Ubuntu/Linux Mint compatible .deb packages using dpkg-deb.

Outputs:
  - firmware-quota_<version>_<arch>.deb

Notes:
  - This script builds binaries from the repo first.
  - GUI package contents are included only if the mixed binary was built with GUI support.
  - The panel applet has its own packager in: panel/packaging/deb/build.sh
EOF
}

die() {
  echo "Error: $*" >&2
  exit 1
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "Missing required command: $1"
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

OUT_DIR="$REPO_DIR/dist"
VERSION=""
ARCH=""
WITH_GUI=1

while [ $# -gt 0 ]; do
  case "$1" in
    --out)
      [ $# -ge 2 ] || die "--out requires a value"
      OUT_DIR="$2"
      shift 2
      ;;
    --version)
      [ $# -ge 2 ] || die "--version requires a value"
      VERSION="$2"
      shift 2
      ;;
    --arch)
      [ $# -ge 2 ] || die "--arch requires a value"
      ARCH="$2"
      shift 2
      ;;
    --no-gui)
      WITH_GUI=0
      shift
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

need_cmd dpkg-deb
need_cmd dpkg
need_cmd make
need_cmd mktemp

if [ -z "$ARCH" ]; then
  ARCH="$(dpkg --print-architecture)"
fi

sanitize_version() {
  # Deb version must be [A-Za-z0-9.+:~ -] and not contain spaces.
  # Replace '/' with '.', and spaces with '.'
  local v="$1"
  v="${v//\//.}"
  v="${v// /\.}"
  echo "$v"
}

detect_version() {
  if [ -n "$VERSION" ]; then
    echo "$(sanitize_version "$VERSION")"
    return 0
  fi

  if command -v git >/dev/null 2>&1 && [ -d "$REPO_DIR/.git" ]; then
    local desc
    desc="$(git -C "$REPO_DIR" describe --tags --always --dirty 2>/dev/null || true)"
    if [ -n "$desc" ]; then
      # Strip leading 'v' (common tag convention)
      desc="${desc#v}"
      echo "$(sanitize_version "$desc")"
      return 0
    fi
  fi

  date +%Y.%m.%d.%H%M%S
}

VERSION="$(detect_version)"

WORK_DIR="$(mktemp -d -t firmware-quota-deb.XXXXXX)"
cleanup() {
  rm -rf "$WORK_DIR"
}
trap cleanup EXIT

mkdir -p "$OUT_DIR"

echo "Building binaries..."
make -C "$REPO_DIR" text
make -C "$REPO_DIR" mixed

HAVE_MIXED_GUI=0
if [ $WITH_GUI -eq 1 ]; then
  # Detect GUI support by checking linked GTK library.
  # This is more robust than grepping --help (the mixed binary still parses --gui even when GUI is not compiled).
  if command -v ldd >/dev/null 2>&1 && ldd "$REPO_DIR/show_quota" 2>/dev/null | grep -q "libgtk-3"; then
    HAVE_MIXED_GUI=1
  fi
fi

# If we don't have GUI, avoid installing tray icons.
INSTALL_TRAY_ICON=$HAVE_MIXED_GUI

echo "Staging firmware-quota package..."
PKG_ROOT="$WORK_DIR/firmware-quota"
mkdir -p \
  "$PKG_ROOT/DEBIAN" \
  "$PKG_ROOT/usr/bin" \
  "$PKG_ROOT/usr/lib/firmware-quota" \
  "$PKG_ROOT/usr/share/icons/hicolor/scalable/apps" \
  "$PKG_ROOT/usr/share/applications" \
  "$PKG_ROOT/usr/share/doc/firmware-quota"

install -m 0755 "$REPO_DIR/show_quota_text" "$PKG_ROOT/usr/bin/show_quota_text"
install -m 0755 "$REPO_DIR/show_quota" "$PKG_ROOT/usr/lib/firmware-quota/show_quota"

# Wrapper helper (keeps the repo wrapper behavior)
if [ -f "$REPO_DIR/show_quota_wrapper.sh" ]; then
  install -m 0755 "$REPO_DIR/show_quota_wrapper.sh" "$PKG_ROOT/usr/bin/show_quota_wrapper"
fi

# Convenience commands (PATH)
ln -sf ../lib/firmware-quota/show_quota "$PKG_ROOT/usr/bin/show_quota"
ln -sf show_quota "$PKG_ROOT/usr/bin/firmware-quota"

# Tray icon (GUI expects firmware-icon.{svg,png} next to the mixed executable)
if [ $INSTALL_TRAY_ICON -eq 1 ]; then
  if [ -f "$REPO_DIR/firmware-icon.svg" ]; then
    install -m 0644 "$REPO_DIR/firmware-icon.svg" "$PKG_ROOT/usr/lib/firmware-quota/firmware-icon.svg"
  elif [ -f "$REPO_DIR/panel/firmware-quota.svg" ]; then
    install -m 0644 "$REPO_DIR/panel/firmware-quota.svg" "$PKG_ROOT/usr/lib/firmware-quota/firmware-icon.svg"
  fi
  if [ -f "$REPO_DIR/firmware-icon.png" ]; then
    install -m 0644 "$REPO_DIR/firmware-icon.png" "$PKG_ROOT/usr/lib/firmware-quota/firmware-icon.png"
  fi
fi

# Desktop menu icon (for app menu)
if [ -f "$REPO_DIR/panel/firmware-quota.svg" ]; then
  install -m 0644 "$REPO_DIR/panel/firmware-quota.svg" "$PKG_ROOT/usr/share/icons/hicolor/scalable/apps/firmware-quota.svg"
elif [ -f "$REPO_DIR/firmware-icon.svg" ]; then
  install -m 0644 "$REPO_DIR/firmware-icon.svg" "$PKG_ROOT/usr/share/icons/hicolor/scalable/apps/firmware-quota.svg"
fi

# Desktop entry (only if GUI support is compiled in)
if [ $HAVE_MIXED_GUI -eq 1 ]; then
  cat >"$PKG_ROOT/usr/share/applications/firmware-quota.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Firmware Quota
Comment=Firmware API quota monitor
TryExec=show_quota
Exec=show_quota --gui
Icon=firmware-quota
Terminal=false
Categories=Utility;
EOF
  chmod 0644 "$PKG_ROOT/usr/share/applications/firmware-quota.desktop"
fi

# Minimal docs
if [ -f "$REPO_DIR/README.md" ]; then
  install -m 0644 "$REPO_DIR/README.md" "$PKG_ROOT/usr/share/doc/firmware-quota/README.md"
fi
if [ -f "$REPO_DIR/panel/README.md" ]; then
  install -m 0644 "$REPO_DIR/panel/README.md" "$PKG_ROOT/usr/share/doc/firmware-quota/README.panel.md"
fi

DEPENDS_BASE="libc6, libcurl4, libstdc++6"
DEPENDS_GUI="libgtk-3-0, libayatana-appindicator3-1, libnotify4"

DEPENDS="$DEPENDS_BASE"
if [ $HAVE_MIXED_GUI -eq 1 ]; then
  DEPENDS="$DEPENDS, $DEPENDS_GUI"
fi

cat >"$PKG_ROOT/DEBIAN/control" <<EOF
Package: firmware-quota
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Maintainer: firmware_showquota packager <noreply@local>
Depends: $DEPENDS
Description: Firmware API quota viewer (CLI + optional GTK tray GUI)
 A small tool that shows Firmware API quota usage in the terminal and (optionally)
 in a compact GTK tray GUI.
EOF

cat >"$PKG_ROOT/DEBIAN/postinst" <<'EOF'
#!/usr/bin/env bash
set -e
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
  gtk-update-icon-cache -q /usr/share/icons/hicolor || true
fi
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database -q /usr/share/applications || true
fi
exit 0
EOF
chmod 0755 "$PKG_ROOT/DEBIAN/postinst"

cat >"$PKG_ROOT/DEBIAN/postrm" <<'EOF'
#!/usr/bin/env bash
set -e
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
  gtk-update-icon-cache -q /usr/share/icons/hicolor || true
fi
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database -q /usr/share/applications || true
fi
exit 0
EOF
chmod 0755 "$PKG_ROOT/DEBIAN/postrm"

MAIN_DEB="$OUT_DIR/firmware-quota_${VERSION}_${ARCH}.deb"
dpkg-deb --build "$PKG_ROOT" "$MAIN_DEB" >/dev/null
echo "Built: $MAIN_DEB"

echo "Done."
