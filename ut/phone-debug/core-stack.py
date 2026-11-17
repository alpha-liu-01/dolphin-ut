#!/usr/bin/env python3
# core-stack.py CORE MAPS SP [BYTES]: like stack-scan.py, but on a core dump (stage 6.3): prints the
# 8-byte values above SP that point into dolphin-ut's executable mapping, as file offsets, for
# addr2line on the same build. For crashes where core-info.py's frame-pointer chain stops at once
# (a fault in code called from the JIT or the interpreter). SP: from the log's "SP:" line (Dolphin's
# fault handler) or core-info.py's registers. Most hits are return addresses, newest first.
import struct, sys
core, maps_path, sp = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
size = int(sys.argv[4]) if len(sys.argv) > 4 else 65536
exe = []
for line in open(maps_path):
    p = line.split()
    if len(p) >= 6 and "x" in p[1] and p[5].endswith("/dolphin-ut"):
        a, b = (int(x, 16) for x in p[0].split("-"))
        exe.append((a, b, int(p[2], 16)))
with open(core, "rb") as f:
    h = f.read(64)
    phoff, = struct.unpack_from("<Q", h, 0x20)
    phentsize, phnum = struct.unpack_from("<HH", h, 0x36)
    f.seek(phoff)
    ph = f.read(phentsize * phnum)
    data = None
    for i in range(phnum):
        t, flags, off, vaddr, paddr, filesz, memsz, align = struct.unpack_from("<IIQQQQQQ", ph, i * phentsize)
        if t == 1 and vaddr <= sp < vaddr + filesz:
            f.seek(off + sp - vaddr)
            data = f.read(min(size, vaddr + filesz - sp))
            break
if data is None:
    sys.exit("SP not in the core")
last = None
for i in range(0, len(data) - 7, 8):
    v = int.from_bytes(data[i:i + 8], "little")
    for a, b, off in exe:
        if a <= v < b and v - a + off != last:
            last = v - a + off
            print(f"sp+{i:#06x} {last:#x}")
