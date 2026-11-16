#!/bin/sh
# mem-watch.sh OUT [SECONDS_BETWEEN]: waits for the test build's dolphin-ut (run-dolphin.sh), then
# samples its memory until it exits: resident and virtual size, the system's available memory and
# swap, and its GPU memory as KGSL counts it (/sys/class/kgsl/kgsl/proc/PID, bytes).
# A growing column points at a leak; a crash after free memory runs out points at the kernel.
out=$1; every=${2:-10}
U=/home/phablet/dolphin-ut-user-w
own() { pgrep -f "^./dolphin-ut -u $U" | head -1; }
i=0; while [ -z "$(own)" ] && [ $i -lt 120 ]; do sleep 1; i=$((i+1)); done
p=$(own); [ -z "$p" ] && { echo "no dolphin-ut started" > "$out"; exit 1; }
k=/sys/class/kgsl/kgsl/proc/$p
mb() { echo $(( ${1:-0} / 1048576 )); }
echo "pid $p; MB: rss vsz avail swapfree | kgsl: kernel user ion mapped imported | kgsl total page_alloc" > "$out"
start=$(date +%s)
while kill -0 "$p" 2>/dev/null; do
  rss=$(awk '/VmRSS/{print $2}' /proc/$p/status 2>/dev/null); vsz=$(awk '/VmSize/{print $2}' /proc/$p/status 2>/dev/null)
  avail=$(awk '/MemAvailable/{print $2}' /proc/meminfo); swap=$(awk '/SwapFree/{print $2}' /proc/meminfo)
  echo "$(( $(date +%s) - start ))s  $((${rss:-0}/1024)) $((${vsz:-0}/1024)) $((avail/1024)) $((swap/1024)) | $(mb $(cat $k/kernel 2>/dev/null)) $(mb $(cat $k/user 2>/dev/null)) $(mb $(cat $k/ion 2>/dev/null)) $(mb $(cat $k/gpumem_mapped 2>/dev/null)) $(mb $(cat $k/imported_mem 2>/dev/null)) | $(mb $(cat /sys/class/kgsl/kgsl/page_alloc))" >> "$out"
  sleep "$every"
done
echo "exited after $(( $(date +%s) - start ))s" >> "$out"
