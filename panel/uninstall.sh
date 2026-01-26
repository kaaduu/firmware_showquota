#!/usr/bin/env bash

set -euo pipefail

HOME_DIR="${HOME:?HOME is not set}"

LIBEXEC_DIR="$HOME_DIR/.local/libexec/mate-panel"
APPLET_SHARE_DIR="$HOME_DIR/.local/share/mate-panel/applets"
DBUS_SERVICES_DIR="$HOME_DIR/.local/share/dbus-1/services"
ICON_DIR="$HOME_DIR/.local/share/icons/hicolor/scalable/apps"

rm -f "$LIBEXEC_DIR/firmware-quota-applet" || true
rm -f "$APPLET_SHARE_DIR/org.firmware.QuotaApplet.mate-panel-applet" || true
rm -f "$DBUS_SERVICES_DIR/org.mate.panel.applet.FirmwareQuotaAppletFactory.service" || true
rm -f "$ICON_DIR/firmware-quota.svg" || true

rmdir "$LIBEXEC_DIR" 2>/dev/null || true
rmdir "$HOME_DIR/.local/libexec/mate-panel" 2>/dev/null || true
rmdir "$HOME_DIR/.local/libexec" 2>/dev/null || true

rmdir "$APPLET_SHARE_DIR" 2>/dev/null || true
rmdir "$HOME_DIR/.local/share/mate-panel/applets" 2>/dev/null || true
rmdir "$HOME_DIR/.local/share/mate-panel" 2>/dev/null || true

rmdir "$DBUS_SERVICES_DIR" 2>/dev/null || true
rmdir "$HOME_DIR/.local/share/dbus-1/services" 2>/dev/null || true
rmdir "$HOME_DIR/.local/share/dbus-1" 2>/dev/null || true

rmdir "$ICON_DIR" 2>/dev/null || true
rmdir "$HOME_DIR/.local/share/icons/hicolor/scalable" 2>/dev/null || true

# Note: we avoid removing any hicolor PNG icon that may be shared.

echo "Uninstalled MATE panel applet (user-local)."
