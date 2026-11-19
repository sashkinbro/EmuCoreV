"""Run the reviewed PAF BusyIndicator offline; no original code enters the UI.

The object host is minimal: record positions/sizes, retain the original packed
color setter, and replace widget notifications. Trig samples use host double
libm rounded to float, so this corpus does not prove original PAF libm parity.
"""
import argparse
import hashlib
import math
from pathlib import Path
import struct
import zlib
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_FPSCR,
    UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_SP, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_S0)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("reference", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
image = args.reference.read_bytes()
if hashlib.sha256(image).hexdigest() != "5a74caf8a0e7b67624c23ea95a67f6b2a79adfb8919352310330c2b10ce10181":
    raise ValueError("Unreviewed PAF reference")
uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
ph_offset, = struct.unpack_from("<Q", image, 72)
section_offset, = struct.unpack_from("<Q", image, 88)
for index in range(2):
    ph = struct.unpack_from("<8I", image, ph_offset + index * 32)
    offset, size, compression, encryption = struct.unpack_from("<4Q", image, section_offset + index * 32)
    if encryption != 2:
        raise ValueError("Reference is not decrypted")
    data = image[offset:offset + size]
    if compression == 2:
        data = zlib.decompress(data)
    uc.mem_map(ph[2], (max(ph[5], len(data)) + 4095) & ~4095)
    uc.mem_write(ph[2], data)
for address, size in ((0x10000, 4096), (0x100000, 65536), (0x200000, 0x20000)):
    uc.mem_map(address, size)
uc.reg_write(UC_ARM_REG_C1_C0_2, 0xf00000)
uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)

def word(address, value): uc.mem_write(address, struct.pack("<I", value))
def scalar(address, value): uc.mem_write(address, struct.pack("<f", value))
def f32(value): return struct.unpack("<f", struct.pack("<f", value))[0]

# The original color setter at 0x811efb4c remains executable and unmodified.
stubs = (0x100100, 0x100110, 0x8108338c, 0x810833ee, 0x8104fd10, 0x8104fe34,
         0x811545ba, 0x8114db0e, 0x8114dc54, 0x8114063c)
for address in stubs:
    uc.mem_write(address, bytes.fromhex("7047"))

def callback(machine, address, size, data):
    if address == 0x100100:
        index = machine.reg_read(UC_ARM_REG_R1)
        machine.reg_write(UC_ARM_REG_R0, 0x210000 + index * 256 if index < 10 else 0)
    elif address in (0x8108338c, 0x810833ee):
        target = machine.reg_read(UC_ARM_REG_R0) + (8 if address == 0x8108338c else 24)
        machine.mem_write(target, bytes(machine.mem_read(machine.reg_read(UC_ARM_REG_R1), 16)))
    elif address in (0x8104fd10, 0x8104fe34):
        value = struct.unpack("<f", struct.pack("<I", machine.reg_read(UC_ARM_REG_S0)))[0]
        result = math.sin(value) if address == 0x8104fd10 else math.cos(value)
        machine.reg_write(UC_ARM_REG_S0, struct.unpack("<I", struct.pack("<f", result))[0])

uc.hook_add(UC_HOOK_CODE, callback)

def call(address):
    uc.reg_write(UC_ARM_REG_R0, 0x200000)
    uc.reg_write(UC_ARM_REG_SP, 0x10f000)
    uc.reg_write(UC_ARM_REG_LR, 0x10001)
    uc.reg_write(UC_ARM_REG_FPSCR, 0)
    uc.emu_start(address | 1, 0x10000, count=100000)
    if uc.reg_read(UC_ARM_REG_PC) != 0x10000:
        raise RuntimeError("Original busy operation did not return")

traces = []
for width, ball in ((64, 10), (80, 10), (96, 8), (120, 16)):
    for fps in (30, 60, 90, 120):
        for interrupted in (False, True):
            uc.mem_write(0x200000, bytes(0x20000))
            word(0x200000, 0x100000)
            for delta in (0x60, 0x90, 0x94): word(0x100000 + delta, 0x100111)
            word(0x1000f8, 0x100101)
            scalar(0x200048, width)
            scalar(0x200290, struct.unpack("<f", struct.pack("<I", 0x3f20d97c))[0])
            scalar(0x2002a0, ball)
            for index in range(10):
                obj = 0x210000 + index * 256
                word(obj, 0x812e37c0)
                word(obj + 44, 0xffffffff)
            steps = []
            actions = [(1, 0)] + [(0, f32(1 / fps))] * int(fps * 4.2)
            if interrupted:
                actions[5:5] = [(2, 0)]
                actions[9:9] = [(1, 0)]
                actions += [(2, 0)] + [(0, f32(1 / fps))] * 4 + [(1, 0)]
                actions += [(0, f32(1 / fps))] * int(fps * .8)
            actions += [(2, 0)] + [(0, f32(1 / fps))] * int(fps * 2)
            actions += [(1, 0), (0, 0), (0, .2), (2, 0)] + [(0, f32(1 / fps))] * fps
            for action, delta in actions:
                scalar(0x81301160, delta)
                scalar(0x81301168, 1)
                call((0x810ddb40, 0x810de1da, 0x810de266)[action])
                state = bytes(uc.mem_read(0x20027c, 4)) + bytes(uc.mem_read(0x200294, 12))
                dots = []
                for index in range(1, 10):
                    obj = 0x210000 + index * 256
                    dots.append(bytes(uc.mem_read(obj + 8, 8)) + bytes(uc.mem_read(obj + 24, 4))
                                + struct.pack("<I", uc.mem_read(obj + 47, 1)[0]))
                steps.append(struct.pack("<If", action, delta) + state + b"".join(dots))
            traces.append(struct.pack("<ffI", width, ball, len(steps)) + b"".join(steps))
args.output.write_bytes(b"PAFBUS01" + struct.pack("<I", len(traces)) + b"".join(traces))
print("Generated", len(traces), "original busy traces")
