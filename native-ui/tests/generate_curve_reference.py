"""Execute original PAF Thumb functions offline to generate comparison vectors.

Requires Python unicorn. An owned, already decrypted SELF is supplied locally;
no firmware instructions or original binary are distributed with these tools.
This is an offline test oracle, never a dependency of the native UI runtime.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import zlib

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB
from unicorn.arm_const import UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_FPSCR, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_S0


# Confirmed entry points for the local reference binary. Validate each function
# region before execution; a different firmware profile requires a fresh audit.
FUNCTIONS = [
    ('linear', 0x810485e4, 46, 'da936a29fdd28e3d2264ca33569f968541f2e8c8eb7e25d9960d5a06b6174ebc'),
    ('slower', 0x8104b212, 52, '4a4795bdbceabf392418d198566c5d82f410cfd4b2d41f122c6a22dfd4a3d990'),
    ('faster', 0x8104a708, 52, '5948d78f61849ffd159d7b71b626e7af5894e5fc9d49048ba01c5153ddae6ce7'),
    ('quadratic_in', 0x8104a73c, 86, '57ff156bc17212c2004e0b2a13f996e9e7e0a54f0f5d32a0e5e63c11e2c78e9c'),
    ('quadratic_out', 0x8104aec6, 130, 'e918d8593b4cd58925b786a33e9e3d5d863d13a37f2d1c640f85f4632b00b4fc'),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    image = args.reference.read_bytes()
    assert image[:4] == b'SCE\x00', 'Expected decrypted SELF'
    ph_offset, = struct.unpack_from('<Q', image, 72)
    section_offset, = struct.unpack_from('<Q', image, 88)
    ph = struct.unpack_from('<8I', image, ph_offset)
    offset, length, compression, encryption = struct.unpack_from('<4Q', image, section_offset)
    assert encryption == 2, 'SELF section is not decrypted'
    code = image[offset:offset + length]
    if compression == 2:
        code = zlib.decompress(code)
    else:
        assert compression == 1, 'Unknown SELF compression'
    base = ph[2]
    for name, address, size, digest in FUNCTIONS:
        start = address - base
        assert start >= 0 and start + size <= len(code), (name, 'function range')
        assert hashlib.sha256(code[start:start + size]).hexdigest() == digest, (name, 'unverified reference code')
    oracle = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    oracle.mem_map(base, (len(code) + 4095) & ~4095)
    oracle.mem_write(base, code)
    stop = 0x10000
    oracle.mem_map(stop, 4096)
    oracle.reg_write(UC_ARM_REG_C1_C0_2, 0xf00000)
    oracle.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    random_values = random.Random(0x504146)
    inputs = {0, 0x80000000, 1, 0x80000001, 0x007fffff, 0x00800000,
              0x3f7fffff, 0x3f800000, 0x3f800001, 0xbf800000,
              0x7f800000, 0xff800000, 0x7fc01234, 0xffc01234,
              0x7f801234, 0xff801234}
    inputs.update(struct.unpack('<I', struct.pack('<f', i / 4096))[0] for i in range(4097))
    inputs.update(random_values.getrandbits(32) for _ in range(4096))
    records = []
    for index, (name, address, _, _) in enumerate(FUNCTIONS):
        for bits in sorted(inputs):
            oracle.reg_write(UC_ARM_REG_FPSCR, 0)  # IEEE, round-to-nearest, no flush/default-NaN.
            oracle.reg_write(UC_ARM_REG_LR, stop | 1)
            oracle.reg_write(UC_ARM_REG_S0, bits)
            oracle.emu_start(address | 1, stop, count=1000)
            assert oracle.reg_read(UC_ARM_REG_PC) == stop, (name, bits, 'did not return')
            records.append((index, bits, oracle.reg_read(UC_ARM_REG_S0)))
        print(f'{name}: {len(inputs)} original-code results', flush=True)
    output = b'PAFCURV1' + struct.pack('<I', len(records)) + b''.join(struct.pack('<III', *r) for r in records)
    args.output.write_bytes(output)
    report = {'referenceSha256': hashlib.sha256(image).hexdigest(), 'corpusSha256': hashlib.sha256(output).hexdigest(),
              'records': len(records), 'inputsPerFunction': len(inputs), 'fpscr': 0,
              'functions': [{'name': n, 'address': hex(a), 'codeBytes': s, 'codeSha256': h} for n, a, s, h in FUNCTIONS]}
    args.output.with_suffix('.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f'Generated {len(records)} records; {args.output}')


if __name__ == '__main__':
    main()
