#!/bin/sh
# Installs the newest click from build-ut/ on the phone over adb (stage 5), as clickable does on
# 24.04: through the click service on the system bus (there is no pkcon).
#   ut/click/install-click.sh [FILE.click]
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
click=${1:-$(ls -t "$root"/build-ut/*.click | head -1)}
file=$(basename "$click")
adb push "$click" "/home/phablet/$file" >/dev/null
adb shell "gdbus call --system --dest com.lomiri.click --object-path /com/lomiri/click \
  --method com.lomiri.click.Install /home/phablet/$file >/dev/null && sleep 8 && rm -f /home/phablet/$file \
  && click list | grep dolphin-ut"
