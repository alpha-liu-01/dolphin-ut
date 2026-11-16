#!/bin/sh
# Dev launcher (stage 3.7): what Lomiri runs for the development desktop entry that dev-install.sh
# writes, so dolphin-ut starts from the app drawer like an installed app and content-hub can reach it
# (APP_ID=dolphin-ut, from the desktop file's name). The click's own launcher replaces this in
# stage 5. Without -u, the user folder is the app's own, ~/.local/share/dolphin-ut (Storage).
# The log: /home/phablet/dolphin-ut-launch.log (the previous run's: dolphin-ut-launch.log.1).
q=/home/phablet/dolphin-ut-probe/qt6w/usr/lib/aarch64-linux-gnu
# Qt 6 has no xdg-shell plugin on the phone; the copy from stage 0 (ut-env.sh, ut_qt6_wayland).
export QT_QPA_PLATFORM=wayland
export QT_PLUGIN_PATH="$q/qt6/plugins"
export LD_LIBRARY_PATH="$q${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_WAYLAND_DISABLE_WINDOWDECORATION=1
# Ubuntu Touch's volume keys change the multimedia role's volume (stage 2).
export PULSE_PROP="media.role=multimedia"
log=/home/phablet/dolphin-ut-launch.log
[ -f "$log" ] && mv -f "$log" "$log.1"
cd /home/phablet/dolphin-ut-arm64 || exit 1
exec ./dolphin-ut "$@" > "$log" 2>&1
