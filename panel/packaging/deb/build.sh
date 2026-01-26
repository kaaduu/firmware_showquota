#!/usr/bin/env bash

set -euo pipefail

usage() {
  cat <<'EOF'
Usage: panel/packaging/deb/build.sh [--out DIR] [--version VERSION] [--arch ARCH]

Builds a Ubuntu/Linux Mint compatible .deb for the MATE panel applet using dpkg-deb.

Outputs:
  - panel/dist/firmware-quota-mate-panel-applet_<version>_<arch>.deb

Notes:
  - This is independent from the firmware-quota CLI/GUI tool package.
  - You still need the panel applet build dependencies installed to build.
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
PANEL_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
REPO_DIR="$(cd "$PANEL_DIR/.." && pwd)"

OUT_DIR="$PANEL_DIR/dist"
VERSION=""
ARCH=""

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
      desc="${desc#v}"
      echo "$(sanitize_version "$desc")"
      return 0
    fi
  fi

  date +%Y.%m.%d.%H%M%S
}

VERSION="$(detect_version)"

WORK_DIR="$(mktemp -d -t firmware-quota-panel-deb.XXXXXX)"
cleanup() {
  rm -rf "$WORK_DIR"
}
trap cleanup EXIT

mkdir -p "$OUT_DIR"

echo "Building panel applet..."
make -C "$PANEL_DIR"

echo "Staging firmware-quota-mate-panel-applet package..."
APPLET_ROOT="$WORK_DIR/firmware-quota-mate-panel-applet"
mkdir -p \
  "$APPLET_ROOT/DEBIAN" \
  "$APPLET_ROOT/usr/libexec/mate-panel" \
  "$APPLET_ROOT/usr/share/mate-panel/applets" \
  "$APPLET_ROOT/usr/share/dbus-1/services" \
  "$APPLET_ROOT/usr/share/icons/hicolor/scalable/apps" \
  "$APPLET_ROOT/usr/share/doc/firmware-quota-mate-panel-applet"

install -m 0755 "$PANEL_DIR/firmware-quota-applet" "$APPLET_ROOT/usr/libexec/mate-panel/firmware-quota-applet"

# Generate descriptor + service with correct absolute applet path.
sed "s|@APPLET_LOCATION@|/usr/libexec/mate-panel/firmware-quota-applet|g" \
  "$PANEL_DIR/org.firmware.QuotaApplet.mate-panel-applet.in" \
  >"$APPLET_ROOT/usr/share/mate-panel/applets/org.firmware.QuotaApplet.mate-panel-applet"

sed "s|@APPLET_LOCATION@|/usr/libexec/mate-panel/firmware-quota-applet|g" \
  "$PANEL_DIR/org.mate.panel.applet.FirmwareQuotaAppletFactory.service.in" \
  >"$APPLET_ROOT/usr/share/dbus-1/services/org.mate.panel.applet.FirmwareQuotaAppletFactory.service"

chmod 0644 "$APPLET_ROOT/usr/share/mate-panel/applets/org.firmware.QuotaApplet.mate-panel-applet"
chmod 0644 "$APPLET_ROOT/usr/share/dbus-1/services/org.mate.panel.applet.FirmwareQuotaAppletFactory.service"

# Icon
if [ -f "$PANEL_DIR/firmware-quota.svg" ]; then
  install -m 0644 "$PANEL_DIR/firmware-quota.svg" "$APPLET_ROOT/usr/share/icons/hicolor/scalable/apps/firmware-quota.svg"
fi

if [ -f "$PANEL_DIR/README.md" ]; then
  install -m 0644 "$PANEL_DIR/README.md" "$APPLET_ROOT/usr/share/doc/firmware-quota-mate-panel-applet/README.md"
fi

DEPENDS="libc6, libstdc++6, libcurl4, mate-panel, libmate-panel-applet-4-1, libgtk-3-0"

cat >"$APPLET_ROOT/DEBIAN/control" <<EOF
Package: firmware-quota-mate-panel-applet
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Maintainer: firmware_showquota packager <noreply@local>
Depends: $DEPENDS
Description: MATE panel applet for Firmware Quota
 Adds a MATE Panel applet (Add to Panel...) that shows Firmware quota usage.
EOF

cat >"$APPLET_ROOT/DEBIAN/postinst" <<'EOF'
#!/usr/bin/env bash
set -e
echo "Installed Firmware Quota MATE panel applet."
echo "Restart panel: mate-panel --replace"
exit 0
EOF
chmod 0755 "$APPLET_ROOT/DEBIAN/postinst"

APPLET_DEB="$OUT_DIR/firmware-quota-mate-panel-applet_${VERSION}_${ARCH}.deb"
dpkg-deb --build "$APPLET_ROOT" "$APPLET_DEB" >/dev/null
echo "Built: $APPLET_DEB"
