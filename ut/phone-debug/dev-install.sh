#!/bin/sh
# Registers the development build with Lomiri and content-hub (stage 3.7), without a click:
#   sh /home/phablet/dolphin-ut-probe/dev-install.sh           install
#   sh /home/phablet/dolphin-ut-probe/dev-install.sh remove    remove
# - ~/.local/share/applications/dolphin-ut.desktop: "Dolphin (dev)" in the app drawer, which runs
#   launch-dolphin.sh. Lomiri gives the app APP_ID=dolphin-ut (a non-click app's ID is its desktop
#   file's name). X-Lomiri-Single-Instance: a non-click app is multi-instance by default, so each
#   start from the drawer or Share from another app would start another copy on the same user
#   folder (S3.7, three copies in the app switcher); a click is single-instance already.
# - ~/.local/share/lomiri-content-hub/dolphin-ut: content-hub's peer file (what a click's
#   "content-hub" hook installs): Dolphin accepts files of any type from other apps (Share in the
#   File Manager).
apps=/home/phablet/.local/share/applications
peers=/home/phablet/.local/share/lomiri-content-hub
probe=/home/phablet/dolphin-ut-probe
if [ "$1" = remove ]; then
  rm -f /home/phablet/.local/share/applications/dolphin-ut.desktop
  rm -f /home/phablet/.local/share/lomiri-content-hub/dolphin-ut
  echo "removed"
  exit 0
fi
mkdir -p "$apps" "$peers"
cat > "$apps/dolphin-ut.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Dolphin (dev)
Comment=GameCube and Wii emulator (development build)
Exec=$probe/launch-dolphin.sh
Icon=$probe/dolphin-ut.svg
Terminal=false
X-Lomiri-Touch=true
X-Lomiri-Single-Instance=true
DESKTOP
cat > "$peers/dolphin-ut" <<PEER
{
    "destination": [
        "all"
    ]
}
PEER
chmod +x "$probe/launch-dolphin.sh"
echo "installed: $apps/dolphin-ut.desktop, $peers/dolphin-ut"
