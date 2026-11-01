"""Parse Cheat.s9 (original tool save) to determine the officer-id layout."""
import struct

data = open(r'G:\aiprojects\game\san9-companion\sanpkcheat\Cheat.s9', 'rb').read()
print('size', len(data), hex(len(data)))

# header
print('first 0xD0 bytes:')
for off in range(0, 0xD0, 16):
    row = data[off:off + 16]
    hexs = ' '.join(f'{b:02X}' for b in row)
    txt = ''.join(chr(b) if 32 <= b < 127 else '.' for b in row)
    print(f'  {off:04X}: {hexs:<48} {txt}')

REC = 0x88
body = len(data) - 0xD0
print(f'\nbody {body} / 0x88 = {body / REC}')
