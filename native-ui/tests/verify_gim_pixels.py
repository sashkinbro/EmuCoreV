"""Compare native GIM output with an independent Python decoder.

Local owned GIM inputs are explicit; no original assets are bundled. The tool
uses a fresh device directory and a fresh host output directory, without
starting or modifying the application. Input filenames are replaced with
numeric ordinals before shell access.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import uuid


def reference_pixels(data):
    """Decode observed linear INDEX4/INDEX8/RGBA8888 planes without native code."""
    assert data[:16] == b'MIG.00.1PSP\0\0\0\0\0'
    images = []

    def children(start, end):
        while start < end:
            kind, _, size, _, payload = struct.unpack_from('<HHIII', data, start)
            yield kind, start, start + payload, start + size
            start += size
        assert start == end

    def plane(start):
        size, _, fmt, order, width, height, bpp, pitch_align, height_align = struct.unpack_from('<9H', data, start)
        assert size == 48 and order == 0
        index, first, end = struct.unpack_from('<III', data, start + 24)
        assert struct.unpack_from('<I', data, start + index)[0] == first
        stride = ((width * bpp + 7) // 8 + pitch_align - 1) // pitch_align * pitch_align
        assert len(data[start + first:start + end]) >= stride * ((height + height_align - 1) // height_align * height_align)
        return fmt, width, height, stride, data[start + first:start + end]

    root_size = struct.unpack_from('<I', data, 20)[0]
    root_start = 16 + struct.unpack_from('<I', data, 28)[0]
    for kind, _, picture_start, picture_end in children(root_start, 16 + root_size):
        if kind == 255:
            continue
        assert kind == 3
        planes = {kind: plane(payload) for kind, _, payload, _ in children(picture_start, picture_end)}
        fmt, width, height, stride, source = planes[4]
        if fmt == 3:
            pixels = b''.join(source[y * stride:y * stride + width * 4] for y in range(height))
        else:
            assert fmt in (4, 5)
            palette_format, colors, rows, _, palette = planes[5]
            assert palette_format == 3 and rows == 1
            output = bytearray()
            for y in range(height):
                for x in range(width):
                    index = source[y * stride + (x if fmt == 5 else x // 2)]
                    if fmt == 4:
                        index = (index >> ((x & 1) * 4)) & 15
                    assert index < colors
                    output.extend(palette[index * 4:index * 4 + 4])
            pixels = bytes(output)
        images.append((width, height, pixels))
    return images


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', required=True)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--inputs', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='New local output directory')
    parser.add_argument('--png-map', type=Path, help='Optional JSON mapping GIM filenames to original PNG paths (requires Pillow)')
    args = parser.parse_args()
    paths = sorted(args.inputs.glob('*.gim'))
    assert paths, 'No GIM inputs'
    args.output.mkdir()  # Refuse to overwrite an existing research result.
    adb = [args.adb, '-s', args.serial]

    def run(*command):
        result = subprocess.run([*adb, *map(str, command)], capture_output=True)
        if result.returncode:
            raise RuntimeError(f'ADB command failed ({result.returncode}): {result.stderr.decode(errors="replace")} {result.stdout.decode(errors="replace")}')
        return result.stdout

    root = '/data/local/tmp/paf-gim-' + uuid.uuid4().hex
    run('shell', 'mkdir', '-p', root + '/output')
    run('push', args.executable, root + '/decode')
    run('shell', 'chmod', '755', root + '/decode')
    with tempfile.TemporaryDirectory(prefix='paf-gim-inputs-') as directory:
        staging = Path(directory) / 'inputs'
        staging.mkdir()
        for index, path in enumerate(paths):
            (staging / f'{index:05}.gim').write_bytes(path.read_bytes())
        run('push', staging, root + '/')
    print(f'Uploaded {len(paths)} GIM inputs; running standalone decoder on {args.serial}', flush=True)
    # root is generated internally. Paths contain only fixed text/hex/ordinals.
    command = (f'for source in {root}/inputs/*.gim; do name=${{source##*/}}; '
               f'{root}/decode "$source" "{root}/output/$name" || exit $?; done')
    lines = run('shell', command).decode().splitlines()
    assert len(lines) == len(paths), 'Missing decoder output'
    run('pull', root + '/output', args.output / 'decoded')
    records = []
    total_pixels = 0
    pngs = json.loads(args.png_map.read_text(encoding='utf-8')) if args.png_map else {}
    verified_pngs = 0
    for index, (path, line) in enumerate(zip(paths, lines)):
        actual = json.loads(line)
        expected = reference_pixels(path.read_bytes())
        assert len(actual) == len(expected), (path, 'picture count')
        for picture, (width, height, pixels) in zip(actual, expected):
            assert (picture['width'], picture['height']) == (width, height), (path, 'dimensions')
            decoded = (args.output / 'decoded' / f'{index:05}.gim' / picture['file']).read_bytes()
            assert decoded == pixels, (path, 'pixel mismatch')
            if path.name in pngs:
                from PIL import Image
                with Image.open(pngs[path.name]) as source_png:
                    source_png = source_png.convert('RGBA')
                    assert source_png.size == (width, height), (path, 'PNG dimensions')
                    assert source_png.tobytes() == decoded, (path, 'original PNG pixels differ')
                    verified_pngs += 1
            records.append({'source': path.name, 'width': width, 'height': height,
                            'rgbaSha256': hashlib.sha256(pixels).hexdigest()})
            total_pixels += width * height
    report = {'device': args.serial, 'inputs': len(paths), 'pictures': len(records), 'pixels': total_pixels,
              'originalPngPairs': verified_pngs,
              'reference': 'independent Python decoder; original PAF draw comparison still pending', 'records': records}
    (args.output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f'PASS {len(paths)} GIM files, {len(records)} pictures, {total_pixels} identical RGBA pixels')
    if pngs:
        assert verified_pngs == len(pngs), 'Not all requested PNG pairs verified'
        print(f'PASS {verified_pngs} original source PNG pairs, identical RGBA pixels')


if __name__ == '__main__':
    main()
