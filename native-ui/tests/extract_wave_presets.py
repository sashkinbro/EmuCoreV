#!/usr/bin/env python3
"""Extract all PS Vita firmware wave themes from paf.elf FUN_810a8286 via Unicorn."""

import json
import pathlib
import struct
from unicorn import Uc, UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_THUMB
from unicorn.arm_const import (
    UC_ARM_REG_C1_C0_2,
    UC_ARM_REG_FPEXC,
    UC_ARM_REG_LR,
    UC_ARM_REG_PC,
    UC_ARM_REG_R0,
    UC_ARM_REG_R1,
    UC_ARM_REG_S0,
    UC_ARM_REG_S1,
    UC_ARM_REG_S2,
    UC_ARM_REG_S3,
    UC_ARM_REG_SP,
)

ROOT = pathlib.Path(__file__).resolve().parents[2]
PAF_ELF = ROOT / "artifacts" / "paf-ghidra" / "paf.elf"
OUT_JSON = ROOT / "artifacts" / "wave-presets-all.json"


def extract_all():
    elf = PAF_ELF.read_bytes()
    e_phoff = struct.unpack_from("<I", elf, 28)[0]
    e_phnum = struct.unpack_from("<H", elf, 44)[0]

    mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    mu.reg_write(UC_ARM_REG_C1_C0_2, 0xF << 20)
    mu.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    for i in range(e_phnum):
        p_type, p_off, p_vaddr, _, p_filesz, p_memsz, _, _ = struct.unpack_from(
            "<8I", elf, e_phoff + i * 32
        )
        if p_type == 1:
            ps = p_vaddr & ~0xFFF
            pe = (p_vaddr + max(p_memsz, p_filesz) + 0xFFF) & ~0xFFF
            mu.mem_map(ps, pe - ps)
            mu.mem_write(p_vaddr, elf[p_off : p_off + p_filesz])

    mu.mem_map(0x20000000, 0x00400000)
    mu.mem_map(0x8FFF0000, 0x00020000)
    mu.mem_write(0x8126FCDC, struct.pack("<I", 0x203FF000))
    mu.mem_write(0x203FF000, struct.pack("<I", 0xCAFEBABE))

    def read_cstr(uc, addr):
        if addr < 0x1000:
            return f"<ptr:{addr:#x}>"
        b = bytes(uc.mem_read(addr, 128))
        return b.split(b"\x00", 1)[0].decode("ascii", errors="replace")

    def ret(uc, r0=None):
        if r0 is not None:
            uc.reg_write(UC_ARM_REG_R0, r0)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    builders = {}
    nodes = {}
    last_str = {}
    maps = {}  # map_addr -> (cat_name, {preset_name: {key: [u32...]}})
    theme_order = []
    alloc_ptr = 0x20100000

    def make_setter(n):
        sregs = [UC_ARM_REG_S0, UC_ARM_REG_S1, UC_ARM_REG_S2, UC_ARM_REG_S3][:n]

        def hook(uc, addr, sz, ud):
            obj = uc.reg_read(UC_ARM_REG_R0)
            key = read_cstr(uc, uc.reg_read(UC_ARM_REG_R1))
            bits = [uc.reg_read(r) for r in sregs]
            builders.setdefault(obj, {})[key] = bits
            ret(uc, 0)

        return hook

    for a, n in [(0x81099550, 1), (0x810995CC, 2), (0x81099652, 3), (0x810996E0, 4)]:
        mu.hook_add(UC_HOOK_CODE, make_setter(n), begin=a, end=a + 2)

    def hook_str_ctor(uc, addr, sz, ud):
        dst = uc.reg_read(UC_ARM_REG_R0)
        last_str[dst] = read_cstr(uc, uc.reg_read(UC_ARM_REG_R1))
        ret(uc, dst)

    def hook_str_dtor(uc, addr, sz, ud):
        ret(uc, uc.reg_read(UC_ARM_REG_R0))

    mu.hook_add(UC_HOOK_CODE, hook_str_ctor, begin=0x811EFAC0, end=0x811EFAC2)
    mu.hook_add(UC_HOOK_CODE, hook_str_dtor, begin=0x811DC254, end=0x811DC256)

    def hook_map_insert(cat_name):
        def hook(uc, addr, sz, ud):
            nonlocal alloc_ptr
            m = uc.reg_read(UC_ARM_REG_R0)
            name = last_str.get(uc.reg_read(UC_ARM_REG_R1), "?")
            node = alloc_ptr
            alloc_ptr += 0x100
            nodes[node + 0x40] = (cat_name, m, name)
            ret(uc, node)

        return hook

    def hook_copy_assign(cat_name):
        def hook(uc, addr, sz, ud):
            dst = uc.reg_read(UC_ARM_REG_R0)
            src = uc.reg_read(UC_ARM_REG_R1)
            cat, m, name = nodes.get(dst, (cat_name, 0, "?"))
            builder_addr = src - 0x40
            snap = {k: list(v) for k, v in builders.get(builder_addr, {}).items()}
            maps.setdefault((m, cat), {})[name] = snap
            ret(uc, dst)

        return hook

    def hook_vec_push(uc, addr, sz, ud):
        vec = uc.reg_read(UC_ARM_REG_R0)
        name = last_str.get(uc.reg_read(UC_ARM_REG_R1), "?")
        if vec == 0x20010D60:
            theme_order.append(name)
        ret(uc, 0)

    def hook_noop(uc, addr, sz, ud):
        ret(uc, 0)

    for a, nm in [
        (0x811FB560, "sim"),
        (0x811FA850, "fog"),
        (0x811FA2A8, "light"),
        (0x811FB030, "sky"),
        (0x811FC110, "wave"),
        (0x811FC7CC, "camera"),
        (0x811FCAF4, "master"),
        (0x811F9C3C, "material"),
    ]:
        mu.hook_add(UC_HOOK_CODE, hook_map_insert(nm), begin=a, end=a + 2)

    for a, nm in [
        (0x81203FDC, "sim"),
        (0x8120409C, "fog"),
        (0x81201618, "material"),
        (0x81204190, "light"),
        (0x811FA908, "sky"),
        (0x812042AC, "wave"),
        (0x81204510, "camera"),
        (0x81204628, "master"),
    ]:
        mu.hook_add(UC_HOOK_CODE, hook_copy_assign(nm), begin=a, end=a + 2)

    mu.hook_add(UC_HOOK_CODE, hook_vec_push, begin=0x81099B1C, end=0x81099B1E)
    mu.hook_add(UC_HOOK_CODE, hook_noop, begin=0x81099BC2, end=0x81099BC4)
    mu.hook_add(UC_HOOK_CODE, hook_noop, begin=0x8109A0B2, end=0x8109A0B4)

    mu.reg_write(UC_ARM_REG_SP, 0x20080000)
    mu.reg_write(UC_ARM_REG_LR, 0x90000001)
    mu.reg_write(UC_ARM_REG_R0, 0x20010000)
    mu.reg_write(UC_ARM_REG_R1, 0x20020000)
    mu.emu_start(0x810A8286 | 1, 0x90000000)

    # Identify map offsets relative to 0x20010000
    categorized = {}
    for (m, cat), entries in sorted(maps.items()):
        off = m - 0x20010000
        label = {
            0x0DA0: "material",
            0x0F90: "light",
            0x1190: "fog0",
            0x1350: "fog1",
            0x1510: "sky",
            0x1840: "sim",
            0x19B8: "wave",
            0x1D28: "camera",
            0x1F28: "master",
        }.get(off, f"{cat}_{off:#x}")
        categorized[label] = {
            name: {k: [f"0x{b:08x}" for b in vals] for k, vals in kv.items()}
            for name, kv in entries.items()
        }

    # Verify effects_default_0512_01 sky against extraction JSON
    ref = json.loads(
        (ROOT / "artifacts" / "paf-ghidra" / "paf-wave-refs" / "effects_default_0512_01-extraction.json").read_text()
    )
    sky_def = categorized["sky"]["effects_default_0512_01"]
    for item in ref["values"]:
        assert sky_def[item["key"]] == item["float_bits"], f"Mismatch {item['key']}"

    out = {"theme_order": theme_order, "categories": categorized}
    OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    OUT_JSON.write_text(json.dumps(out, indent=2), encoding="utf-8")
    print(f"Extracted {len(theme_order)} themes across {len(categorized)} categories -> {OUT_JSON}")
    for label, entries in categorized.items():
        counts = sorted(set(len(v) for v in entries.values()))
        print(f"  {label:10s}: {len(entries):2d} entries, key counts={counts}")


if __name__ == "__main__":
    extract_all()
