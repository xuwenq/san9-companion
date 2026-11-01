"""Resolve the officer-id space at the hook sites.

Questions:
 1. RTTI name of the class whose instances appear at 0x4FF08B (RTTI desc 0x6060e8).
 2. RTTI name of the class that owns 0x44BDA4 (setter of +0x94) / 0x44CF20 (getter).
 3. Are they the same class?
"""
import struct
import capstone
import pefile

GAME = r'G:\aiprojects\game\san9-companion\San9PK.exe'
pe = pefile.PE(GAME)
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True


def at(va, n):
    return img[va - base:va - base + n]


def rtti_name(td):
    """td = address of the MSVC TypeDescriptor."""
    nptr = struct.unpack_from('<I', at(td + 4, 4))[0]
    raw = at(nptr, 96)
    return raw[:raw.find(b'\x00')].decode('ascii', 'replace')


def vtable_for(method_va):
    """Find the .rdata vtable containing method_va, return (vtable_start, rtti_name)."""
    pat = struct.pack('<I', method_va)
    start = 0
    while True:
        i = img.find(pat, start)
        if i < 0:
            return None
        va = base + i
        # vtable entries are contiguous; the COL pointer sits at vtable_start-4
        # Walk back to find a plausible vtable start: entries must be in .text
        # Heuristic: the slot is 4-aligned; look for the COL at vtable_start-4
        # whose +0x0C points to a TypeDescriptor with a printable name.
        for back in range(4, 0x100, 4):
            cand = va - back
            col = struct.unpack_from('<I', at(cand - 4, 4))[0]
            if not (0x400000 <= col < 0x600000):
                continue
            try:
                td = struct.unpack_from('<I', at(col + 0x0C, 4))[0]
                if not (0x400000 <= td < 0x600000):
                    continue
                nm = rtti_name(td)
            except Exception:
                continue
            if nm and nm.startswith('.'):
                return cand, nm
        start = i + 1
    return None


print('=== RTTI of 0x6060e8 (class seen at 0x4FF08B) ===')
try:
    print('  ', rtti_name(0x6060e8))
except Exception as e:
    print('  err', e)

for m in (0x44BDA4, 0x44CF20, 0x4FB760, 0x44BD30, 0x44BD20):
    r = vtable_for(m)
    print(f'=== vtable for {m:#x} -> {r} ===')
