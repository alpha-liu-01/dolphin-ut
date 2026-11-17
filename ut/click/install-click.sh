#!/bin/sh
# Installs the click on the phone over adb (stage 5), as clickable does on 24.04: through the click
# service on the system bus (there is no pkcon).
#   ut/click/install-click.sh [FILE.click]
#
# Without a file, it takes the newest click built for the phone's Ubuntu Touch release: the one in
# build-ut/ for 24.04-2.x, or in build-ut/ut-24.04-1.x/ for 24.04-1.x (CLICK_UT in build-click.sh).
# With several phones connected, choose one with ANDROID_SERIAL=<serial> (adb devices).
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
if [ $# -ge 1 ]; then
  click=$1
else
  frameworks=$(adb shell ls /usr/share/click/frameworks/)
  case "$frameworks" in
    *ubuntu-touch-24.04-2.x.framework*) dir="$root/build-ut"; ut=24.04-2.x ;;
    *ubuntu-touch-24.04-1.x.framework*) dir="$root/build-ut/ut-24.04-1.x"; ut=24.04-1.x ;;
    *) echo "the phone is neither Ubuntu Touch 24.04-2.x nor 24.04-1.x" >&2; exit 1 ;;
  esac
  click=$(ls -t "$dir"/*.click 2>/dev/null | head -1) || true
  if [ -z "$click" ]; then
    echo "the phone runs $ut: build its click first (CLICK_UT=$ut ut/click/build-click.sh)" >&2
    exit 1
  fi
  echo "Ubuntu Touch $ut: $click"
fi
file=$(basename "$click")
adb push "$click" "/home/phablet/$file" >/dev/null
adb shell "gdbus call --system --dest com.lomiri.click --object-path /com/lomiri/click \
  --method com.lomiri.click.Install /home/phablet/$file >/dev/null && sleep 8 && rm -f /home/phablet/$file \
  && click list | grep dolphin-ut"
