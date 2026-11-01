"""Symbolic stack simulation of 0x44F830 to decide what lands in record+4.

Question: does the low word of CPersonData+4 hold the officer-table index?

Call site 0x45415B:
    mov  ebx, 0x1258EE0
    xor  edi, edi
loop:
    push edi            ; -> arg?  (the loop index)
    push esi            ; -> arg?  (a stream/reader)
    mov  ecx, ebx       ; this = &table[i]
    call 0x44F830
    inc  edi
    add  ebx, 0x128
    cmp  edi, 0x320
"""
import capstone
import pefile
from capstone.x86 import X86_OP_REG, X86_OP_MEM, X86_OP_IMM

GAME = r'G:\aiprojects\game\san9-companion\San9PK.exe'
pe = pefile.PE(GAME)
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True

E = 0x1000  # symbolic entry esp
stack = {E: 'RET'}


def slot(off):
    return stack.get(E + off, 'UNDEF')


def setslot(off, v):
    stack[E + off] = v


def dis(va, n):
    return list(md.disasm(img[va - base:va - base + n], va))


# call-site frame
setslot(4, 'ARG1(esi)')
setslot(8, 'ARG2(edi=index)')
setslot(12, 'UNDEF')
setslot(16, 'UNDEF')

esp = E - 8  # after callee's push esi; push edi
registers = {'eax': '?', 'ecx': 'this=table[i]', 'edx': '?', 'esi': '?', 'edi': '?'}


def get_reg(r):
    return registers.get(r, '?')


def set_reg(r, v):
    registers[r] = v


for ins in dis(0x44F830, 0x120):
    m, o = ins.mnemonic, ins.op_str
    if m == 'push':
        esp -= 4
        stack[esp] = f'push({o})'
    elif m == 'pop':
        stack[esp] = 'pop'
        esp += 4
    elif m == 'ret':
        n = int(o, 0) if o else 0
        print(f'  ret {n}  (callee pops {n} bytes)')
        break
    elif m == 'add' and o.startswith('esp'):
        n = int(o.split(', ')[1], 0)
        moved = {k + n: v for k, v in stack.items()}
        stack.clear()
        stack.update(moved)
        esp += n
    elif m == 'mov':
        d, s = ins.operands[0], ins.operands[1]
        if d.type == X86_OP_REG and s.type == X86_OP_REG:
            set_reg(ins.reg_name(d.reg), get_reg(ins.reg_name(s.reg)))
        elif d.type == X86_OP_REG and s.type == X86_OP_MEM:
            base_r = ins.reg_name(s.mem.base)
            if base_r == 'esp':
                val = slot(s.mem.disp + (esp - E))
                set_reg(ins.reg_name(d.reg), val)
                print(f'  {ins.address:#x}: {m} {o}   -> {ins.reg_name(d.reg)} = {val}')
            else:
                set_reg(ins.reg_name(d.reg), f'[{base_r}{s.mem.disp:+d}]')
        elif d.type == X86_OP_MEM and d.mem.base and ins.reg_name(d.mem.base) == 'esp':
            src = get_reg(ins.reg_name(s.reg)) if s.type == X86_OP_REG else f'#{s.imm}'
            stack[esp + d.mem.disp] = src
        elif d.type == X86_OP_REG and s.type == X86_OP_IMM:
            set_reg(ins.reg_name(d.reg), f'#{s.imm:#x}')
    elif m == 'call':
        tgt = int(o, 16)
        if tgt == 0x446800:
            print(f'>>> {ins.address:#x} call 0x446800')
            print(f'    ecx = {get_reg("ecx")}')
            print(f'    arg  = {stack.get(esp, "?")}   (esp-relative)')
            break
        elif tgt in (0x4467B0, 0x4466A0, 0x446680):
            # these are ret-4 helpers; consume one pushed arg
            print(f'  {ins.address:#x} call {o}  arg={stack.get(esp,"?")}')
            stack.pop(esp, None)
            esp += 4
        else:
            print(f'  {ins.address:#x} call {o}')
