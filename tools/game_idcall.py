"""Find how the game obtains the officer id passed to the officer-table accessor 0x4531B0."""
import struct
import capstone
import pefile

pe = pefile.PE(r'G:\aiprojects\game\san9-companion\San9PK.exe')
base = pe.OPTIONAL_HEADER.ImageBase
text = [s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text'][0]
code = text.get_data()
va0 = base + text.VirtualAddress
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

TARGET = 0x4531B0
calls = []
for off in range(len(code) - 5):
    if code[off] == 0xE8:
        rel = struct.unpack_from('<i', code, off + 1)[0]
        if va0 + off + 5 + rel == TARGET:
            calls.append(va0 + off)

print(f'{len(calls)} call sites of {TARGET:#x}')
for c in calls:
    start = c - 24
    if start < va0:
        start = va0
    chunk = code[start - va0:c + 5 - va0]
    lines = []
    for i in md.disasm(chunk, start):
        mark = '  <<<' if i.address == c else ''
        lines.append(f'{i.address:08X}: {i.bytes.hex(" "):<24} {i.mnemonic} {i.op_str}{mark}')
    print('\n'.join(lines))
    print('-' * 70)
