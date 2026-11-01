"""Disassemble San9PK.exe at given virtual addresses (offline, no game needed)."""
import sys
import capstone
import pefile

pe = pefile.PE(r'G:\aiprojects\game\san9-companion\San9PK.exe')
base = pe.OPTIONAL_HEADER.ImageBase
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True


def read(va, size):
    off = pe.get_offset_from_rva(va - base)
    return pe.__data__[off:off + size]


def dis(va, size=0x60, label=''):
    print(f'--- {label} 0x{va:08X} ---')
    data = read(va, size)
    for i in md.disasm(data, va):
        print(f'  {i.address:08X}: {i.bytes.hex(" "):<28} {i.mnemonic} {i.op_str}')
    print()


if __name__ == '__main__':
    for arg in sys.argv[1:]:
        va, _, sz = arg.partition(':')
        dis(int(va, 16), int(sz, 0) if sz else 0x60, arg)
