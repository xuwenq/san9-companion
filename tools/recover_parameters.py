"""Recover parameter-file/DDX metadata, cross-checking independent load/save paths.

No execution of the sample. Generated names are control IDs, not guessed symbols.
"""
from pathlib import Path
import json
import re

from project_paths import PROJECT_ROOT, SOURCE_ROOT
from dialog_customizations import control_title
def recover():
    def source(va):
        return (PROJECT_ROOT / f'recovery/evidence/decompiled/sub_{va}.c').read_text(encoding='utf-8')

    load = source('4191C0').split('aSan9pkhardpers')[0]
    scalar = [int(x) * 4 for x in re.findall(
        r'\*\(\(_DWORD \*\)this \+ (\d+)\) = atoi\(String\[0\]\)', load)]
    assert len(scalar) == 32
    groups = []
    for var in ('v3', 'v4'):
        matches = re.findall(r'\*\(\(_DWORD \*\)this \+ (\d+)\) = \(?' + var +
                             r' & (0x[0-9A-Fa-f]+|\d+)\)?(?: != 0)?;', load)
        groups.append([(int(offset) * 4, int(mask, 0)) for offset, mask in matches])
    assert [len(g) for g in groups] == [25, 31]
    assert all(mask == 1 << i for g in groups for i, (_, mask) in enumerate(g))

    # Track only direct copy statements before DoModal; unknown expressions fail closed.
    transfer = source('418810').split('if ( CDialog::DoModal')[0]
    local_offsets = {name: int(offset, 16) - 4 for name, offset in re.findall(
        r'int (v\d+); // \[esp\+([0-9A-F]+)h\]', transfer)}
    values = {}
    for name, expr in re.findall(r'^  (v\d+) = ([^;]+);', transfer, re.M):
        host = re.fullmatch(r'\*\(_DWORD \*\)\(this \+ (\d+)\)', expr)
        if host:
            values[name] = int(host[1])
        elif expr in values:
            values[name] = values[expr]
    dialog_to_host = {offset: values[name] for name, offset in local_offsets.items()
                      if name in values}
    assert len(dialog_to_host) == 88

    # The save function expresses each mask as nested low-bit-first ORs.
    save = source('403780')
    formats = re.findall(r'CString::Format\(\s*\(CString \*\)v11,\s*"%d\\n",\s*(.*?)\);', save, re.S)
    assert len(formats) == 34
    file_fields = []
    for line, expr in enumerate(formats, 1):
        if expr == 'v4':
            expr = '*((_DWORD *)this + 30)'
        dialog_offsets = [int(i) * 4 for i in re.findall(r'\*\(\(_DWORD \*\)this \+ (\d+)\)', expr)]
        if line in (15, 16):
            expected = groups[line - 15]
            actual = [(dialog_to_host[o], 1 << i) for i, o in enumerate(dialog_offsets)]
            assert actual == expected, (line, actual, expected)
        else:
            expected_offset = scalar[line - 1 if line < 15 else line - 3]
            assert len(dialog_offsets) == 1
            assert dialog_to_host[dialog_offsets[0]] == expected_offset
        for bit, offset in enumerate(dialog_offsets):
            file_fields.append(dict(line=line, mask=(1 << bit) if line in (15, 16) else 0,
                                    host_offset=dialog_to_host[offset], dialog_offset=offset))

    init = source('409430')
    defaults = {int(i) * 4: int(value) for i, value in re.findall(
        r'\*\(\(_DWORD \*\)this \+ (\d+)\) = (-?\d+);', init)}
    ddx = source('402F80')
    controls = {}
    for match in re.finditer(r'DDX_(Text|Check)\(a2, (\d+), \((unsigned int|int) \*\)\(this \+ (\d+)\)\);', ddx):
        kind, cid, datatype, offset = match.groups()
        row = dict(control_id=int(cid), kind=kind.lower(), unsigned=datatype == 'unsigned int')
        if kind == 'Text':
            validator = re.match(r'\s*DDV_MinMax(?:U?Int)\(a2, .*?, (-?(?:0x[0-9A-Fa-f]+|\d+))u?, (0x[0-9A-Fa-f]+|\d+)u?\);', ddx[match.end():])
            assert validator, cid
            row.update(minimum=int(validator[1], 0), maximum=int(validator[2], 0))
        else:
            row.update(minimum=0, maximum=1)
        controls[int(offset)] = row
    assert len(controls) == 88
    dialogs = json.loads((PROJECT_ROOT / 'recovery/resources/dialogs.json').read_text(encoding='utf-8'))
    dialog = next(d['dialog'] for d in dialogs if d['resource']['keys'][1] == 131)
    captions = {c['id']: c['title'] for c in dialog['controls']}
    for row in file_fields:
        row.update(controls[row['dialog_offset']])
        row['default'] = defaults[row['host_offset']]
        # The caption is the original resource's title, with the intentional
        # UI customizations applied (see tools/dialog_customizations.py).
        row['caption'] = control_title(131, row['control_id'], captions[row['control_id']])
    assert {r['host_offset'] for r in file_fields} == set(range(148, 500, 4))
    inputs = ['4191C0', '409430', '418810', '403780', '402F80', '403FF0']
    return dict(schema_version=1, file_lines=34, fields=file_fields,
                evidence={va: f'evidence/decompiled/sub_{va}.c' for va in inputs},
                boundaries=['ASCII decimal prefix within signed 32-bit range',
                            'No MFC UI, file dialog, game process, or injection backend'])


def outputs(schema):
    fields = sorted(schema['fields'], key=lambda r: r['dialog_offset'])
    lines = ['// Generated by tools/recover_parameters.py. Do not hand-edit.',
             '// DDX order; file order is explicit, not the object-memory order.']
    for r in fields:
        lines.append('    {' + ', '.join(str(x).lower() for x in (
            r['control_id'], r['host_offset'], r['dialog_offset'], r['line'],
            r['mask'], r['default'], r['minimum'], r['maximum'], r['unsigned'])) + '},')
    return {'parameters.json': json.dumps(schema, ensure_ascii=False, indent=2) + '\n',
            'parameter_fields.inc': '\n'.join(lines) + '\n'}


if __name__ == '__main__':
    schema = recover()
    for name, content in outputs(schema).items():
        (SOURCE_ROOT / 'generated' / name).write_text(content, encoding='utf-8')
    print(f"Recovered {len(schema['fields'])} fields in {schema['file_lines']} lines; load/save/transfer/DDX agree")
