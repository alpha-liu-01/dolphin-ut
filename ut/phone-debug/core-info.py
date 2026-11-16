#!/usr/bin/env python3
# core-info.py CORE MAPS: the crash in an arm64 core dump, read on the phone (cores of dolphin-ut
# are tens of GB, mostly the reserved fastmem space, so they are not copied off).
# Prints the signal, its code and fault address, the crashing thread's registers and a backtrace
# along the frame-pointer chain, each address placed in a library with MAPS (run-dolphin.sh's
# copy of /proc/PID/maps, kept with the core).
import struct, sys

core = sys.argv[1]
maps = sys.argv[2] if len(sys.argv) > 2 else None
f = open(core, 'rb')
hdr = f.read(64)
phoff, = struct.unpack_from('<Q', hdr, 32)
phentsize, phnum = struct.unpack_from('<HH', hdr, 54)

regions = []
for line in open(maps) if maps else []:
    p = line.split()
    lo, hi = (int(x, 16) for x in p[0].split('-'))
    regions.append((lo, hi, int(p[2], 16), p[1], p[5] if len(p) > 5 else ''))


def where(a):
    for lo, hi, off, perms, name in regions:
        if lo <= a < hi:
            return f'{name or "[anon]"}+{a - lo + off:#x} ({perms})'
    return '?'


loads, notes = [], []
for i in range(phnum):
    f.seek(phoff + i * phentsize)
    ptype, flags, off, va, pa, filesz, memsz = struct.unpack('<IIQQQQQ', f.read(48))
    if ptype == 1:
        loads.append((va, filesz, off))
    elif ptype == 4:
        f.seek(off)
        notes.append(f.read(filesz))


def read(addr, size):
    for va, filesz, off in loads:
        if va <= addr and addr + size <= va + filesz:
            f.seek(off + addr - va)
            return f.read(size)
    return None


SIGINFO, NT_FILE = 0x53494749, 0x46494c45
threads, siginfo = [], None
# The core's own list of mapped files (NT_FILE), used when MAPS is empty or missing.
for blob in notes:
    pos = 0
    while pos + 12 <= len(blob):
        namesz, descsz, ntype = struct.unpack_from('<III', blob, pos)
        d = pos + 12 + ((namesz + 3) & ~3)
        if ntype == NT_FILE and not regions:
            count, page = struct.unpack_from('<QQ', blob, d)
            names = blob[d + 16 + count * 24:d + descsz].split(b'\0')
            for k in range(count):
                lo, hi, pgoff = struct.unpack_from('<QQQ', blob, d + 16 + k * 24)
                regions.append((lo, hi, pgoff * page, 'core', names[k].decode(errors='replace')))
        pos = d + ((descsz + 3) & ~3)
for blob in notes:
    pos = 0
    while pos + 12 <= len(blob):
        namesz, descsz, ntype = struct.unpack_from('<III', blob, pos)
        d = pos + 12 + ((namesz + 3) & ~3)
        if ntype == 1:  # NT_PRSTATUS
            sig, = struct.unpack_from('<h', blob, d + 12)
            pid, = struct.unpack_from('<i', blob, d + 32)
            threads.append((pid, sig, struct.unpack_from('<34Q', blob, d + 112)))
        elif ntype == SIGINFO and siginfo is None:
            signo, errno, code = struct.unpack_from('<iii', blob, d)
            addr, = struct.unpack_from('<Q', blob, d + 16)
            siginfo = (signo, code, addr)
        pos = d + ((descsz + 3) & ~3)

if siginfo:
    signo, code, addr = siginfo
    names = {7: {1: 'BUS_ADRALN', 2: 'BUS_ADRERR', 3: 'BUS_OBJERR'}, 11: {1: 'SEGV_MAPERR', 2: 'SEGV_ACCERR'}}
    print(f'signal {signo}, code {names.get(signo, {}).get(code, code)}, address {addr:#x} {where(addr)}')

# The first thread in the core is the one that took the signal.
pid, sig, regs = threads[0]
print(f'thread {pid} (of {len(threads)}), signal {sig}')
print(f'  pc {regs[32]:#x} {where(regs[32])}')
print(f'  lr {regs[30]:#x} {where(regs[30])}')
print(f'  sp {regs[31]:#x}')
for r in range(0, 30, 2):
    print(f'  x{r:<2} {regs[r]:#018x} {where(regs[r])[:60]:60}  x{r+1:<2} {regs[r+1]:#018x} {where(regs[r+1])[:60]}')
print('backtrace (frame pointers):')
fp, n = regs[29], 0
while fp and n < 24:
    frame = read(fp, 16)
    if not frame:
        break
    fp, ret = struct.unpack('<QQ', frame)
    print(f'  #{n} {ret:#x} {where(ret)}')
    n += 1
