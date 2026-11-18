"""Execute the reviewed shell wobble function offline with explicit trig samples.

This verifies phase stepping and basis construction, not the original libm or
widget scheduling. Generated numeric output stays outside version control.
"""

import argparse
import hashlib
import math
from pathlib import Path
import struct
import zlib
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_FPSCR,
                              UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_SP,
                              UC_ARM_REG_R0, UC_ARM_REG_S0)

REFERENCE_SHA = "caaed896f4780f559b3858e5b27897c05888b559dc1a2d7f80c761ca65c0af09"


def single(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    image = args.reference.read_bytes()
    if hashlib.sha256(image).hexdigest() != REFERENCE_SHA:
        raise ValueError("Unreviewed shell motion reference")
    oracle = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    ph_offset, = struct.unpack_from("<Q", image, 72)
    section_offset, = struct.unpack_from("<Q", image, 88)
    for index in range(2):
        ph = struct.unpack_from("<8I", image, ph_offset + index * 32)
        offset, size, compression, encryption = struct.unpack_from("<4Q", image, section_offset + index * 32)
        assert encryption == 2
        data = image[offset:offset + size]
        data = zlib.decompress(data) if compression == 2 else data
        oracle.mem_map(ph[2], (max(ph[5], len(data)) + 4095) & ~4095)
        oracle.mem_write(ph[2], data)
    for address, size in [(0x10000, 4096), (0x100000, 65536), (0x200000, 8192)]:
        oracle.mem_map(address, size)
    oracle.reg_write(UC_ARM_REG_C1_C0_2, 0xf00000)
    oracle.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    oracle.mem_write(0x81555154, struct.pack("<I", 0x201000))
    # Supply precisely recorded float samples at the two external math calls.
    # The shell function and its vector/cross helpers remain unmodified.
    for address in (0x8145b0a8, 0x8145b128):
        oracle.mem_write(address, bytes.fromhex("1eff2fe1"))  # ARM bx lr
    samples = {}

    def sample(uc, address, size, data):
        if address in samples:
            uc.reg_write(UC_ARM_REG_S0, samples[address])

    oracle.hook_add(UC_HOOK_CODE, sample, begin=0x8145b0a8, end=0x8145b128)
    records = []
    for strength in [0, .5, 1, 2]:
        for index in range(33):
            phase = single(-math.pi + index * math.pi / 16)
            delta, scale = single(1 / 60), single(1)
            expected_phase = single(phase + single(single(delta * scale) * single(.5)))
            while expected_phase >= single(math.pi):
                expected_phase = single(expected_phase - single(2 * math.pi))
            cosine, sine = single(math.cos(expected_phase)), single(math.sin(expected_phase))
            samples.update({0x8145b0a8: bits(cosine), 0x8145b128: bits(sine)})
            oracle.mem_write(0x200000, bytes(80))
            oracle.mem_write(0x200040, struct.pack("<f", phase))
            oracle.mem_write(0x2010ac, struct.pack("<f", strength))
            oracle.mem_write(0x81467aa8, struct.pack("<f", delta))
            oracle.mem_write(0x81467ab0, struct.pack("<f", scale))
            oracle.reg_write(UC_ARM_REG_R0, 0x200000)
            oracle.reg_write(UC_ARM_REG_LR, 0x10001)
            oracle.reg_write(UC_ARM_REG_SP, 0x10f000)
            oracle.reg_write(UC_ARM_REG_FPSCR, 0)
            oracle.emu_start(0x810cf333, 0x10000, count=100000)
            assert oracle.reg_read(UC_ARM_REG_PC) == 0x10000, "Shell function did not return"
            actual_phase = bytes(oracle.mem_read(0x200040, 4))
            assert actual_phase == struct.pack("<f", expected_phase), "Unexpected original clock update"
            records.append(struct.pack("<6f", cosine, sine, strength, phase, delta, scale)
                           + actual_phase + bytes(oracle.mem_read(0x200000, 48)))
    output = b"PAFWOB01" + struct.pack("<I", len(records)) + b"".join(records)
    args.output.write_bytes(output)
    print("Original motion cases:", len(records), "corpus SHA256:", hashlib.sha256(output).hexdigest())


if __name__ == "__main__":
    main()
