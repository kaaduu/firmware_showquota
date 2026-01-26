Panel Applet Packaging

This folder builds a standalone .deb for the MATE panel applet.

Build:
  ./panel/packaging/deb/build.sh

Artifacts:
  panel/dist/

Install:
  sudo dpkg -i panel/dist/firmware-quota-mate-panel-applet_*_*.deb
  mate-panel --replace

Notes:
  - This package is independent from the firmware-quota CLI/GUI tool.
  - Build deps (panel): libmate-panel-applet-dev, pkg-config, and build tooling.
