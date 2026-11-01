"""Find who fills the officer table 0x1258EE0 and what field at +0x04 holds."""
import struct
import pefile

GAME = r'G:\aiprojects\game\san9-companion\San9PK.exe'
pe = pefile.PE(GAME)
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()

TARGETS = [0x1258EE0, 0x4531B0, 0x14FDAD0]


def scan(pattern):
    """Find 4-byte LE occurrences of pattern in the whole image."""
    out = []
    start = 0
    while True:
        i = img.find(pattern, start)
        if i < 0:
            break
        out.append(base + i)
        start = i + 1
    return out


for t in TARGETS:
    pat = struct.pack('<I', t)
    hits = scan(pat)
    print(f'=== refs to {t:#x} ({len(hits)}) ===')
    for h in hits[:40]:
        sec = ''
        for s in pe.sections:
            lo = base + s.VirtualAddress
            hi = lo + max(s.Misc_VirtualSize, s.SizeOfRawData)
            if lo <= h < hi:
                sec = s.Name.rstrip(b'\0').decode()
        print(f'  {h:#x}  {sec}')
    print()
