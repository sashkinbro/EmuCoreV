// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include <cheat/functions.h>

#include <mem/ptr.h>
#include <mem/state.h>
#include <util/log.h>

#include <algorithm>

namespace cheat {

namespace {

// Safety net so that a malformed database cannot make the vblank thread walk codes forever.
constexpr size_t max_executed_lines = 4096;
// The format documents at most five levels of indirection, but real packs use six and seven with the
// very same layout (head, one line per further offset, closing line). Eight is a sanity bound only.
// PROVISIONAL: the limit above five is not backed by the plugin documentation.
constexpr uint8_t max_pointer_level = 8;

// Outcome of one code line.
enum class Step {
    done, //!< executed
    skipped, //!< the target is not available right now (null pointer, unmapped page): drop the line, retry next vblank
    stop_frame, //!< a precondition is missing (module not loaded yet): end this vblank's run, retry next vblank
    malformed, //!< the code can never run: the cheat is broken for good
};

uint32_t width_size(CodeWidth width) {
    switch (width) {
    case CodeWidth::bits8: return 1;
    case CodeWidth::bits16: return 2;
    default: return 4;
    }
}

uint32_t width_mask(CodeWidth width) {
    switch (width) {
    case CodeWidth::bits8: return 0xFFu;
    case CodeWidth::bits16: return 0xFFFFu;
    default: return 0xFFFFFFFFu;
    }
}

// `is_valid_addr_range()` takes an end that is one past the last byte of the access.
bool is_accessible(const MemState &mem, uint32_t address, uint32_t size) {
    // The first page is the null guard page: reserved but never mapped, so a
    // cheat pointer chain that resolves to 0 would otherwise fault here.
    if ((address < KiB(4)) || (address + size < address))
        return false;

    if (!is_valid_addr_range(mem, address, address + size))
        return false;

    // With the page table the allocator can report a page as allocated while
    // its host entry is not installed yet; dereferencing it would fault.
    if (mem.use_page_table) {
        for (uint32_t offset = 0; offset < size; ++offset) {
            if (mem.page_table[(address + offset) / KiB(4)] == nullptr)
                return false;
        }
    }

    return true;
}

// The guest pages a value spans are not necessarily contiguous in host memory, so go byte by byte.
bool read_bytes(const MemState &mem, uint32_t address, uint8_t *destination, uint32_t size) {
    if (!is_accessible(mem, address, size))
        return false;

    for (uint32_t i = 0; i < size; ++i)
        destination[i] = *Ptr<uint8_t>(address + i).get(mem);

    return true;
}

bool write_bytes(MemState &mem, uint32_t address, const uint8_t *source, uint32_t size) {
    if (!is_accessible(mem, address, size))
        return false;

    for (uint32_t i = 0; i < size; ++i)
        *Ptr<uint8_t>(address + i).get(mem) = source[i];

    return true;
}

bool read_value(const MemState &mem, uint32_t address, CodeWidth width, uint32_t &value) {
    uint8_t bytes[4] = {};
    const uint32_t size = width_size(width);
    if (!read_bytes(mem, address, bytes, size))
        return false;

    value = 0;
    for (uint32_t i = 0; i < size; ++i)
        value |= static_cast<uint32_t>(bytes[i]) << (i * 8);

    return true;
}

bool write_value(MemState &mem, uint32_t address, CodeWidth width, uint32_t value) {
    uint8_t bytes[4] = {};
    const uint32_t size = width_size(width);
    for (uint32_t i = 0; i < size; ++i)
        bytes[i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);

    return write_bytes(mem, address, bytes, size);
}

struct PointerBlock {
    uint32_t address = 0;
    std::vector<uint32_t> offsets;
    // Closing line, it carries the value for a pointer write and nothing for a pointer MOV.
    CodeLine closing;
};

class Interpreter {
public:
    Interpreter(const CheatState &state, Cheat &cheat, MemState &mem, uint32_t buttons, const JitInvalidate &invalidate_jit)
        : m_state(state)
        , m_cheat(cheat)
        , m_mem(mem)
        , m_buttons(buttons)
        , m_invalidate_jit(invalidate_jit) {}

    void run() {
        size_t executed = 0;
        while ((m_index < m_cheat.lines.size()) && (executed < max_executed_lines)) {
            ++executed;
            const Step step = execute(m_cheat.lines[m_index]);
            if (step == Step::malformed) {
                m_cheat.broken = true;
                m_cheat.enabled = false;
                break;
            }
            if (step == Step::stop_frame)
                break;
        }
    }

private:
    // Addresses become relative to a module segment once a `$B2` code has been seen.
    uint32_t resolve(uint32_t address) const {
        const uint64_t resolved = static_cast<uint64_t>(m_relative_base) + address;
        if (resolved > UINT32_MAX) {
            m_address_overflow = true;
            return 0;
        }
        return static_cast<uint32_t>(resolved);
    }

    bool write(uint32_t address, CodeWidth width, uint32_t value) {
        return !m_address_overflow && write_value(m_mem, address, width, value);
    }
    // An access that failed because the target is not mapped or readable yet is not a defect of the code.
    static Step unless_unavailable(bool ok) {
        if (!ok)
            LOG_DEBUG_ONCE("A cheat target is not available yet, skipping it until it is");
        return ok ? Step::done : Step::skipped;
    }
    size_t code_line_count(size_t index) const {
        if (index >= m_cheat.lines.size())
            return 0;

        const CodeLine &line = m_cheat.lines[index];
        switch (line.type()) {
        case CodeType::compression:
            return 2;
        case CodeType::pointer_write:
            return 1 + pointer_block_lines(line);
        case CodeType::pointer_compression:
            // The block, then the line holding the count and the gaps.
            return 1 + pointer_block_lines(line) + 1;
        case CodeType::pointer_mov: {
            // A pointer MOV is a destination block followed by a source block.
            const size_t destination = 1 + pointer_block_levels(index, mov_destination_closer);
            const size_t source_index = index + destination;
            if ((source_index >= m_cheat.lines.size()) || (m_cheat.lines[source_index].type() != CodeType::pointer_mov))
                return destination;
            return destination + 1 + pointer_block_levels(source_index, mov_source_closer);
        }
        default:
            return 1;
        }
    }

    static size_t pointer_block_lines(const CodeLine &head) {
        return std::min<size_t>(head.param(), max_pointer_level);
    }

    // Number of lines after the head of the pointer block at `index`. The head level is trusted unless the
    // block is closed by a known line (`$88` / `$89` of a pointer MOV): packs write a head level that does not
    // match their lines (e.g. `$8601` over three lines), the closing line then gives the real depth.
    size_t pointer_block_levels(size_t index, uint16_t closer) const {
        const size_t level = pointer_block_lines(m_cheat.lines[index]);
        if (closer == 0)
            return level;

        const auto closes = [&](size_t count) {
            const size_t at = index + count;
            return (at < m_cheat.lines.size()) && ((m_cheat.lines[at].control >> 8) == closer);
        };
        if ((level >= 1) && closes(level))
            return level;
        for (size_t count = 1; count <= max_pointer_level; ++count) {
            if (closes(count))
                return count;
        }

        return level;
    }
    static constexpr uint16_t mov_destination_closer = 0x88;
    static constexpr uint16_t mov_source_closer = 0x89;

    // A related count is a number of code lines below the identifier, the
    // original plugin skips line by line so a pointer block is skipped whole.
    void skip_related_lines(uint8_t related) {
        const size_t remaining = std::max<size_t>(related, 1);
        m_index = std::min(m_index + remaining, m_cheat.lines.size());
    }

    Step execute(const CodeLine &line) {
        const auto type = line.type();
        if ((type == CodeType::write || type == CodeType::pointer_write ||
             type == CodeType::compression || type == CodeType::mov ||
             type == CodeType::pointer_compression || type == CodeType::pointer_mov ||
             type == CodeType::arm_write) && line.op() > 2)
            return Step::malformed;
        switch (line.type()) {
        case CodeType::write:
            return execute_write(line);
        case CodeType::pointer_write:
            return execute_pointer_write(line);
        case CodeType::compression:
            return execute_compression(line);
        case CodeType::mov:
            return execute_mov(line);
        case CodeType::pointer_compression:
            return execute_pointer_compression(line);
        case CodeType::pointer_mov:
            return execute_pointer_mov(line);
        case CodeType::arm_write:
            return execute_arm_write(line);
        case CodeType::relative_base:
            return execute_relative_base(line);
        case CodeType::button_pad:
            return execute_button_pad(line);
        case CodeType::condition:
            return execute_condition(line);
        default:
            LOG_WARN_ONCE("Cheat '{}' uses the unsupported code type ${:04X}", m_cheat.name, line.control);
            return Step::malformed;
        }
    }

    // `$0X00 <address> <value>`
    Step execute_write(const CodeLine &line) {
        ++m_index;
        return unless_unavailable(write(resolve(line.first), static_cast<CodeWidth>(line.op()), line.second));
    }

    // `$5X00 <destination> <source>`
    Step execute_mov(const CodeLine &line) {
        ++m_index;

        const auto width = static_cast<CodeWidth>(line.op());
        uint32_t value = 0;
        return unless_unavailable(read_value(m_mem, resolve(line.second), width, value) &&
            write(resolve(line.first), width, value));
    }

    // `$4X01 <address> <value>` followed by `$<count> <address gap> <value gap>`
    Step execute_compression(const CodeLine &line) {
        if (m_index + 1 >= m_cheat.lines.size()) {
            LOG_WARN_ONCE("Cheat '{}' ends with an incomplete compression code", m_cheat.name);
            return Step::malformed;
        }

        const CodeLine gaps = m_cheat.lines[m_index + 1];
        m_index += 2;

        const auto width = static_cast<CodeWidth>(line.op());
        // The whole control word of the second line is the iteration count.
        const uint32_t count = gaps.control;
        const uint32_t base = resolve(line.first);
        for (uint32_t i = 0; i < count; ++i) {
            const uint64_t address = static_cast<uint64_t>(base) + static_cast<uint64_t>(i) * gaps.first;
            if (address > UINT32_MAX || !write(static_cast<uint32_t>(address), width, line.second + (i * gaps.second)))
                return unless_unavailable(false);
        }

        return Step::done;
    }

    // `$3X<level> <address> <offset>`, one line per further offset, then the value.
    Step execute_pointer_write(const CodeLine &line) {
        PointerBlock block;
        if (!parse_pointer_block(line, block))
            return Step::malformed;

        uint32_t address = 0;
        return unless_unavailable(follow_pointers(block, address) &&
            write(address, static_cast<CodeWidth>(line.op()), block.closing.second));
    }

    // `$8X<level> ...` writes to `$8<4|5|6><level> ...`, both blocks closed by a `$88` / `$89` line.
    Step execute_pointer_mov(const CodeLine &line) {
        PointerBlock destination;
        if (!parse_pointer_block(line, destination, mov_destination_closer))
            return Step::malformed;

        if (m_index >= m_cheat.lines.size()) {
            LOG_WARN_ONCE("Cheat '{}' ends with an incomplete pointer MOV code", m_cheat.name);
            return Step::malformed;
        }

        const CodeLine source_line = m_cheat.lines[m_index];
        if ((source_line.type() != CodeType::pointer_mov) || (source_line.op() < 4) || (source_line.op() > 6)) {
            LOG_WARN_ONCE("Cheat '{}' has a pointer MOV code without a source block", m_cheat.name);
            return Step::malformed;
        }

        PointerBlock source;
        if (!parse_pointer_block(source_line, source, mov_source_closer))
            return Step::malformed;

        const auto width = static_cast<CodeWidth>(line.op());
        uint32_t destination_address = 0;
        uint32_t source_address = 0;
        if (!follow_pointers(destination, destination_address) || !follow_pointers(source, source_address))
            return unless_unavailable(false);

        uint32_t value = 0;
        return unless_unavailable(read_value(m_mem, source_address, width, value) &&
            write(destination_address, width, value));
    }

    // A `$3` block followed by the `$<count> <address gap> <value gap>` line of a `$4` code.
    Step execute_pointer_compression(const CodeLine &line) {
        PointerBlock block;
        if (!parse_pointer_block(line, block))
            return Step::malformed;

        if (m_index >= m_cheat.lines.size()) {
            LOG_WARN_ONCE("Cheat '{}' ends with an incomplete pointer compression code", m_cheat.name);
            return Step::malformed;
        }

        const CodeLine gaps = m_cheat.lines[m_index];
        ++m_index;

        uint32_t address = 0;
        if (!follow_pointers(block, address))
            return unless_unavailable(false);

        const auto width = static_cast<CodeWidth>(line.op());
        const uint32_t count = gaps.control;
        for (uint32_t i = 0; i < count; ++i) {
            const uint64_t target = static_cast<uint64_t>(address) + static_cast<uint64_t>(i) * gaps.first;
            if (target > UINT32_MAX || !write(static_cast<uint32_t>(target), width, block.closing.second + (i * gaps.second)))
                return unless_unavailable(false);
        }

        return Step::done;
    }

    // `$AX00 <address> <value>`, X is 0 for an 8-bit, 1 for a 16-bit and 2 for a 32-bit value.
    // `$A0` is documented by the VitaCheat wiki only through its example `$A000 810A12A6 00000001`
    // (writes 0x01, the original byte comes back when the code is turned off).
    Step execute_arm_write(const CodeLine &line) {
        ++m_index;

        const auto width = static_cast<CodeWidth>(line.op());
        const uint32_t size = width_size(width);
        const uint32_t address = resolve(line.first);
        const uint32_t value = line.second & width_mask(width);

        uint32_t current = 0;
        if (!read_value(m_mem, address, width, current))
            return unless_unavailable(false);

        // Patching guest code once is enough, only pay for the recompiler flush when it changed.
        if (current == value)
            return Step::done;

        const bool already_saved = std::any_of(m_cheat.saved_memory.begin(), m_cheat.saved_memory.end(),
            [address](const SavedMemory &saved) { return saved.address == address; });
        if (!already_saved) {
            SavedMemory saved;
            saved.address = address;
            saved.bytes.resize(size);
            if (!read_bytes(m_mem, address, saved.bytes.data(), size))
                return unless_unavailable(false);
            m_cheat.saved_memory.push_back(std::move(saved));
        }

        if (!write(address, width, value))
            return unless_unavailable(false);
        if (m_invalidate_jit)
            m_invalidate_jit(address, size);

        return Step::done;
    }

    // `$B2<module> 0000000<segment> 00000000`
    Step execute_relative_base(const CodeLine &line) {
        ++m_index;

        if (line.op() != 2) {
            LOG_WARN_ONCE("Cheat '{}' uses the unsupported code type ${:04X}", m_cheat.name, line.control);
            return Step::malformed;
        }

        const uint8_t module_index = line.param();
        const uint32_t segment_index = line.first & 0xF;
        if (segment_index >= 2) {
            LOG_WARN_ONCE("Cheat '{}' refers to segment {}, a module only has two", m_cheat.name, segment_index);
            return Step::malformed;
        }

        // A module that is not registered (yet) is not a defect of the code: the codes below it must not run
        // against an unrelocated base, so end this vblank and look again at the next one.
        if (module_index >= m_state.modules.size()) {
            LOG_DEBUG_ONCE("Cheat '{}' refers to module {}, which is not registered", m_cheat.name, module_index);
            return Step::stop_frame;
        }

        const auto &segment = m_state.modules[module_index].segments[segment_index];
        if (segment.size == 0) {
            LOG_DEBUG_ONCE("Cheat '{}' refers to module {} segment {}, which is empty", m_cheat.name, module_index, segment_index);
            return Step::stop_frame;
        }

        m_relative_base = segment.address;

        return Step::done;
    }

    // `$C2<lines> <pad type> <button mask>`
    Step execute_button_pad(const CodeLine &line) {
        ++m_index;

        // The pad type selects between the Vita pad and a DualShock, Vita3K only exposes one pad.
        const uint32_t mask = line.second;
        if ((m_buttons & mask) != mask)
            skip_related_lines(line.param());

        return Step::done;
    }

    // `$DX<lines> <address> <value>`
    Step execute_condition(const CodeLine &line) {
        ++m_index;

        const uint8_t op = line.op();
        if (op > 0xB) {
            LOG_WARN_ONCE("Cheat '{}' uses the unsupported condition ${:04X}", m_cheat.name, line.control);
            return Step::malformed;
        }

        const auto width = static_cast<CodeWidth>(op % 3);
        const uint32_t mask = width_mask(width);
        const uint32_t expected = line.second & mask;

        uint32_t value = 0;
        bool satisfied = read_value(m_mem, resolve(line.first), width, value);
        if (satisfied) {
            switch (op / 3) {
            case 0: satisfied = value == expected; break;
            case 1: satisfied = value != expected; break;
            case 2: satisfied = value > expected; break;
            default: satisfied = value < expected; break;
            }
        }

        if (!satisfied)
            skip_related_lines(line.param());

        return Step::done;
    }

    // Databases word the control field of the follow-up lines freely, so only `second` is read.
    bool parse_pointer_block(const CodeLine &head, PointerBlock &block, uint16_t closer = 0) {
        const uint8_t level = static_cast<uint8_t>(pointer_block_levels(m_index, closer));
        if ((level == 0) || (head.param() > max_pointer_level && closer == 0)) {
            LOG_WARN_ONCE("Cheat '{}' uses the invalid pointer level {} of code ${:04X}", m_cheat.name, level, head.control);
            return false;
        }

        if (m_index + level >= m_cheat.lines.size()) {
            LOG_WARN_ONCE("Cheat '{}' ends with an incomplete pointer block", m_cheat.name);
            return false;
        }

        block.address = head.first;
        block.offsets.reserve(level);
        block.offsets.push_back(head.second);
        for (uint8_t i = 1; i < level; i++)
            block.offsets.push_back(m_cheat.lines[m_index + i].second);

        block.closing = m_cheat.lines[m_index + level];
        m_index += level + 1;

        return true;
    }

    bool follow_pointers(const PointerBlock &block, uint32_t &address) const {
        address = resolve(block.address);
        if (m_address_overflow)
            return false;
        for (const uint32_t offset : block.offsets) {
            uint32_t pointer = 0;
            if (!read_value(m_mem, address, CodeWidth::bits32, pointer))
                return false;
            const uint64_t next = static_cast<uint64_t>(pointer) + offset;
            if (pointer == 0 || next > UINT32_MAX)
                return false;
            address = static_cast<uint32_t>(next);
        }

        return true;
    }

    const CheatState &m_state;
    Cheat &m_cheat;
    MemState &m_mem;
    const uint32_t m_buttons;
    const JitInvalidate &m_invalidate_jit;

    size_t m_index = 0;
    uint32_t m_relative_base = 0;
    mutable bool m_address_overflow = false;
};

void restore_cheat(Cheat &cheat, MemState &mem, const JitInvalidate &invalidate_jit) {
    for (auto it = cheat.saved_memory.rbegin(); it != cheat.saved_memory.rend(); ++it) {
        const uint32_t size = static_cast<uint32_t>(it->bytes.size());
        if (write_bytes(mem, it->address, it->bytes.data(), size) && invalidate_jit)
            invalidate_jit(it->address, size);
    }

    cheat.saved_memory.clear();
}

} // namespace

void unload(CheatState &state) {
    const std::lock_guard<std::mutex> lock(state.mutex);

    state.file = {};
    state.modules.clear();
    state.buttons.store(0, std::memory_order_relaxed);
}

void clear_file(CheatState &state, MemState &mem, const JitInvalidate &invalidate_jit) {
    const std::lock_guard<std::mutex> lock(state.mutex);
    for (auto &cheat : state.file.cheats)
        restore_cheat(cheat, mem, invalidate_jit);
    state.file = {};
}

bool load(CheatState &state, const fs::path &cheats_dir, const std::string &title_id) {
    const auto path = find_cheat_file(cheats_dir, title_id);
    if (path.empty()) {
        LOG_DEBUG("No cheat file found for {} in {}", title_id, cheats_dir);
        const std::lock_guard<std::mutex> lock(state.mutex);
        state.file = {};
        return false;
    }

    CheatFile file = parse_cheat_file(path, title_id);
    for (auto &cheat : file.cheats)
        cheat.enabled = cheat.enabled_on_boot;

    const std::lock_guard<std::mutex> lock(state.mutex);
    state.file = std::move(file);

    return !state.file.cheats.empty();
}

bool reload(CheatState &state, const fs::path &cheats_dir, const std::string &title_id, MemState &mem, const JitInvalidate &invalidate_jit) {
    // The saved originals go away with the old cheats, so undo the ARM writes first.
    set_all_cheats_enabled(state, false, mem, invalidate_jit);

    return load(state, cheats_dir, title_id);
}

void add_module(CheatState &state, const CheatModule &module) {
    const std::lock_guard<std::mutex> lock(state.mutex);

    state.modules.push_back(module);
}

void set_buttons(CheatState &state, uint32_t buttons) {
    state.buttons.store(buttons, std::memory_order_relaxed);
}

void apply(CheatState &state, MemState &mem, const JitInvalidate &invalidate_jit) {
    const std::lock_guard<std::mutex> lock(state.mutex);

    const uint32_t buttons = state.buttons.load(std::memory_order_relaxed);

    for (auto &cheat : state.file.cheats) {
        if (state.enabled && cheat.enabled && !cheat.broken) {
            Interpreter(state, cheat, mem, buttons, invalidate_jit).run();
            // A failed line must not leave an enabled claim or a partial ARM patch.
            if (cheat.broken)
                restore_cheat(cheat, mem, invalidate_jit);
        } else if (!cheat.saved_memory.empty()) {
            restore_cheat(cheat, mem, invalidate_jit);
        }
    }
}

bool set_cheat_enabled(CheatState &state, size_t index, bool enabled, MemState &mem, const JitInvalidate &invalidate_jit) {
    const std::lock_guard<std::mutex> lock(state.mutex);

    if (index >= state.file.cheats.size())
        return false;

    Cheat &cheat = state.file.cheats[index];
    if (enabled && cheat.broken)
        return false; // Reload/reimport resets a failed code; toggling is not a retry.
    if (cheat.enabled == enabled)
        return true;

    cheat.enabled = enabled;
    if (!enabled)
        restore_cheat(cheat, mem, invalidate_jit);
    return true;
}

bool set_all_cheats_enabled(CheatState &state, bool enabled, MemState &mem, const JitInvalidate &invalidate_jit) {
    const std::lock_guard<std::mutex> lock(state.mutex);

    bool accepted = false;
    for (auto &cheat : state.file.cheats) {
        if (enabled && cheat.broken) {
            cheat.enabled = false;
            continue;
        }
        accepted = true;
        cheat.enabled = enabled;
        if (!enabled)
            restore_cheat(cheat, mem, invalidate_jit);
    }
    return accepted;
}

void set_enabled(CheatState &state, bool enabled, MemState &mem, const JitInvalidate &invalidate_jit) {
    const std::lock_guard<std::mutex> lock(state.mutex);

    if (state.enabled == enabled)
        return;

    state.enabled = enabled;
    if (!enabled) {
        for (auto &cheat : state.file.cheats)
            restore_cheat(cheat, mem, invalidate_jit);
    }
}

size_t enabled_cheat_count(const CheatState &state) {
    const std::lock_guard<std::mutex> lock(state.mutex);

    return std::count_if(state.file.cheats.begin(), state.file.cheats.end(),
        [](const Cheat &cheat) { return cheat.enabled && !cheat.broken; });
}

CheatFile snapshot(const CheatState &state) {
    const std::lock_guard<std::mutex> lock(state.mutex);

    return state.file;
}

bool save(CheatState &state) {
    const std::lock_guard<std::mutex> lock(state.mutex);

    if (!save_cheat_file(state.file))
        return false;

    for (auto &cheat : state.file.cheats)
        cheat.enabled_on_boot = cheat.enabled;

    return true;
}

} // namespace cheat
