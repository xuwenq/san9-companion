"""Scan San9PK.exe for memory operands at a given displacement, with context."""
import sys
import capstone
import pefile
from capstone.x86 import X86_OP_MEM

pe = pefile.PE(r'G:\aiprojects\game\san9-companion\San9PK.exe')
base = pe.OPTIONAL_HEADER.ImageBase
text = [s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text'][0]
code = text.get_data()
va0 = base + text.VirtualAddress
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True

targets = [int(a, 16) for a in sys.argv[1:]] or [0x94]
hits = []
for i in md.disasm(code, va0):
    if i.mnemonic == 'call' or i.mnemonic == 'jmp':
        continue
    for op in i.operands:
        if op.type == X86_OP_MEM and op.mem.disp in targets:
            hits.append((i.address, i.bytes.hex(' '), i.mnemonic, i.op_str))
            break

print(f'{len(hits)} hits for disp in {[hex(t) for t in targets]}')
for a, b, m, o in hits:
    print(f'  {a:08X}: {b:<26} {m} {o}')
