"""Find whether the officer-table index is derived from word[reg+4]."""
import struct
import capstone
import pefile

pe = pefile.PE(r'G:\aiprojects\game\san9-companion\San9PK.exe')
base = pe.OPTIONAL_HEADER.ImageBase
text = [s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text'][0]
code = text.get_data()
va0 = base + text.VirtualAddress
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True

TARGET = 0x4531B0
hits = []
for off in range(len(code) - 5):
    if code[off] != 0xE8:
        continue
    rel = struct.unpack_from('<i', code, off + 1)[0]
    if va0 + off + 5 + rel != TARGET:
        continue
    # look back up to 20 bytes for the index source
    start = max(va0, va0 + off - 24)
    chunk = code[start - va0:off + 5 - va0]
    prev = None
    for i in md.disasm(chunk, start):
        if i.address == va0 + off:
            break
        prev = i
    if prev is None:
        continue
    # is the pushed value a word load from [reg+4]?
    interesting = ('word ptr' in prev.op_str and prev.op_str.endswith(' + 4]')) or \
                 ('word ptr' in prev.op_str and prev.op_str.endswith(' + 4]')) or \
                 (prev.mnemonic == 'movzx' and '+ 4]' in prev.op_str)
    if interesting:
        hits.append((va0 + off, prev))

print(f'{len(hits)} call sites where index comes from a word load at [reg+4]')
for c, prev in hits:
    print(f'  call @{c:#x}   index source: {prev.mnemonic} {prev.op_str}')
