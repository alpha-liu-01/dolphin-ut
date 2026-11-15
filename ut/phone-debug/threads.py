# Per-thread CPU use and core over 3 s windows: python3 threads.py PID [WINDOWS] (stage 3.0 benchmarks).
import os, sys, time
pid = sys.argv[1]
def snap():
    out = {}
    for t in os.listdir(f"/proc/{pid}/task"):
        try:
            s = open(f"/proc/{pid}/task/{t}/stat").read()
            comm = s[s.index("(")+1:s.rindex(")")]
            f = s[s.rindex(")")+2:].split()
            out[t] = (comm, int(f[11]) + int(f[12]), int(f[36]))  # utime+stime, processor
        except OSError:
            pass
    return out
hz = os.sysconf("SC_CLK_TCK")
for _ in range(int(sys.argv[2]) if len(sys.argv) > 2 else 3):
    a = snap(); time.sleep(3); b = snap()
    rows = sorted(((b[t][1] - a[t][1]) / hz / 3 * 100, b[t][0], b[t][2], t) for t in b if t in a)
    print(" | ".join(f"{c} {p:.0f}% cpu{cpu}" for p, c, cpu, t in reversed(rows) if p >= 3))
