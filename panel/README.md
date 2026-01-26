MATE Panel Applet (Firmware Quota)

This folder contains a system-integrated MATE Panel applet ("Add to Panel...") that displays
Firmware quota usage as a compact progress bar with a tiny percent overlay.

It is an out-of-process applet factory (D-Bus activated) built on libmate-panel-applet.

Compatibility
  - Works on: MATE (mate-panel)
  - Does NOT work on: Cinnamon panel (different applet framework)

Screenshot
  - panel/screenshots/panel-applet.png
  - panel/screenshots/panel-applet-window-line.png
  - panel/screenshots/panel-applet-delta.png
  - panel/screenshots/panel-applet-5deltas.png

Build Requirements
  - libmate-panel-applet-dev
  - GTK3 dev packages (pulled by libmate-panel-applet-dev on most distros)
  - libcurl (already required by the main project)

Build
  ./panel/build.sh

  (equivalent to: make -C panel)

Install Options

1) System-wide install (recommended)
  This is the reliable way to make the applet appear in the "Add to Panel..." catalog.

  ./panel/install-system.sh

  Installs:
    - /usr/share/mate-panel/applets/org.firmware.QuotaApplet.mate-panel-applet
    - /usr/share/dbus-1/services/org.mate.panel.applet.FirmwareQuotaAppletFactory.service
    - /usr/libexec/mate-panel/firmware-quota-applet
    - /usr/share/icons/hicolor/scalable/apps/firmware-quota.svg

  Then restart the panel:
    mate-panel --replace

2) User-local install
  Some MATE builds do not scan user-local applet directories for the "Add to Panel" catalog.
  Use this only if you know your environment supports it.

  ./panel/install.sh

Uninstall

  ./panel/uninstall-system.sh
  mate-panel --replace

  ./panel/uninstall.sh
  mate-panel --replace

Add To Panel
  Right-click the panel -> Add to Panel... -> Firmware Quota

Usage

  - The applet draws a bar showing usage.
  - The overlay text shows a tiny percent (or "--" while initializing, or "ERR" on error).
  - Hovering shows a tooltip with usage, reset time, and next refresh countdown.

Right-Click Menu
  The applet integrates into the standard MATE applet menu (so you also get Move/Remove/Lock).

  Actions provided by this applet:
    - Refresh Now
    - API Key
      - Set...          (stores to ~/.config/firmware-quota/env, chmod 600)
      - Reload          (reloads key from environment / env file)
      - Clear Stored Key
    - Refresh Rate (15s / 30s / 60s / 120s)
    - Window Timer Line (Off / 1px / 2px / 3px / 4px / 6px)
    - Width
      - -10px / +10px / -100px / +100px / Reset (120px)
      - presets: 80/100/120/160/200/300/400/500/600/800/1000/1200/1600px

API Key Resolution
  The applet needs an API key to query the quota endpoint.

  It checks in this order:
    1) Desktop session environment variable: FIRMWARE_API_KEY
    2) Env file: ~/.config/firmware-quota/env

  Notes:
    - Panel applets do not source ~/.bashrc.
    - Storing a key in ~/.config/firmware-quota/env is plaintext; keep file permissions at 600.

Persisted Settings
  - Width is stored per applet instance in: ~/.config/firmware-quota/panel-applet.conf
    The key is the applet prefs-path (e.g. /org/mate/panel/objects/object-6/prefs/)
  - Window timer line thickness is stored per applet instance in the same file under:
    <prefs-path>#time_line_px

Troubleshooting

  Applet not visible in Add to Panel...
    - Use system-wide install (install-system.sh)
    - Restart: mate-panel --replace

  Clicking Add does nothing
    - On some systems, broken GTK modules can crash panel applets.
    - The D-Bus service Exec unsets GTK_MODULES to avoid appmenu-gtk-module ABI issues.

  Crashes when removing the applet
    - The applet has lifetime hardening to avoid use-after-free when fetch threads complete.
