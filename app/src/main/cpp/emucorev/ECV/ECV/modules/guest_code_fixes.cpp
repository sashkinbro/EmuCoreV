// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later

#include <modules/guest_code_fixes.h>

#include <emuenv/state.h>
#include <kernel/state.h>
#include <mem/functions.h>
#include <util/log.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

namespace {

// Adapted from Vita3K-Plus ccd2e20333. ExecuteChunk releases its scheduler
// lock while dispatching a chunk. A completion can reset the scan cursor to
// zero before the blocked chunk is requeued. Unconditionally incrementing it
// then loses that kick and leaves completed work at queue position zero stuck.
// Keep zero intact; a nonzero cursor still advances as before.
constexpr std::array<int, 40> fios_pattern = {
    0x41, 0xF2, 0x7C, 0x00, 0x58, 0xF8, 0x00, 0x20, 0x40, 0x46, 0x39, 0x1C, -1, -1, -1, -1,
    0x58, 0xF8, 0x04, 0x00, 0x01, 0x30, 0x48, 0xF8, 0x04, 0x00, 0x02, 0xE0, 0x28, 0x1C, -1, -1, -1, -1,
    0x1E, 0x20, 0xC8, 0xF2, 0x82, 0x00
};
constexpr std::array<int, 26> fios_replacement = {
    0x08, 0xEB, 0x04, 0x06, 0xB2, 0x68, 0x40, 0x46, 0x39, 0x1C, 0x00, 0xBF, -1, -1, -1, -1,
    0x30, 0x68, 0x10, 0xB1, 0x01, 0x30, 0x30, 0x60, 0x00, 0xBF
};

} // namespace

void apply_guest_code_fixes(EmuEnvState &emuenv, const SceKernelModuleInfo &module) {
    // Resistance's bundled libfios3 has the same vulnerable implementation.
    // The full instruction signature, not the filename/version, gates the fix.
    if (std::strncmp(module.module_name, "SceLibFios2", sizeof(module.module_name)) != 0
        && std::strncmp(module.module_name, "SceLibFios3", sizeof(module.module_name)) != 0)
        return;

    Address site = 0;
    std::array<uint8_t, fios_replacement.size()> original{};
    for (const auto &segment : module.segments) {
        const Address base = segment.vaddr.address();
        if ((segment.perms & 1) == 0 || segment.memsz < fios_pattern.size()
            || !base || segment.memsz > std::numeric_limits<Address>::max() - base)
            continue;
        std::vector<uint8_t> code(segment.memsz);
        if (!debug_safe_copy_guest(emuenv.mem, base, code.data(), segment.memsz))
            continue;
        for (size_t offset = base & 1; offset + fios_pattern.size() <= code.size(); offset += 2) {
            if (!std::equal(fios_pattern.begin(), fios_pattern.end(), code.begin() + offset,
                    [](int expected, uint8_t byte) { return expected < 0 || expected == byte; }))
                continue;
            if (site) {
                LOG_WARN("{}: ambiguous FIOS scheduler signature; leaving guest code unchanged", module.module_name);
                return;
            }
            site = base + static_cast<Address>(offset);
            std::copy_n(code.begin() + offset, original.size(), original.begin());
        }
    }
    if (!site)
        return;

    auto replacement = original;
    for (size_t i = 0; i < replacement.size(); ++i)
        if (fios_replacement[i] >= 0)
            replacement[i] = static_cast<uint8_t>(fios_replacement[i]);
    std::array<uint8_t, fios_replacement.size()> check{};
    const bool applied = debug_safe_write_guest(emuenv.mem, site, replacement.data(), replacement.size())
        && debug_safe_copy_guest(emuenv.mem, site, check.data(), check.size()) && check == replacement;
    if (!applied) {
        debug_safe_write_guest(emuenv.mem, site, original.data(), original.size());
        LOG_ERROR("{}: could not apply FIOS scheduler fix", module.module_name);
    }
    emuenv.kernel.invalidate_jit_cache(site, fios_pattern.size());
    if (applied)
        LOG_INFO("{}: applied FIOS scheduler kick fix at 0x{:08X}", module.module_name, site);
}
