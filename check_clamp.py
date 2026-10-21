import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
pe = pefile.PE('San9PK.exe')
base = pe.OPTIONAL_HEADER.ImageBase
md = Cs(CS_ARCH_X86, CS_MODE_32)
md.detail = True
for va in [0x408649, 0x408687, 0x409854]:
    d = pe.get_data(va-base-0x20, 0x70)
    lines=['=== 0x%X ===' % va]
    for ins in md.disasm(d, va-0x20):
        mark=''
        for op in ins.operands:
            if op.type==1 and op.imm in (0x3E8,0xA4,0x58): mark=' <== %d/off' % op.imm
        lines.append('  0x%X  %-20s %-7s %s%s' % (ins.address, ins.bytes.hex(' '), ins.mnemonic, ins.op_str, mark))
    print('\n'.join(lines))
    print()
