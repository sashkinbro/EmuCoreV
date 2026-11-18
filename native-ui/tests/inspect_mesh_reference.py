"""Validate a debug-only original PAF mesh capture, without starting emulation.

The capture and optional original GXP files are local research inputs. This
checks geometry/buffer integrity, not original draw replay or pixel equivalence.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct


def inspect(directory, original_programs=None):
    lines = (directory / 'mesh.txt').read_text().splitlines()
    assert lines[0] == 'PAF_MESH_CAPTURE 1' and lines[-1] == 'DONE', 'Incomplete mesh capture'
    single, attributes, streams, uniforms, textures, palettes = {}, [], {}, [], [], []
    unavailable = set()
    for line in lines[1:-1]:
        kind, *values = line.split()
        if kind in ('vertex_guid', 'fragment_guid', 'draw', 'viewport', 'cull'):
            assert kind not in single, ('Duplicate capture record', kind)
            single[kind] = list(map(int, values))
        elif kind == 'attribute':
            assert len(values) == 5
            attributes.append(list(map(int, values)))
        elif kind == 'stream':
            assert len(values) == 4
            index, stride, source, size = map(int, values)
            assert index not in streams and 0 <= index < 16 and source in (0, 1, 2, 3)
            streams[index] = {'stride': stride, 'source': source, 'bytes': size}
        elif kind in ('uniform', 'palette'):
            assert len(values) == 3 and values[0] in ('vertex', 'fragment')
            stage, index, size = values[0], int(values[1]), int(values[2])
            (uniforms if kind == 'uniform' else palettes).append((stage, index, size))
        elif kind == 'texture':
            assert len(values) == 10 and values[0] in ('vertex', 'fragment')
            stage = values[0]
            index, width, height, fmt, size, *descriptor = map(int, values[1:])
            assert 0 <= index < 16 and width > 0 and height > 0 and len(descriptor) == 4
            textures.append({'stage': stage, 'index': index, 'width': width, 'height': height,
                             'format': fmt, 'bytes': size, 'descriptor': descriptor})
        elif kind == 'texture_unavailable':
            assert len(values) == 2 and values[0] in ('vertex', 'fragment')
            unavailable.add((values[0], int(values[1])))
        else:
            raise AssertionError(('Unknown capture record', kind))
    assert set(single) == {'vertex_guid', 'fragment_guid', 'draw', 'viewport', 'cull'}
    primitive, index_format, count, instances = single['draw']
    assert index_format in (0, 0x01000000) and count > 0 and instances > 0
    assert len(single['viewport']) == 7 and len(single['cull']) == 1
    index_bytes = (directory / 'indices.bin').read_bytes()
    size = 2 if index_format == 0 else 4
    assert len(index_bytes) == count * size, 'Index buffer size mismatch'
    indices = struct.unpack('<' + str(count) + ('H' if size == 2 else 'I'), index_bytes)
    maximum = max(indices)
    hashes = {}

    def data(name, expected):
        blob = (directory / name).read_bytes()
        assert len(blob) == expected and expected > 0, (name, 'byte count mismatch')
        hashes[name] = hashlib.sha256(blob).hexdigest()
        return blob

    for index, stream in streams.items():
        data(f'stream-{index}.bin', stream['bytes'])
    decoded_attributes = []
    for index, offset, fmt, components, register in attributes:
        assert index in streams and fmt in (8, 9) and 1 <= components <= 4 and offset >= 0, 'Unreviewed vertex attribute'
        stream = streams[index]
        scalar_bytes = 2 if fmt == 8 else 4
        last = instances - 1 if stream['source'] in (2, 3) else maximum
        assert offset + stream['stride'] * last + scalar_bytes * components <= stream['bytes'], 'Vertex index exceeds stream'
        blob = (directory / f'stream-{index}.bin').read_bytes()
        values = [struct.unpack_from('<' + ('e' if fmt == 8 else 'f') * components,
                                     blob, offset + stream['stride'] * vertex) for vertex in range(last + 1)]
        assert all(math.isfinite(v) for row in values for v in row), 'Non-finite reference vertex'
        decoded_attributes.append({'stream': index, 'register': register, 'components': components,
                                   'range': [[min(row[i] for row in values), max(row[i] for row in values)] for i in range(components)]})
    for stage, index, size in uniforms:
        assert 0 <= index < 16
        data(f'{stage}-ubo-{index}.bin', size)
    for stage, index, size in palettes:
        data(f'{stage}-texture-{index}-palette.bin', size)
    known = {hashlib.sha256(p.read_bytes()).hexdigest() for p in original_programs.glob('*.gxp')} if original_programs else None
    programs = {}
    for stage in ('vertex', 'fragment'):
        blob = (directory / f'{stage}.gxp').read_bytes()
        assert len(blob) >= 156 and blob[:4] == b'GXP\0' and struct.unpack_from('<I', blob, 8)[0] == len(blob)
        assert list(struct.unpack_from('<II', blob, 12)) == single[stage + '_guid'], 'Program GUID mismatch'
        assert bool(struct.unpack_from('<I', blob, 20)[0] & 1) == (stage == 'fragment'), 'Program stage mismatch'
        digest = hashlib.sha256(blob).hexdigest()
        if known is not None:
            assert digest in known, (stage, 'Program differs from supplied original shaders')
        programs[stage] = digest
    texture_ids = {(t['stage'], t['index']) for t in textures}
    assert unavailable <= texture_ids and len(texture_ids) == len(textures)
    for texture in textures:
        key = texture['stage'], texture['index']
        if key not in unavailable:
            data(f'{key[0]}-texture-{key[1]}.bin', texture['bytes'])
        texture['sourceAvailable'] = key not in unavailable
    return {'geometryValidated': True, 'pixelReplayVerified': False, 'primitive': primitive,
            'indexCount': count, 'maximumIndex': maximum, 'instances': instances, 'streams': streams,
            'attributes': decoded_attributes, 'programSha256': programs, 'bufferSha256': hashes,
            'textures': textures, 'unavailableTextureSources': [list(key) for key in sorted(unavailable)],
            'limitation': 'No texture GPU freshness, original shader-stage execution, framebuffer feedback or pixel equivalence verified'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--original-programs', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    report = inspect(args.capture, args.original_programs)
    text = json.dumps(report, indent=2)
    if args.output:
        args.output.write_text(text, encoding='utf-8')
    print(f'PASS mesh integrity: {report["indexCount"]} indices, maximum vertex {report["maximumIndex"]}, {len(report["streams"])} streams')
    print(f'Unavailable texture sources: {report["unavailableTextureSources"]}; pixel replay remains unverified')


if __name__ == '__main__':
    main()
