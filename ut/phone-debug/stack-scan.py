#!/usr/bin/env python3
"""Heuristic stack scan for a stuck thread when no debugger is available on the phone.

  python3 stack-scan.py PID TID [BYTES]

Reads the thread's stack pointer from /proc/PID/task/TID/syscall, then scans BYTES (default 32 KiB)
of /proc/PID/mem upward from there and prints every 8-byte value that points into an executable
mapping, as "library+offset" (file offset). Most hits are return addresses, newest first; some are
stale. Symbolize the offsets afterwards with addr2line/nm on a copy of the library (with
`aarch64-linux-gnu-addr2line -f -e <lib> <offset>` for ELF files where vaddr == file offset).
"""
import sys

pid, tid = sys.argv[1], sys.argv[2]
size = int(sys.argv[3]) if len(sys.argv) > 3 else 32768
fields = open(f"/proc/{pid}/task/{tid}/syscall").read().split()
sp = int(fields[-2], 16)
maps = []
for line in open(f"/proc/{pid}/maps"):
    parts = line.split()
    if len(parts) < 6 or "x" not in parts[1]:
        continue
    a, b = (int(x, 16) for x in parts[0].split("-"))
    maps.append((a, b, int(parts[2], 16), parts[5]))
with open(f"/proc/{pid}/mem", "rb") as mem:
    mem.seek(sp)
    data = mem.read(size)
print(f"syscall {fields[0]}, sp {sp:#x}")
last = None
for i in range(0, len(data) - 7, 8):
    v = int.from_bytes(data[i:i + 8], "little")
    for a, b, off, name in maps:
        if a <= v < b:
            hit = (name, v - a + off)
            if hit != last:
                print(f"  sp+{i:#06x}  {name}+{v - a + off:#x}")
            last = hit
            break
