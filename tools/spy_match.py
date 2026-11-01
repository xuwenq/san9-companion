"""Check whether SanIXSpy.dll's patch table matches San9PK.exe byte-for-byte."""
import struct
import pefile

GAME = r'G:\aiprojects\game\san9-companion\San9PK.exe'
SPY = r'G:\aiprojects\game\san9-companion\sanpkcheat\SanIXSpy.dll'

gp = pefile.PE(GAME)
gbase = gp.OPTIONAL_HEADER.ImageBase
gimg = gp.get_memory_mapped_image()

sp = pefile.PE(SPY)
sbase = sp.OPTIONAL_HEADER.ImageBase
sdata = [s for s in sp.sections if s.Name.rstrip(b'\0') == b'.data'][0]
sd = sdata.get_data()
sva = sbase + sdata.VirtualAddress

TABLE = sva
COUNT = 0x32C // 0x1C
ok = 0
bad = []
for i in range(COUNT):
    off = i * 0x1C
    addr = struct.unpack_from('<I', sd, off)[0]
    orig = sd[off + 4:off + 9]
    stub = struct.unpack_from('<I', sd, off + 0x0C)[0]
    if not (0x400000 <= addr < 0x600000):
        bad.append((i, addr, 'addr out of game range'))
        continue
    g = gimg[addr - gbase:addr - gbase + len(orig)]
    match = (g == orig)
    ok += match
    print(f'entry {i:2d} @{addr:#010x} orig={orig.hex(" ")} '
          f'game={g.hex(" ")} {"OK" if match else "MISMATCH"}  stub={stub:#x}')
print(f'\n{ok}/{COUNT} entries match San9PK.exe exactly')
