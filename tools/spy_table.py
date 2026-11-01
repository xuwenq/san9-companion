"""Parse the SanIXSpy.dll patch table (29 entries x 0x1C bytes)."""
import struct
import capstone
import pefile

pe = pefile.PE(r'G:\aiprojects\game\san9-companion\sanpkcheat\SanIXSpy.dll')
base = pe.OPTIONAL_HEADER.ImageBase
data = [s for s in pe.sections if s.Name.rstrip(b'\0') == b'.data'][0]
d = data.get_data()
dva = base + data.VirtualAddress

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)


def read(va, n):
    off = pe.get_offset_from_rva(va - base)
    return pe.__data__[off:off + n]


print(f'table @ {dva:#x}, {0x32C // 0x1C} entries of 0x1C bytes')
print(f'{"idx":>3} {"game addr":>10} {"orig":<18} {"patch":<18} {"stub":>10} {"size":>6}')
rows = []
for i in range(0x32C // 0x1C):
    off = i * 0x1C
    addr = struct.unpack_from('<I', d, off)[0]
    orig = d[off + 4:off + 9]
    patch = d[off + 9:off + 0x0E]
    stub = struct.unpack_from('<I', d, off + 0x0C)[0]
    size = struct.unpack_from('<I', d, off + 0x18)[0]
    rows.append((addr, orig, patch, stub, size))
    print(f'{i:>3} {addr:>#10x} {orig.hex(" "):<18} {patch.hex(" "):<18} {stub:>#10x} {size:>6}')

print('\n=== stub disassembly ===')
for i, (addr, orig, patch, stub, size) in enumerate(rows):
    if not stub or stub < base:
        continue
    code = read(stub, max(size, 16))
    print(f'\n--- entry {i}: game {addr:#x}, stub {stub:#x}, size {size} ---')
    print(f'    original: {orig.hex(" ")}')
    for ins in md.disasm(code, stub):
        print(f'    {ins.address:08X}: {ins.bytes.hex(" "):<26} {ins.mnemonic} {ins.op_str}')
        if ins.mnemonic in ('ret', 'jmp') and ins.address > stub + 4:
            break
