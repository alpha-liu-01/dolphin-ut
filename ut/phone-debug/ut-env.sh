# Source this in an adb shell to get what a Lomiri app gets (see docs/ut-port/stage1-build.md):
#   . /home/phablet/dolphin-ut-probe/ut-env.sh
# libhybris needs the static TLS settings below, or it fails with
# "cannot allocate memory in static TLS block". Lomiri sets them through /etc/profile.d/hybris-static-tls.sh.
export XDG_RUNTIME_DIR=/run/user/32011
export WAYLAND_DISPLAY=wayland-0
export DISPLAY=:0
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/32011/bus
export GLIBC_TUNABLES=glibc.rtld.optional_static_tls=4096
export LD_PRELOAD=/usr/lib/aarch64-linux-gnu/libtls-padding.so
export QT_WAYLAND_DISABLE_WINDOWDECORATION=1
export QT_IM_MODULE=maliit

# Qt 6 has no xdg-shell plugin on the phone; use the copy unpacked from libqt6wlshellintegration6 in stage 0.
ut_qt6_wayland() {
  q=/home/phablet/dolphin-ut-probe/qt6w/usr/lib/aarch64-linux-gnu
  export QT_QPA_PLATFORM=wayland
  export QT_PLUGIN_PATH="$q/qt6/plugins"
  export LD_LIBRARY_PATH="$q${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
}
