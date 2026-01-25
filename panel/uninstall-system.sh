#!/usr/bin/env bash

set -euo pipefail

PREFIX="/usr"
LIBEXEC_DIR="$PREFIX/libexec/mate-panel"
APPLET_SHARE_DIR="$PREFIX/share/mate-panel/applets"
DBUS_SERVICES_DIR="$PREFIX/share/dbus-1/services"
ICON_DIR="$PREFIX/share/icons/hicolor/48x48/apps"

sudo rm -f "$LIBEXEC_DIR/firmware-quota-applet" || true
sudo rm -f "$APPLET_SHARE_DIR/org.firmware.QuotaApplet.mate-panel-applet" || true
sudo rm -f "$DBUS_SERVICES_DIR/org.mate.panel.applet.FirmwareQuotaAppletFactory.service" || true
sudo rm -f "$ICON_DIR/firmware-quota.png" || true

# Remove any /usr/local remnants.
sudo rm -f /usr/local/libexec/mate-panel/firmware-quota-applet || true
sudo rm -f /usr/local/share/mate-panel/applets/org.firmware.QuotaApplet.mate-panel-applet || true
sudo rm -f /usr/local/share/dbus-1/services/org.mate.panel.applet.FirmwareQuotaAppletFactory.service || true
sudo rm -f /usr/local/share/icons/hicolor/48x48/apps/firmware-quota.png || true

echo "Uninstalled MATE panel applet from $PREFIX."
echo "Restart panel: mate-panel --replace"
