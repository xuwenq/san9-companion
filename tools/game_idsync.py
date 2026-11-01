"""Do the 武将表 index and the person-record id (word[rec+4]) denote the same thing?

Strategy: look for call sites of the officer-table accessor 0x4531B0 whose pushed
argument is a `movzx eax, word [reg+4]` (i.e. a person-record id).
"""
import struct
import capstone
import pefile
from capstone.x86 import X86_OP_MEM, X86_OP_REG, X86_OP_IMM

GAME = r'G:\aiprojects\game\san9-companion\San9PK.exe'
pe = pefile.PE(GAME)
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
text = [s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text'][0]
code = text.get_data()
va0 = base + text.VirtualAddress
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True

TARGET = 0x4531B0
OFFSET = 0x94


def at(va, n):
    return img[va - base:va - base + n]


sites = []
for off in range(len(code) - 5):
    if code[off] == 0xE8 and va0 + off + 5 + struct.unpack_from('<i', code, off + 1)[0] == TARGET:
        sites.append(va0 + off)

# For each call site, scan backwards 48 bytes for `mov <reg>, dword [reg+0x94]`
# or `movzx <reg>, word [reg+4]` feeding the push.
print('=== call sites whose index arg comes from dword[reg+0x94] ===')
for s in sites:
    window = list(md.disasm(at(s - 48, 56), s - 48))
    src = None
    for ins in window:
        if ins.address >= s:
            break
        for op in ins.operands:
            if op.type == X86_OP_MEM and op.mem.disp == OFFSET and op.size == 4:
                src = f'dword[+0x94] @{ins.address:#x}'
    if src:
        print(f'  call {s:#x}  <- {src}')

print()
print('=== call sites whose index arg is a pushed imm in 0..0x351 ===')
immcount = {}
for s in sites:
    window = list(md.disasm(at(s - 16, 20), s - 16))
    for ins in window:
        if ins.address >= s:
            break
        if ins.mnemonic == 'push' and ins.operands and ins.operands[0].type == X86_OP_IMM:
            v = ins.operands[0].imm & 0xFFFFFFFF
            if v < 0x352:
                immcount.setdefault(v, []).append(s)
for v in sorted(immcount):
    print(f'  push {v:#x} ({v}) at {len(immcount[v])} sites, e.g. {immcount[v][0]:#x}')

print()
print('=== functions that write dword[obj+0x94] (18 sites expected) ===')
w = []
for off in range(len(code) - 6):
    if code[off] == 0x89 and code[off + 2] == 0x94 and code[off + 3] == 0:
        w.append(va0 + off)
for x in w[:20]:
    print(f'  {x:#x}')
