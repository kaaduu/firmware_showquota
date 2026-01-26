#!/usr/bin/env bash

set -euo pipefail

die() {
  echo "Error: $*" >&2
  exit 1
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

APPLET_BIN_SRC="$REPO_DIR/panel/firmware-quota-applet"
[ -x "$APPLET_BIN_SRC" ] || die "Build the applet first: ./panel/build.sh"

PREFIX="/usr"
LIBEXEC_DIR="$PREFIX/libexec/mate-panel"
APPLET_SHARE_DIR="$PREFIX/share/mate-panel/applets"
DBUS_SERVICES_DIR="$PREFIX/share/dbus-1/services"
ICON_DIR="$PREFIX/share/icons/hicolor/48x48/apps"

APPLET_BIN_DST="$LIBEXEC_DIR/firmware-quota-applet"

sudo install -d "$LIBEXEC_DIR" "$APPLET_SHARE_DIR" "$DBUS_SERVICES_DIR" "$ICON_DIR"

# Remove any prior /usr/local install to avoid duplicates.
sudo rm -f /usr/local/libexec/mate-panel/firmware-quota-applet || true
sudo rm -f /usr/local/share/mate-panel/applets/org.firmware.QuotaApplet.mate-panel-applet || true
sudo rm -f /usr/local/share/dbus-1/services/org.mate.panel.applet.FirmwareQuotaAppletFactory.service || true
sudo rm -f /usr/local/share/icons/hicolor/48x48/apps/firmware-quota.png || true
sudo install -m 0755 "$APPLET_BIN_SRC" "$APPLET_BIN_DST"

tmp_applet="$(mktemp)"
tmp_service="$(mktemp)"

sed "s|@APPLET_LOCATION@|$APPLET_BIN_DST|g" \
  "$REPO_DIR/panel/org.firmware.QuotaApplet.mate-panel-applet.in" \
  >"$tmp_applet"

sed "s|@APPLET_LOCATION@|$APPLET_BIN_DST|g" \
  "$REPO_DIR/panel/org.mate.panel.applet.FirmwareQuotaAppletFactory.service.in" \
  >"$tmp_service"

sudo install -m 0644 "$tmp_applet" "$APPLET_SHARE_DIR/org.firmware.QuotaApplet.mate-panel-applet"
sudo install -m 0644 "$tmp_service" "$DBUS_SERVICES_DIR/org.mate.panel.applet.FirmwareQuotaAppletFactory.service"

rm -f "$tmp_applet" "$tmp_service"

if [ -f "$REPO_DIR/firmware-icon.png" ]; then
  sudo install -m 0644 "$REPO_DIR/firmware-icon.png" "$ICON_DIR/firmware-quota.png" || true
  if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    sudo gtk-update-icon-cache -q "$PREFIX/share/icons/hicolor" || true
  fi
fi

echo "Installed MATE panel applet (system-wide to $PREFIX)."
echo "Now restart panel: mate-panel --replace"
