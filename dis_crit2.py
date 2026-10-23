import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
pe = pefile.PE('San9PK.exe')
base = pe.OPTIONAL_HEADER.ImageBase
md = Cs(CS_ARCH_X86, CS_MODE_32)
md.detail = True
d = pe.get_data(0x506020-base, 0xE0)
lines=[]
for ins in md.disasm(d, 0x506020):
    mark=' <== 10% kakuritsu' if ins.address in (0x506035,) else ''
    lines.append('0x%X  %-20s %-7s %s%s' % (ins.address, ins.bytes.hex(' '), ins.mnemonic, ins.op_str, mark))
open('dis_crit2.txt','w',encoding='utf-8').write('\n'.join(lines))
print('ok')
