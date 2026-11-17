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
# The click for 24.04-1.x bundles Qt 6 with its QML modules (Lomiri Components and Content for
# Qt 6), and libhybris' Vulkan with its Wayland plugin, as that system has neither (stage 7,
# ut/click/build-click.sh CLICK_UT). The click for 24.04-2.x has these folders only from the phone.
if [ -d "$lib/qt6/qml" ]; then
  export QML_IMPORT_PATH="$lib/qt6/qml"
  export QML2_IMPORT_PATH="$lib/qt6/qml"
fi
if [ -d "$lib/libhybris" ]; then
  # The Vulkan library's Wayland plugin; and the phone's driver, which 1.x's libhybris does not find
  # by itself: the stand-in libvndksupport (lib/hybris, stage 4) loads it from the vendor's hw
  # folder. 1.x's libhybris searches /system/lib64:/odm/lib64:/vendor/lib64 by default.
  export HYBRIS_VULKANPLATFORM_DIR="$lib/libhybris"
  export HYBRIS_LD_LIBRARY_PATH="$app/lib/hybris:${HYBRIS_LD_LIBRARY_PATH:-/system/lib64:/odm/lib64:/vendor/lib64}"
  export DOLPHIN_UT_VK_SYSTEM_DIR=/vendor/lib64/hw
fi
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
