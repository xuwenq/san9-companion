"""Locate vtable slots by searching the whole image for method pointers."""
import struct
import pefile

GAME = r'G:\aiprojects\game\san9-companion\San9PK.exe'
pe = pefile.PE(GAME)
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()

for m in (0x44BDA4, 0x44CF20, 0x4FB760, 0x44BD20, 0x44BD30, 0x44BD60):
    pat = struct.pack('<I', m)
    hits = []
    s = 0
    while True:
        i = img.find(pat, s)
        if i < 0:
            break
        hits.append(base + i)
        s = i + 1
    print(f'{m:#x}: {len(hits)} hits -> {[hex(h) for h in hits[:12]]}')
