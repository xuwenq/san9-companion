"""Dump SanIXSpy.dll patch table raw layout + full stub disassembly."""
import struct
import capstone
import pefile

pe = pefile.PE(r'G:\aiprojects\game\san9-companion\sanpkcheat\SanIXSpy.dll')
base = pe.OPTIONAL_HEADER.ImageBase
data = [s for s in pe.sections if s.Name.rstrip(b'\0') == b'.data'][0]
d = data.get_data()
dva = base + data.VirtualAddress
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True

GAME = pefile.PE(r'G:\aiprojects\game\san9-companion\San9PK.exe')
gbase = GAME.OPTIONAL_HEADER.ImageBase


def read(va, n):
    off = pe.get_offset_from_rva(va - base)
    return pe.__data__[off:off + n]


def game_dis(va, n):
    off = GAME.get_offset_from_rva(va - gbase)
    code = GAME.__data__[off:off + n]
    return [(i.address, i.bytes.hex(' '), i.mnemonic, i.op_str) for i in md.disasm(code, va)]


print('=== raw table bytes (0x1C per entry) ===')
for i in range(0x32C // 0x1C):
    off = i * 0x1C
    print(f'{i:>3} @{dva+off:#x}: {d[off:off+0x1C].hex(" ")}')

STUBS = {}
for i in range(0x32C // 0x1C):
    off = i * 0x1C
    addr = struct.unpack_from('<I', d, off)[0]
    stub = struct.unpack_from('<I', d, off + 0x0C)[0]
    STUBS[i] = (addr, stub, d[off + 4:off + 9])

print('\n=== stubs (disassemble until first jmp/ret after 12 bytes) ===')
for i, (addr, stub, orig) in STUBS.items():
    print(f'\n--- entry {i}: game {addr:#x} orig {orig.hex(" ")} stub {stub:#x} ---')
    for a, b, m, o in game_dis(addr, 24):
        print(f'    GAME {a:08X}: {b:<26} {m} {o}')
    for ins in md.disasm(read(stub, 0x60), stub):
        print(f'    STUB {ins.address:08X}: {ins.bytes.hex(" "):<26} {ins.mnemonic} {ins.op_str}')
        if ins.mnemonic in ('ret', 'jmp') and ins.address > stub + 8:
            break
