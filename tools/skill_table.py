"""Read the game's skill table +0x7C (interval) and officer table from the mapped image."""
import pefile

pe = pefile.PE(r'G:\aiprojects\game\san9-companion\San9PK.exe')
img = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase


def at(va, n):
    off = va - base
    return img[off:off + n]


print('=== skill table 0x14FDAD0, record 0x8C, 41 entries: +0x7C (interval), +0x64, +0x58 ===')
for i in range(41):
    rec = at(0x14FDAD0 + i * 0x8C, 0x8C)
    name = rec[0x10:0x20].split(b'\x00')[0]
    try:
        nm = name.decode('gbk')
    except Exception:
        nm = repr(name)
    print(f'  skill {i:2d}: +0x7C={rec[0x7C]:3d}  +0x64={rec[0x64]:3d}  '
          f'+0x58={int.from_bytes(rec[0x58:0x5A], "little"):5d}  name={nm}')
