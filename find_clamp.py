import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
pe = pefile.PE('San9PK.exe')
base = pe.OPTIONAL_HEADER.ImageBase
text = pe.sections[0]
data = pe.get_data(text.VirtualAddress, text.SizeOfRawData)
tv = base + text.VirtualAddress
md = Cs(CS_ARCH_X86, CS_MODE_32)
md.detail = True
pat = bytes.fromhex('b8 e8 03 00 00')  # mov eax, 0x3E8
idxs=[]; i=0
while True:
    i=data.find(pat,i)
    if i<0: break
    idxs.append(tv+i); i+=1
lines=['mov eax,1000 sites: %d' % len(idxs)]
for va in idxs:
    d = pe.get_data(va-base-0x14, 0x30)
    lines.append('=== 0x%X ===' % va)
    for ins in md.disasm(d, va-0x14):
        lines.append('  0x%X  %-20s %-7s %s' % (ins.address, ins.bytes.hex(' '), ins.mnemonic, ins.op_str))
open('clamp_sites.txt','w',encoding='utf-8').write('\n'.join(lines))
print('done', len(idxs))
