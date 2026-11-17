#!/bin/sh
# Dev launcher (stage 3.7): what Lomiri runs for the development desktop entry that dev-install.sh
# writes, so dolphin-ut starts from the app drawer like an installed app and content-hub can reach it
# (APP_ID=dolphin-ut, from the desktop file's name). The click's own launcher replaces this in
# stage 5. Without -u, the user folder is the app's own, ~/.local/share/dolphin-ut (Storage).
# The log: /home/phablet/dolphin-ut-launch.log (the previous run's: dolphin-ut-launch.log.1).
q=/home/phablet/dolphin-ut-probe/qt6w/usr/lib/aarch64-linux-gnu
c=/opt/click.ubuntu.com/dolphin-ut.alphaliu01/current/lib/aarch64-linux-gnu
export QT_QPA_PLATFORM=wayland
if [ ! -d "$q" ] && [ -d "$c/qt6/qml" ]; then
  # Ubuntu Touch 24.04-1.x has no Qt 6: the runtime of the installed 24.04-1.x click (stage 7),
  # as its launcher sets it. Over adb, Lomiri 1.x does not answer a Wayland client it did not start,
  # so tests there go through this entry (dolphin-ut-launch.args).
  export QT_PLUGIN_PATH="$c/qt6/plugins"
  export LD_LIBRARY_PATH="$c${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  export QML_IMPORT_PATH="$c/qt6/qml" QML2_IMPORT_PATH="$c/qt6/qml"
  export HYBRIS_VULKANPLATFORM_DIR="$c/libhybris"
  export HYBRIS_LD_LIBRARY_PATH="$c/../../lib/hybris:${HYBRIS_LD_LIBRARY_PATH:-/system/lib64:/odm/lib64:/vendor/lib64}"
  export DOLPHIN_UT_VK_SYSTEM_DIR=/vendor/lib64/hw
else
  # Qt 6 has no xdg-shell plugin on the phone; the copy from stage 0 (ut-env.sh, ut_qt6_wayland).
  export QT_PLUGIN_PATH="$q/qt6/plugins"
  export LD_LIBRARY_PATH="$q${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
export QT_WAYLAND_DISABLE_WINDOWDECORATION=1
# Ubuntu Touch's volume keys change the multimedia role's volume (stage 2).
export PULSE_PROP="media.role=multimedia"
log=/home/phablet/dolphin-ut-launch.log
[ -f "$log" ] && mv -f "$log" "$log.1"
# Test arguments for one Lomiri-managed start (the drawer passes none): a file with, for example,
#   --start "/home/phablet/Documents/Dolphin/Games/x.iso"
# is read once and deleted. Lomiri treats a process it started differently from one run over adb
# (stage 5: it keeps window states per app).
args=/home/phablet/dolphin-ut-launch.args
if [ -f "$args" ]; then
  eval "set -- $(cat "$args") \"\$@\""
  rm -f "$args"
fi
cd /home/phablet/dolphin-ut-arm64 || exit 1
exec ./dolphin-ut "$@" > "$log" 2>&1
