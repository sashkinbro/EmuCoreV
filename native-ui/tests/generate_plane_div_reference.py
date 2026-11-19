#!/usr/bin/env python3
"""Generate bit-exact PlaneObj 9-slice / div reference corpus from Vita paf.elf via Unicorn."""

import hashlib
import math
import pathlib
import struct
import sys

from unicorn import Uc, UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_THUMB
from unicorn.arm_const import (
    UC_ARM_REG_C1_C0_2,
    UC_ARM_REG_FPEXC,
    UC_ARM_REG_LR,
    UC_ARM_REG_PC,
    UC_ARM_REG_R0,
    UC_ARM_REG_S0,
    UC_ARM_REG_SP,
)

ROOT = pathlib.Path(__file__).resolve().parents[2]
PAF_ELF = ROOT / "artifacts" / "paf-ghidra" / "paf.elf"
OUT_CORPUS = ROOT / "artifacts" / "plane-div-corpus.bin"

# Ground-truth paf.elf entry points & imports
ADDR_UPDATE_LAYOUT = 0x810848E8
ADDR_UPDATE_DIV_POS = 0x81084B1A
ADDR_UPDATE_DIV_TEX = 0x810852C4
ADDR_SCE_PAF_COSF = 0x8104FE34
ADDR_STACK_CANARY_PTR = 0x8126FCDC
ADDR_DAT_8129AAAC = 0x8129AAAC
STACK_SENTINEL = 0x90000000


def load_paf_elf():
    data = PAF_ELF.read_bytes()
    sha256 = hashlib.sha256(data).hexdigest()
    if data[:4] != b"\x7fELF":
        raise RuntimeError(f"Expected ELF at {PAF_ELF}")
    e_phoff = struct.unpack_from("<I", data, 28)[0]
    e_phnum = struct.unpack_from("<H", data, 44)[0]
    segments = []
    for i in range(e_phnum):
        p_type, p_off, p_vaddr, _, p_filesz, p_memsz, _, _ = struct.unpack_from(
            "<8I", data, e_phoff + i * 32
        )
        if p_type == 1:
            segments.append((p_vaddr, p_memsz, data[p_off : p_off + p_filesz]))
    return sha256, segments


def setup_emulator(segments):
    mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    # Enable VFP/NEON (CP10/CP11 full access in CPACR, FPEXC.EN = 1)
    mu.reg_write(UC_ARM_REG_C1_C0_2, 0xF << 20)
    mu.reg_write(UC_ARM_REG_FPEXC, 0x40000000)

    for vaddr, memsz, payload in segments:
        page_start = vaddr & ~0xFFF
        page_end = (vaddr + max(memsz, len(payload)) + 0xFFF) & ~0xFFF
        mu.mem_map(page_start, page_end - page_start)
        mu.mem_write(vaddr, payload)

    # Map fixed scratch, stack, and stack sentinel
    mu.mem_map(0x20000000, 0x00100000)
    mu.mem_map(0x8FFF0000, 0x00020000)

    # Set a deterministic stack canary value at *DAT_8126fcdc
    canary_target = 0x200FF000
    mu.mem_write(ADDR_STACK_CANARY_PTR, struct.pack("<I", canary_target))
    mu.mem_write(canary_target, struct.pack("<I", 0xCAFEBABE))

    def hook_cosf(uc, address, size, user_data):
        s0_bits = uc.reg_read(UC_ARM_REG_S0)
        val = struct.unpack("<f", struct.pack("<I", s0_bits))[0]
        res = math.cos(val)
        uc.reg_write(UC_ARM_REG_S0, struct.unpack("<I", struct.pack("<f", res))[0])
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    mu.hook_add(UC_HOOK_CODE, hook_cosf, begin=ADDR_SCE_PAF_COSF, end=ADDR_SCE_PAF_COSF + 2)
    return mu


def vertex_count_for_mode(mu, div_mode):
    if div_mode == 0:
        return 0
    return struct.unpack("<I", mu.mem_read(ADDR_DAT_8129AAAC + (div_mode - 1) * 12, 4))[0]


def make_plane_case(
    mu,
    *,
    pos_x,
    pos_y,
    pos_z,
    size_x,
    size_y,
    has_rot=False,
    rot_y=0.0,
    align_x=0,
    align_y=0,
    tex_width=64,
    tex_height=64,
    div_mode=3,
    use_presets=False,
    preset_h=0,
    preset_v=0,
    div_ratio_h=(0.0, 0.0),
    div_ratio_v=(0.0, 0.0),
    div_ratio_stretch=(0.0, 0.0),
    margin_left=0,
    margin_right=0,
    margin_top=0,
    margin_bottom=0,
    div_border_scale=1.0,
    div_stretch_offset=0.0,
):
    obj = 0x20001000
    rot_ptr = 0x20001200
    surf_ptr = 0x20001300
    surf_custom = 0x20001400
    vb_wrap = 0x20001500
    buf_holder = 0x20001540
    buf_ptr = 0x20001580
    verts_ptr = 0x20002000

    mu.mem_write(0x20001000, b"\x00" * 0x2000)

    # PlaneObj fields matching PlaneObjC1 (0x81081964) + setters
    mu.mem_write(obj + 0x08, struct.pack("<4f", pos_x, pos_y, pos_z, 0.0))
    mu.mem_write(obj + 0x18, struct.pack("<4f", size_x, size_y, 0.0, 0.0))
    if has_rot:
        mu.mem_write(rot_ptr, struct.pack("<4f", 0.0, rot_y, 0.0, 0.0))
        mu.mem_write(obj + 0x28, struct.pack("<I", rot_ptr))
    mu.mem_write(obj + 0x54, struct.pack("<2i", align_x, align_y))
    mu.mem_write(obj + 0x5C, struct.pack("<2i", 0, 0))  # scale_mode = 0
    mu.mem_write(obj + 0x88, struct.pack("<4f", 1.0, 1.0, 1.0, 1.0))  # scale_value = 1
    mu.mem_write(obj + 0xB8, struct.pack("<I", 0))  # use Surface div fields
    mu.mem_write(obj + 0xBC, struct.pack("<f", div_border_scale))
    mu.mem_write(obj + 0xC0, struct.pack("<f", div_stretch_offset))
    mu.mem_write(obj + 0xC4, struct.pack("<I", surf_ptr))
    mu.mem_write(obj + 0xD4, struct.pack("<I", vb_wrap))

    # Vertex buffer pointer chain used by UpdateDivVerticesPosition & UpdateDivVerticesTexcoord
    mu.mem_write(vb_wrap + 0x08, struct.pack("<I", buf_holder))
    mu.mem_write(buf_holder, struct.pack("<I", buf_ptr))
    mu.mem_write(buf_ptr, struct.pack("<I", verts_ptr))

    # Surface fields populated by RCO parser (0x81057856) + ScePafGraphics_36960B24 (0x8108cf7a)
    mu.mem_write(surf_ptr + 0x1C, struct.pack("<2h", tex_width, tex_height))
    mu.mem_write(surf_ptr + 0x84, struct.pack("<H", div_mode))
    if use_presets:
        ph, pv = preset_h, preset_v
        has_custom = (ph == 4) or (pv == 4) or any(v != 0.0 for v in div_ratio_stretch)
    else:
        ph, pv = 4, 4
        has_custom = True
    mu.mem_write(surf_ptr + 0x86, struct.pack("<2B", ph, pv))
    if has_custom:
        mu.mem_write(surf_ptr + 0x88, struct.pack("<I", surf_custom))
        mu.mem_write(
            surf_custom,
            struct.pack("<6f", *div_ratio_h, *div_ratio_v, *div_ratio_stretch),
        )
    # Note margin_v order swapped into +0x8e (bottom) and +0x92 (top) per 0x81057856
    mu.mem_write(
        surf_ptr + 0x8C,
        struct.pack("<4H", margin_left, margin_bottom, margin_right, margin_top),
    )

    def call(addr):
        mu.reg_write(UC_ARM_REG_SP, 0x20080000)
        mu.reg_write(UC_ARM_REG_LR, STACK_SENTINEL | 1)
        mu.reg_write(UC_ARM_REG_R0, obj)
        mu.emu_start(addr | 1, STACK_SENTINEL)

    call(ADDR_UPDATE_LAYOUT)
    call(ADDR_UPDATE_DIV_POS)
    call(ADDR_UPDATE_DIV_TEX)

    layout_size = struct.unpack("<4f", mu.mem_read(obj + 0x68, 16))
    layout_pos = struct.unpack("<4f", mu.mem_read(obj + 0x78, 16))
    vcount = vertex_count_for_mode(mu, div_mode)
    verts = []
    for vi in range(vcount):
        vx, vy, _vz, vu, vv = struct.unpack("<5f", mu.mem_read(verts_ptr + vi * 20, 20))
        verts.append((vx, vy, vu, vv))

    # Return inputs normalized for C++ PlaneDivSpec + outputs
    return {
        "pos_x": pos_x,
        "pos_y": pos_y,
        "pos_z": pos_z,
        "size_x": size_x,
        "size_y": size_y,
        "has_rot": int(has_rot),
        "rot_y": rot_y,
        "align_x": align_x,
        "align_y": align_y,
        "tex_width": tex_width,
        "tex_height": tex_height,
        "div_mode": div_mode,
        "use_presets": int(use_presets),
        "preset_h": ph,
        "preset_v": pv,
        "div_ratio_h": div_ratio_h,
        "div_ratio_v": div_ratio_v,
        "div_ratio_stretch": div_ratio_stretch,
        "margin_left": margin_left,
        "margin_right": margin_right,
        "margin_top": margin_top,
        "margin_bottom": margin_bottom,
        "div_border_scale": div_border_scale,
        "div_stretch_offset": div_stretch_offset,
        "out_center": (layout_pos[0], layout_pos[1], layout_pos[2]),
        "out_size": (layout_size[0], layout_size[1]),
        "out_verts": verts,
    }


def main():
    sha256, segments = load_paf_elf()
    print(f"paf.elf sha256={sha256}")
    mu = setup_emulator(segments)

    cases = [
        # Exact LiveArea gate ca6a6db1 from livearea_plugin.rco (template 0x1467f749)
        dict(
            pos_x=480.0, pos_y=-228.0, pos_z=0.0, size_x=292.0, size_y=170.0,
            tex_width=74, tex_height=82, div_mode=3,
            div_ratio_h=(0.487800002, 0.487800002),
            div_ratio_v=(0.487800002, 0.487800002),
            margin_left=5, margin_right=5, margin_top=9, margin_bottom=9,
        ),
        # Preset 1 (Third), Preset 2 (FortyNine), Preset 3 (Half), Asymmetric margins & align
        dict(
            pos_x=-100.0, pos_y=50.0, pos_z=1.0, size_x=400.0, size_y=120.0,
            tex_width=64, tex_height=64, div_mode=3, use_presets=True,
            preset_h=1, preset_v=3, margin_left=4, margin_right=12, margin_top=3, margin_bottom=11,
            align_x=1, align_y=2,
        ),
        dict(
            pos_x=0.0, pos_y=0.0, pos_z=0.0, size_x=200.0, size_y=90.0,
            tex_width=64, tex_height=64, div_mode=3, use_presets=True,
            preset_h=2, preset_v=0, div_border_scale=0.5, has_rot=True, rot_y=0.35,
        ),
        # Overflow clamping (plane smaller than border pixels)
        dict(
            pos_x=10.0, pos_y=-20.0, pos_z=0.0, size_x=40.0, size_y=36.0,
            tex_width=74, tex_height=82, div_mode=3,
            div_ratio_h=(0.487800002, 0.487800002),
            div_ratio_v=(0.487800002, 0.487800002),
            margin_left=5, margin_right=5, margin_top=9, margin_bottom=9,
        ),
        # Tiny <= 1.0f collapse guard
        dict(
            pos_x=0.0, pos_y=0.0, pos_z=0.0, size_x=1.0, size_y=0.5,
            tex_width=64, tex_height=64, div_mode=3,
            div_ratio_h=(0.25, 0.25), div_ratio_v=(0.25, 0.25),
        ),
        # 1x3 (mode 1) with margins
        dict(
            pos_x=0.0, pos_y=0.0, pos_z=0.0, size_x=100.0, size_y=240.0,
            tex_width=32, tex_height=64, div_mode=1,
            div_ratio_v=(0.25, 0.35),
            margin_left=4, margin_right=8, margin_top=6, margin_bottom=10,
        ),
        # 3x1 (mode 2) with margins
        dict(
            pos_x=12.0, pos_y=34.0, pos_z=0.0, size_x=300.0, size_y=48.0,
            tex_width=64, tex_height=32, div_mode=2,
            div_ratio_h=(0.2, 0.4),
            margin_left=7, margin_right=3, margin_top=2, margin_bottom=6,
        ),
        # 3x5 (mode 4) with stretch ratios & offset
        dict(
            pos_x=0.0, pos_y=0.0, pos_z=0.0, size_x=200.0, size_y=360.0,
            tex_width=64, tex_height=128, div_mode=4,
            div_ratio_h=(0.25, 0.25), div_ratio_v=(0.15, 0.2),
            div_ratio_stretch=(0.2, 0.15), div_stretch_offset=24.0,
            margin_left=4, margin_right=4, margin_top=8, margin_bottom=8,
        ),
        # 5x3 (mode 5) with stretch ratios & negative offset
        dict(
            pos_x=0.0, pos_y=0.0, pos_z=0.0, size_x=420.0, size_y=120.0,
            tex_width=128, tex_height=64, div_mode=5,
            div_ratio_h=(0.15, 0.2), div_ratio_v=(0.25, 0.25),
            div_ratio_stretch=(0.2, 0.15), div_stretch_offset=-45.0,
            margin_left=5, margin_right=5, margin_top=6, margin_bottom=6,
        ),
        # 5x3 (mode 5) tight squeeze where stretch sum exceeds available center
        dict(
            pos_x=0.0, pos_y=0.0, pos_z=0.0, size_x=70.0, size_y=50.0,
            tex_width=128, tex_height=64, div_mode=5,
            div_ratio_h=(0.2, 0.2), div_ratio_v=(0.3, 0.3),
            div_ratio_stretch=(0.2, 0.2),
        ),
        # Modes 6, 7, 8, 9
        dict(
            pos_x=0.0, pos_y=0.0, pos_z=0.0, size_x=220.0, size_y=160.0,
            tex_width=64, tex_height=64, div_mode=6,
            div_ratio_h=(0.3, 0.3), div_ratio_v=(0.25, 0.25),
        ),
        dict(
            pos_x=0.0, pos_y=0.0, pos_z=0.0, size_x=220.0, size_y=160.0,
            tex_width=64, tex_height=64, div_mode=7,
            div_ratio_h=(0.3, 0.3), div_ratio_v=(0.25, 0.25),
        ),
        dict(
            pos_x=0.0, pos_y=0.0, pos_z=0.0, size_x=90.0, size_y=180.0,
            tex_width=40, tex_height=64, div_mode=8,
            div_ratio_h=(0.2, 0.2), div_ratio_v=(0.25, 0.3),
            margin_left=3, margin_right=5, margin_top=4, margin_bottom=8,
        ),
        dict(
            pos_x=0.0, pos_y=0.0, pos_z=0.0, size_x=240.0, size_y=60.0,
            tex_width=64, tex_height=40, div_mode=9,
            div_ratio_h=(0.25, 0.3), div_ratio_v=(0.2, 0.2),
            margin_left=3, margin_right=5, margin_top=4, margin_bottom=8,
        ),
        # Mode 0 (non-divided with margins, scale_mode=0)
        dict(
            pos_x=5.0, pos_y=10.0, pos_z=0.0, size_x=100.0, size_y=80.0,
            tex_width=64, tex_height=64, div_mode=0,
            margin_left=4, margin_right=8, margin_top=2, margin_bottom=10,
            align_x=2, align_y=1,
        ),
    ]

    out = bytearray(b"PAFPDV01")
    out += struct.pack("<I", len(cases))
    for cfg in cases:
        rec = make_plane_case(mu, **cfg)
        out += struct.pack(
            "<5fIf2i2hI3B",
            rec["pos_x"],
            rec["pos_y"],
            rec["pos_z"],
            rec["size_x"],
            rec["size_y"],
            rec["has_rot"],
            rec["rot_y"],
            rec["align_x"],
            rec["align_y"],
            rec["tex_width"],
            rec["tex_height"],
            rec["div_mode"],
            rec["use_presets"],
            rec["preset_h"],
            rec["preset_v"],
        )
        out += struct.pack(
            "<6f4H2f",
            *rec["div_ratio_h"],
            *rec["div_ratio_v"],
            *rec["div_ratio_stretch"],
            rec["margin_left"],
            rec["margin_right"],
            rec["margin_top"],
            rec["margin_bottom"],
            rec["div_border_scale"],
            rec["div_stretch_offset"],
        )
        out += struct.pack("<5fI", *rec["out_center"], *rec["out_size"], len(rec["out_verts"]))
        for v in rec["out_verts"]:
            out += struct.pack("<4f", *v)

    OUT_CORPUS.parent.mkdir(parents=True, exist_ok=True)
    OUT_CORPUS.write_bytes(out)
    print(f"Wrote {len(cases)} bit-exact plane_div cases ({len(out)} bytes) -> {OUT_CORPUS}")


if __name__ == "__main__":
    main()
