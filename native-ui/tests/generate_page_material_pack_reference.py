"""Capture the original PageTurn MaterialPack setter for the native builder."""
import argparse
import hashlib
import struct
from pathlib import Path

from generate_page_material_reference import record


DEFAULT_MATERIAL = (
    1.0, 1.0, 1.0, 1.0,
    *(struct.unpack('<f', struct.pack('<I', bits))[0] for bits in (
        0x3f649247, 0x3f8879ee, 0x4185f15e, 0x3da491b0, 0x3f4d30ad,
        0x3e9349e0, 0x404321c9, 0x3ec49214, 0x3dea454e)),
)
CUSTOM_MATERIAL = (
    .25, .5, .75, .875, .5, 2.0, 8.0, .125, .75, .4, 1.5, .625, .3,
)
DEFAULT_SHAPE = (
    *(struct.unpack('<f', struct.pack('<I', bits))[0] for bits in
      (0x3e6de871, 0x3f1b6da8, 0x3c1246bf, 0x40536db5)),
)
CUSTOM_SHAPE = (.18, .95, .012, 4.0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    image = args.reference.read_bytes()
    if hashlib.sha256(image).hexdigest() != '5a74caf8a0e7b67624c23ea95a67f6b2a79adfb8919352310330c2b10ce10181':
        raise ValueError('Unreviewed PAF reference')

    records = []
    for width, height in ((960, 544), (896, 448), (840, 512), (1234, 567)):
        aspect = width / height
        points = ((aspect - .12, .85), (aspect - .02, .99), (aspect + .1, 1.1))
        for shape_style in (DEFAULT_SHAPE, CUSTOM_SHAPE):
            for material in (DEFAULT_MATERIAL, CUSTOM_MATERIAL):
                for point in points:
                    expected = record(image, width, height, point, shape_style,
                        material_values=material, material_pack=True)
                    if len(expected) != 16:
                        raise ValueError('Invalid original MaterialPack record')
                    values = (width, height, *point, *shape_style, *material)
                    records.append(struct.pack('<21f', *values) + struct.pack('<16I', *expected))

    args.output.write_bytes(b'PAFMPK01' + struct.pack('<I', len(records)) + b''.join(records))
    print(f'Generated {len(records)} original MaterialPack records')


if __name__ == '__main__':
    main()
