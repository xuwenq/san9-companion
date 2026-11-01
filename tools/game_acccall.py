"""Find every call to the officer-table accessor 0x4531B0 and show context."""
import struct
import capstone
import pefile

GAME = r'G:\aiprojects\game\san9-companion\San9PK.exe'
pe = pefile.PE(GAME)
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
text = [s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text'][0]
code = text.get_data()
va0 = base + text.VirtualAddress
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True

TARGET = 0x4531B0


def at(va, n):
    return img[va - base:va - base + n]


sites = []
for off in range(len(code) - 5):
    if code[off] == 0xE8 and va0 + off + 5 + struct.unpack_from('<i', code, off + 1)[0] == TARGET:
        sites.append(va0 + off)
print(f'{len(sites)} call sites to {TARGET:#x}')

for s in sites:
    print(f'\n--- call at {s:#x} ---')
    for i in md.disasm(at(s - 24, 40), s - 24):
        mark = '  <<<' if i.address == s else ''
        print(f'  {i.address:08X}: {i.bytes.hex(" "):<22} {i.mnemonic} {i.op_str}{mark}')
