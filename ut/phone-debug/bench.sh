#!/bin/sh
# bench.sh NAME SECONDS CPUS|- [extra dolphin-ut args]: runs the game from save state slot 1 and prints
# the mean speed after the first 10 s, and the big cluster's mean clock over the run.
#   CPUS: pin all threads there (taskset), or - for the scheduler's choice.
#   UCLAMP=N (0..1024): set uclamp_min on all threads (after the game starts).
#   GAME: the game (default Need for Speed: Most Wanted).
name=$1; secs=$2; cpus=$3; shift 3
U=/home/phablet/dolphin-ut-user-w
P=/home/phablet/dolphin-ut-probe
C=/sys/devices/system/cpu/cpufreq/policy6/stats/time_in_state
K=/sys/class/kgsl/kgsl-3d0
cd $P
cat $C > $U/bench-tis0
( sleep 15; while :; do echo "$(($(cat $K/gpuclk)/1000000)) $(cat $K/gpu_busy_percentage | tr -d ' %')"; sleep 2; done ) > $U/bench-gpu 2>/dev/null &
g=$!
GAME=${GAME:-"/home/phablet/dolphin-ut-games/Need for Speed - Most Wanted.iso"} sh run-dolphin.sh ut-start "$secs" --state-slot 1 -C Dolphin.DSP.Volume=0 "$@" > "$U/bench-$name.out" 2>&1 &
if [ "$cpus" != - ] || [ -n "$UCLAMP" ]; then
  i=0; while [ $i -lt 50 ] && ! pidof dolphin-ut >/dev/null; do sleep 0.1; i=$((i+1)); done
  sleep 3
  [ "$cpus" != - ] && taskset -a -p -c "$cpus" "$(pidof dolphin-ut)" >/dev/null
  [ -n "$UCLAMP" ] && python3 $P/uclamp.py "$(pidof dolphin-ut)" "$UCLAMP" >/dev/null
fi
wait %2 2>/dev/null; while pidof dolphin-ut >/dev/null || pgrep -f run-dolphin.sh >/dev/null; do sleep 1; done
kill $g
cat $C > $U/bench-tis1
gpu=$(awk '{c += $1; b += $2; n++} END { if (n) printf "GPU %.0f MHz %.0f%% busy", c/n, b/n }' $U/bench-gpu)
cp "$U/run-ut-start.log" "$U/bench-$name.log"
clk=$(paste $U/bench-tis0 $U/bench-tis1 | awk '{d = $4 - $2; s += d * $1; t += d} END { if (t) printf "%.0f", s / t / 1000 }')
grep "perf:" "$U/bench-$name.log" | awk -v n="$name" -v clk="$clk" -v gpu="$gpu" '
  { split($2, t, ":"); s = t[1]*3600 + t[2]*60 + t[3]; if (!first) first = s
    if (s - first >= 10) { sp = $8; sub("%", "", sp); sum += sp; fps += $4; c++; if (min == "" || sp+0 < min) min = sp; if (sp+0 > max) max = sp } }
  END { if (c) printf "%-24s speed mean %3.0f%% (min %s, max %s), %4.1f fps, big cores %s MHz, %s, %d samples\n", n, sum/c, min, max, fps/c, clk, gpu, c }'
