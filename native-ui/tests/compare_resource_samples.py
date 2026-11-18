"""Compare the native Android reader with paired XML/RCO reference inputs.

Inputs are supplied locally; firmware and third-party samples are not bundled.
Runs only a standalone CLI, without starting or changing the emulator app.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import xml.etree.ElementTree as ET


def f32(value):
    return struct.unpack('<f', struct.pack('<f', float(value)))[0]


def identity_hash(value):
    return int.from_bytes(hashlib.sha1(value.encode('utf-8')).digest()[:4], 'big')


def compare(source, decoded):
    xml_nodes = list(ET.parse(source).getroot().iter())
    nodes = decoded['elements']
    assert len(nodes) == len(xml_nodes), (source, 'element count', len(nodes), len(xml_nodes))
    ids = {node.attrib['id']: parsed['offset'] for node, parsed in zip(xml_nodes, nodes) if 'id' in node.attrib}
    checked = 0
    for xml, node in zip(xml_nodes, nodes):
        assert xml.tag == node['name'], (source, xml.tag, node['name'])
        attrs = {a['name']: a for a in node['attributes']}
        # The resource compiler inserts decompressed payload sizes.
        added = set(attrs) - set(xml.attrib)
        assert added <= {'origsize'}, (source, xml.tag, 'unexpected generated attributes', added)
        assert set(xml.attrib) <= set(attrs), (source, xml.tag, 'missing attributes')
        if 'origsize' in added:
            assert attrs['origsize']['type'] == 1 and attrs['origsize']['value'] > 0
        for name, expected in xml.attrib.items():
            a = attrs[name]
            kind, actual = a['type'], a['value']
            if kind in (3, 4):
                assert actual == expected, (source, name, actual, expected)
            elif kind == 1:
                assert actual == int(expected, 0), (source, name, actual, expected)
            elif kind == 2:
                assert f32(actual) == f32(expected), (source, name, actual, expected)
            elif kind in (6, 7):
                convert = int if kind == 6 else f32
                values = [convert(v.strip()) for v in expected.split(',')]
                assert [convert(v) for v in actual] == values, (source, name, actual, values)
            elif kind == 5:
                assert actual == identity_hash(expected), (source, name, actual, expected)
            elif kind in (9, 10, 11, 12):
                if kind in (9, 10):
                    assert actual['name'] == expected, (source, name, actual, expected)
                else:
                    assert actual['hash'] == identity_hash(expected), (source, name, actual, expected)
                assert actual['element'] == ids.get(expected, -1), (source, name, actual, ids.get(expected))
            elif kind == 8:
                # XML has source filenames. RCO stores their compiled payloads;
                # payload decoding is a separate reconstruction step.
                assert actual['bytes'] > 0, (source, name, 'empty embedded file')
            else:
                raise AssertionError((source, name, 'unverified type', kind))
            checked += 1
    return len(nodes), checked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', required=True)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--samples', type=Path, required=True)
    args = parser.parse_args()
    adb = [args.adb, '-s', args.serial]

    def run(*command):
        return subprocess.run([*adb, *map(str, command)], check=True, capture_output=True).stdout

    remote = '/data/local/tmp/emucorev-paf-inspect'
    run('push', args.executable, remote)
    run('shell', 'chmod', '755', remote)
    total_nodes = total_attributes = count = 0
    failures = []
    for source in sorted(args.samples.rglob('cxml/sample_plugin.xml')):
        binary = source.parent.parent / 'sample_plugin.rco'
        if not binary.exists():
            continue
        run('push', binary, '/data/local/tmp/emucorev-paf-test.rco')
        decoded = json.loads(run('shell', remote, '/data/local/tmp/emucorev-paf-test.rco'))
        try:
            nodes, attributes = compare(source, decoded)
        except AssertionError as error:
            failures.append(str(error))
            print(f'FAIL {source.parent.parent.name}: {error}', flush=True)
            continue
        total_nodes += nodes
        total_attributes += attributes
        count += 1
        print(f'PASS {source.parent.parent.name}: {nodes} elements, {attributes} attributes', flush=True)
    assert count, 'No paired samples found'
    print(f'PASS total: {count} resources, {total_nodes} elements, {total_attributes} attributes')
    if failures:
        raise SystemExit(f'{len(failures)} XML/RCO comparisons failed; do not treat these inputs as verified pairs')


if __name__ == '__main__':
    main()
