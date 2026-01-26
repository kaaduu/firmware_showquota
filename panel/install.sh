#!/usr/bin/env bash

set -euo pipefail

die() {
  echo "Error: $*" >&2
  exit 1
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

HOME_DIR="${HOME:?HOME is not set}"

APPLET_BIN_SRC="$SCRIPT_DIR/firmware-quota-applet"

[ -x "$APPLET_BIN_SRC" ] || die "Build the applet first: ./panel/build.sh"

LIBEXEC_DIR="$HOME_DIR/.local/libexec/mate-panel"
APPLET_SHARE_DIR="$HOME_DIR/.local/share/mate-panel/applets"
DBUS_SERVICES_DIR="$HOME_DIR/.local/share/dbus-1/services"
ICON_DIR="$HOME_DIR/.local/share/icons/hicolor/scalable/apps"

mkdir -p "$LIBEXEC_DIR" "$APPLET_SHARE_DIR" "$DBUS_SERVICES_DIR" "$ICON_DIR"

APPLET_BIN_DST="$LIBEXEC_DIR/firmware-quota-applet"

install -m 0755 "$APPLET_BIN_SRC" "$APPLET_BIN_DST"

# Generate applet descriptor + dbus service using the user-local absolute path.
sed "s|@APPLET_LOCATION@|$APPLET_BIN_DST|g" \
  "$SCRIPT_DIR/org.firmware.QuotaApplet.mate-panel-applet.in" \
  >"$APPLET_SHARE_DIR/org.firmware.QuotaApplet.mate-panel-applet"

sed "s|@APPLET_LOCATION@|$APPLET_BIN_DST|g" \
  "$SCRIPT_DIR/org.mate.panel.applet.FirmwareQuotaAppletFactory.service.in" \
  >"$DBUS_SERVICES_DIR/org.mate.panel.applet.FirmwareQuotaAppletFactory.service"

chmod 0644 "$APPLET_SHARE_DIR/org.firmware.QuotaApplet.mate-panel-applet"
chmod 0644 "$DBUS_SERVICES_DIR/org.mate.panel.applet.FirmwareQuotaAppletFactory.service"

# Optional icon for the Add-to-Panel list.
if [ ! -f "$ICON_DIR/firmware-quota.svg" ] && [ -f "$SCRIPT_DIR/firmware-quota.svg" ]; then
  install -m 0644 "$SCRIPT_DIR/firmware-quota.svg" "$ICON_DIR/firmware-quota.svg"
fi

echo "Installed MATE panel applet (user-local)."
echo "- Binary:   $APPLET_BIN_DST"
echo "- Applet:   $APPLET_SHARE_DIR/org.firmware.QuotaApplet.mate-panel-applet"
echo "- D-Bus:    $DBUS_SERVICES_DIR/org.mate.panel.applet.FirmwareQuotaAppletFactory.service"
echo "- Icon:     $ICON_DIR/firmware-quota.svg"
echo ""
echo "Test: mate-panel-test-applets --iid OAFIID:FIRMWARE_QuotaApplet"
echo "Then: right-click panel -> Add to Panel... -> Firmware Quota"
