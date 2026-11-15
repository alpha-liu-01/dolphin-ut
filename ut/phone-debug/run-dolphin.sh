#!/bin/sh
# Stage 2 test launcher: runs the deployed Dolphin (/home/phablet/dolphin-ut-arm64) on Lomiri's
# Wayland, with the environment a click app gets, then stops it after N seconds.
#
#   sh run-dolphin.sh MODE [SECONDS] [extra dolphin args...]
#
# MODE:  ogl | ogl-fs | vk | vk-fs   the test game in batch mode (-b), windowed or fullscreen
#        ui                          the main window, nothing started (start the game by tapping it)
#        ui-main                     the main window with "Render to Main Window"
# SECONDS defaults to 60. The log goes to /home/phablet/dolphin-ut-user-w/run-MODE.log; the
# per-second FPS/speed shown on screen is also in that log only if the OSD prints it (it does not).
# The game is /home/phablet/dolphin-ut-games/*.wbfs (the first one), or $GAME.
mode=${1:-ogl}; secs=${2:-60}; [ $# -ge 2 ] && shift 2 || shift $#
U=/home/phablet/dolphin-ut-user-w
mkdir -p "$U/Config"
. /home/phablet/dolphin-ut-probe/ut-env.sh
ut_qt6_wayland
# What a click app inherits from the session; Dolphin must drop ubuntumirclient itself.
export QT_QPA_PLATFORM="ubuntumirclient;wayland-egl;xcb"
# Ubuntu Touch's PulseAudio keeps volumes per media role (module-stream-restore with
# /etc/pulse/touch-stream-restore.table), and the volume keys change the "multimedia" role.
# Dolphin's stream (Cubeb, PulseAudio backend) has no role, so it would ignore the volume keys.
# libpulse adds PULSE_PROP to the client's properties, which the stream inherits.
export PULSE_PROP="media.role=multimedia"
cd /home/phablet/dolphin-ut-arm64 || exit 1
G=${GAME:-$(ls /home/phablet/dolphin-ut-games/*.wbfs /home/phablet/dolphin-ut-games/*.iso /home/phablet/dolphin-ut-games/*.rvz 2>/dev/null | head -1)}
common="-u $U -C Dolphin.Core.CPUThread=True -C Graphics.Settings.ShowFPS=True -C Graphics.Settings.ShowSpeed=True"
case "$mode" in
  ogl)     set -- $common -b -v OGL -C Dolphin.Interface.ConfirmStop=False -C Dolphin.Display.Fullscreen=False "$@" -e "$G" ;;
  ogl-fs)  set -- $common -b -v OGL -C Dolphin.Interface.ConfirmStop=False -C Dolphin.Display.Fullscreen=True "$@" -e "$G" ;;
  vk)      set -- $common -b -v Vulkan -C Dolphin.Interface.ConfirmStop=False -C Dolphin.Display.Fullscreen=False "$@" -e "$G" ;;
  vk-fs)   set -- $common -b -v Vulkan -C Dolphin.Interface.ConfirmStop=False -C Dolphin.Display.Fullscreen=True "$@" -e "$G" ;;
  ui)      set -- $common -C Dolphin.Display.RenderToMain=False "$@" ;;
  ui-main) set -- $common -C Dolphin.Display.RenderToMain=True "$@" ;;
  *) echo "unknown mode $mode" >&2; exit 2 ;;
esac
log="$U/run-$mode.log"
maps="$U/run-$mode.maps"
rm -f "$maps" core core.*
# Crash data: a core dump of private memory only (0x11: anonymous private + ELF headers; the
# emulated console's shared memory arena would add gigabytes), and the process's memory map,
# saved every 2 s, to place a crashing PC in a library (crash-resolve.py).
ulimit -c unlimited 2>/dev/null
echo 0x11 > /proc/self/coredump_filter 2>/dev/null
echo "running: dolphin-emu $* (for ${secs}s, log $log)"
./dolphin-emu "$@" > "$log" 2>&1 &
p=$!
( while kill -0 $p 2>/dev/null; do cp /proc/$p/maps "$maps.tmp" 2>/dev/null && mv "$maps.tmp" "$maps"; sleep 2; done ) &
i=0; while [ $i -lt "$secs" ] && kill -0 $p 2>/dev/null; do sleep 1; i=$((i+1)); done
if kill -0 $p 2>/dev/null; then
  kill -INT $p
  j=0; while [ $j -lt 15 ] && kill -0 $p 2>/dev/null; do sleep 1; j=$((j+1)); done
  if kill -0 $p 2>/dev/null; then echo "still running 15 s after SIGINT (a Confirm-on-Stop dialog?), sending a second SIGINT"; kill -INT $p; sleep 3; kill -9 $p 2>/dev/null; fi
  echo "stopped after ${i}s"
else
  wait $p; st=$?; echo "exited by itself after ${i}s (status $st)"
  if [ $st -ge 128 ]; then
    echo "crashed (signal $((st-128))); crash data:"
    python3 /home/phablet/dolphin-ut-probe/crash-resolve.py "$log" "$maps"
    ls -la core* 2>/dev/null | sed 's/^/  core dump: /'
  fi
fi
tr -cd '\11\12\40-\176' < "$log" | grep -E " E\[| W\[|Panic|panic|Fatal|fatal|crash|Segmentation|Aborted" \
  | grep -vE "keys.bin|Translation|MO file|FEAT_AFP|ScreenSaver|Dispatcher is|IOS_FS|IOS_ES" | head -20
