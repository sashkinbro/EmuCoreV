"""Prepare PageTurn's original procedural lookup images during development.

Runs the reviewed original generators, including PAF's own expf, in an offline
ARM VM. Only graphics allocation/upload/wrap and byte-vector allocation are
hosted. The application receives ordinary PNGs, never this VM or reference code.
Requires Unicorn and Pillow. Inputs and generated resources stay outside Git.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
from PIL import Image
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC,
    UC_ARM_REG_FPSCR, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_SP,
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_S0)

REFERENCE_SHA = "5a74caf8a0e7b67624c23ea95a67f6b2a79adfb8919352310330c2b10ce10181"
GL, CREATE, RESERVE, UPLOAD = 0x810760c8, 0x811eb9dc, 0x811eb8f4, 0x8107ccf0
WRAP_S, WRAP_T, DELETE = 0x8107c95c, 0x8107c982, 0x81053778
EXPF = 0x81050320


def sha(data):
    return hashlib.sha256(data).hexdigest()


def generate(image, entry, exponential):
    vm = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    ph_offset, = struct.unpack_from("<Q", image, 72)
    section_offset, = struct.unpack_from("<Q", image, 88)
    for index in range(2):
        ph = struct.unpack_from("<8I", image, ph_offset + index * 32)
        offset, size, compression, encryption = struct.unpack_from("<4Q", image, section_offset + index * 32)
        if encryption != 2:
            raise ValueError("Reference must be decrypted")
        data = image[offset:offset + size]
        if compression == 2:
            data = zlib.decompress(data)
        vm.mem_map(ph[2], (max(ph[5], len(data)) + 4095) & ~4095)
        vm.mem_write(ph[2], data)
    for address, size in ((0x10000, 4096), (0x100000, 65536), (0x200000, 0x100000)):
        vm.mem_map(address, size)
    vm.reg_write(UC_ARM_REG_C1_C0_2, 0xf00000)
    vm.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    for address in (GL, CREATE, RESERVE, UPLOAD, WRAP_S, WRAP_T, DELETE):
        vm.mem_write(address, bytes.fromhex("7047"))

    def word(address, value):
        vm.mem_write(address, struct.pack("<I", value))

    def read_word(address):
        return struct.unpack("<I", vm.mem_read(address, 4))[0]

    uploads, wraps, exp_calls = [], [], []
    dimensions = (128, 8) if exponential else (64, 64)

    def host(machine, address, size, _):
        r0, r1 = machine.reg_read(UC_ARM_REG_R0), machine.reg_read(UC_ARM_REG_R1)
        if address == GL:
            machine.reg_write(UC_ARM_REG_R0, 0x210000)
        elif address == CREATE:
            width, height = machine.reg_read(UC_ARM_REG_R2), machine.reg_read(UC_ARM_REG_R3)
            if (width, height) != dimensions:
                raise ValueError("Unexpected original lookup dimensions")
            word(r0, 0x220000)
            word(r0 + 4, 0x221000)
            word(0x221004, 1)
            word(0x221008, 1)
            word(0x220088, width)
            word(0x22008c, height)
        elif address == RESERVE:
            if r1 > 16384:
                raise ValueError("Original byte vector exceeds extraction bounds")
            word(r0, 0x240000)
            word(r0 + 8, 16384)
            machine.reg_write(UC_ARM_REG_R0, 1)
        elif address == UPLOAD:
            if r0 != 0x220000 or r1 != 0:
                raise ValueError("Unexpected original texture upload")
            width, height = machine.reg_read(UC_ARM_REG_R2), machine.reg_read(UC_ARM_REG_R3)
            if (width, height) != dimensions:
                raise ValueError("Upload dimensions differ")
            source = read_word(machine.reg_read(UC_ARM_REG_SP) + 8)
            if source != 0x240000:
                raise ValueError("Upload does not reference the hosted byte vector")
            uploads.append(bytes(machine.mem_read(source, width * height)))
        elif address in (WRAP_S, WRAP_T):
            if r0 != 0x220000:
                raise ValueError("Unexpected wrap target")
            wraps.append(("s" if address == WRAP_S else "t", r1))
        elif address == EXPF:
            # Observe entry without replacing any math instructions.
            exp_calls.append(machine.reg_read(UC_ARM_REG_S0))

    vm.hook_add(UC_HOOK_CODE, host)
    vm.reg_write(UC_ARM_REG_R0, 0x200000 if exponential else 0)
    vm.reg_write(UC_ARM_REG_R1, 128 if exponential else 0x200000)
    vm.reg_write(UC_ARM_REG_S0, 0x40a00000)  # Original constructor's range 5.0.
    vm.reg_write(UC_ARM_REG_SP, 0x10f000)
    vm.reg_write(UC_ARM_REG_LR, 0x10001)
    vm.reg_write(UC_ARM_REG_FPSCR, 0)
    try:
        vm.emu_start(entry | 1, 0x10000, count=3000000)
    except Exception as error:
        raise RuntimeError(f"Original lookup generation failed at {vm.reg_read(UC_ARM_REG_PC):08x}") from error
    if vm.reg_read(UC_ARM_REG_PC) != 0x10000 or len(uploads) != 1:
        raise ValueError("Original lookup generation did not complete one upload")
    if wraps != [("s", 5), ("t", 5)]:
        raise ValueError("Unexpected original wrap configuration")
    if len(exp_calls) != (1024 if exponential else 0):
        raise ValueError("Original expf call count differs")
    pixels = uploads[0]
    width, height = dimensions
    if exponential:
        row = pixels[:width]
        if row[0] != 255 or row[-1] != 1 or any(a < b for a, b in zip(row, row[1:])):
            raise ValueError("Invalid exponential lookup endpoints/order")
        if pixels != row * height:
            raise ValueError("Original exponential lookup rows disagree")
    elif pixels[0] != 0 or pixels[-1] != 255 or any(
            pixels[y * width + x] != pixels[x * width + y]
            for y in range(height) for x in range(width)):
        raise ValueError("Invalid radial lookup endpoints/symmetry")
    return dimensions, pixels, wraps, len(exp_calls)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--pack", type=Path, required=True)
    args = parser.parse_args()
    image = args.reference.read_bytes()
    if sha(image) != REFERENCE_SHA:
        raise ValueError("Unreviewed PAF reference")
    target = args.pack / "effects" / "lookups"
    if target.exists():
        raise ValueError("Lookup output must be a new directory")
    # Finish both original computations before creating the output pack.
    results = [(name, entry, generate(image, entry, exponential)) for name, entry, exponential in (
        ("exp-minus", 0x81214330, True), ("corner-distance", 0x810c7be0, False))]
    target.mkdir(parents=True)
    manifest = {"version": 1, "referenceSha256": REFERENCE_SHA,
        "runtimeSource": "bundled scalar lookup PNGs", "originalExpfExecuted": True, "images": []}
    for name, entry, ((width, height), pixels, wraps, exp_calls) in results:
        path = target / f"{name}.png"
        Image.frombytes("L", (width, height), pixels).save(path, compress_level=9)
        with Image.open(path) as check:
            if check.mode != "L" or check.tobytes() != pixels:
                raise ValueError("Lookup PNG round trip differs")
        manifest["images"].append({"path": path.relative_to(args.pack).as_posix(),
            "entry": f"0x{entry:08x}", "width": width, "height": height,
            "originalExpfCalls": exp_calls,
            "encoding": "scalar8", "pixelSha256": sha(pixels), "pngSha256": sha(path.read_bytes()),
            "originalWrap": dict(wraps), "exponentialRange": 5 if name == "exp-minus" else None})
        print(name, width, height, sha(pixels), flush=True)
    (target / "lookups.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
