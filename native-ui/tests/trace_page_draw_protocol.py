"""Record the original PageTurn renderer's GPU draw protocol offline.

Runs a reviewed libpaf renderer entry in Unicorn with hosted graphics calls and
writes the ordered protocol as JSON: surface activation and texture binds,
per-pass material/uniform setter blocks, program binds, render-state changes and
draw submissions. The reference, corpora and generated traces stay outside the
source tree; no original instructions or host VM enter the native UI.

    python native-ui/tests/trace_page_draw_protocol.py OWNED_DECRYPTED_LIBPAF OUTPUT_TRACE.json [--fold]

Without --fold the controlled page is flat (the renderer degenerates the fold and
draws its two flat surfaces). With --fold a non-zero fold point selects the
rounded-edge material set and three surfaces. The input fixture is a development
state, not a claim about the shell's lock-screen configuration. Graphics and
shader submission are hosted, so this proves the recorded arithmetic/ordering
only; it does not render a frame.
"""
import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path

from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
from unicorn.arm_const import (
    UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_R0, UC_ARM_REG_R1,
    UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC,
    UC_ARM_REG_FPSCR,
)

REFERENCE_SHA256 = "5a74caf8a0e7b67624c23ea95a67f6b2a79adfb8919352310330c2b10ce10181"
# Reviewed renderer entry: ScePafGraphics_9F1421A6 dispatches this PageTurn draw.
ENTRY = 0x810ca2f8
GRAPHICS_STUBS = (0x81053778, 0x810760c8, 0x8107668c, 0x81076744, 0x810768c0,
    0x810768d4, 0x81076936, 0x81076a50, 0x8107963a, 0x81079722,
    0x8107a16a, 0x8107a260, 0x8107d07c, 0x8108995c, 0x8108997a,
    0x8108c806, 0x8108c81c, 0x810c71e0, 0x811473f8, 0x81147568)
VTABLE_STUB_BASE = 0x100100
VTABLE_SIZE = 24
BUNDLE_OBJECT_OFFSETS = (0x14, 0x2bc, 0x39c, 0x604, 0x74c, 0xb14,
    0xc9c, 0xf44, 0xfbc, 0x1384)

# Development fixture. The earlier material corpus used the same neutral values,
# so recovered blocks stay comparable with that verified corpus.
WIDTH, HEIGHT = 960.0, 544.0
POINT = (WIDTH / HEIGHT - 0.02, 0.99)
FOLD_POINT = (-0.3, 0.2)
STYLE = (0.21137017, 0.60689974, 0.008927729, 3.1533337)
MATERIAL = (0x3f649247, 0x3f8879ee, 0x4185f15e, 0x3da491b0, 0x3f4d30ad,
    0x3e9349e0, 0x404321c9, 0x3ec49214, 0x3dea454e)


def load_reference(image, reason):
    if hashlib.sha256(image).hexdigest() != REFERENCE_SHA256:
        raise ValueError("Unreviewed PAF reference; expected the decrypted image")
    u = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    ph = struct.unpack_from("<Q", image, 72)[0]
    sec = struct.unpack_from("<Q", image, 88)[0]
    ranges = []
    for i in range(2):
        p = struct.unpack_from("<8I", image, ph + 32 * i)
        o, n, c, e = struct.unpack_from("<4Q", image, sec + 32 * i)
        if e != 2:
            raise ValueError("Reference must be decrypted")
        data = image[o:o + n]
        data = zlib.decompress(data) if c == 2 else data
        size = (max(p[5], len(data)) + 4095) & ~4095
        u.mem_map(p[2], size)
        u.mem_write(p[2], data)
        ranges.append((p[2], p[2] + size))
    return u, ranges


def trace(image, fold):
    u, module_ranges = load_reference(image, "PageTurn draw")

    def word(address, value):
        u.mem_write(address, struct.pack("<I", value))

    def floats(address, *values):
        u.mem_write(address, struct.pack("<" + "f" * len(values), *values))

    def matrix(address, x=1, y=1):
        floats(address, x, 0, 0, 0, 0, y, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)

    u.mem_map(0x10000, 4096)
    u.mem_map(0x100000, 65536)
    u.mem_map(0x200000, 0x100000)
    u.reg_write(UC_ARM_REG_C1_C0_2, 0xf00000)
    u.reg_write(UC_ARM_REG_FPEXC, 0x40000000)

    # Graphics entry points become single-instruction stubs; arguments are read
    # from the CPU registers by the hook below before the stub returns.
    for address in GRAPHICS_STUBS:
        u.mem_write(address, bytes.fromhex("7047"))
    for slot in range(VTABLE_SIZE):
        address = VTABLE_STUB_BASE + slot * 16
        u.mem_write(address, bytes.fromhex("7047"))
        word(0x260000 + slot * 4, address | 1)

    renderer, bundle, gl, data, surface, mesh = (
        0x200000, 0x204000, 0x210000, 0x230000, 0x240000, 0x250000)
    word(renderer, bundle)
    for offset in BUNDLE_OBJECT_OFFSETS:
        word(bundle + offset, 0x260000)
    for offset, stack in ((0, 0x220000), (0x20, 0x221000), (0x40, 0x222000)):
        word(gl + offset, stack)
        word(gl + offset + 0x10, 1)
        matrix(stack)
        matrix(stack + 64)
    u.mem_write(gl + 0xc0, b"\x01\x01")
    word(data, surface)
    floats(data + 8, WIDTH, HEIGHT, 0, 0)
    floats(data + 0x18, *FOLD_POINT if fold else (0, 0))
    floats(data + 0x20, 1, 1, 1, 1)
    floats(data + 0x30, 0, 0, WIDTH, HEIGHT)
    floats(data + 0x40, WIDTH * .5, HEIGHT * .5, 0, 1)
    floats(data + 0x50, 0, 0, 0, 1)
    floats(data + 0x60, *POINT)
    u.mem_write(data + 0x68, b"\x01")
    floats(surface + 0x30, 1 / WIDTH, 1 / HEIGHT)
    floats(renderer + 0x70 * 4 + 8, 1, 1, 1, 1)
    for i, bits in enumerate(MATERIAL):
        word(renderer + 0x70 * 4 + 0x24 + i * 12, bits)
    style = (STYLE[0], STYLE[1], .08, STYLE[2], 250, .3, STYLE[3], 1, 16.2, .48, .3)
    for i, value in enumerate(style):
        floats(renderer + (0x209 + i * 3) * 4, value)
    floats(renderer + 0x25a * 4, 1, 1, 1, 1)
    light = renderer + 0xee * 4
    floats(light + 8, 1.3, .5)
    floats(light + 0x1c, 2200)
    floats(light + 0x50, .2, .2, .2, 1)
    floats(light + 0x70, 1, 1, 1, 1)
    floats(light + 0x8c, 1)
    floats(renderer + 5 * 4, 5)

    # Draw submission state. Gl::+0x28 opens the batch; the mesh draw-info
    # supplies readable pointer chains and visible-fan ranges. The renderer
    # prepares every surface first and then submits them in order.
    word(gl + 0x28, 1)
    word(mesh + 4, 0x260800)
    word(mesh + 0xc, 0x260c00)
    word(0x260c00, 0x260c10)
    word(0x260c10, 0)
    word(mesh + 0x24, 0x260d00)
    word(mesh + 0x28, 1)
    word(0x260d00, 0)
    word(0x260d04, 3)
    word(mesh + 0x34, 0x260e00)
    word(0x260e00, 0)
    word(0x260e04, 3)
    word(0x260800 + 0xfc, 0)
    word(mesh + 0x38, 2 if fold else 1)

    events = []
    recent = []
    hosted_imports = {}

    def in_module(address):
        return any(low <= address < high for low, high in module_ranges)

    def read_block(address, size=64):
        try:
            return list(struct.unpack("<%dI" % (size // 4), bytes(u.mem_read(address, size))))
        except UcError:
            return None

    def hook(vm, address, _size, _):
        recent.append(address)
        if len(recent) > 24:
            recent.pop(0)
        if address == 0x810760c8:
            # ArlInstance::Gl() is hosted: return the renderer GL object.
            vm.reg_write(UC_ARM_REG_R0, gl)
            return
        if VTABLE_STUB_BASE <= address < VTABLE_STUB_BASE + VTABLE_SIZE * 16:
            obj = vm.reg_read(UC_ARM_REG_R0)
            source = vm.reg_read(UC_ARM_REG_R1)
            events.append({"kind": "setter",
                "object": hex(obj - bundle) if 0x204000 <= obj < 0x208000 else hex(obj),
                "method": hex((address - VTABLE_STUB_BASE) // 16),
                "source": hex(source), "block": read_block(source)})
            return
        if address in GRAPHICS_STUBS:
            event = {"kind": "graphics", "address": hex(address),
                "args": [hex(vm.reg_read(register)) for register in (
                    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)]}
            if address in (0x8107963a, 0x8107a16a):
                program = vm.reg_read(UC_ARM_REG_R0)
                event["program_object"] = (
                    hex(program - bundle) if 0x204000 <= program < 0x208000 else hex(program))
            events.append(event)
            return
        if address != 0x10000 and not in_module(address):
            # Unresolved imported firmware function reached by the draw path:
            # host it once and record the call site. Only the protocol ordering
            # matters here; no imported behaviour is reconstructed.
            if address not in hosted_imports:
                events.append({"kind": "import", "pc": hex(address),
                    "caller": hex(recent[-2]) if len(recent) > 1 else None})
                u.mem_write(address, bytes.fromhex("7047"))
            hosted_imports[address] = hosted_imports.get(address, 0) + 1

    u.hook_add(UC_HOOK_CODE, hook)
    u.reg_write(UC_ARM_REG_R0, renderer)
    u.reg_write(UC_ARM_REG_R1, data)
    u.reg_write(UC_ARM_REG_R2, mesh)
    u.reg_write(UC_ARM_REG_SP, 0x10f000)
    u.reg_write(UC_ARM_REG_LR, 0x10001)
    u.reg_write(UC_ARM_REG_FPSCR, 0)
    try:
        u.emu_start(ENTRY | 1, 0x10000, count=20000000)
    except UcError as error:
        raise RuntimeError("Original renderer did not return: %s at %s (recent %s)" % (
            error, hex(u.reg_read(UC_ARM_REG_PC)), [hex(x) for x in recent]))
    if u.reg_read(UC_ARM_REG_PC) != 0x10000:
        raise RuntimeError("Original renderer did not return")
    return events


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--fold", action="store_true",
        help="trace the rounded-edge material set instead of the flat surfaces")
    args = parser.parse_args()
    events = trace(args.reference.read_bytes(), args.fold)
    args.output.write_text(json.dumps(events, indent=1))
    setters = sum(1 for event in events if event["kind"] == "setter")
    graphics = [event for event in events if event["kind"] == "graphics"]
    draws = [event for event in graphics if event["address"] == "0x8107d07c"]
    binds = [event for event in graphics if "program_object" in event]
    print("Recorded %d protocol events (%d material blocks, %d program binds, %d draws)"
        % (len(events), setters, len(binds), len(draws)))


if __name__ == "__main__":
    main()
