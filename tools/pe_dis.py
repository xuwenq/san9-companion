"""Disassemble a PE32 file at given virtual addresses."""
import sys
import capstone
import pefile

path = sys.argv[1]
pe = pefile.PE(path)
base = pe.OPTIONAL_HEADER.ImageBase
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True


def read(va, size):
    off = pe.get_offset_from_rva(va - base)
    return pe.__data__[off:off + size]


for arg in sys.argv[2:]:
    va, _, sz = arg.partition(':')
    va = int(va, 16)
    size = int(sz, 0) if sz else 0x60
    print(f'--- {arg} (file {path}) ---')
    for i in md.disasm(read(va, size), va):
        print(f'  {i.address:08X}: {i.bytes.hex(" "):<28} {i.mnemonic} {i.op_str}')
    print()
