"""Audit every game patch we install for call-contract violations.

Defect class found on 2026-10-31 (crash #3):
  a patch replaces a game call site with a block that returns a DIFFERENT value
  (or clobbers eax) than the original call did, and the caller dereferences the
  result without a null check.

For each descriptor we write into the game we recover:
  * the original game bytes at the target (San9PK.exe),
  * the payload bytes we install,
and then check whether the payload is a call into the allocation region whose
  return value the game consumes.
"""
import json
import re
import struct

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

ROOT = '.'
GAME = 'San9PK.exe'
CD_BASE = 0x41F000
ALLOC_BASE = 0x20000          # value observed in the 2026-10-31 log
OT_BASE = 0xA0000            # original-tool domain base

pe = pefile.PE(GAME)
IMAGE_BASE = pe.OPTIONAL_HEADER.ImageBase

cd = open('src/generated/companion_data.bin', 'rb').read()


def game_bytes(va, size):
    off = pe.get_offset_from_rva(va - IMAGE_BASE)
    return pe.__data__[off:off + size]


def cd_bytes(va, size):
    if not (CD_BASE <= va and va + size <= CD_BASE + len(cd)):
        return None
    return cd[va - CD_BASE:va - CD_BASE + size]


def reloc_delta():
    """delta applied by rebase(): allocBase - originalToolBase."""
    return ALLOC_BASE - OT_BASE


# ---------------------------------------------------------------- descriptors
def all_descriptors():
    out = []
    for va in range(CD_BASE, CD_BASE + len(cd) - 11, 4):
        t, l, s = struct.unpack_from('<3I', cd, va - CD_BASE)
        if l == 0 or l > 0x2000:
            continue
        if not (0x400000 <= t < 0x1759000):
            continue
        out.append((va, t, l, s))
    return out


DESCS = all_descriptors()


def descriptors_for_target(target):
    return [d for d in DESCS if d[1] == target]


# ------------------------------------------------------- descriptors we write
def written_descriptor_vas():
    src = open('src/runtime/RuntimeInjection2026.cpp', encoding='utf-8', errors='replace').read()
    vas = set()
    for m in re.finditer(r'writeDescriptor(?:Range)?\(\s*(0x[0-9A-Fa-f]+)u', src):
        vas.add(int(m.group(1), 16))
    return vas


WRITTEN = written_descriptor_vas()


def is_written(desc_va):
    return desc_va in WRITTEN


# ------------------------------------------------------------- call analysis
cs = Cs(CS_ARCH_X86, CS_MODE_32)
cs.detail = True

# relocation slots that hold an original-tool address
RELOC = set()
for line in open('src/generated/relocations.inc', encoding='utf-8', errors='replace'):
    m = re.search(r'\{(?:true|false),\s*0x([0-9A-Fa-f]+)\}', line)
    if m:
        RELOC.add(int(m.group(1), 16))


def payload_after_rebase(desc_va, source, length):
    """Return the payload bytes as installed, with relocated displacements fixed."""
    b = bytearray(cd_bytes(source, length) or b'')
    # apply relocation to any 4-byte slot covered by the payload
    for i in range(0, max(0, len(b) - 3)):
        slot = source + i
        if slot in RELOC:
            v = struct.unpack_from('<I', b, i)[0]
            v = (v + reloc_delta()) & 0xFFFFFFFF
            struct.pack_into('<I', b, i, v)
    return bytes(b)


def analyse_call(payload, install_va):
    """Describe the payload's call/return contract.

    Returns dict with:
      kind      : 'call' | 'ret' | 'other'
      target    : resolved call target (allocation or game)
      consumes_ret : True if the game's next instruction reads the returned reg
    """
    info = {'kind': 'other', 'target': None, 'call_eax': False, 'insns': []}
    for i in cs.disasm(payload, install_va):
        info['insns'].append((i.address, i.mnemonic, i.op_str))
        if i.mnemonic == 'call' and info['kind'] == 'other':
            if i.op_str == 'eax':
                info['kind'] = 'call'
                info['call_eax'] = True
            else:
                try:
                    info['kind'] = 'call'
                    info['target'] = int(i.op_str, 16)
                except ValueError:
                    pass
        elif i.mnemonic == 'ret':
            if info['kind'] == 'other':
                info['kind'] = 'ret'
    return info


def following_insns(va, count):
    try:
        b = game_bytes(va, 0x20)
    except Exception:
        return []
    return list(cs.disasm(b, va))[:count]


def reads_register(insn, reg):
    return re.search(r'\b%s\b' % reg, insn.op_str) is not None


def main():
    print('descriptors in companion_data : %d' % len(DESCS))
    print('descriptor VA slots written   : %d' % len(WRITTEN))
    print()

    findings = []
    for desc_va, target, length, source in DESCS:
        if not is_written(desc_va):
            continue

        payload = payload_after_rebase(desc_va, source, length)
        if not payload:
            continue

        # does the payload begin with a call into the allocation region?
        info = analyse_call(payload, target)
        if info['kind'] != 'call':
            continue

        call_target = info['target']
        if info['call_eax']:
            # mov eax, imm ; call eax  -> resolve the immediate
            call_target = None
            for (addr, mn, op) in info['insns']:
                if mn == 'mov' and op.startswith('eax,'):
                    try:
                        call_target = int(op.split(',')[1].strip(), 16)
                    except ValueError:
                        pass
                if mn == 'call':
                    break
        if call_target is None:
            continue

        # the payload's call target must be an allocation address
        if not (ALLOC_BASE <= call_target < ALLOC_BASE + 0xB000):
            continue

        # Now look at the ORIGINAL game bytes at that call site, and what the
        # game does with the value the original call returned.
        orig = game_bytes(target, length)
        orig_insns = list(cs.disasm(orig, target))
        orig_kind = None
        for i in orig_insns:
            if i.mnemonic == 'call':
                orig_kind = 'call'
                break
            if i.mnemonic == 'ret':
                orig_kind = 'ret'
                break
        if orig_kind != 'call':
            continue

        # what does the game do right AFTER the patched region?
        after = following_insns(target + length, 8)
        consumes = []
        for i in after:
            if i.mnemonic in ('mov', 'lea', 'push', 'test', 'cmp', 'add', 'sub', 'or', 'and'):
                if reads_register(i, 'eax') or reads_register(i, 'ecx'):
                    consumes.append(i)
            if consumes:
                break

        findings.append((desc_va, target, length, source, call_target,
                         orig.hex(), payload.hex(), consumes))

    print('=== patched game call sites that now jump into our allocation ===')
    print('count: %d' % len(findings))
    print()
    for (desc_va, target, length, source, call_target,
         orig_hex, pay_hex, consumes) in findings:
        print('desc@%08X -> game %08X len=%d  -> alloc %08X'
              % (desc_va, target, length, call_target))
        print('   original: %s' % orig_hex)
        print('   payload : %s' % pay_hex)
        for i in consumes:
            print('   game next: %08X: %s %s' % (i.address, i.mnemonic, i.op_str))
        print()


if __name__ == '__main__':
    main()
