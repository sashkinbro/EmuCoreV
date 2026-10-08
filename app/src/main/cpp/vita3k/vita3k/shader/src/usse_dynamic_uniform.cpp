// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

#include <gxm/types.h>
#include <shader/usse_decoder_helpers.h>
#include <shader/usse_program_analyzer.h>

#include <array>
#include <algorithm>
#include <bitset>
#include <cstddef>
#include <cstdint>

namespace shader::usse {

namespace {

constexpr std::size_t register_bank_count = static_cast<std::size_t>(RegisterBank::FPINTERNAL) + 1;
constexpr std::size_t register_count = 256;
using BufferReach = std::array<std::array<uint32_t, register_count>, register_bank_count>;

uint8_t usse_bits(const uint64_t instruction, const int lowest, const int count) {
    return static_cast<uint8_t>((instruction >> lowest) & ((1ull << count) - 1));
}

uint32_t get_uniform_buffer_base_sa(const SceGxmProgram &program) {
    const SceGxmProgramParameterContainer *containers = program.container();
    for (uint32_t i = 0; i < program.container_count; ++i) {
        if (containers[i].container_index == 19)
            return containers[i].base_sa_offset;
    }
    return 0;
}

} // namespace

// Identify uniform buffers whose data can be read through a register-computed
// address. USSE uses the uniform-buffer base SA as an address; a register offset
// (or an address propagated through another register) means declared size alone
// cannot cover every shader read.
uint32_t get_dynamic_uniform_buffers(const SceGxmProgram &program) {
    BufferReach reach{};
    std::array<uint32_t, register_count> buffer_mask_for_base{};
    std::bitset<register_count> other_base;
    uint32_t all_buffers = 0;

    const uint32_t base_sa = get_uniform_buffer_base_sa(program);
    const SceGxmUniformBufferInfo *buffer_infos = program.uniform_buffer();
    for (uint32_t i = 0; i < program.uniform_buffer_count; ++i) {
        const SceGxmUniformBufferInfo &info = buffer_infos[i];
        const uint64_t sa = static_cast<uint64_t>(base_sa) + info.ldst_base_offset;
        if (sa >= register_count)
            continue;

        if (info.reside_buffer < SCE_GXM_REAL_MAX_UNIFORM_BUFFER) {
            const uint32_t mask = 1u << info.reside_buffer;
            buffer_mask_for_base[sa] |= mask;
            reach[static_cast<size_t>(RegisterBank::SECATTR)][sa] |= mask;
            all_buffers |= mask;
        } else {
            // Texture, literal, and thread-buffer SAs are addresses too, but
            // they must not make an unrelated read look like a uniform access.
            other_base.set(static_cast<size_t>(sa));
        }
    }
    if (all_buffers == 0)
        return 0;

    const auto slot = [&](const Operand &operand, const int offset) -> uint32_t * {
        const int bank = static_cast<int>(operand.bank);
        const int number = static_cast<int>(operand.num) + offset;
        if (bank < 0 || static_cast<std::size_t>(bank) >= register_bank_count || number < 0 || number >= static_cast<int>(register_count))
            return nullptr;
        return &reach[static_cast<std::size_t>(bank)][static_cast<std::size_t>(number)];
    };

    uint32_t dynamic_buffers = 0;
    const auto scan = [&](const uint64_t *code, const uint64_t count, const bool secondary) {
        if (!code)
            return;

        for (uint64_t i = 0; i < count; ++i) {
            const uint64_t inst = code[i];
            const uint32_t opcode = static_cast<uint32_t>(inst >> 59);
            Operand dest;
            std::array<Operand, 3> srcs{};
            int src_count = 0;
            int repeat = 0;

            switch (opcode) {
            case 0b10000: // SOP2
            case 0b10001: // SOP3
            case 0b10010: // SOP2M
            case 0b10011: // I8MAD
            case 0b10100: // I16MAD
            case 0b10101: // I32MAD
            case 0b11010: // I32MAD2
            case 0b01010: // VBW
            case 0b01011:
            case 0b01100:
            case 0b01101:
            case 0b01110: {
                decode_dest(dest, usse_bits(inst, 21, 7), usse_bits(inst, 32, 2), usse_bits(inst, 51, 1), false, 7, secondary);
                decode_src12(srcs[src_count++], usse_bits(inst, 7, 7), usse_bits(inst, 30, 2), usse_bits(inst, 49, 1), false, 7, secondary);
                decode_src12(srcs[src_count++], usse_bits(inst, 0, 7), usse_bits(inst, 28, 2), usse_bits(inst, 48, 1), false, 7, secondary);
                if (opcode == 0b10001 || (opcode >= 0b10011 && opcode <= 0b10101) || opcode == 0b11010)
                    decode_src0(srcs[src_count++], usse_bits(inst, 14, 7), usse_bits(inst, 34, 1), usse_bits(inst, 47, 1), false, 7, secondary);
                if (opcode >= 0b01010 && opcode <= 0b01110)
                    repeat = usse_bits(inst, 44, 4);
                else if (opcode != 0b10001 && opcode != 0b10010)
                    repeat = usse_bits(inst, 44, 3);
                break;
            }
            case 0b00111: { // VMOV
                const uint8_t data_type = usse_bits(inst, 40, 3);
                const bool double_regs = data_type >= static_cast<uint8_t>(DataType::C10) && data_type <= static_cast<uint8_t>(DataType::F32);
                const uint8_t reg_bits = double_regs ? 7 : 6;
                decode_dest(dest, usse_bits(inst, 18, 6), usse_bits(inst, 32, 2), usse_bits(inst, 51, 1), double_regs, reg_bits, secondary);
                decode_src12(srcs[src_count++], usse_bits(inst, 6, 6), usse_bits(inst, 30, 2), usse_bits(inst, 49, 1), double_regs, reg_bits, secondary);
                if (usse_bits(inst, 46, 2) != 0)
                    decode_src12(srcs[src_count++], usse_bits(inst, 0, 6), usse_bits(inst, 28, 2), usse_bits(inst, 48, 1), double_regs, reg_bits, secondary);
                repeat = usse_bits(inst, 44, 2);
                break;
            }
            case 0b11101: // LDR
            case 0b11110: { // STR
                Operand base, offset, load_offset;
                decode_src0(base, usse_bits(inst, 14, 7), usse_bits(inst, 34, 1), usse_bits(inst, 50, 1), false, 7, secondary);
                decode_src12(offset, usse_bits(inst, 7, 7), usse_bits(inst, 30, 2), usse_bits(inst, 49, 1), false, 7, secondary);
                decode_src12(load_offset, usse_bits(inst, 0, 7), usse_bits(inst, 28, 2), usse_bits(inst, 48, 1), false, 7, secondary);

                const int base_number = static_cast<int>(base.num);
                if (base.bank == RegisterBank::SECATTR && base_number >= 0 && base_number < static_cast<int>(register_count)
                    && other_base.test(static_cast<size_t>(base_number)))
                    continue;

                const bool register_offset = offset.bank != RegisterBank::IMMEDIATE
                    || (opcode == 0b11101 && load_offset.bank != RegisterBank::IMMEDIATE);
                if (base.bank == RegisterBank::SECATTR && base_number >= 0 && base_number < static_cast<int>(register_count)
                    && buffer_mask_for_base[static_cast<size_t>(base_number)] != 0) {
                    if (register_offset)
                        dynamic_buffers |= buffer_mask_for_base[static_cast<size_t>(base_number)];
                } else {
                    const uint32_t *base_reach = slot(base, 0);
                    dynamic_buffers |= (base_reach && *base_reach) ? *base_reach : all_buffers;
                }
                continue;
            }
            default:
                continue;
            }

            for (int r = 0; r <= repeat; ++r) {
                uint32_t from = 0;
                for (int s = 0; s < src_count; ++s) {
                    if (const uint32_t *src_reach = slot(srcs[s], r))
                        from |= *src_reach;
                }
                if (uint32_t *dest_reach = slot(dest, r); from && dest_reach)
                    *dest_reach |= from;
            }
        }
    };

    const uint64_t secondary_start_offset = offsetof(SceGxmProgram, secondary_program_offset) + static_cast<uint64_t>(program.secondary_program_offset);
    const uint64_t secondary_end_offset = offsetof(SceGxmProgram, secondary_program_offset_end) + static_cast<uint64_t>(program.secondary_program_offset_end);
    const uint64_t secondary_available = secondary_end_offset >= secondary_start_offset
        ? (secondary_end_offset - secondary_start_offset) / sizeof(uint64_t)
        : 0;
    const uint64_t secondary_count = std::min<uint64_t>(program.secondary_program_instr_count, secondary_available);
    const uint64_t *secondary = program.secondary_program_start();

    // A second pass catches address propagation through loops where a value is
    // written after its first use in the linear instruction order.
    for (int pass = 0; pass < 2; ++pass) {
        scan(secondary, secondary_count, true);
        scan(program.primary_program_start(), program.primary_program_instr_count, false);
    }

    uint32_t block_mask = 0;
    for (uint32_t buffer = 0; buffer < SCE_GXM_REAL_MAX_UNIFORM_BUFFER; ++buffer) {
        if ((dynamic_buffers & (1u << buffer)) == 0)
            continue;
        const uint32_t block = buffer < SCE_GXM_MAX_UNIFORM_BUFFERS
            ? buffer + SCE_GXM_UNIFORM_BUFFER_OFFSET
            : SCE_GXM_DEFAULT_UNIFORM_BUFFER_CONTAINER_INDEX;
        block_mask |= 1u << block;
    }
    return block_mask;
}

} // namespace shader::usse
