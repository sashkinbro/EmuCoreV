"""Record reviewed PageTurn calculations/events from an owned PAF, offline.

Widget notifications, pointer acquisition/coordinates and the base draw host are
stubbed. The original layout, normalization, drag arithmetic, hit-test and
release-decision instructions remain unchanged. No reference code ships in UI.
"""
import argparse
import hashlib
from pathlib import Path
import random
import struct
import zlib
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_FPSCR,
    UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_SP, UC_ARM_REG_R0, UC_ARM_REG_R1,
    UC_ARM_REG_R2, UC_ARM_REG_S0, UC_ARM_REG_S1, UC_ARM_REG_S2)

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
    if compression == 2: data = zlib.decompress(data)
    uc.mem_map(ph[2], (max(ph[5], len(data)) + 4095) & ~4095)
    uc.mem_write(ph[2], data)
for address, size in ((0x10000, 4096), (0x100000, 65536), (0x200000, 0x20000)):
    uc.mem_map(address, size)
uc.reg_write(UC_ARM_REG_C1_C0_2, 0xf00000)
uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)

def word(address, value): uc.mem_write(address, struct.pack("<I", value))
def scalar(address, value): uc.mem_write(address, struct.pack("<f", value))
def bits(value): return struct.unpack("<I", struct.pack("<f", value))[0]
def point(address, values): uc.mem_write(address, struct.pack("<2f", *values))
def read(address, length): return bytes(uc.mem_read(address, length))

events = []
pointer = (0, 0)
stubs = (0x100100, 0x100110, 0x81100680, 0x81154a4c, 0x81102d54,
         0x8110226c, 0x8115c2cc, 0x8114daa4, 0x81144892, 0x811612e2, 0x8113c33c,
         0x810760c8, 0x81212be0, 0x811dbe78, 0x81053778)
for address in stubs: uc.mem_write(address, bytes.fromhex("7047"))

def callback(machine, address, size, data):
    if address == 0x81100680:
        machine.mem_write(0x2002c8, read(machine.reg_read(UC_ARM_REG_R1), 8))
    elif address == 0x8110226c:
        point(machine.reg_read(UC_ARM_REG_R2), pointer)
        machine.reg_write(UC_ARM_REG_R0, 0)
    elif address == 0x8115c2cc:
        machine.reg_write(UC_ARM_REG_R0, 0)
    elif address == 0x8113c33c:
        machine.reg_write(UC_ARM_REG_R0, 1)
    elif address == 0x100100:
        events.append(machine.reg_read(UC_ARM_REG_R1))
    elif address == 0x100110:
        word(0x20027c, machine.reg_read(UC_ARM_REG_R1))
    elif address == 0x810760c8:
        machine.reg_write(UC_ARM_REG_R0, 0x217000)
    elif address == 0x81212be0:
        vector = machine.reg_read(UC_ARM_REG_R0)
        if vector not in (0x207024, 0x207034):
            raise RuntimeError("Unexpected mesh vector allocation")
        word(vector, 0x214000 if vector == 0x207024 else 0x215000)
        word(vector + 8, 128)
        machine.reg_write(UC_ARM_REG_R0, 1)
    elif address == 0x811dbe78:
        machine.reg_write(UC_ARM_REG_R0, machine.reg_read(UC_ARM_REG_R1))

uc.hook_add(UC_HOOK_CODE, callback)

def call(address, r1=0, r0=0x200000):
    uc.reg_write(UC_ARM_REG_R0, r0)
    uc.reg_write(UC_ARM_REG_R1, r1)
    uc.reg_write(UC_ARM_REG_SP, 0x10f000)
    uc.reg_write(UC_ARM_REG_LR, 0x10001)
    uc.reg_write(UC_ARM_REG_FPSCR, 0)
    uc.emu_start(address | 1, 0x10000, count=100000)
    if uc.reg_read(UC_ARM_REG_PC) != 0x10000:
        raise RuntimeError("Original PageTurn operation did not return")

rng = random.Random(0x50414745)
cases = []
for width, height in ((960, 544), (896, 448), (840, 512), (1234, 567)):
    for fraction, aspect in ((.15, .8), (.2, 1), (.1, .6)):
        # Include corner/rectangle boundaries and the diagonal exclusion edge.
        fold = struct.unpack("<f", struct.pack("<f", height * fraction))[0]
        for dx, dy in ((0, 0), (1, -50), (-20, -20), (-fold - 50, -50),
                       (-fold - 51, -50), (-50, -fold - 50), (-50, -fold - 51),
                       (-10, -fold * .5), (-100, -100), (-280, 0), (50, 0), (80, 0)):
            for outward in (0, 1):
                values = [width, height, fraction, aspect, width * .5 + dx, height * .5 + dy,
                    width * .5 - 40, height * .5 - 40, 2, -3, 1, 1, .999, dx, dy, 280]
                cases.append((values, outward))
for _ in range(128):
    width, height = rng.uniform(200, 1800), rng.uniform(100, 900)
    values = [width, height, rng.uniform(.05, .3), rng.uniform(.4, 1.4),
        rng.uniform(-width, width), rng.uniform(-height, height),
        rng.uniform(-width, width), rng.uniform(-height, height),
        rng.uniform(-20, 20), rng.uniform(-20, 20),
        rng.random(), rng.random(), rng.random(), rng.uniform(-400, 400), rng.uniform(-400, 400), rng.uniform(1, 400)]
    cases.append((values, rng.randrange(2)))
for x, y in ((960, 544), (3840, 2176), (-3840, -2176), (-3840, 2176), (3840, -2176), (0, 0)):
    cases.append(([960, 544, .15, .8, x, y, 420, 210, 0, 0, 1, 1, 1, 0, 0, 280], 0))

records = []
for values, outward in cases:
    packed = struct.pack("<16fI", *values, outward)
    values = struct.unpack("<16f", packed[:64])
    uc.mem_write(0x200000, bytes(0x20000))
    word(0x200000, 0x100000)
    word(0x100060, 0x100101)
    word(0x100170, 0x100111)
    word(0x200164, 0x210000)
    point(0x200048, values[:2])
    uc.reg_write(UC_ARM_REG_S0, bits(values[2]))
    uc.reg_write(UC_ARM_REG_S1, bits(values[3]))
    call(0x81100744)
    expected = read(0x2002c8, 8) + read(0x200280, 12)
    point(0x2002c8, values[4:6])
    point(0x2002d0, values[8:10])
    for address, value in zip((0x2001ac, 0x2001a8, 0x2001a0), values[10:13]): scalar(address, value)
    word(0x2002f0, 0x211000)
    word(0x2002f4, 0x212000)
    call(0x811012d6)
    expected += read(0x211058, 16) + read(0x21202c, 4)
    word(0x20027c, 2)
    point(0x200298, values[4:6])
    point(0x2002a0, values[6:8])
    point(0x2002c0, values[8:10])
    scalar(0x81301168, 1)
    uc.reg_write(UC_ARM_REG_S2, bits(1))
    call(0x81100f9a)
    expected += read(0x2002c8, 8)
    word(0x20027c, 0)
    pointer = values[4:6]
    events.clear()
    word(0x203004, 0x41)
    call(0x81102384, 0x203000)
    expected += struct.pack("<I", int(0x10000001 in events))
    for gamepad in (False, True):
        word(0x20027c, 3 if gamepad else 2)
        point(0x200298, values[13:15])
        point(0x2002a0, (0, 0))
        scalar(0x20028c, values[15])
        uc.mem_write(0x2002ae, bytes([outward * 2]))
        word(0x203004, 0x12 if gamepad else 0x42)
        word(0x203008, 0x1000000 if gamepad else 0)
        events.clear()
        call(0x8110287c if gamepad else 0x81102384, 0x203000)
        if len(events) != 1:
            raise RuntimeError(f"Unexpected original release events: {events}")
        expected += struct.pack("<I", events[0])
    if len(expected) != 60:
        raise ValueError("Invalid PageTurn reference record")
    shape_inputs = struct.pack("<4f", values[4] / values[1], values[5] / values[1], values[0] / values[1], 1)
    uc.mem_write(0x205060, shape_inputs[:8])
    uc.mem_write(0x205068, b"\x01")
    uc.mem_write(0x206000, shape_inputs[8:])
    uc.reg_write(UC_ARM_REG_R2, 0x206000)
    call(0x810c9f34, 0x205000, 0x204000)
    shape = read(0x204000, 48)
    for offset in (0x30, 0x48, 0x60, 0x78): shape += read(0x204000 + offset, 20)
    shape += read(0x204090, 8) + read(0x204098, 20) + read(0x2040b0, 20)
    shape += read(0x2040c8, 16) + read(0x2040e0, 8)
    shape += struct.pack("<I", uc.mem_read(0x2040d8, 1)[0])
    if len(shape) != 204: raise ValueError("Invalid fold-shape reference record")
    word(0x207000, 200)  # Skip GPU buffer allocation; use caller-supplied storage.
    word(0x20700c, 0x208000)
    word(0x208000, 0x208100)
    word(0x208100, 0x209000)
    word(0x207018, 0x7fc00000)  # Force the original dirty comparison.
    word(0x20701c, 0x7fc00000)
    uc.reg_write(UC_ARM_REG_R2, 0x205060)
    call(0x810c71e0, 0x204000, 0x207000)
    base, = struct.unpack("<I", read(0x207020, 4))
    groups = []
    for offset in (0x24, 0x34):
        address, count = struct.unpack("<2I", read(0x207000 + offset, 8))
        if count > 128: raise ValueError("Original mesh fan count exceeds test storage")
        group = [struct.unpack("<2I", read(address + i * 8, 8)) for i in range(count)]
        groups.append([(first - base, count) for first, count in group])
    fans = groups[0] + groups[1]
    vertex_count = max((first + count for first, count in fans), default=0)
    if vertex_count > 1024 or sum(count for _, count in fans) != vertex_count:
        raise ValueError("Original mesh is not a contiguous fan sequence")
    mesh = struct.pack("<3I", vertex_count, len(groups[0]), len(groups[1]))
    mesh += read(0x209000 + base * 8, vertex_count * 8)
    mesh += b"".join(struct.pack("<2I", *fan) for fan in fans)
    records.append(packed + expected + shape_inputs + shape + mesh)
args.output.write_bytes(b"PAFPAG03" + struct.pack("<I", len(records)) + b"".join(records))
print("Generated", len(records), "original PageTurn records")
