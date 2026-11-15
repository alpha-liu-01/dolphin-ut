#!/usr/bin/env python3
"""Resolve the PC and LR from Dolphin's crash handler output to library offsets.

  python3 crash-resolve.py LOG MAPS

LOG is a Dolphin log that contains the JitArm64 exception handler's register dump
("PC: 0x…", "R30: 0x…", "Access Address: 0x…"). MAPS is a copy of /proc/PID/maps from the same
run (run-dolphin.sh saves one every 2 s). Prints library+file offset for PC and LR, which can then
be symbolized with addr2line / nm on a copy of that library.
"""
import re
import sys

log = open(sys.argv[1], "rb").read().decode("ascii", "replace")
maps = []
for line in open(sys.argv[2]):
    parts = line.split()
    if len(parts) < 6:
        continue
    a, b = (int(x, 16) for x in parts[0].split("-"))
    maps.append((a, b, int(parts[2], 16), parts[5], parts[1]))


def resolve(addr):
    for a, b, off, name, perms in maps:
        if a <= addr < b:
            return f"{name}+{addr - a + off:#x} ({perms})"
    return "not in any mapping"


found = False
for label, pattern in (("PC", r"PC: (0x[0-9a-f]+)"), ("LR (R30)", r"R30: (0x[0-9a-f]+)"),
                       ("Access address", r"Access Address: (0x[0-9a-f]+)")):
    m = re.findall(pattern, log)
    if m:
        found = True
        addr = int(m[-1], 16)
        print(f"{label:15} {addr:#x}  {resolve(addr) if label != 'Access address' else ''}")
if not found:
    print("no register dump in the log")
