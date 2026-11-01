"""Scan San9PK.exe .text for instructions of interest (offline)."""
import sys
import capstone
import pefile

pe = pefile.PE(r'G:\aiprojects\game\san9-companion\San9PK.exe')
base = pe.OPTIONAL_HEADER.ImageBase
text = None
for s in pe.sections:
    if s.Name.rstrip(b'\0') == b'.text':
        text = s
assert text
code = text.get_data()
va0 = base + text.VirtualAddress
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True

mode = sys.argv[1] if len(sys.argv) > 1 else '94'

for i in md.disasm(code, va0):
    if mode == '94':
        # mov reg, [reg+0x94]  (excluding 0x194/0x294/0x394 etc.)
        if i.mnemonic != 'mov':
            continue
        op = i.op_str
        if '0x94]' in op or '+ 0x94]' in op:
            if '- 0x' in op or '+ 0x194' in op or '+ 0x294' in op:
                continue
            print(f'{i.address:08X}: {i.mnemonic} {i.op_str}')
    elif mode == 'call':
        if i.mnemonic == 'call' and i.op_str in ('0x44cf20', '0x44cf30'):
            print(f'{i.address:08X}: {i.mnemonic} {i.op_str}')
    elif mode == 'range':
        lo, hi = int(sys.argv[2], 16), int(sys.argv[3], 16)
        if lo <= i.address < hi:
            print(f'  {i.address:08X}: {i.bytes.hex(" "):<26} {i.mnemonic} {i.op_str}')
