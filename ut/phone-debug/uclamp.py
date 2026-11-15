# uclamp.py PID UTIL_MIN: sets uclamp_min on every thread of PID (sched_setattr, keeping policy and
# priority). Prints the result per thread.
import ctypes, os, struct, sys
libc = ctypes.CDLL(None, use_errno=True)
SYS_sched_setattr = 274  # aarch64
FLAGS = 0x08 | 0x10 | 0x20  # KEEP_POLICY | KEEP_PARAMS | UTIL_CLAMP_MIN
pid, util_min = sys.argv[1], int(sys.argv[2])
attr = struct.pack("=IIQiIQQQII", 56, 0, FLAGS, 0, 0, 0, 0, 0, util_min, 1024)
buf = ctypes.create_string_buffer(attr, 56)
res = {}
for t in os.listdir(f"/proc/{pid}/task"):
    r = libc.syscall(SYS_sched_setattr, int(t), buf, 0)
    res.setdefault("ok" if r == 0 else os.strerror(ctypes.get_errno()), []).append(t)
print({k: len(v) for k, v in res.items()})
