"""Find functions reading BOTH word[reg+4] and dword[reg+0x94] of the same base register."""
import capstone
import pefile
from capstone.x86 import X86_OP_MEM, X86_OP_REG

pe = pefile.PE(r'G:\aiprojects\game\san9-companion\San9PK.exe')
base = pe.OPTIONAL_HEADER.ImageBase
text = [s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text'][0]
code = text.get_data()
va0 = base + text.VirtualAddress
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True

insns = list(md.disasm(code, va0))


def base_reg(ins):
    """Return the base register of a memory operand, if any."""
    for op in ins.operands:
        if op.type == X86_OP_MEM:
            return ins.reg_name(op.mem.base)
    return None


# group by rough function: split on 'ret' boundaries
funcs = []
cur = []
for ins in insns:
    cur.append(ins)
    if ins.mnemonic == 'ret':
        funcs.append(cur)
        cur = []
if cur:
    funcs.append(cur)

print(f'{len(funcs)} rough functions')
for f in funcs:
    w4 = {}   # reg -> has word [reg+4]
    d94 = {}
    for ins in f:
        for op in ins.operands:
            if op.type != X86_OP_MEM:
                continue
            r = ins.reg_name(op.mem.base)
            if not r or r in ('eip', 'rip'):
                continue
            disp = op.mem.disp
            size = op.size
            if disp == 4 and size == 2:
                w4.setdefault(r, ins.address)
            if disp == 0x94 and size == 4:
                d94.setdefault(r, ins.address)
    for r in set(w4) & set(d94):
        print(f'  reg {r}: word[{r}+4] @{w4[r]:#x}  and dword[{r}+0x94] @{d94[r]:#x}')
        for ins in f:
            if ins.address in (w4[r], d94[r]):
                print(f'      {ins.address:#x}: {ins.mnemonic} {ins.op_str}')
