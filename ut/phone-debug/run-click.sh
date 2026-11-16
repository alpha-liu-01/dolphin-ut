#!/bin/sh
# Runs the installed click (stage 5) under its own AppArmor profile, with the environment
# lomiri-app-launch gives it (stage5-packaging.md), so adb tests can pass arguments:
#   sh run-click.sh SECONDS [dolphin-ut args...]     e.g. --start "/home/phablet/Documents/Dolphin/Games/x.iso"
# The game must be somewhere the click may read (~/Documents). Screen on and unlocked. The output is
# printed and kept in /home/phablet/dolphin-ut-click-run.log; the app is stopped after SECONDS.
pkg=${CLICK_NAME:-dolphin-ut.alphaliu01}
seconds=${1:-60}
[ $# -gt 0 ] && shift
dir=/opt/click.ubuntu.com/$pkg/current
version=$(click list | awk -v p="$pkg" '$1 == p { print $2 }')
[ -n "$version" ] || { echo "$pkg is not installed" >&2; exit 1; }
appid="${pkg}_dolphin-ut_${version}"
uid=$(id -u)
log=/home/phablet/dolphin-ut-click-run.log
cd "$dir" || exit 1
env -i HOME=/home/phablet USER=phablet PATH=/usr/bin:/bin LANG="${LANG:-C.UTF-8}" \
  APP_ID="$appid" APP_DIR="$dir" APP_DESKTOP_FILE_PATH="$dir/dolphin-ut.desktop" \
  LOMIRI_APPLICATION_ISOLATION=1 XDG_RUNTIME_DIR=/home/phablet/.cache WAYLAND_DISPLAY=wayland-0 \
  DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$uid/bus" XDG_SESSION_TYPE=mir XDG_CURRENT_DESKTOP=Lomiri \
  MIR_SOCKET=/home/phablet/.cache/mir_socket \
  LD_PRELOAD=/usr/lib/aarch64-linux-gnu/libtls-padding.so \
  GLIBC_TUNABLES=glibc.rtld.optional_static_tls=4096 \
  QT_FILE_SELECTORS=ubuntu-touch QT_OPENGL_VARIANT=gles QT_QUICK_CONTROLS_MOBILE=1 QT_QUICK_CONTROLS_STYLE=Suru \
  aa-exec -p "$appid" -- "$dir/launcher.sh" "$@" > "$log" 2>&1 &
pid=$!
sleep "$seconds"
pkill -TERM -f "^$dir/dolphin-ut|click.ubuntu.com/.*/$pkg/dolphin-ut" 2>/dev/null
sleep 3
kill "$pid" 2>/dev/null
cat "$log"
