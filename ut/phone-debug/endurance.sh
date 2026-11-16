#!/bin/sh
# Stage 4 step 4: a long run as an installed app. Samples temperatures, battery and clocks every
# 30 s while you play the click (started from the app drawer, so Lomiri manages it), then adds the
# game's speed from its log. Start it before unplugging:
#   adb shell sh /home/phablet/dolphin-ut-probe/endurance.sh 900
# Results: /home/phablet/dolphin-ut-endurance.txt
#
# Unplugging stops adbd.service, and systemd kills everything in its cgroup, nohup or not. So the
# script restarts itself as a unit of the user's systemd, outside adbd.
secs=${1:-900}
if [ -z "$ENDURANCE_DETACHED" ]; then
  export XDG_RUNTIME_DIR=/run/user/$(id -u)
  systemctl --user stop dolphin-ut-endurance 2>/dev/null
  systemd-run --user --collect --unit=dolphin-ut-endurance -E ENDURANCE_DETACHED=1 \
    sh "$(cd "${0%/*}" && pwd)/${0##*/}" "$secs" && echo "sampling for $secs s; you can unplug now"
  exit
fi
out=/home/phablet/dolphin-ut-endurance.txt
zone() {  # the temperature (°C) of the first thermal zone of that type
  for z in /sys/class/thermal/thermal_zone*; do
    [ "$(cat "$z/type" 2>/dev/null)" = "$1" ] && { echo $(( $(cat "$z/temp") / 1000 )); return; }
  done
  echo -
}
bigmax() {  # the hottest big core
  m=0; for t in cpu-1-0-usr cpu-1-1-usr cpu-1-2-usr cpu-1-3-usr; do v=$(zone $t); [ "$v" != - ] && [ "$v" -gt "$m" ] && m=$v; done; echo $m
}
B=/sys/class/power_supply/battery
K=/sys/class/kgsl/kgsl-3d0
start=$(date +%s)
echo "start $(date '+%F %T'), $secs s; battery $(cat $B/capacity)% $(cat $B/status)" > $out
echo "time  skin  big-core  gpu  battery-temp  battery%  current  status  big-MHz  gpu-MHz  gpu-busy" >> $out
while [ $(( $(date +%s) - start )) -lt "$secs" ]; do
  printf '%s  %s  %s  %s  %s  %s  %s  %s  %s  %s  %s\n' "$(date +%T)" "$(zone sdm-skin-therm-usr)" "$(bigmax)" \
    "$(zone gpuss-0-usr)" "$(( $(cat $B/temp) / 10 ))" "$(cat $B/capacity)" "$(cat $B/current_now)" \
    "$(cat $B/status)" "$(( $(cat /sys/devices/system/cpu/cpufreq/policy6/scaling_cur_freq) / 1000 ))" \
    "$(( $(cat $K/gpuclk) / 1000000 ))" "$(cat $K/gpu_busy_percentage | tr -d ' %')" >> $out
  sleep 30
done
echo "end $(date '+%F %T'); battery $(cat $B/capacity)% $(cat $B/status)" >> $out
echo "speed (the click's log, perf lines during the run):" >> $out
XDG_RUNTIME_DIR=/run/user/$(id -u) journalctl --user -u 'lomiri-app-launch--application-click--dolphin-ut*' \
  --since "@$start" -o cat 2>/dev/null | grep -E "perf:|start |CPU boost|title:" >> $out
echo done >> $out
