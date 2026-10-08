// Validate the ARM32 import arguments that a logical continuation will replay.
// Reads only immutable staged memory; it never dispatches an import.
#pragma once
#include <algorithm>
#include <cstring>
#include <emucorev/savestate/memory_image.h>
#include <functional>
#include <kernel/thread/thread_state.h>
#include <limits>
#include <set>

namespace emucorev::savestate {
using SavedImportRead = std::function<bool(uint64_t, void *, size_t)>;
using SavedImportRange = std::function<bool(uint64_t, size_t)>;

inline bool validate_kernel_import_abi(const WaitContinuationSnapshot &wait,
    const SavedImportRange &contains, const SavedImportRead &read, std::string &error) {
    if (wait.operation != WaitOperation::creation_import && wait.operation != WaitOperation::callback_dispatch)
        return true;
    const auto fail = [&] { error = "invalid saved arguments for replayed kernel import"; return false; };
    const auto &context = wait.context;
    const auto arg = [&](unsigned index, uint32_t &value) {
        if (index < 4) {
            value = context.cpu_registers[index];
            return true;
        }
        // All whitelisted arguments are 32-bit words: no ABI doubleword padding.
        const uint64_t address = uint64_t{ context.get_sp() } + (index - 4) * 4;
        return contains(address, 4) && read(address, &value, 4);
    };
    const auto pointer = [&](unsigned index, size_t size, bool optional = true) {
        uint32_t address;
        return arg(index, address) && ((!address && optional) || (address && contains(address, size)));
    };
    const auto name_valid = [&](Address address) {
        if (!address)
            return false;
        // ThreadState::init copies a C string, so coverage alone is insufficient.
        for (uint64_t offset = 0; offset < 4096; ++offset) {
            char byte;
            if (!contains(uint64_t{ address } + offset, 1) || !read(uint64_t{ address } + offset, &byte, 1))
                return false;
            if (!byte)
                return true;
        }
        return false;
    };
    const uint32_t nid = wait.args[0];
    if (wait.operation == WaitOperation::creation_import) {
        if (!restartable_creation_import(nid) || !name_valid(context.cpu_registers[0]))
            return fail();
        const Address entry = context.cpu_registers[1];
        if (entry && !contains(entry & ~Address{ 1 }, 2))
            return fail();
        const int32_t priority = static_cast<int32_t>(context.cpu_registers[2]);
        if (priority > SCE_KERNEL_LOWEST_PRIORITY_USER
            && (priority < SCE_KERNEL_HIGHEST_DEFAULT_PRIORITY || priority > SCE_KERNEL_LOWEST_DEFAULT_PRIORITY))
            return fail();
        uint32_t stack_size = context.cpu_registers[3];
        if (nid == 0xC0FAF6A3U) {
            SceKernelCreateThread_opt options{};
            const auto address = context.cpu_registers[3];
            if (!address || !contains(address, sizeof(options)) || !read(address, &options, sizeof(options)))
                return fail();
            stack_size = static_cast<uint32_t>(options.stack_size);
            if (options.option && !contains(options.option.address(), sizeof(SceKernelThreadOptParam)))
                return fail();
        } else {
            // The bridge decodes three additional ABI words even when the driver
            // implementation ignores pOptParam. LibKernel also allocates options.
            uint32_t attr, affinity, option;
            if (!arg(4, attr) || !arg(5, affinity) || !arg(6, option))
                return fail();
            if (nid == 0xC5C11EE7U
                && ((option && !contains(option, sizeof(SceKernelThreadOptParam)))
                    || context.get_sp() < sizeof(SceKernelCreateThread_opt)
                    || !contains(uint64_t{ context.get_sp() } - sizeof(SceKernelCreateThread_opt), sizeof(SceKernelCreateThread_opt))))
                return fail();
        }
        // init aligns an int stack size before memset. Reject overflow/negative
        // sizes before invoking that path, without inventing a smaller guest cap.
        if (!stack_size || stack_size > uint32_t(std::numeric_limits<int32_t>::max() - 4095))
            return fail();
        return true;
    }
    bool valid = false;
    switch (nid) {
    case check_callback_import:
    case 0xCEA3FC52U:
    case 0x24460BB3U: // SignalCB currently ignores its scalar arguments.
    case 0x814C90AFU:
    case 0x3E796EF5U:
    case 0x78B41B92U:
    case 0x05F27764U:
        valid = true;
        break;
    case 0xDB9F5333U:
    case 0x2BDAA524U: // MutexCB / SemaCB
    case 0xF8E06784U:
    case 0x174692B4U:
        valid = pointer(2, 4);
        break;
    case 0x5D86D763U:
    case 0x2D4A62B7U: // Read/write RWLockCB / CondCB
    case 0xDBD09B09U:
    case 0xA4777082U:
    case 0x452B0AB3U:
    case 0x4CE42CE2U:
        valid = pointer(1, 4);
        break;
    case 0x7D483C33U:
    case 0xA0490795U: // EventCB: result, user data, stacked timeout.
        valid = pointer(2, 4) && pointer(3, 8) && pointer(4, 4);
        break;
    case 0x401E0C68U:
    case 0xE737B1DFU: // EventFlagCB
        valid = pointer(3, 4) && pointer(4, 4);
        break;
    case 0x72DBB96BU:
    case 0x8FA54B07U: // LwCondCB dereferences the work area after dispatch.
        valid = pointer(0, sizeof(SceKernelLwCondWork), false) && pointer(1, 4);
        break;
    case 0x3148C6B6U:
        valid = pointer(0, sizeof(SceKernelLwMutexWork)) && pointer(2, 4);
        break;
    case 0xFA3D4491U:
    case 0xC54941EDU:
        valid = pointer(1, 4) && pointer(2, 4);
        break;
    case 0x33AF829BU:
    case 0xA5CA74ACU: {
        uint32_t size;
        valid = arg(2, size) && pointer(1, size, size == 0) && pointer(4, 4) && pointer(5, 4);
        break;
    }
    default: break;
    }
    return valid || fail();
}

inline bool validate_kernel_import_abis(const std::vector<ThreadState::Snapshot> &threads,
    const MemoryImage &memory, std::string &error) {
    const auto contains = [&](uint64_t address, size_t size) { return memory.contains(address, size); };
    const auto read = [&](uint64_t address, void *out, size_t size) { return memory.read_bytes(address, out, size, error); };
    for (const auto &thread : threads)
        for (const auto &wait : thread.waits)
            if (!validate_kernel_import_abi(wait, contains, read, error))
                return false;
    return true;
}

inline bool validate_kernel_thread_allocations(const std::vector<ThreadState::Snapshot> &threads,
    uint32_t user_tls_size, const std::vector<MemorySpanInfo> &spans, std::string &error) {
    // ThreadState owns two independent alloc_block allocations. Coverage inside
    // a larger span is insufficient: its destructor frees that allocation head.
    std::set<Address> owned;
    const auto exact_allocation = [&](Address address, uint64_t size) {
        if (!address || address % kMemoryImagePageSize || !size || size > UINT32_MAX)
            return false;
        const uint64_t rounded = (size + kMemoryImagePageSize - 1) / kMemoryImagePageSize;
        if (!owned.insert(address).second)
            return false;
        const auto it = std::lower_bound(spans.begin(), spans.end(), address,
            [](const MemorySpanInfo &span, Address key) { return span.address < key; });
        return it != spans.end() && it->address == address && it->page_count == rounded;
    };
    for (const auto &thread : threads) {
        if (thread.stack_size <= 0
            || !exact_allocation(thread.stack_addr, static_cast<uint32_t>(thread.stack_size))
            || !exact_allocation(thread.tls_addr, uint64_t{ 0x800 } + user_tls_size)) {
            error = "saved thread stack or TLS does not own its exact memory allocation";
            return false;
        }
    }
    return true;
}

inline bool validate_kernel_thread_allocations(const std::vector<ThreadState::Snapshot> &threads,
    uint32_t user_tls_size, const MemoryImage &memory, std::string &error) {
    return validate_kernel_thread_allocations(threads, user_tls_size, memory.spans(), error);
}
} // namespace emucorev::savestate
