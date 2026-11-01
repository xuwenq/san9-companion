"""Inspectable 32-bit address model; never opens a process or executes payload bytes."""
from pathlib import Path
import argparse
import json
import struct

from project_paths import PROJECT_ROOT, SOURCE_ROOT
DATA_VA = 0x41F000
BASE_SLOT = 0x420050
ALLOCATION_SIZE = 0xB000


class PatchImage:
    def __init__(self, root=SOURCE_ROOT):
        self.root = Path(root)
        self.data = bytearray((self.root / 'generated/companion_data.bin').read_bytes())
        self.relocations = json.loads((self.root / 'generated/relocations.json').read_text(encoding='utf-8'))
        self.object = bytearray(7844)
        for entry in self.relocations:
            if entry['kind'] == 'object_field':
                struct.pack_into('<I', self.object, entry['offset'], entry['initial_value'])
        self.base = self.read32(BASE_SLOT)

    def _offset(self, va, size):
        offset = va - DATA_VA
        if size < 0 or not 0 <= offset <= len(self.data) - size:
            raise ValueError(f'Outside recovered data: 0x{va:08X}, size={size}')
        return offset

    def read(self, va, size):
        offset = self._offset(va, size)
        return bytes(self.data[offset:offset + size])

    def read32(self, va):
        return int.from_bytes(self.read(va, 4), 'little')

    def object32(self, offset):
        if not 0 <= offset <= len(self.object) - 4:
            raise ValueError('Object field out of bounds')
        return struct.unpack_from('<I', self.object, offset)[0]

    def rebase(self, new_base):
        if not 0 < new_base <= 0x100000000 - ALLOCATION_SIZE or new_base & 0xFFF:
            raise ValueError('Base must be a nonzero page-aligned uint32 allocation address')
        delta = (new_base - self.base) & 0xFFFFFFFF
        self.object[96:100] = struct.pack('<I', new_base)
        if delta == 0:
            return []
        changes = []
        for row in self.relocations:
            if row['kind'] == 'object_field':
                buffer, pos = self.object, row['offset']
            else:
                buffer, pos = self.data, self._offset(row['va'], 4)
            before = struct.unpack_from('<I', buffer, pos)[0]
            after = (before + delta) & 0xFFFFFFFF
            struct.pack_into('<I', buffer, pos, after)
            changes.append(dict(row, before=before, after=after))
        # 0x41A400 returns a flag; its caller at 0x409E10 updates this slot.
        struct.pack_into('<I', self.data, self._offset(BASE_SLOT, 4), new_base)
        self.base = new_base
        return changes

    def static_catalog(self):
        """Return baseline static writes after relocation; not a runtime patch plan."""
        from pe_image import PEImage
        from reconstruct import evaluate
        sample = self.root.parent / 'recovery/sample-2026/San9PKHard.exe'
        image = PEImage(sample)
        offset = DATA_VA - image.base
        image.memory[offset:offset + len(self.data)] = self.data
        from call_evidence import memory_calls
        calls = memory_calls(self.root)
        rows = []
        for call in calls:
            if call['api'] != 'WriteProcessMemory':
                continue
            try:
                target, source, size = [evaluate(call['args'][i], image) for i in (1,2,3)]
                if not 0 < size <= 0x100000:
                    continue
                payload = image.read(source, size)
            except (ValueError, KeyError):
                continue
            rows.append(dict(function_va=call['function_va'], instruction_va=call['instruction_va'],
                             target_va=target, source_va=source, size=size, bytes_hex=payload.hex()))
        return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=lambda x:int(x,0), default=0x10000000)
    parser.add_argument('--out', type=Path, default=PROJECT_ROOT / 'out/offline')
    args = parser.parse_args()
    image = PatchImage()
    changes = image.rebase(args.base)
    catalog = image.static_catalog()
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'relocated_data.bin').write_bytes(image.data)
    for name, value in [('relocation_trace.json', changes), ('static_writes.json', catalog)]:
        (args.out / name).write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(base=f'0x{image.base:08X}', allocation_size=ALLOCATION_SIZE,
                         relocated_entries=len(changes), static_write_candidates=len(catalog),
                         live_process_opened=False, output=str(args.out)), indent=2))


if __name__ == '__main__':
    main()
