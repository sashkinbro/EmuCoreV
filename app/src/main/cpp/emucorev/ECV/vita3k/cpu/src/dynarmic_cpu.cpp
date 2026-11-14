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

#include "cpu/common.h"
#include <cpu/impl/dynarmic_cpu.h>
#include <cpu/state.h>
#include <util/log.h>

#include <cpu/functions.h>
#include <mem/functions.h>
#include <mem/ptr.h>

#include <dynarmic/frontend/A32/a32_ir_emitter.h>
#include <dynarmic/interface/A32/coprocessor.h>
#include <dynarmic/interface/exclusive_monitor.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// Log the first few invalid guest accesses in full, then suppress (a guest null-deref loop can log GBs).
static bool should_log_invalid_access() {
#ifdef NDEBUG
    return false;
#else
    if (!spdlog::default_logger_raw()->should_log(spdlog::level::err))
        return false;
    static std::atomic<uint64_t> count{ 0 };
    const uint64_t n = count.fetch_add(1, std::memory_order_relaxed);
    if (n == 8)
        LOG_ERROR("Further invalid guest memory accesses will be suppressed (guest likely spinning on a bad pointer)");
    return n < 8;
#endif
}

static std::uint64_t coproc_noop(void *, std::uint32_t, std::uint32_t) {
    return 0;
}

static Dynarmic::A32::Coprocessor::Callback coproc_noop_callback() {
    return Dynarmic::A32::Coprocessor::Callback{ &coproc_noop, std::nullopt };
}

// Registered for every coprocessor slot Vita3K does not emulate.
class ArmDynarmicNullCP : public Dynarmic::A32::Coprocessor {
public:
    using CoprocReg = Dynarmic::A32::CoprocReg;

    ~ArmDynarmicNullCP() override = default;

    std::optional<Callback> CompileInternalOperation(bool, unsigned, CoprocReg, CoprocReg, CoprocReg, unsigned) override {
        return coproc_noop_callback();
    }
    CallbackOrAccessOneWord CompileSendOneWord(bool, unsigned, CoprocReg, CoprocReg, unsigned) override {
        return coproc_noop_callback();
    }
    CallbackOrAccessTwoWords CompileSendTwoWords(bool, unsigned, CoprocReg) override {
        return coproc_noop_callback();
    }
    CallbackOrAccessOneWord CompileGetOneWord(bool, unsigned, CoprocReg, CoprocReg, unsigned) override {
        return coproc_noop_callback();
    }
    CallbackOrAccessTwoWords CompileGetTwoWords(bool, unsigned, CoprocReg) override {
        return coproc_noop_callback();
    }
    std::optional<Callback> CompileLoadWords(bool, bool, CoprocReg, std::optional<std::uint8_t>) override {
        return coproc_noop_callback();
    }
    std::optional<Callback> CompileStoreWords(bool, bool, CoprocReg, std::optional<std::uint8_t>) override {
        return coproc_noop_callback();
    }
};

class ArmDynarmicCP15 : public Dynarmic::A32::Coprocessor {
    uint32_t tpidruro;
    uint32_t sctlr;
    uint32_t dacr;

public:
    using CoprocReg = Dynarmic::A32::CoprocReg;

    explicit ArmDynarmicCP15()
        : tpidruro(0)
        , sctlr(0)
        , dacr(0) {
    }

    ~ArmDynarmicCP15() override = default;

    std::optional<Callback> CompileInternalOperation(bool two, unsigned opc1, CoprocReg CRd,
        CoprocReg CRn, CoprocReg CRm,
        unsigned opc2) override {
        return coproc_noop_callback();
    }

    CallbackOrAccessOneWord CompileSendOneWord(bool two, unsigned opc1, CoprocReg CRn,
        CoprocReg CRm, unsigned opc2) override {
        // MCR p15, 0, Rt, c13, c0, 3 — write TPIDRURO
        if (CRn == CoprocReg::C13 && CRm == CoprocReg::C0 && opc1 == 0 && opc2 == 3) {
            return &tpidruro;
        }

        // MCR p15, 0, Rt, c1, c0, 0 — write SCTLR
        if (!two && CRn == CoprocReg::C1 && CRm == CoprocReg::C0 && opc1 == 0 && opc2 == 0) {
            return &sctlr;
        }

        // MCR p15, 0, Rt, c3, c0, 0 — write DACR
        if (!two && CRn == CoprocReg::C3 && CRm == CoprocReg::C0 && opc1 == 0 && opc2 == 0) {
            return &dacr;
        }

        LOG_WARN("Unhandled CP15 MCR: two={} opc1={} CRn={} CRm={} opc2={}", two, opc1, (int)CRn, (int)CRm, opc2);
        return coproc_noop_callback();
    }

    CallbackOrAccessTwoWords CompileSendTwoWords(bool two, unsigned opc, CoprocReg CRm) override {
        return coproc_noop_callback();
    }

    CallbackOrAccessOneWord CompileGetOneWord(bool two, unsigned opc1, CoprocReg CRn, CoprocReg CRm,
        unsigned opc2) override {
        // MRC p15, 0, Rt, c13, c0, 3 — read TPIDRURO (thread-local storage)
        if (CRn == CoprocReg::C13 && CRm == CoprocReg::C0 && opc1 == 0 && opc2 == 3) {
            return &tpidruro;
        }

        // MRC p15, 0, Rt, c1, c0, 0 — read SCTLR
        if (!two && CRn == CoprocReg::C1 && CRm == CoprocReg::C0 && opc1 == 0 && opc2 == 0) {
            return &sctlr;
        }

        // MRC p15, 0, Rt, c3, c0, 0 — read DACR
        if (!two && CRn == CoprocReg::C3 && CRm == CoprocReg::C0 && opc1 == 0 && opc2 == 0) {
            return &dacr;
        }

        LOG_WARN("Unhandled CP15 MRC: two={} opc1={} CRn={} CRm={} opc2={}", two, opc1, (int)CRn, (int)CRm, opc2);
        return coproc_noop_callback();
    }

    CallbackOrAccessTwoWords CompileGetTwoWords(bool two, unsigned opc, CoprocReg CRm) override {
        return coproc_noop_callback();
    }

    std::optional<Callback> CompileLoadWords(bool two, bool long_transfer, CoprocReg CRd,
        std::optional<std::uint8_t> option) override {
        return coproc_noop_callback();
    }

    std::optional<Callback> CompileStoreWords(bool two, bool long_transfer, CoprocReg CRd,
        std::optional<std::uint8_t> option) override {
        return coproc_noop_callback();
    }

    void set_tpidruro(uint32_t tpidruro) {
        this->tpidruro = tpidruro;
    }

    uint32_t get_tpidruro() const {
        return tpidruro;
    }

    void set_sctlr(uint32_t sctlr) {
        this->sctlr = sctlr;
    }

    uint32_t get_sctlr() const {
        return sctlr;
    }

    void set_dacr(uint32_t dacr) {
        this->dacr = dacr;
    }

    uint32_t get_dacr() const {
        return dacr;
    }
};

class ArmDynarmicCallback : public Dynarmic::A32::UserCallbacks {
    friend class DynarmicCPU;

    CPUState *parent;
    DynarmicCPU *cpu;
#ifndef NDEBUG
    bool no_execute_diagnostics_logged = false;
#endif

public:
    explicit ArmDynarmicCallback(CPUState &parent, DynarmicCPU &cpu)
        : parent(&parent)
        , cpu(&cpu) {}

    ~ArmDynarmicCallback() override = default;

    void log_no_execute_diagnostics(uint32_t pc) {
#ifndef NDEBUG
        if (no_execute_diagnostics_logged)
            return;
        no_execute_diagnostics_logged = true;

        CPUContext crash_ctx = cpu->save_context();
        uint32_t last_import_nid = 0;
        uint32_t last_import_lr = 0;
        get_last_import_call(last_import_nid, last_import_lr);

        std::string details = fmt::format(
            "NoExecute diagnostic: pc=0x{:08X}\n{}\n"
            "Last HLE import: nid=0x{:08X}, caller LR=0x{:08X}\n",
            pc, crash_ctx.description(), last_import_nid, last_import_lr);

        // Copy instruction words instead of disassembling live guest memory:
        // another guest thread can unmap/protect it after the diagnostic read.
        const uint32_t lr_raw = crash_ctx.cpu_registers[14];
        const bool thumb_caller = (lr_raw & 1) != 0;
        const uint32_t lr = lr_raw & ~1u;
        if (lr >= parent->mem->host_page_size + 64) {
            std::string window;
            const uint64_t end = uint64_t{ lr } + 2;
            for (uint64_t address = lr - 64; address <= end;) {
                if (address > std::numeric_limits<uint32_t>::max())
                    break;
                uint32_t instruction_word = 0;
                const uint16_t instruction_size = thumb_caller ? 2 : 4;
                if (!debug_safe_copy_guest(*parent->mem, static_cast<Address>(address),
                        &instruction_word, instruction_size))
                    break;
                window += fmt::format("  0x{:08X}: 0x{:08X}\n", address, instruction_word);
                address += instruction_size;
            }
            details += fmt::format("Caller instruction words before LR (thumb={}):\n{}", thumb_caller, window);
        } else {
            details += fmt::format("Caller code unavailable for LR=0x{:08X}\n", lr_raw);
        }

        // Copy bounded words at each register target without dereferencing an
        // unchecked guest address. Keep underflow/overflow out of Ptr::get().
        std::string register_memory;
        for (int reg = 0; reg < 14; ++reg) {
            const uint32_t value = crash_ctx.cpu_registers[reg];
            const uint32_t base = value & ~3u;
            register_memory += fmt::format("  [r{}=0x{:08X}]:", reg, value);
            for (int word_offset = -2; word_offset < 6; ++word_offset) {
                const int64_t address = int64_t{ base } + word_offset * int64_t{ 4 };
                uint32_t word = 0;
                if (address >= parent->mem->host_page_size
                    && address <= std::numeric_limits<uint32_t>::max()
                    && debug_safe_copy_guest(*parent->mem, static_cast<Address>(address), &word, sizeof(word))) {
                    register_memory += fmt::format(" {:08X}", word);
                } else {
                    register_memory += " ????????";
                }
            }
            register_memory += "\n";
        }
        details += fmt::format("Memory near register targets (words -2..+5):\n{}", register_memory);

        // r0/r6 are useful `this` candidates for indirect calls. Safely read
        // the object vtable-like pointer and its first four entries.
        const auto append_table = [&](const char *reg_name, uint32_t object) {
            uint32_t table = 0;
            if (!debug_safe_copy_guest(*parent->mem, object, &table, sizeof(table))) {
                details += fmt::format("{} object 0x{:08X}: first word unavailable\n", reg_name, object);
                return;
            }
            details += fmt::format("{} object 0x{:08X}: table=0x{:08X} entries:", reg_name, object, table);
            for (uint32_t slot = 0; slot < 4; ++slot) {
                uint32_t entry = 0;
                const uint64_t entry_address = uint64_t{ table } + slot * sizeof(uint32_t);
                if (entry_address <= std::numeric_limits<uint32_t>::max()
                    && debug_safe_copy_guest(*parent->mem, static_cast<Address>(entry_address), &entry, sizeof(entry))) {
                    details += fmt::format(" [{}]=0x{:08X}", slot, entry);
                } else {
                    details += fmt::format(" [{}]=<unmapped>", slot);
                }
            }
            details += "\n";
        };
        const uint32_t r0 = crash_ctx.cpu_registers[0];
        const uint32_t r6 = crash_ctx.cpu_registers[6];
        append_table("r0", r0);
        if (r6 != r0)
            append_table("r6", r6);

        // NoExecuteFault is terminal, so critical logging guarantees this
        // single diagnostic is retained without per-frame output.
        LOG_CRITICAL("{}", details);
#endif
    }

    std::optional<std::uint32_t> MemoryReadCode(Dynarmic::A32::VAddr addr) override {
        if (cpu->log_mem)
            LOG_TRACE("Instruction fetch at address 0x{:X}", addr);
        const Ptr<uint32_t> ptr{ static_cast<Address>(addr) };
        if (!ptr || !ptr.valid(*parent->mem) || ptr.address() < parent->mem->host_page_size) {
            if (!confirm_invalid_access(addr))
                return MemoryRead32(addr);
            return std::nullopt;
        }
        return MemoryRead32(addr);
    }

    static void TraceInstruction(uint64_t self_, uint64_t address, uint64_t is_thumb) {
#ifndef NDEBUG
        if (!spdlog::default_logger_raw()->should_log(spdlog::level::trace))
            return;
        ArmDynarmicCallback &self = *reinterpret_cast<ArmDynarmicCallback *>(self_);

        std::string disassembly = [&]() -> std::string {
            if (!address || !Ptr<uint32_t>{ (uint32_t)address }.valid(*self.parent->mem)) {
                return "invalid address";
            }
            return disassemble(*self.parent, address);
        }();
        LOG_TRACE("{} ({}): {} {}", log_hex(self_), self.parent->thread_id, log_hex(address), disassembly);
#endif
    }

    inline static std::recursive_timed_mutex loader_lock_mutex;
#ifndef NDEBUG
    inline static std::atomic<int> loader_lock_timeouts{ 0 };
#endif
    int loader_lock_held = 0;

    static void MonoLoaderLockAcquire(uint64_t self_, uint64_t, uint64_t) {
        ArmDynarmicCallback &self = *reinterpret_cast<ArmDynarmicCallback *>(self_);
        if (self.loader_lock_held > 0) {
            ++self.loader_lock_held;
            return;
        }
        if (!loader_lock_mutex.try_lock_for(std::chrono::seconds(5))) {
#ifndef NDEBUG
            if (spdlog::default_logger_raw()->should_log(spdlog::level::warn) && loader_lock_timeouts.fetch_add(1) < 20)
                LOG_WARN("Timed out waiting for the emulated mono loader lock on thread {}, proceeding unserialised", self.parent->thread_id);
#endif
            return;
        }
        self.loader_lock_held = 1;
    }

    static void MonoLoaderLockRelease(uint64_t self_, uint64_t, uint64_t) {
        ArmDynarmicCallback &self = *reinterpret_cast<ArmDynarmicCallback *>(self_);
        if (self.loader_lock_held == 0)
            return;
        if (--self.loader_lock_held == 0)
            loader_lock_mutex.unlock();
    }

    enum class MonoLoaderOp {
        None,
        Lock,
        Unlock,
    };

    static constexpr bool is_movw(uint32_t insn, uint32_t rd) {
        return (insn & 0xFFF0F000) == (0xE3000000 | (rd << 12));
    }
    static constexpr bool is_movt(uint32_t insn, uint32_t rd) {
        return (insn & 0xFFF0F000) == (0xE3400000 | (rd << 12));
    }

    // Neither mono_loader_lock nor mono_loader_unlock is exported, so they cannot be resolved
    // by NID, and each mono build places them somewhere different. Recognise them from their own code
    // instead, as dynarmic translates them. Both are the same 19-instruction body:
    //
    //      push {r4, lr}
    //      movw/movt r0, #<&loader_lock_enabled>   ; immediates vary per build
    //      ldr  r0, [r0]
    //      cmp  r0, #0
    //      beq  epilogue                           ; disabled: no mutex is ever taken
    //      movw/movt r0, #<&nest_count_tls_key>
    //      ldr  r4, [r0]
    //      movw/movt ip, #<&tls_get> ; mov r0, r4 ; blx ip
    //      movw ip, ... ; add/sub r1, r0, #1       ; the ONLY differing word: +1 lock, -1 unlock
    //      movt ip, ... ; mov r0, r4 ; blx ip      ; tls_set(key, nest +/- 1)
    //  epilogue:
    //      pop  {r4, pc}
    MonoLoaderOp classify_mono_loader_lock(Dynarmic::A32::VAddr pc) {
        static constexpr uint32_t PUSH_R4_LR = 0xE92D4010;
        static constexpr uint32_t LDR_R0_R0 = 0xE5900000;
        static constexpr uint32_t CMP_R0_0 = 0xE3500000;
        static constexpr uint32_t BEQ_EPILOGUE = 0x0A00000B; // beq +11 words -> the pop below
        static constexpr uint32_t LDR_R4_R0 = 0xE5904000;
        static constexpr uint32_t MOV_R0_R4 = 0xE1A00004;
        static constexpr uint32_t BLX_IP = 0xE12FFF3C;
        static constexpr uint32_t ADD_R1_R0_1 = 0xE2801001; // lock:   nest + 1
        static constexpr uint32_t SUB_R1_R0_1 = 0xE2401001; // unlock: nest - 1
        static constexpr uint32_t POP_R4_PC = 0xE8BD8010;
        static constexpr size_t BODY_WORDS = 19;

        const auto word_at = [&](size_t i) -> std::optional<uint32_t> {
            const Ptr<uint32_t> word{ static_cast<Address>(pc + i * 4) };
            if (!word.valid(*parent->mem))
                return std::nullopt;
            return *word.get(*parent->mem);
        };

        const auto first = word_at(0);
        if (!first || *first != PUSH_R4_LR)
            return MonoLoaderOp::None;

        uint32_t w[BODY_WORDS];
        w[0] = *first;
        for (size_t i = 1; i < BODY_WORDS; i++) {
            const auto word = word_at(i);
            if (!word)
                return MonoLoaderOp::None;
            w[i] = *word;
        }

        const bool shape = is_movw(w[1], 0) && is_movt(w[2], 0) && w[3] == LDR_R0_R0
            && w[4] == CMP_R0_0 && w[5] == BEQ_EPILOGUE
            && is_movw(w[6], 0) && is_movt(w[7], 0) && w[8] == LDR_R4_R0
            && is_movw(w[9], 12) && is_movt(w[10], 12) && w[11] == MOV_R0_R4 && w[12] == BLX_IP
            && is_movw(w[13], 12) && is_movt(w[15], 12) && w[16] == MOV_R0_R4 && w[17] == BLX_IP
            && w[18] == POP_R4_PC;
        if (!shape)
            return MonoLoaderOp::None;

        if (w[14] == ADD_R1_R0_1)
            return MonoLoaderOp::Lock;
        if (w[14] == SUB_R1_R0_1)
            return MonoLoaderOp::Unlock;
        return MonoLoaderOp::None;
    }

    // Emitting IR before dynarmic evaluates an instruction's condition makes ir.block non-empty
    bool may_be_conditional(bool is_thumb, Dynarmic::A32::VAddr pc, Dynarmic::A32::IREmitter &ir) {
        if (is_thumb)
            return ir.current_location.IT().IsInITBlock();

        const auto insn = MemoryReadCode(pc);
        if (!insn)
            return true; // unreadable: assume the worst rather than risk the loop
        // 0xE is AL, 0xF the unconditional encoding space; everything else is a real condition
        return (*insn >> 28) < 0xE;
    }

    void PreCodeTranslationHook(bool is_thumb, Dynarmic::A32::VAddr pc, Dynarmic::A32::IREmitter &ir) override {
        if (!is_thumb) {
            if (const MonoLoaderOp op = classify_mono_loader_lock(pc); op != MonoLoaderOp::None) {
                static std::once_flag announced;
                std::call_once(announced, [pc] {
                    LOG_INFO("Serialising mono's loader lock: found it at 0x{:X}, and it takes no mutex in this build", pc);
                });
                ir.CallHostFunction(op == MonoLoaderOp::Lock ? &MonoLoaderLockAcquire : &MonoLoaderLockRelease,
                    ir.Imm64((uint64_t)this), ir.Imm64(0), ir.Imm64(0));
            }
        }
#ifndef NDEBUG
        if (cpu->log_code && !may_be_conditional(is_thumb, pc, ir)) {
            ir.CallHostFunction(&TraceInstruction, ir.Imm64((uint64_t)this), ir.Imm64(pc), ir.Imm64(is_thumb));
        }
#endif
    }

    // rechecks a failed lock-free validity test under the allocator lock
    bool confirm_invalid_access(Dynarmic::A32::VAddr addr) {
        if (addr >= parent->mem->host_page_size && is_valid_addr_synced(*parent->mem, static_cast<Address>(addr))) {
            return false;
        }
        return true;
    }

    template <typename T>
    T MemoryRead(Dynarmic::A32::VAddr addr) {
        Ptr<T> ptr{ addr };
        if (!ptr || !ptr.valid(*parent->mem) || ptr.address() < parent->mem->host_page_size) {
            const bool confirm = confirm_invalid_access(addr);
            if (!confirm)
                return *ptr.get(*parent->mem);
            if (should_log_invalid_access()) {
                LOG_ERROR("Invalid read of uint{}_t at address: 0x{:x}\n{}", sizeof(T) * 8, addr, this->cpu->save_context().description());

                auto pc = this->cpu->get_pc();
                if (pc < parent->mem->host_page_size)
                    LOG_CRITICAL("PC is 0x{:x}", pc);
                else
                    LOG_ERROR("Executing: {}", disassemble(*parent, pc, nullptr));
            }
            return 0;
        }

        T ret = *ptr.get(*parent->mem);
        if (cpu->log_mem) {
            LOG_TRACE("Read uint{}_t at address: 0x{:x}, val = 0x{:x}", sizeof(T) * 8, addr, ret);
        }
        return ret;
    }

    uint8_t MemoryRead8(Dynarmic::A32::VAddr addr) override {
        return MemoryRead<uint8_t>(addr);
    }

    uint16_t MemoryRead16(Dynarmic::A32::VAddr addr) override {
        return MemoryRead<uint16_t>(addr);
    }

    uint32_t MemoryRead32(Dynarmic::A32::VAddr addr) override {
        return MemoryRead<uint32_t>(addr);
    }

    uint64_t MemoryRead64(Dynarmic::A32::VAddr addr) override {
        return MemoryRead<uint64_t>(addr);
    }

    template <typename T>
    void MemoryWrite(Dynarmic::A32::VAddr addr, T value) {
        Ptr<T> ptr{ addr };
        if (!ptr || !ptr.valid(*parent->mem) || ptr.address() < parent->mem->host_page_size) {
            if (!confirm_invalid_access(addr)) {
                *ptr.get(*parent->mem) = value;
                return;
            }
            if (should_log_invalid_access()) {
                LOG_ERROR("Invalid write of uint{}_t at addr: 0x{:x}, val = 0x{:x}\n{}", sizeof(T) * 8, addr, value, this->cpu->save_context().description());

                auto pc = this->cpu->get_pc();
                if (pc < parent->mem->host_page_size)
                    LOG_CRITICAL("PC is 0x{:x}", pc);
                else
                    LOG_ERROR("Executing: {}", disassemble(*parent, pc, nullptr));
            }
            return;
        }

        *ptr.get(*parent->mem) = value;
        if (cpu->log_mem) {
            LOG_TRACE("Write uint{}_t at addr: 0x{:x}, val = 0x{:x}", sizeof(T) * 8, addr, value);
        }
    }

    void MemoryWrite8(Dynarmic::A32::VAddr addr, uint8_t value) override {
        MemoryWrite<uint8_t>(addr, value);
    }

    void MemoryWrite16(Dynarmic::A32::VAddr addr, uint16_t value) override {
        MemoryWrite<uint16_t>(addr, value);
    }

    void MemoryWrite32(Dynarmic::A32::VAddr addr, uint32_t value) override {
        MemoryWrite<uint32_t>(addr, value);
    }

    void MemoryWrite64(Dynarmic::A32::VAddr addr, uint64_t value) override {
        MemoryWrite<uint64_t>(addr, value);
    }

    template <typename T>
    bool MemoryWriteExclusive(Dynarmic::A32::VAddr addr, T value, T expected) {
        Ptr<T> ptr{ addr };
        if (!ptr || !ptr.valid(*parent->mem) || ptr.address() < parent->mem->host_page_size) {
            if (should_log_invalid_access()) {
                LOG_ERROR("Invalid exclusive write of uint{}_t at addr: 0x{:x}, val = 0x{:x}, expected = 0x{:x}\n{}", sizeof(T) * 8, addr, value, expected, this->cpu->save_context().description());

                auto pc = this->cpu->get_pc();
                if (pc < parent->mem->host_page_size)
                    LOG_CRITICAL("PC is 0x{:x}", pc);
                else
                    LOG_ERROR("Executing: {}", disassemble(*parent, pc, nullptr));
            }
            return false;
        }

        auto result = Ptr<T>(addr).atomic_compare_and_swap(*parent->mem, value, expected);
        if (cpu->log_mem) {
            LOG_TRACE("Write uint{}_t at addr: 0x{:x}, val = 0x{:x}, expected = 0x{:x}", sizeof(T) * 8, addr, value, expected);
        }
        return result;
    }

    bool MemoryWriteExclusive8(Dynarmic::A32::VAddr addr, uint8_t value, uint8_t expected) override {
        return MemoryWriteExclusive(addr, value, expected);
    }

    bool MemoryWriteExclusive16(Dynarmic::A32::VAddr addr, uint16_t value, uint16_t expected) override {
        return MemoryWriteExclusive(addr, value, expected);
    }

    bool MemoryWriteExclusive32(Dynarmic::A32::VAddr addr, uint32_t value, uint32_t expected) override {
        return MemoryWriteExclusive(addr, value, expected);
    }

    bool MemoryWriteExclusive64(Dynarmic::A32::VAddr addr, uint64_t value, uint64_t expected) override {
        return MemoryWriteExclusive(addr, value, expected); // Ptr<uint64_t>(addr).atomic_compare_and_swap(*parent->mem, value, expected);
    }

    void InterpreterFallback(Dynarmic::A32::VAddr addr, size_t num_insts) override {
        LOG_ERROR("Unimplemented instruction at address {}:\n{}", log_hex(addr), save_context(*parent).description());
    }

    void ExceptionRaised(uint32_t pc, Dynarmic::A32::Exception exception) override {
        switch (exception) {
        case Dynarmic::A32::Exception::Breakpoint: {
            cpu->break_ = true;
            cpu->jit->HaltExecution();
            if (cpu->is_thumb_mode())
                cpu->set_pc(pc | 1);
            else
                cpu->set_pc(pc);
            break;
        }
        case Dynarmic::A32::Exception::WaitForInterrupt: {
            cpu->halted = true;
            cpu->jit->HaltExecution();
            break;
        }
        case Dynarmic::A32::Exception::PreloadDataWithIntentToWrite:
        case Dynarmic::A32::Exception::PreloadData:
        case Dynarmic::A32::Exception::PreloadInstruction:
        case Dynarmic::A32::Exception::SendEvent:
        case Dynarmic::A32::Exception::SendEventLocal:
        case Dynarmic::A32::Exception::WaitForEvent:
            break;
        case Dynarmic::A32::Exception::Yield:
            break;
        case Dynarmic::A32::Exception::UndefinedInstruction:
            LOG_CRITICAL("Halting thread: undefined instruction at address 0x{:X}, instruction 0x{:X} ({})\n{}", pc, MemoryReadCode(pc).value_or(0), disassemble(*parent, pc, nullptr), cpu->save_context().description());
            cpu->crashed = true;
            cpu->jit->HaltExecution();
            break;
        case Dynarmic::A32::Exception::UnpredictableInstruction:
            LOG_WARN("Unpredictable instruction at address 0x{:X}, instruction 0x{:X} ({})", pc, MemoryReadCode(pc).value_or(0), disassemble(*parent, pc, nullptr));
            InterpreterFallback(pc, 1);
            break;
        case Dynarmic::A32::Exception::DecodeError: {
            LOG_CRITICAL("Halting thread: decode error at address 0x{:X}, instruction 0x{:X} ({})\n{}", pc, MemoryReadCode(pc).value_or(0), disassemble(*parent, pc, nullptr), cpu->save_context().description());
            cpu->crashed = true;
            cpu->jit->HaltExecution();
            break;
        }
        case Dynarmic::A32::Exception::NoExecuteFault:
            LOG_CRITICAL("Halting thread: attempted to execute unmapped memory at pc = 0x{:X}\n{}", pc, cpu->save_context().description());
            log_no_execute_diagnostics(pc);
            cpu->crashed = true;
            cpu->jit->HaltExecution();
            break;
        default:
            LOG_WARN("Unknown exception {} Raised at pc = 0x{:x}", static_cast<size_t>(exception), pc);
            LOG_TRACE("at address 0x{:X}, instruction 0x{:X} ({})", pc, MemoryReadCode(pc).value_or(0), disassemble(*parent, pc, nullptr));
        }
    }

    void CallSVC(uint32_t svc) override {
        parent->svc_called = true;
        parent->svc = svc;
        cpu->jit->HaltExecution(Dynarmic::HaltReason::UserDefined8);
    }

    void AddTicks(uint64_t ticks) override {}

    uint64_t GetTicksRemaining() override {
        return 1ull << 60;
    }
};

Dynarmic::ExclusiveMonitor DynarmicCPU::shared_monitor(MAX_CORE_COUNT);

std::unique_ptr<Dynarmic::A32::Jit> DynarmicCPU::make_jit() {
    Dynarmic::A32::UserConfig config{};
    config.arch_version = Dynarmic::A32::ArchVersion::v7;
    config.callbacks = cb.get();
    if (parent->mem->use_page_table) {
        config.page_table = (log_mem || !cpu_opt) ? nullptr : reinterpret_cast<decltype(config.page_table)>(parent->mem->page_table.get());
        config.absolute_offset_page_table = true;
    } else if (!log_mem && cpu_opt) {
        config.fastmem_pointer = std::bit_cast<uintptr_t>(parent->mem->memory.get());
    }
    config.hook_hint_instructions = true;
    config.global_monitor = &shared_monitor;
    config.coprocessors[15] = cp15;
    static const std::shared_ptr<ArmDynarmicNullCP> null_cp = std::make_shared<ArmDynarmicNullCP>();
    for (auto &coproc : config.coprocessors) {
        if (!coproc)
            coproc = null_cp;
    }
    config.processor_id = core_id;
    config.optimizations = cpu_opt ? Dynarmic::all_safe_optimizations : Dynarmic::no_optimizations;
    config.enable_cycle_counting = false;

    return std::make_unique<Dynarmic::A32::Jit>(config);
}

DynarmicCPU::DynarmicCPU(CPUState *state, std::size_t processor_id, bool cpu_opt)
    : parent(state)
    , cb(std::make_unique<ArmDynarmicCallback>(*state, *this))
    , cp15(std::make_shared<ArmDynarmicCP15>())
    , core_id(processor_id)
    , cpu_opt(cpu_opt) {
    jit = make_jit();
}

DynarmicCPU::~DynarmicCPU() = default;

int DynarmicCPU::run() {
    // apply deferred logging changes now safely outside of jit->Run()
    if (has_pending_log_code || has_pending_log_mem) {
        bool rebuild = false;
        if (has_pending_log_code) {
            has_pending_log_code = false;
            if (log_code != pending_log_code) {
                log_code = pending_log_code;
                rebuild = true;
            }
        }
        if (has_pending_log_mem) {
            has_pending_log_mem = false;
            if (log_mem != pending_log_mem) {
                log_mem = pending_log_mem;
                rebuild = true;
            }
        }
        if (rebuild)
            rebuild_jit();
    }
    halted = false;
    break_ = false;
    crashed = false;
    parent->svc_called = false;
    Dynarmic::HaltReason halt_reason;
    do {
        halt_reason = jit->Run();
    } while ((halt_reason == Dynarmic::HaltReason::Step) || (halt_reason == Dynarmic::HaltReason::CacheInvalidation));

    if (crashed)
        return -1;

    return halted;
}

int DynarmicCPU::step() {
    parent->svc_called = false;
    jit->Step();
    return 0;
}

bool DynarmicCPU::hit_breakpoint() {
    return break_;
}

void DynarmicCPU::trigger_breakpoint() {
    break_ = true;
    stop();
}

void DynarmicCPU::rebuild_jit() {
    const CPUContext ctx = save_context();
    jit = make_jit();
    load_context(ctx);
}

void DynarmicCPU::set_log_code(bool log) {
    if (log_code == log)
        return;

    pending_log_code = log;
    has_pending_log_code = true;
}

void DynarmicCPU::set_log_mem(bool log) {
    if (log_mem == log)
        return;

    pending_log_mem = log;
    has_pending_log_mem = true;
}

bool DynarmicCPU::get_log_code() {
    return log_code;
}

bool DynarmicCPU::get_log_mem() {
    return log_mem;
}

void DynarmicCPU::stop() {
    jit->HaltExecution();
}

uint32_t DynarmicCPU::get_reg(uint8_t idx) {
    return jit->Regs()[idx];
}

uint32_t DynarmicCPU::get_sp() {
    return jit->Regs()[13];
}

uint32_t DynarmicCPU::get_pc() {
    return jit->Regs()[15];
}

void DynarmicCPU::set_reg(uint8_t idx, uint32_t val) {
    jit->Regs()[idx] = val;
}

void DynarmicCPU::set_cpsr(uint32_t val) {
    jit->SetCpsr(val);
}

uint32_t DynarmicCPU::get_tpidruro() {
    return cp15->get_tpidruro();
}

void DynarmicCPU::set_tpidruro(uint32_t val) {
    cp15->set_tpidruro(val);
}

void DynarmicCPU::set_pc(uint32_t val) {
    if (val & 1) {
        set_cpsr(get_cpsr() | 0x20);
        val = val & 0xFFFFFFFE;
    } else {
        set_cpsr(get_cpsr() & 0xFFFFFFDF);
        val = val & 0xFFFFFFFC;
    }
    jit->Regs()[15] = val;
}

void DynarmicCPU::set_lr(uint32_t val) {
    jit->Regs()[14] = val;
}

void DynarmicCPU::set_sp(uint32_t val) {
    jit->Regs()[13] = val;
}

uint32_t DynarmicCPU::get_cpsr() {
    return jit->Cpsr();
}

uint32_t DynarmicCPU::get_fpscr() {
    return jit->Fpscr();
}

void DynarmicCPU::set_fpscr(uint32_t val) {
    jit->SetFpscr(val);
}

CPUContext DynarmicCPU::save_context() {
    CPUContext ctx;
    ctx.cpu_registers = jit->Regs();
    static_assert(sizeof(ctx.fpu_registers) == sizeof(jit->ExtRegs()));
    memcpy(ctx.fpu_registers.data(), jit->ExtRegs().data(), sizeof(ctx.fpu_registers));
    ctx.fpscr = jit->Fpscr();
    ctx.cpsr = jit->Cpsr();
    ctx.tpidruro = cp15->get_tpidruro();
    ctx.sctlr = cp15->get_sctlr();
    ctx.dacr = cp15->get_dacr();

    return ctx;
}

void DynarmicCPU::load_context(const CPUContext &ctx) {
    jit->Regs() = ctx.cpu_registers;
    static_assert(sizeof(ctx.fpu_registers) == sizeof(jit->ExtRegs()));
    memcpy(jit->ExtRegs().data(), ctx.fpu_registers.data(), sizeof(ctx.fpu_registers));
    jit->SetCpsr(ctx.cpsr);
    jit->SetFpscr(ctx.fpscr);
    cp15->set_tpidruro(ctx.tpidruro);
    cp15->set_sctlr(ctx.sctlr);
    cp15->set_dacr(ctx.dacr);
}

uint32_t DynarmicCPU::get_lr() {
    return jit->Regs()[14];
}

float DynarmicCPU::get_float_reg(uint8_t idx) {
    return std::bit_cast<float>(jit->ExtRegs()[idx]);
}

void DynarmicCPU::set_float_reg(uint8_t idx, float val) {
    jit->ExtRegs()[idx] = std::bit_cast<uint32_t>(val);
}

bool DynarmicCPU::is_thumb_mode() {
    return jit->Cpsr() & 0x20;
}

std::size_t DynarmicCPU::processor_id() const {
    return core_id;
}

void DynarmicCPU::invalidate_jit_cache(Address start, size_t length) {
    jit->InvalidateCacheRange(start, length);
}

void DynarmicCPU::clear_exclusive() {
    shared_monitor.ClearProcessor(core_id);
}
