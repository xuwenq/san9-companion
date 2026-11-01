"""Regression for IDA array definitions also used as scalar operands."""
import json
import re
from pathlib import Path
import sys
import unittest

PROJECT_ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = PROJECT_ROOT / 'src'
sys.path.insert(0, str(PROJECT_ROOT / 'tools'))
from recover_runtime import scalar_array
from pe_image import PEImage
from runtime_layout import layout, header, name_state_offsets
from injection_names import original_names


class RuntimeGenerationTests(unittest.TestCase):
    def test_officer_ability_restore_registration(self):
        text = original_names((SOURCE_ROOT / 'generated/StateConstants.inc').read_text(encoding='utf-8'), '409840')
        values = {int(offset): int(value, 0) for offset, value in re.findall(
            r'field<DWORD>\((\d+)\) = static_cast<DWORD>\((0x[0-9A-Fa-f]+|\d+)\);', text)}
        self.assertEqual(values[6564], 0x4B2E37)
        self.assertEqual(values[6568].to_bytes(4, 'little'), bytes.fromhex('54 8B 4E 5C'))
        self.assertEqual(values[6572].to_bytes(4, 'little'), bytes.fromhex('60 8B 4E 58'))

    def test_mixed_scalar_array_views(self):
        objects = json.loads((PROJECT_ROOT / 'recovery/evidence/runtime/objects.json').read_text(encoding='utf-8'))
        row = objects[str(0x41FD34)]
        self.assertTrue(scalar_array(row))
        symbols = (SOURCE_ROOT / 'generated/Symbols.inc').read_text(encoding='utf-8')
        self.assertIn('#define dword_41FD34 (global<int>(0x41FD34))', symbols)
        branches = original_names((SOURCE_ROOT / 'runtime/RuntimeInjection2026.cpp').read_text(encoding='utf-8'), '409E10')
        image = PEImage(PROJECT_ROOT / 'recovery/sample-2026/San9PKHard.exe')
        self.assertEqual(image.read(0x41FD30, 16),
                         bytes.fromhex('24 80 50 00 92 81 50 00 0c 00 00 00 0d 00 00 00'))
        self.assertIn('(LPVOID)(dword_41FD34 + 3)', branches)
        self.assertIn('&dword_42D948, 0x190u', branches)
        self.assertIn('&byte_42DAD8, 0x32u', branches)

    def test_configuration_layout_contract(self):
        value=layout()
        # Independent recovered offsets/lengths, including the two former half-copy errors.
        self.assertEqual([(r['offset'],r['size']) for r in value['configuration']],
                         [(500,3200),(3704,696),(4408,2),(4412,4),(4416,4),(4420,300),
                          (4836,1),(4838,164),(7624,1),(7625,216)])
        self.assertEqual(value['state_size'],7844)
        self.assertEqual((SOURCE_ROOT/'generated/RuntimeLayout2026.h').read_text(encoding='utf-8'),header())
        branches='\n'.join((SOURCE_ROOT/'runtime'/name).read_text(encoding='utf-8') for name in ('RuntimeVersion2026.cpp','RuntimeInjection2026.cpp','RuntimeConfiguration2026.cpp','RuntimeZone2026.cpp'))
        for row in value['configuration']:
            self.assertNotRegex(branches,rf"\bstate \+ {row['offset']}\b")
            # Some branches address the interior of a record (skill interval at +4839).
            self.assertIn(row['name'],header())
        self.assertIn('applyGameConfiguration(reason)', branches)

    def test_lifecycle_layout_and_typed_dispatch(self):
        rows=layout()['lifecycle']
        self.assertEqual([(r['offset'],r['retention']) for r in rows],
            [(96,'baseline'),(108,'reset'),(112,'reset'),(116,'reset'),(120,'preserve'),
             (124,'preserve'),(128,'preserve'),(132,'reset'),(136,'reset'),(140,'reset'),(144,'reset')])
        self.assertTrue(all(r['size']==4 for r in rows))
        text=(SOURCE_ROOT/'runtime/RuntimeInjection2026.cpp').read_text(encoding='utf-8')
        for r in rows: self.assertNotRegex(text,rf"\bstate \+ {r['offset']}\b")
        modify=text.split('void Runtime2026::modifyBranches(',1)[1]
        self.assertIn('ModifyReason2026 reason',modify)
        self.assertNotRegex(modify,r'\ba2\b')
        for name in ('TimerTick','GameNotification','GameDialogClosed','ParametersConfirmed'):
            self.assertTrue('ModifyReason2026::'+name in modify,name)

    def test_offset_naming_is_reversible_and_bounded(self):
        source='state + 500; state + 5000; state + 3704; state + 4996; 500;'
        named=name_state_offsets(source)
        self.assertIn('state + 5000;',named)
        self.assertTrue(named.endswith(' 500;'))
        values={r['name']:str(r['offset']) for r in layout()['configuration']}
        self.assertEqual(re.sub(r'san9::layout2026::(\w+)',lambda m:values[m[1]],named),source)

    def test_target_operand_against_original_instructions(self):
        image = PEImage(PROJECT_ROOT / 'recovery/sample-2026/San9PKHard.exe')
        # mov ecx,[0041FD34]; ... add ecx,3; push ecx; ... call WriteProcessMemory
        self.assertEqual(image.read(0x4139E0,6), bytes.fromhex('8b 0d 34 fd 41 00'))
        self.assertEqual(image.read(0x4139FD,3), bytes.fromhex('83 c1 03'))
        self.assertEqual(image.read(0x413A01,4), bytes.fromhex('51 52 ff d7'))


if __name__ == '__main__':
    unittest.main()
