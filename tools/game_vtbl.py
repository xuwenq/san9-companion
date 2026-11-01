"""Find the vtable containing the +0x94 setter (0x44BDA4) and its callers."""
import struct
import capstone
import pefile

pe = pefile.PE(r'G:\aiprojects\game\san9-companion\San9PK.exe')
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
text = [s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text'][0]
code = text.get_data()
va0 = base + text.VirtualAddress
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

SETTER = 0x44BDA4

# find the vtable: a dword-aligned run containing SETTER
found = None
for va in range(0x60F000, 0x612000, 4):
    try:
        v = struct.unpack_from('<I', img[va - base:va - base + 4])[0]
    except IndexError:
        continue
    if v == SETTER:
        # walk back to find the vtable start (heuristic: preceded by a pointer to 0x60xxxx rtti)
        start = va
        for back in range(va, va - 0x200, -4):
            pv = struct.unpack_from('<I', img[back - base:back - base + 4])[0]
            if 0x400000 <= pv <= 0x600000:
                start = back + 4
                break
        found = (start, va)
        break

print('vtable entry for +0x94 setter:', hex(found[1]) if found else None)
if found:
    start, slot = found
    print('vtable base guess', hex(start))
    for i in range(-8, 12):
        v = struct.unpack_from('<I', img[start + i * 4 - base:start + i * 4 - base + 4])[0]
        mark = '  <-- set +0x94' if start + i * 4 == slot else ''
        print(f'  {start + i * 4:#x}: {v:#010x}{mark}')

# Find indirect calls through vtable slot: pattern  FF 50 xx / FF 52 xx with that offset
if found:
    slot_off = slot - start
    print(f'\nlooking for "call [reg+0x{slot_off:x}]" ...')
    for off in range(len(code) - 3):
        if code[off] == 0xFF and code[off + 1] in (0x50, 0x51, 0x52, 0x53, 0x90, 0x91, 0x92, 0x93):
            if code[off + 2] == slot_off:
                print(f'  {va0 + off:#x}: {code[off:off+3].hex(" ")}')
