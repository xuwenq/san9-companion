"""Resolve: what is esi at 0x4FF08B, and where does the proficiency-cap function's
esi come from?  Uses the RTTI type descriptor 0x6060e8 and caller analysis."""
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


def at(va, n):
    return img[va - base:va - base + n]


def dis(va, n=0x40):
    return list(md.disasm(at(va, n), va))


def callers(t):
    out = []
    for off in range(len(code) - 5):
        if code[off] == 0xE8 and va0 + off + 5 + struct.unpack_from('<i', code, off + 1)[0] == t:
            out.append(va0 + off)
    return out


print('=== 1. RTTI type descriptor 0x6060e8 ===')
# The descriptor is a std::type_info: [vptr][name ptr]
name_ptr = struct.unpack_from('<I', at(0x6060e8 + 4, 4))[0]
print('  name ptr', hex(name_ptr))
try:
    raw = at(name_ptr, 64)
    end = raw.find(b'\x00')
    print('  name =', raw[:end].decode('ascii', 'replace'))
except Exception as e:
    print('  err', e)

print('\n=== 2. who calls the proficiency-cap function 0x44C980 ? ===')
for c in callers(0x44C980):
    print(f'  caller {c:#x}:')
    for i in dis(c - 20, 30):
        mark = '  <<<' if i.address == c else ''
        print(f'    {i.address:08X}: {i.bytes.hex(" "):<24} {i.mnemonic} {i.op_str}{mark}')

print('\n=== 3. who calls the interval-setup function 0x4FF030 ? ===')
for c in callers(0x4FF030)[:6]:
    print(f'  caller {c:#x}:')
    for i in dis(c - 24, 32):
        mark = '  <<<' if i.address == c else ''
        print(f'    {i.address:08X}: {i.bytes.hex(" "):<24} {i.mnemonic} {i.op_str}{mark}')

print('\n=== 4. 武将属性表 0x1258EE0 first records: +0x00..+0x20 (file image) ===')
for i in range(4):
    rec = at(0x1258EE0 + i * 0x128, 0x20)
    print(f'  rec {i}: {rec.hex(" ")}')
