import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
pe = pefile.PE('San9PK.exe')
base = pe.OPTIONAL_HEADER.ImageBase
md = Cs(CS_ARCH_X86, CS_MODE_32)
md.detail = True
lines=[]
for va in [0x48709B, 0x48DC9E, 0x48EE3A, 0x48E817, 0x490C8A, 0x492BC6, 0x493687, 0x493D27, 0x4966D0]:
    d = pe.get_data(va-base-0x08, 0x20)
    lines.append('=== 0x%X ===' % va)
    for ins in md.disasm(d, va-0x08):
        mark=' <== PATCH' if ins.address==va else ''
        lines.append('  0x%X  %-20s %-7s %s%s' % (ins.address, ins.bytes.hex(' '), ins.mnemonic, ins.op_str, mark))
open('dis_crit.txt','w',encoding='utf-8').write('\n'.join(lines))
print('ok')
