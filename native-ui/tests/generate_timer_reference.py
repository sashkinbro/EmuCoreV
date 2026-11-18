"""Execute original scalar PAF Timer functions offline, never in the UI runtime.

Requires Unicorn and the exact locally reviewed decrypted reference binary.
Generated state words, not firmware instructions, form the comparison corpus.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB
from unicorn.arm_const import UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_FPSCR, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_SP, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_S0

REFERENCE_SHA = '5a74caf8a0e7b67624c23ea95a67f6b2a79adfb8919352310330c2b10ce10181'


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    image = args.reference.read_bytes()
    assert hashlib.sha256(image).hexdigest() == REFERENCE_SHA, 'Unreviewed timer reference binary'
    ph_offset, = struct.unpack_from('<Q', image, 72)
    section_offset, = struct.unpack_from('<Q', image, 88)
    oracle = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    for index in range(2):
        ph = struct.unpack_from('<8I', image, ph_offset + index * 32)
        offset, size, compression, encryption = struct.unpack_from('<4Q', image, section_offset + index * 32)
        assert encryption == 2
        data = image[offset:offset + size]
        if compression == 2:
            data = zlib.decompress(data)
        else:
            assert compression == 1
        oracle.mem_map(ph[2], (max(ph[5], len(data)) + 4095) & ~4095)
        oracle.mem_write(ph[2], data)
    for address, size in [(0x10000, 4096), (0x100000, 65536), (0x200000, 4096)]:
        oracle.mem_map(address, size)
    oracle.reg_write(UC_ARM_REG_C1_C0_2, 0xf00000)
    oracle.reg_write(UC_ARM_REG_FPEXC, 0x40000000)

    def call(address):
        oracle.reg_write(UC_ARM_REG_LR, 0x10001)
        oracle.reg_write(UC_ARM_REG_SP, 0x10f000)
        oracle.reg_write(UC_ARM_REG_FPSCR, 0)
        oracle.emu_start(address | 1, 0x10000, count=100000)
        assert oracle.reg_read(UC_ARM_REG_PC) == 0x10000, 'Original function did not return'

    def snapshot():
        data = oracle.mem_read(0x200000, 28)
        assert struct.unpack_from('<I', data, 20)[0] == 0, 'Unexpected curve parameter allocation'
        return [struct.unpack_from('<I', data, p)[0] for p in (4, 8, 12, 16, 24)]

    traces = []
    frames = 64
    intervals = [0, 1, 1, 2, 0.5, 1.5, 1]
    for curve, identifier in enumerate([0, 1, 2, 7, 21]):
        for unit in [16.666666, 8.333333, 250]:
            for duration in [0, -10, 0.1, 16.666666, 17, 100, 250, 1000, 1234.5,
                             1e20, float('inf'), float('-inf'), float('nan')]:
                for repeat in [0, 1, 4, 12]:
                    for count in [-1, 0, 2]:
                        oracle.reg_write(UC_ARM_REG_S0, bits(unit))
                        call(0x81047a28)  # InitTickUnit
                        oracle.mem_write(0x200000, bytes(28))
                        oracle.mem_write(0x10f000, bytes(4))  # NULL curve params.
                        for register, value in [(UC_ARM_REG_R0, 0x200000), (UC_ARM_REG_R1, identifier),
                                                (UC_ARM_REG_R2, repeat), (UC_ARM_REG_R3, count & 0xffffffff),
                                                (UC_ARM_REG_S0, bits(duration))]:
                            oracle.reg_write(register, value)
                        call(0x8104845e)  # InitTimer
                        words = [bits(unit), bits(duration), curve, repeat, count & 255, frames, *snapshot()]
                        for frame in range(frames):
                            command = 1 if frame in (14, 48) else 0
                            state = {20: 3, 22: 1, 26: 4, 28: 1, 32: 2}.get(frame, 255)
                            if command:
                                oracle.reg_write(UC_ARM_REG_R0, 0x200000)
                                call(0x81048444)  # ResetTimer
                            if state != 255:
                                flags = snapshot()[3]
                                oracle.mem_write(0x200010, struct.pack('<I', (flags & ~0xf000) | state << 12))
                            interval = intervals[frame % len(intervals)]
                            oracle.mem_write(0x81301168, struct.pack('<f', interval))
                            oracle.reg_write(UC_ARM_REG_R0, 0x200000)
                            call(0x8104c162)  # DoTick
                            words.extend([bits(interval), command, state, oracle.reg_read(UC_ARM_REG_S0), *snapshot()])
                        traces.append(words)
        print(f'Curve {identifier}: original timer traces captured', flush=True)
    output = b'PAFTIM01' + struct.pack('<I', len(traces))
    output += b''.join(struct.pack('<' + str(len(words)) + 'I', *words) for words in traces)
    args.output.write_bytes(output)
    report = {'referenceSha256': REFERENCE_SHA, 'corpusSha256': hashlib.sha256(output).hexdigest(),
              'traces': len(traces), 'frames': len(traces) * frames, 'fpscr': 0,
              'scope': 'five scalar curves, observed repeat modes, resets, held states, explicit intervals; no widget scheduling'}
    args.output.with_suffix('.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f'Generated {len(traces)} traces, {len(traces) * frames} frames')


if __name__ == '__main__':
    main()
