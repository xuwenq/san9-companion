"""Resolve the class of a vtable by walking the MSVC RTTI chain.

MSVC x86: vtable[-1] = pointer to CompleteObjectLocator (COL).
COL layout (32-bit): signature(4) offset(4) cdOffset(4) pTypeDescriptor(4) pClassHierarchy(4)
pTypeDescriptor: spare(4) name(ptr to string)
"""
import struct
import pefile

GAME = r'G:\aiprojects\game\san9-companion\San9PK.exe'
pe = pefile.PE(GAME)
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()


def at(va, n):
    return img[va - base:va - base + n]


def u32(va):
    return struct.unpack_from('<I', at(va, 4))[0]


def td_name(td):
    nptr = u32(td + 4)
    raw = at(nptr, 96)
    return raw[:raw.find(b'\x00')].decode('ascii', 'replace')


def col_of(vt):
    """vtable address -> (name, col_addr)"""
    col = u32(vt - 4)
    if not (0x400000 <= col < 0x600000):
        return None
    sig = u32(col)
    if sig != 0:
        return None
    td = u32(col + 12)
    if not (0x400000 <= td < 0x600000):
        return None
    try:
        nm = td_name(td)
    except Exception:
        return None
    if not nm.startswith('.'):
        return None
    return nm, col


def find_vtables():
    """Scan .rdata for vtables: a run of dwords that are code addresses,
    preceded by a COL pointer."""
    out = []
    i = 0
    rdata = None
    for s in pe.sections:
        if s.Name.rstrip(b'\0') == b'.rdata':
            rdata = (base + s.VirtualAddress, s.Misc_VirtualSize)
    lo, hi = rdata
    va = lo
    while va < hi - 8:
        # candidate vtable start: a code address
        v = u32(va)
        if 0x401000 <= v < 0x5FF000:
            r = col_of(va)
            if r:
                out.append((va, r[0], r[1]))
                # skip the whole table
                n = 1
                while 0x401000 <= u32(va + 4 * n) < 0x5FF000:
                    n += 1
                va += 4 * n
                continue
        va += 4
    return out


vts = find_vtables()
print(f'{len(vts)} vtables found')

want = {'CPersonData', 'CUnitData', 'CCityData', 'CHeishiPersonData', 'CUserHeishiPersonData',
        'CTourokuPersonData', 'CPersonList', 'CUnitList'}
for vt, name, col in vts:
    if name in want:
        print(f'\n=== {name} vtable @ {vt:#x} (COL {col:#x}) ===')
        n = 0
        while 0x401000 <= u32(vt + 4 * n) < 0x5FF000:
            print(f'  +{n*4:#05x}: {u32(vt + 4*n):#010x}')
            n += 1
        print(f'  ({n} entries)')
