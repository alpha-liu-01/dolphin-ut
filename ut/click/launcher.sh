#!/bin/sh
# The click's launcher (stage 5, docs/ut-port/stage5-packaging.md): what launch-dolphin.sh sets for
# the development entry, with the bundled Qt 6 Wayland shell integration (the phone has none).
# Shell builtins only: the click's AppArmor profile does not let it run other programs (readlink,
# dirname, mkdir, mv all denied). The output goes to the session journal:
#   XDG_RUNTIME_DIR=/run/user/32011 journalctl --user -u 'lomiri-app-launch--application-click--dolphin-ut*'
# (ut/phone-debug/click-log.sh).
app=${0%/*}
lib="$app/lib/aarch64-linux-gnu"
export QT_QPA_PLATFORM=wayland
export QT_PLUGIN_PATH="$lib/qt6/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
export LD_LIBRARY_PATH="$lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_WAYLAND_DISABLE_WINDOWDECORATION=1
# Ubuntu Touch's volume keys change the multimedia role's volume (stage 2).
export PULSE_PROP="media.role=multimedia"
# A confined app gets XDG_RUNTIME_DIR=~/.cache (LOMIRI_APPLICATION_ISOLATION), but Lomiri's Wayland
# socket is in the real runtime folder, /run/user/<uid>/wayland-0, which the profile allows.
# libwayland takes an absolute WAYLAND_DISPLAY; the folder comes from the session bus address.
runtime=${DBUS_SESSION_BUS_ADDRESS#unix:path=}
runtime=${runtime%/bus}
display="$runtime/${WAYLAND_DISPLAY:-wayland-0}"
[ -S "$display" ] && export WAYLAND_DISPLAY="$display"
# The same for PulseAudio: libpulse looks in $XDG_RUNTIME_DIR/pulse/native, and the audio policy group
# allows the real one.
[ -S "$runtime/pulse/native" ] && export PULSE_SERVER="unix:$runtime/pulse/native"
exec "$app/dolphin-ut" "$@"
