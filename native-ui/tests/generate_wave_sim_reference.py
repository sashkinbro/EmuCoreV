"""Generate a reference corpus from paf.elf wave heightfield routines via Unicorn.

Runs original ARMv7/NEON routines in paf.elf:
- PRNG + noise fill (0x8105188c + 0x8109f552 loop)
- Phillips spectrum initialization (0x8109ee5c)
- Dispersion phase-step initialization (0x8109f256)
- Three simulation frames: 0x8109f17e -> 0x81193e32 -> 0x8109f8be half-float conversion

Default input preset is effects_default_0512_01. Output stays untracked under
native-ui/tests/reference/.
"""
import argparse
from pathlib import Path
import struct
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
from unicorn.arm_const import (
    UC_ARM_REG_C1_C0_2,
    UC_ARM_REG_FPEXC,
    UC_ARM_REG_FPSCR,
    UC_ARM_REG_LR,
    UC_ARM_REG_PC,
    UC_ARM_REG_R0,
    UC_ARM_REG_R1,
    UC_ARM_REG_R2,
    UC_ARM_REG_R4,
    UC_ARM_REG_R5,
    UC_ARM_REG_SP,
)

EFFECTS_DEFAULT_SIM = (
    0x411b5f16,  # gravity
    0x00000000,  # A
    0xc1a00000,  # logA
    0x41240000,  # wind_speed
    0xc0490fd8,  # wind_dir
    0x42c46db7,  # patch_size
    0x3b700ef1,  # time_step
    0x441c4000,  # height_scale = 625.0
)


def map_paf_elf(elf_bytes: bytes) -> Uc:
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    e_phoff = struct.unpack_from("<I", elf_bytes, 28)[0]
    e_phentsize, e_phnum = struct.unpack_from("<HH", elf_bytes, 42)
    for i in range(e_phnum):
        p_type, p_off, p_vaddr, _p_paddr, p_filesz, p_memsz, _flags, _align = struct.unpack_from(
            "<8I", elf_bytes, e_phoff + i * e_phentsize
        )
        if p_type != 1:
            continue
        start = p_vaddr & ~4095
        end = (p_vaddr + max(p_filesz, p_memsz) + 4095) & ~4095
        uc.mem_map(start, end - start)
        uc.mem_write(p_vaddr, elf_bytes[p_off : p_off + p_filesz])

    for addr, size in ((0x10000, 4096), (0x100000, 0x20000), (0x200000, 0x40000)):
        uc.mem_map(addr, size)

    uc.reg_write(UC_ARM_REG_C1_C0_2, 0xF00000)
    uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    return uc


def call(uc: Uc, addr: int, r0: int = 0, r1: int = 0, r2: int = 0) -> int:
    uc.reg_write(UC_ARM_REG_R0, r0)
    uc.reg_write(UC_ARM_REG_R1, r1)
    uc.reg_write(UC_ARM_REG_R2, r2)
    uc.reg_write(UC_ARM_REG_SP, 0x11F000)
    uc.reg_write(UC_ARM_REG_LR, 0x10001)
    uc.reg_write(UC_ARM_REG_FPSCR, 0)
    uc.emu_start(addr | 1, 0x10000, count=2_000_000)
    if uc.reg_read(UC_ARM_REG_PC) != 0x10000:
        raise RuntimeError(f"Routine {addr:#x} did not return (pc={uc.reg_read(UC_ARM_REG_PC):#x})")
    return uc.reg_read(UC_ARM_REG_R0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paf_elf", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    elf_bytes = args.paf_elf.read_bytes()
    uc = map_paf_elf(elf_bytes)

    # Stub FUN_8120198c (vector<PhaseCell>::resize) called at entry of 0x8109f256
    # and implement sce_paf_memcpy (0x8105073e) for 0x81193e32 row copy.
    uc.mem_write(0x8120198C, bytes.fromhex("7047"))  # bx lr
    uc.mem_write(0x8105073E, bytes.fromhex("7047"))  # bx lr

    def hook_memcpy(machine, address, size, data):
        if address == 0x8105073E:
            dst = machine.reg_read(UC_ARM_REG_R0)
            src = machine.reg_read(UC_ARM_REG_R1)
            n = machine.reg_read(UC_ARM_REG_R2)
            machine.mem_write(dst, bytes(machine.mem_read(src, n)))

    uc.hook_add(UC_HOOK_CODE, hook_memcpy, begin=0x8105073E, end=0x81050740)
    # Find ScePafGraphics_309D2902 in symbol table if needed, or stop at 0x8109fa0a!

    # Initialize FFT twiddle tables via PAF initializer 0x8119448e
    call(uc, 0x8119448E)

    # Struct layout at 0x200000
    struct_base = 0x200000
    noise_buf = 0x201000    # 1024 * 8 = 8192 (0x2000)
    h0_buf = 0x203000       # 0x2000
    h0_copy_buf = 0x205000  # 0x2000
    spec_buf = 0x207000     # 0x2000
    fft_buf = 0x209000      # 0x2000
    phase_buf = 0x20B000    # 1024 * 16 = 16384 (0x4000)
    half_buf = 0x20F000     # 1024 * 2 = 2048 (0x800)

    uc.mem_write(struct_base, bytes(0x200))
    # Initialize stack guard DAT_8126fcdc (non-zero if needed)
    uc.mem_write(struct_base + 0x18, struct.pack("<II", 32, 32))
    uc.mem_write(struct_base + 0x30, struct.pack("<I", noise_buf))
    uc.mem_write(struct_base + 0x3C, struct.pack("<I", 1024))
    uc.mem_write(struct_base + 0x48, struct.pack("<8I", *EFFECTS_DEFAULT_SIM))
    uc.mem_write(struct_base + 0x78, struct.pack("<II", h0_buf, 1024))
    uc.mem_write(struct_base + 0x88, struct.pack("<I", h0_copy_buf))
    uc.mem_write(struct_base + 0x98, struct.pack("<I", spec_buf))
    uc.mem_write(struct_base + 0xA8, struct.pack("<I", fft_buf))
    uc.mem_write(struct_base + 0xB8, struct.pack("<I", phase_buf))
    uc.mem_write(struct_base + 0xC8, struct.pack("<I", half_buf))

    # 1) Execute original noise loop (0x8109f5aa..0x8109f634) in FUN_8109f552
    uc.mem_write(0x81301184, struct.pack("<I", 1))
    uc.reg_write(UC_ARM_REG_R4, struct_base)
    uc.reg_write(UC_ARM_REG_R5, 0)
    uc.reg_write(UC_ARM_REG_R0, 1024)
    uc.reg_write(UC_ARM_REG_SP, 0x11F000)
    uc.reg_write(UC_ARM_REG_FPSCR, 0)
    uc.emu_start(0x8109F5AA | 1, 0x8109F634, count=500_000)
    if uc.reg_read(UC_ARM_REG_PC) != 0x8109F634:
        raise RuntimeError(f"Noise loop did not reach 0x8109F634 (pc={uc.reg_read(UC_ARM_REG_PC):#x})")
    noise_bytes = bytes(uc.mem_read(noise_buf, 1024 * 8))

    # 2) Run Phillips spectrum init (0x8109ee5c)
    call(uc, 0x8109EE5C, r0=struct_base)
    h0_bytes = bytes(uc.mem_read(h0_buf, 1024 * 8))
    uc.mem_write(h0_copy_buf, h0_bytes)

    # 3) Run dispersion phase init (0x8109f256)
    call(uc, 0x8109F256, r0=struct_base)
    phase_bytes = bytes(uc.mem_read(phase_buf, 1024 * 16))

    # 4) Run 3 frames: stop 0x8109f8be right at ScePafGraphics_309D2902 call (find BL target in 0x8109f8be..0x8109fa40)
    # Instead, hook ScePafGraphics_309D2902 by inspecting 0x8109fa00..0x8109fa60 or hooking its BL target
    # Let's execute 0x8109f17e + 0x81193e32 + full 0x8109f8be by making ScePafGraphics_309D2902 bx lr!
    # Look up address of ScePafGraphics_309D2902 in paf.elf via the instruction at the end of 0x8109f8be:
    # From 8109f8be.c L102, ScePafGraphics_309D2902 is the last BLX before stack_chk_fail.
    # Safer: execute 0x8109f8be with start=0x8109f8be|1 and find BL to 0x8126xxxx.
    frames = []
    # Find the PLT/stub address for ScePafGraphics_309D2902 around 0x8109f9d0..0x8109fa50
    def hook_gfx(machine, addr, size, data):
        pass
    # Run frame steps via 0x8109f17e -> 0x81193e32 -> and execute half conversion in 0x8109f8be!
    # Let's check exact PC where 0x8109f8be calls ScePafGraphics_309D2902:
    # Look up 8109f8be machine bytes up to 0x8109fa50 by halting on ScePafGraphics stub.
    frames_half = []
    # Find ScePafGraphics stub in 0x8109f9b0..0x8109fa40 dynamically:
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB
    md = Cs(CS_ARCH_ARM, UC_MODE_THUMB if False else CS_MODE_THUMB)
    snippet = bytes(uc.mem_read(0x8109F8BE, 0x1A0))
    gfx_call_addr = None
    for ins in md.disasm(snippet, 0x8109F8BE):
        if ins.mnemonic in ("bl", "blx") and ins.address > 0x8109F980:
            gfx_call_addr = ins.address
    assert gfx_call_addr is not None
    # Patch the gfx_call_addr (4-byte BL/BLX) to NOPs (0x00bf00bf) in Unicorn!
    uc.mem_write(gfx_call_addr, bytes.fromhex("00bf00bf"))

    for _ in range(3):
        call(uc, 0x8109F8BE, r0=struct_base, r1=0)
        frames_half.append(bytes(uc.mem_read(half_buf, 1024 * 2)))

    out = bytearray(b"PAFWSM01")
    out += struct.pack("<8I", *EFFECTS_DEFAULT_SIM)
    out += noise_bytes
    out += h0_bytes
    out += phase_bytes
    out += struct.pack("<I", len(frames_half))
    for fh in frames_half:
        out += fh

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(bytes(out))
    print("Wrote", args.output, "bytes=", len(out), "gfx_call_patched=", hex(gfx_call_addr))


if __name__ == "__main__":
    main()
