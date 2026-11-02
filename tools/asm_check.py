"""Verify the four per-officer injection stubs byte-for-byte with capstone.

Each stub is emitted as exact x86 bytes with 0x00 placeholders for runtime
addresses/offsets. We then patch the placeholder rel8/rel32/imm32 and disassemble
to confirm every instruction is exactly what we intend. The authoritative C++
arrays in Runtime2026.cpp are manually transcribed from the verified offsets here.
"""
import capstone

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True


def assemble(name, instrs, base, imm32=None, rel8=None, rel32=None):
    """instrs: list of hex strings concatenated. imm32: {byte_off: value}.
    rel8: {byte_off_of_jump_instruction: target_offset}. rel32:
    {byte_off_of_jmp: target_va}."""
    code = bytes.fromhex("".join(instrs))
    b = bytearray(code)
    for off, val in (imm32 or {}).items():
        b[off:off+4] = val.to_bytes(4, 'little', signed=False)
    for off, tgt in (rel8 or {}).items():
        b[off+1] = (tgt - (off + 2)) & 0xFF
    for off, tgt in (rel32 or {}).items():
        rel = tgt - (base + off + 5)
        b[off+1:off+5] = rel.to_bytes(4, 'little', signed=True)
    print(f"===== {name}  len={len(code)}  base=0x{base:08X} =====")
    for i in md.disasm(bytes(b), base):
        print(f"  +{i.address-base:04X}  {i.bytes.hex(' '):<30} {i.mnemonic} {i.op_str}")
    print()
    return bytes(b)


# ============================================================
# 1) INTERVAL  hook 0x4FF08B  (5-byte original F7 F9 8B CE 50)
#    return 0x4FF090
#    intervalFlat[]: byte[800][41], 0xFF = "not customized" sentinel.
# ============================================================
# On entry: eax=dividend([skill+0x7C]), ecx=divisor(1|3), esi=CPersonData,
#           ebx=unit([ebx+0x34]=skill id). Stack has function-saved ebx,ebp,edi,esi.
# Custom path writes the configured days straight into eax (bypasses idiv).
# Both paths re-run the original tail: mov ecx,esi; push eax; jmp 0x4FF090.
INTERVAL = assemble("interval", [
    "53",                                  # 00 push ebx
    "56",                                  # 01 push esi
    "50",                                  # 02 push eax
    "51",                                  # 03 push ecx
    "8B4334",                              # 04 mov eax,[ebx+0x34]   skill id
    "83E03F",                              # 07 and eax,0x3F
    "83F829",                              # 0A cmp eax,0x29
    "73", "00",                            # 0D jae NC(0x3E)
    "0FB74E04",                            # 0F movzx ecx,word[esi+4]   officer
    "81F920030000",                        # 13 cmp ecx,0x320
    "73", "00",                            # 19 jae NC(0x3E)
    "6BC929",                              # 1B imul ecx,ecx,0x29      officer*41
    "03C1",                                # 1E add eax,ecx            index
    "B9", "00000000",                      # 20 mov ecx,intervalArray (imm32@0x21)
    "803C01FF",                            # 25 cmp byte[ecx+eax],0xFF
    "74", "00",                            # 29 je NC(0x3E)
    "0FB60C01",                            # 2B movzx ecx,byte[ecx+eax]
    "8BC1",                                # 2F mov eax,ecx            custom days
    "83C408",                              # 31 add esp,8
    "5E",                                  # 34 pop esi
    "5B",                                  # 35 pop ebx
    "8BCE",                                # 36 mov ecx,esi
    "50",                                  # 38 push eax
    "E9", "00000000",                      # 39 jmp 0x4FF090 (rel32)
    "59",                                  # 3E NC: pop ecx            divisor
    "58",                                  # 3F pop eax               dividend
    "99",                                  # 40 cdq
    "F7F9",                                # 41 idiv ecx
    "5E",                                  # 43 pop esi
    "5B",                                  # 44 pop ebx
    "8BCE",                                # 45 mov ecx,esi
    "50",                                  # 47 push eax
    "E9", "00000000",                      # 48 jmp 0x4FF090 (rel32)
], base=0x10000000,
    imm32={0x21: 0x12345000},
    rel8={0x0D: 0x3E, 0x19: 0x3E, 0x29: 0x3E},
    rel32={0x39: 0x4FF090, 0x48: 0x4FF090})
print("INTERVAL: imm32@0x21, rel8@0x0D/0x19/0x29->0x3E, rel32@0x39/0x48->0x4FF090\n")


# ============================================================
# 2) ACTIVATION  hook 0x4FEC59  (5-byte original 6A 0B 52 FF D5)
#    return 0x4FEC5E
#    activationBoost[]: byte[800].
#
#    0x4FEC59 is the **activation** (把握) roll inside 0x4FEBB0, NOT the resist
#    roll.  The game computes "chance = 100 + jitter" (0x4FEC4C..0x4FEC55) and
#    then rolls it; a failure returns 0 from 0x4FEBB0, i.e. the skill does not
#    activate at all.  "把握概率 100%" therefore means: the matched officer
#    skips the roll entirely and reports success (eax = 1).
#
#    The resist roll lives in a different function (0x4FEB20, chance =
#    (opponent value - attacker proficiency) / 10) and must stay untouched.
#
#    Unit -> [+0x30] CPersonData -> word[+4] officer index.
#    Stack accounting: kakuritsu is __cdecl (export ?kakuritsu@@YAHHW4RANDOM_KIND@@H@Z,
#    "YAH" == __cdecl), so the caller cleans the arguments.  The original
#    "add esp,0x14" at 0x4FEC5E clears exactly five pushes: eax(0x4FEC25) +
#    ebp(0x4FEC30) + 1(0x4FEC53) + 0xB + edx.  The hit path therefore has to push
#    0xB and edx exactly like the original, otherwise the stack is off by 8 bytes
#    per activation.
# ============================================================
ACTIVATION = assemble("activation", [
    "8BCE",                                # 00 mov ecx, esi          unit
    "8B4130",                              # 02 mov eax, [ecx+0x30]   CPersonData
    "85C0",                                # 05 test eax, eax
    "74", "00",                            # 07 je NORMAL(0x2C)
    "0FB74004",                            # 09 movzx eax, word [eax+4]  officer
    "3D20030000",                          # 0D cmp eax, 0x320
    "73", "00",                            # 12 jae NORMAL(0x2C)
    "B900000000",                          # 14 mov ecx, activationArray (imm32@0x15)
    "803C0800",                            # 19 cmp byte [eax+ecx], 0
    "74", "00",                            # 1D je NORMAL(0x2C)
    "6A0B",                                # 1F push 0xB              hit: same args as original
    "52",                                  # 21 push edx              chance argument
    "B801000000",                          # 22 mov eax, 1            skip the roll
    "E9", "00000000",                      # 27 jmp 0x4FEC5E (rel32@0x28)
    "6A0B",                                # 2C NORMAL: push 0xB
    "52",                                  # 2E push edx
    "FF15F0F25F00",                        # 2F call [0x5FF2F0]      original roll
    "E9", "00000000",                      # 35 jmp 0x4FEC5E (rel32@0x36)
], base=0x10000000,
    imm32={0x15: 0x12346000},
    rel8={0x07: 0x2C, 0x12: 0x2C, 0x1D: 0x2C},
    rel32={0x27: 0x4FEC5E, 0x35: 0x4FEC5E})
print("ACTIVATION: arr imm32@0x15, rel8@0x07/0x12/0x1D->0x2C, rel32(E9)@0x27/0x35->0x4FEC5E\n")


# ============================================================
# 3) CRITICAL  hook 0x50603B  (5-byte original 83 C4 0C 85 C0)
#    return 0x506040
#    criticalHit[]: byte[800]. Walk [esi+0x80]->[+0x88] chain.
# ============================================================
CRITICAL = assemble("critical", [
    "83C40C",                              # 00 add esp,0xC
    "50",                                  # 03 push eax
    "53",                                  # 04 push ebx
    "57",                                  # 05 push edi
    "8B8680000000",                        # 06 mov eax,[esi+0x80]
    "85C0",                                # 0C test eax,eax
    "74", "00",                            # 0E je NF(0x4A)
    "8B8088000000",                        # 10 mov eax,[eax+0x88]
    "89C7",                                # 16 mov edi,eax
    # 18 LOOP:
    "85FF",                                # 18 test edi,edi
    "74", "00",                            # 1A je NF(0x4A)
    "8B4708",                              # 1C mov eax,[edi+8]
    "0FB74004",                            # 1F movzx eax,word[eax+4]
    "3D20030000",                          # 23 cmp eax,0x320
    "73", "00",                            # 28 jae NEXT(0x46)
    "BB", "00000000",                      # 2A mov ebx,criticalArray (imm32@0x2B)
    "803C0300",                            # 2F cmp byte[ebx+eax],0
    "74", "00",                            # 33 je NEXT(0x46)
    "B801000000",                          # 35 mov eax,1
    # 3A FOUND:
    "83C404",                              # 3A add esp,4   discard saved result
    "5F",                                  # 3D pop edi
    "5B",                                  # 3E pop ebx
    "85C0",                                # 3F test eax,eax
    "E9", "00000000",                      # 41 jmp 0x506040 (rel32@0x41)
    # 46 NEXT:
    "8B3F",                                # 46 mov edi,[edi]
    "EB", "00",                            # 48 jmp LOOP(0x18)
    # 4A NF:
    "58",                                  # 4A pop eax  restore original result
    "5F",                                  # 4B pop edi
    "5B",                                  # 4C pop ebx
    "85C0",                                # 4D test eax,eax
    "E9", "00000000",                      # 4F jmp 0x506040 (rel32@0x4F)
], base=0x10000000,
    imm32={0x2B: 0x12347000},
    rel8={0x0E: 0x4A, 0x1A: 0x4A, 0x28: 0x46, 0x33: 0x46, 0x48: 0x18},
    rel32={0x41: 0x506040, 0x4F: 0x506040})
print("CRITICAL: arr imm32@0x2B, rel8@0x0E/0x1A->0x4A, 0x28/0x33->0x46, 0x48->0x18, rel32@0x41/0x4F->0x506040\n")


# ============================================================
# 4) GROWTH  hook codeBlockTail = dword_42F27C + 0x40
#    original 6 bytes: 89 44 24 04 EB BC (mov [esp+4],eax; jmp normal)
#    replaced by: E9 rel32 90  (6 bytes)
#    growthBitset[]: byte[800]; multiplier imm32.
# ============================================================
GROWTH = assemble("growth", [
    "89C1",                                # 00 mov ecx,eax      ; probability
    "0FB74604",                            # 02 movzx eax,word[esi+4]  ; officer
    "3D20030000",                          # 06 cmp eax,0x320
    "73", "00",                            # 0B jae SKIP(0x1E)
    "BA", "00000000",                      # 0D mov edx,growthBitset (imm32@0x0E)
    "803C0200",                            # 12 cmp byte[edx+eax],0
    "74", "00",                            # 16 je SKIP(0x1E)
    "69C9", "00000000",                    # 18 imul ecx,ecx,multiplier (imm32@0x1A)
    "894C2404",                            # 1E SKIP: mov [esp+4],ecx
    "FF25F0F25F00",                        # 22 jmp [0x5FF2F0]
], base=0x10000000,
    imm32={0x0E: 0x12348000, 0x1A: 3},
    rel8={0x0B: 0x1E, 0x16: 0x1E})
print("GROWTH: bitset imm32@0x0E, multiplier imm32@0x1A, rel8@0x0B/0x16->0x1E, len", len(GROWTH), "\n")