"""Inventory embedded GXP programs from owned decrypted SELF reference files.

This is research tooling, not a runtime loader or decryption implementation.
Original programs and generated inventories must stay outside the source tree.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib


def word(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def inspect_program(program):
    assert len(program) >= 156 and program[:4] == b'GXP\0', 'Invalid GXP header'
    assert word(program, 8) == len(program), 'Invalid GXP byte count'
    count, relative = struct.unpack_from('<II', program, 36)
    offset = 40 + relative
    assert 156 <= offset <= len(program) and offset + count * 16 <= len(program), 'Invalid GXP parameter range'
    parameters = []
    for index in range(count):
        start = offset + index * 16
        delta, first, second, semantic, semantic_index, array, resource = struct.unpack_from('<i4BII', program, start)
        name_offset = start + delta
        assert offset + count * 16 <= name_offset < len(program), 'Invalid GXP parameter name offset'
        end = program.index(0, name_offset)
        name = program[name_offset:end].decode('utf-8')
        parameters.append({'name': name, 'category': first & 15, 'type': first >> 4,
                           'components': second & 15, 'container': second >> 4,
                           'semantic': semantic, 'semanticIndex': semantic_index, 'arraySize': array, 'resource': resource})
    instructions, relative = struct.unpack_from('<II', program, 60)
    primary = 64 + relative
    assert 156 <= primary <= len(program) and primary + instructions * 8 <= len(program), 'Invalid GXP primary instructions'
    return {'bytes': len(program), 'sha256': hashlib.sha256(program).hexdigest(), 'flags': word(program, 20),
            'stage': 'fragment' if word(program, 20) & 1 else 'vertex',
            'primaryInstructions': instructions, 'parameters': parameters}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='New directory for research outputs')
    parser.add_argument('references', type=Path, nargs='+')
    args = parser.parse_args()
    args.output.mkdir()
    inventory = []
    for ordinal, reference in enumerate(args.references):
        image = reference.read_bytes()
        assert image[:4] == b'SCE\0', 'Expected decrypted SELF'
        ph_offset, = struct.unpack_from('<Q', image, 72)
        section_offset, = struct.unpack_from('<Q', image, 88)
        # The referenced executables keep embedded GXP programs in their first
        # load segment. Further segment discovery requires an explicit audit.
        ph = struct.unpack_from('<8I', image, ph_offset)
        offset, size, compression, encryption = struct.unpack_from('<4Q', image, section_offset)
        assert encryption == 2, 'Section is not decrypted'
        raw = image[offset:offset + size]
        assert len(raw) == size, 'Truncated SELF section'
        if compression == 2:
            raw = zlib.decompress(raw)
        else:
            assert compression == 1, 'Unknown SELF compression'
        module = {'file': reference.name, 'sha256': hashlib.sha256(image).hexdigest(), 'programs': []}
        for match in re.finditer(b'GXP\0', raw):
            start = match.start()
            size = word(raw, start + 8)
            assert 156 <= size <= len(raw) - start, 'Invalid embedded GXP size'
            program = raw[start:start + size]
            record = inspect_program(program)
            record['address'] = ph[2] + start
            record['file'] = f'{ordinal:02}-{ph[2] + start:08x}.gxp'
            (args.output / record['file']).write_bytes(program)
            module['programs'].append(record)
        assert module['programs'], f'No validated programs in {reference}'
        print(f'{reference.name}: {len(module["programs"])} validated embedded programs', flush=True)
        inventory.append(module)
    (args.output / 'inventory.json').write_text(json.dumps(inventory, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
