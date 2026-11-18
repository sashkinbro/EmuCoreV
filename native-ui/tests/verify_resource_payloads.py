"""Compare native extraction to independent Python zlib and UTF-16 decoding.

Owned RCO inputs are supplied explicitly and never bundled. Each Android run
creates a fresh research directory beneath /data/local/tmp. No app is started.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import uuid
import zlib


def text_values(data):
    """Read raw RCSF string records independently of the native JSON reader."""
    header = struct.unpack_from('<20I', data)
    tree = data[header[2]:header[2] + header[3]]
    wide = data[header[10]:header[10] + header[11]]
    values = []
    cursor = 0
    while cursor < len(tree):
        count = struct.unpack_from('<I', tree, cursor + 4)[0]
        for index in range(count):
            _, kind, offset, length = struct.unpack_from('<4I', tree, cursor + 28 + index * 16)
            if kind == 4:
                raw = wide[offset * 2:offset * 2 + length]
                assert len(raw) == length
                values.append(raw.decode('utf-16-le', errors='surrogatepass'))
        cursor += 28 + count * 16
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', required=True)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('resources', type=Path, nargs='+')
    args = parser.parse_args()
    adb = [args.adb, '-s', args.serial]

    def run(*command):
        return subprocess.run([*adb, *map(str, command)], check=True, capture_output=True).stdout

    root = '/data/local/tmp/paf-verify-' + uuid.uuid4().hex
    run('shell', 'mkdir', root)
    remote = root + '/inspect'
    run('push', args.executable, remote)
    run('shell', 'chmod', '755', remote)
    payload_count = string_count = locale_count = 0
    for ordinal, path in enumerate(args.resources):
        source = root + '/' + str(ordinal) + '.rco'
        output = root + '/' + str(ordinal) + '-files'
        run('push', path, source)
        decoded = json.loads(run('shell', remote, source, '--extract', output))
        raw = path.read_bytes()
        offset, size = struct.unpack_from('<II', raw, 72)
        files = raw[offset:offset + size]
        resource_count = 0
        for element in decoded['elements']:
            attrs = {a['name']: a for a in element['attributes']}
            for index, a in enumerate(element['attributes']):
                if a['type'] != 8:
                    continue
                offset, size = a['raw']
                payload = files[offset:offset + size]
                if attrs.get('compress', {}).get('value') == 'on':
                    payload = zlib.decompress(payload)
                    assert len(payload) == attrs['origsize']['value']
                name = output + '/' + str(element['offset']) + '_' + str(index) + '.bin'
                actual = run('shell', 'sha256sum', name).decode().split()[0]
                assert actual == hashlib.sha256(payload).hexdigest(), (path, element['offset'], 'payload bytes')
                if payload[:4] == b'RCSF':
                    locale = json.loads(run('shell', remote, name))
                    expected = text_values(payload)
                    actual = [a['value'] for e in locale['elements'] for a in e['attributes'] if a['type'] == 4]
                    assert actual == expected, (path, element['offset'], 'UTF-16 values')
                    string_count += len(expected)
                    locale_count += 1
                payload_count += 1
                resource_count += 1
        print(f'PASS {path.name}: {resource_count} exact payloads', flush=True)
    print(f'PASS {payload_count} byte-identical payloads, {locale_count} locale documents, {string_count} UTF-16 strings')
    print(f'Isolated device research outputs: {root}')


if __name__ == '__main__':
    main()
