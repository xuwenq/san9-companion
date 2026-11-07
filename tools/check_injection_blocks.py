"""Validate every injected x86 byte block used by the companion.

For each block we disassemble it with capstone and check, for every branch:

  * the displacement is interpreted with the correct signedness
    (rel8 is signed, rel32 is signed);
  * the branch target lands on an instruction boundary inside the block.

A branch that leaves the block is a bug: on x86 the CPU would execute whatever
bytes happen to follow the block.  The 2026-11-07 in-game crash was exactly
that -- a guard jump encoded as a short jump whose displacement exceeded the
signed rel8 range, so it wrapped negative and jumped 88 bytes before the
block.

Usage:  python tools/check_injection_blocks.py [--json]

The block sources are read straight from the C++ so the checker and the
product can never drift apart.
"""
import argparse
import json
import os
import re
import sys

try:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
except ImportError:  # pragma: no cover
    print("capstone is required: pip install capstone", file=sys.stderr)
    raise SystemExit(2)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUNTIME = os.path.join(ROOT, "src", "runtime", "Runtime2026.cpp")

# Blocks that are written into the game process as raw bytes.
# Each entry: name -> (variable name in Runtime2026.cpp, known external
# branches).  A branch to an address outside the block is only allowed when the
# block declares it (for example the "jmp 0x4FF090" tails).
REL32_OUT = {
    # per-officer hooks jump back into the game after the stub
    "boostCode": (0x4FEC5E,),
    "criticalCode": (0x506040,),
    "intervalCode": (0x4FF090,),
}

# Fragments that are patched *into* existing game code rather than entered as
# a standalone stub.  Their relative branches are relative to the game
# address they are written to, not to the fragment, so they cannot be
# checked in isolation.
FRAGMENTS = {
    "original",       # growth boost: 6 bytes patched into the game block tail
}


def strip_comments(text):
    """Remove // and /* */ comments so hex literals inside them are ignored."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def read_cpp():
    with open(RUNTIME, encoding="utf-8") as handle:
        return handle.read()


def extract_byte_arrays(source):
    """Return {name: bytes} for every `static const BYTE name[] = { ... };`."""
    blocks = {}
    pattern = re.compile(
        r"(?:static\s+)?const\s+BYTE\s+(\w+)\s*(?:\[[^\]]*\])?\s*=\s*\{(.*?)\};",
        re.S,
    )
    for match in pattern.finditer(strip_comments(source)):
        name, body = match.group(1), match.group(2)
        values = re.findall(r"0x([0-9A-Fa-f]{2})", body)
        if not values:
            continue
        blocks[name] = bytes(int(v, 16) for v in values)
    return blocks


def check_block(name, code, allowed_out=()):
    """Return a list of problem strings for one block."""
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    instructions = list(md.disasm(code, 0))
    problems = []
    if not instructions:
        return [f"{name}: capstone could not disassemble the block"]
    covered = instructions[-1].address + instructions[-1].size
    if covered != len(code):
        problems.append(
            f"{name}: disassembly covers {covered} of {len(code)} bytes "
            f"(trailing bytes are not code)"
        )
    boundaries = {i.address for i in instructions}
    for instruction in instructions:
        mnemonic = instruction.mnemonic
        if not (mnemonic.startswith("j") or mnemonic == "call"):
            continue
        if not instruction.operands or instruction.operands[0].type != 2:  # IMM
            continue
        # A rel32 jump whose displacement field is still all zeros is a
        # placeholder that the runtime fills in with a real game address
        # (for example the "jmp 0x4FF090" tails).  Its target cannot be
        # checked here.
        if instruction.size == 5 and mnemonic == "jmp":
            field = code[instruction.address + 1:instruction.address + 5]
            if field == b"\x00\x00\x00\x00":
                continue
        target = instruction.operands[0].imm
        if target in boundaries:
            continue
        if target in allowed_out:
            continue
        if 0 <= target < len(code):
            problems.append(
                f"{name}: 0x{instruction.address:04X} {mnemonic} 0x{target:04X} "
                f"lands in the middle of an instruction"
            )
        else:
            problems.append(
                f"{name}: 0x{instruction.address:04X} {mnemonic} leaves the block "
                f"(target 0x{target:08X}, block 0..0x{len(code) - 1:X})"
            )
    return problems


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    source = read_cpp()
    blocks = extract_byte_arrays(source)
    if not blocks:
        print("no byte arrays found in", RUNTIME, file=sys.stderr)
        return 2

    results = {}
    problems = []
    for name, code in sorted(blocks.items()):
        if name in FRAGMENTS:
            results[name] = {"size": len(code), "problems": [], "skipped": "game-code fragment"}
            continue
        found = check_block(name, code, REL32_OUT.get(name, ()))
        results[name] = {"size": len(code), "problems": found}
        problems.extend(found)

    if args.json:
        print(json.dumps(results, indent=2, ensure_ascii=False))
    else:
        for name, info in sorted(results.items()):
            state = "OK" if not info["problems"] else "FAIL"
            print(f"{state:4}  {name:24} {info['size']:4} bytes")
            for problem in info["problems"]:
                print(f"        {problem}")
        print()
        print(f"{len(results)} blocks checked, {len(problems)} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
