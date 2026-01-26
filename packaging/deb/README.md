Debian/Ubuntu Packaging

This folder contains a minimal packaging script that builds .deb artifacts using
`dpkg-deb` (no debhelper required).

Build:
  ./packaging/deb/build.sh

Artifacts are written to:
  dist/

Install locally:
  sudo dpkg -i dist/firmware-quota_*_*.deb
  sudo apt-get -f install

Panel applet is packaged separately:
  ./panel/packaging/deb/build.sh
  sudo dpkg -i panel/dist/firmware-quota-mate-panel-applet_*_*.deb
  mate-panel --replace
