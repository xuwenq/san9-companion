"""Scan every uploaded original-tool block for unguarded null dereferences.

Defect class (crash #2, 2026-10-30): the original-tool block unk_42C894 does

    call 0x5B71D0        ; lookup, may return 0
    mov  ecx,[0x661140]
    mov  [eax+4],ecx    ; <-- writes through a possibly-NULL eax

i.e. it dereferences the result of a call without checking it for NULL.

This script scans all 329 blocks we upload for the same shape:
    a call (direct or through eax) whose result register is dereferenced
    before any test/cmp of that register.
"""
import re
import struct

from capstone import Cs, CS_ARCH_X86, CS_MODE_32

cd = open('src/generated/companion_data.bin', 'rb').read()
CD_BASE = 0x41F000
ALLOC_BASE = 0x20000
OT_BASE = 0xA0000

cs = Cs(CS_ARCH_X86, CS_MODE_32)
cs.detail = True

# ---- upload targets -> (source, size) as installed by RuntimeInjection2026.cpp
src_cpp = open('src/runtime/RuntimeInjection2026.cpp', encoding='utf-8', errors='replace').read()
sym2va = {}
for line in open('src/generated/Symbols.inc', encoding='utf-8', errors='replace'):
    m = re.match(r'\s*#define\s+(\w+)\s*\(global<[^>]+>\(0x([0-9A-Fa-f]+)\)\)', line)
    if m:
        sym2va[m.group(1)] = int(m.group(2), 16)

local_sizes = {}
for m in re.finditer(r'(?:static\s+const\s+)?BYTE\s+(\w+)\[\]\s*=\s*\{(.*?)\};', src_cpp, re.S):
    local_sizes[m.group(1)] = len(re.findall(r'0x[0-9a-fA-F]{1,2}', m.group(2)))

uploads = {}
for m in re.finditer(r'writePatch\(\s*(\w+)\s*,\s*&(\w+)\s*,\s*([^)]+)\)', src_cpp):
    slot, sym, szs = m.group(1), m.group(2), m.group(3).strip()
    if sym not in sym2va:
        continue
    va = sym2va[sym]
    if szs.startswith('sizeof('):
        n = local_sizes.get(szs[7:-1])
    else:
        try:
            n = int(szs.rstrip('uU'), 0)
        except ValueError:
            n = None
    if n is None:
        continue
    # the slot symbol points at the upload-target dword; read the target from data
    slot_va = sym2va.get(slot)
    if slot_va is None:
        continue
    off = slot_va - CD_BASE
    if not (0 <= off <= len(cd) - 4):
        continue
    target = struct.unpack_from('<I', cd, off)[0]
    uploads[(target, n)] = (va, sym)

print('uploads resolved: %d' % len(uploads))

# ---- relocation slots (original-tool addresses get rebased by delta)
RELOC = set()
for line in open('src/generated/relocations.inc', encoding='utf-8', errors='replace'):
    m = re.search(r'\{(?:true|false),\s*0x([0-9A-Fa-f]+)\}', line)
    if m:
        RELOC.add(int(m.group(1), 16))

# A direct call inside a block: `E8 rel32` whose target lands in the game image.
GAME_LO, GAME_HI = 0x400000, 0x1759000


def find_deref_after_call(insns, base):
    """Find a call whose result register is dereferenced before being tested."""
    hits = []
    for idx, ins in enumerate(insns):
        if ins.mnemonic != 'call':
            continue
        # which register receives the result?  x86 -> eax for near calls
        reg = 'eax'
        # look at the following instructions for a memory deref of that register
        for j in range(idx + 1, min(idx + 8, len(insns))):
            nxt = insns[j]
            ops = nxt.op_str
            # a test/cmp on the register ends the window
            if nxt.mnemonic in ('test', 'cmp'):
                if re.search(r'\b%s\b' % reg, ops):
                    break
                continue
            # a dereference of the register
            deref = re.search(r'(?:byte|word|dword|qword)\s+ptr\s+\[([a-z0-9]+)(?:\+0x[0-9a-f]+)?\]', ops)
            if deref and deref.group(1) == reg:
                hits.append((ins.address, ins.op_str, nxt.address, nxt.mnemonic, nxt.op_str))
                break
    return hits


print()
print('=== blocks that dereference a call result without testing it ===')
total = 0
report = []
for (target, size), (src_va, sym) in sorted(uploads.items()):
    if not (CD_BASE <= src_va and src_va + size <= CD_BASE + len(cd)):
        continue
    data = bytearray(cd[src_va - CD_BASE:src_va - CD_BASE + size])
    # apply rebase delta to relocated 4-byte slots
    for i in range(0, max(0, len(data) - 3)):
        if (src_va + i) in RELOC:
            v = struct.unpack_from('<I', data, i)[0]
            struct.pack_into('<I', data, i, (v + (ALLOC_BASE - OT_BASE)) & 0xFFFFFFFF)
    block_va = target
    insns = list(cs.disasm(bytes(data), block_va))
    if not insns:
        continue
    for (call_va, call_op, deref_va, mn, ops) in find_deref_after_call(insns, block_va):
        total += 1
        report.append((sym, block_va, size, call_va, call_op, deref_va, mn, ops))

for (sym, bva, size, cva, cop, dva, mn, ops) in report:
    print('  %-26s block %08X (%d B)' % (sym, bva, size))
    print('      call  %08X: call %s' % (cva, cop))
    print('      deref %08X: %s %s' % (dva, mn, ops))
    print()
print('total findings: %d' % total)
