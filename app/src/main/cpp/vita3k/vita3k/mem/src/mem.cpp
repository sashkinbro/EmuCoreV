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

#include <mem/functions.h>
#include <mem/state.h>

#include <util/align.h>
#include <util/log.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <csignal>
#include <dlfcn.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <unistd.h>
#include <unwind.h>
#endif

constexpr uint32_t STANDARD_PAGE_SIZE = KiB(4);
constexpr size_t TOTAL_MEM_SIZE = GiB(4);
constexpr bool LOG_PROTECT = false;
#ifdef NDEBUG
constexpr bool PAGE_NAME_TRACKING = false;
#else
constexpr bool PAGE_NAME_TRACKING = true;
#endif

// TODO: support multiple handlers
static AccessViolationHandler access_violation_handler;
static void register_access_violation_handler(const AccessViolationHandler &handler);

static Address alloc_inner(MemState &state, uint32_t start_page, uint32_t page_count, const char *name, const bool force);
static void delete_memory(uint8_t *memory);

#ifdef _WIN32
static std::string get_error_msg() {
    return std::system_category().message(GetLastError());
}
#else
static std::string get_error_msg() {
    return strerror(errno);
}
#endif

bool init(MemState &state, const bool use_page_table) {
#ifdef _WIN32
    SYSTEM_INFO system_info = {};
    GetSystemInfo(&system_info);
    state.host_page_size = system_info.dwPageSize;
#else
    state.host_page_size = static_cast<int>(sysconf(_SC_PAGESIZE));
#endif

    assert(state.host_page_size >= 4096); // Limit imposed by Unicorn.

    void *preferred_address = reinterpret_cast<void *>(1ULL << 34);

#ifdef _WIN32
    state.memory = Memory(static_cast<uint8_t *>(VirtualAlloc(preferred_address, TOTAL_MEM_SIZE, MEM_RESERVE, PAGE_NOACCESS)), delete_memory);
    if (!state.memory) {
        // fallback
        state.memory = Memory(static_cast<uint8_t *>(VirtualAlloc(nullptr, TOTAL_MEM_SIZE, MEM_RESERVE, PAGE_NOACCESS)), delete_memory);

        if (!state.memory) {
            LOG_CRITICAL("VirtualAlloc failed: {}", get_error_msg());
            return false;
        }
    }
#else
    // http://man7.org/linux/man-pages/man2/mmap.2.html
    const int prot = PROT_NONE;
    const int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    const int fd = 0;
    const off_t offset = 0;
    // preferred_address is only a hint for mmap, if it can't use it, the kernel will choose itself the address
    state.memory = Memory(static_cast<uint8_t *>(mmap(preferred_address, TOTAL_MEM_SIZE, prot, flags, fd, offset)), delete_memory);
    if (state.memory.get() == MAP_FAILED) {
        LOG_CRITICAL("mmap failed {}", get_error_msg());
        return false;
    }
#endif

    const size_t table_length = TOTAL_MEM_SIZE / STANDARD_PAGE_SIZE;
    state.alloc_table = AllocPageTable(new AllocMemPage[table_length]);
    memset(state.alloc_table.get(), 0, sizeof(AllocMemPage) * table_length);

    state.allocator.set_maximum(table_length);

    const auto handler = [&state](uint8_t *addr, bool write) noexcept {
        return handle_access_violation(state, addr, write);
    };
    register_access_violation_handler(handler);

    const Address null_address = alloc_inner(state, 0, state.host_page_size / STANDARD_PAGE_SIZE, "null", true);
    assert(null_address == 0);
#ifdef _WIN32
    DWORD old_protect = 0;
    const BOOL ret = VirtualProtect(state.memory.get(), state.host_page_size, PAGE_NOACCESS, &old_protect);
    LOG_CRITICAL_IF(!ret, "VirtualAlloc failed: {}", get_error_msg());
#else
    const int ret = mprotect(state.memory.get(), state.host_page_size, PROT_NONE);
    LOG_CRITICAL_IF(ret == -1, "mprotect failed: {}", get_error_msg());
#endif

    state.use_page_table = use_page_table;
    if (use_page_table) {
        state.page_table = PageTable(new PagePtr[TOTAL_MEM_SIZE / KiB(4)]);
        // we use an absolute offset (it is faster), so each entry is the same
        std::fill_n(state.page_table.get(), TOTAL_MEM_SIZE / KiB(4), state.memory.get());
    }

    return true;
}

static void delete_memory(uint8_t *memory) {
    if (memory != nullptr) {
#ifdef _WIN32
        const BOOL ret = VirtualFree(memory, 0, MEM_RELEASE);
        assert(ret);
#else
        munmap(memory, TOTAL_MEM_SIZE);
#endif
    }
}

bool is_valid_addr(const MemState &state, Address addr) {
    const uint32_t page_num = addr / STANDARD_PAGE_SIZE;
    return addr && state.allocator.free_slot_count(page_num, page_num + 1) == 0;
}

// re-check under the allocator's writer lock
bool is_valid_addr_synced(MemState &state, Address addr) {
    if (is_valid_addr(state, addr))
        return true;
    const std::lock_guard<std::mutex> lock(state.generation_mutex);
    return is_valid_addr(state, addr);
}

bool is_valid_addr_range(const MemState &state, Address start, Address end) {
    const uint32_t start_page = start / STANDARD_PAGE_SIZE;
    const uint32_t end_page = (end + STANDARD_PAGE_SIZE - 1) / STANDARD_PAGE_SIZE;
    return state.allocator.free_slot_count(start_page, end_page) == 0;
}

bool debug_safe_copy_guest(const MemState &state, Address addr, void *dst, uint32_t size) {
    if (!addr || addr + size < addr)
        return false;
#ifdef _WIN32
    __try {
        memcpy(dst, &state.memory[addr], size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    if (!is_valid_addr_range(state, addr, addr + size))
        return false;
    memcpy(dst, &state.memory[addr], size);
    return true;
#endif
}

bool debug_safe_write_guest(MemState &state, Address addr, const void *src, uint32_t size) {
    if (!addr || addr + size < addr)
        return false;
#ifdef _WIN32
    __try {
        memcpy(&state.memory[addr], src, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    if (!is_valid_addr_range(state, addr, addr + size))
        return false;
    memcpy(&state.memory[addr], src, size);
    return true;
#endif
}

static Address alloc_inner(MemState &state, uint32_t start_page, uint32_t page_count, const char *name, const bool force) {
    int page_num;
    if (force) {
        if (state.allocator.allocate_at(start_page, page_count) < 0) {
            return 0;
        }
        page_num = start_page;
    } else {
        page_num = state.allocator.allocate_from(start_page, page_count, false);
        if (page_num < 0)
            return 0;
    }

    const uint32_t size = page_count * STANDARD_PAGE_SIZE;
    const Address addr = page_num * STANDARD_PAGE_SIZE;

    const Address commit_start = align_down(addr, state.host_page_size);
    const Address commit_end = align(addr + size, state.host_page_size);
    const uint32_t commit_size = commit_end - commit_start;
    uint8_t *const commit_ptr = &state.memory[commit_start];

    // Make memory chunk available to access
#ifdef _WIN32
    const void *const ret = VirtualAlloc(commit_ptr, commit_size, MEM_COMMIT, PAGE_READWRITE);
    LOG_CRITICAL_IF(!ret, "VirtualAlloc failed: {}", get_error_msg());
#else
    const int ret = mprotect(commit_ptr, commit_size, PROT_READ | PROT_WRITE);
    LOG_CRITICAL_IF(ret == -1, "mprotect failed: {}", get_error_msg());
#endif
    std::memset(&state.memory[addr], 0, size);

    AllocMemPage &page = state.alloc_table[page_num];
    assert(!page.allocated);
    page.allocated = 1;
    page.size = page_count;

    if (PAGE_NAME_TRACKING) {
        state.page_name_map.emplace(page_num, name);
    }

    return addr;
}

Address alloc_aligned(MemState &state, uint32_t size, const char *name, unsigned int alignment, Address start_addr) {
    if (alignment == 0)
        return alloc(state, size, name, start_addr);
    const std::lock_guard<std::mutex> lock(state.generation_mutex);
    size += alignment;
    const uint32_t page_count = align(size, STANDARD_PAGE_SIZE) / STANDARD_PAGE_SIZE;
    const Address addr = alloc_inner(state, start_addr / STANDARD_PAGE_SIZE, page_count, name, false);
    const Address align_addr = align(addr, alignment);
    const uint32_t page_num = addr / STANDARD_PAGE_SIZE;
    const uint32_t align_page_num = align_addr / STANDARD_PAGE_SIZE;

    if (page_num != align_page_num) {
        AllocMemPage &page = state.alloc_table[page_num];
        AllocMemPage &align_page = state.alloc_table[align_page_num];
        const uint32_t remnant_front = align_page_num - page_num;
        state.allocator.free(page_num, remnant_front);
        page.allocated = 0;
        align_page.allocated = 1;
        align_page.size = page.size - remnant_front;
    }

    return align_addr;
}

static void align_to_page(MemState &state, Address &addr, Address &size) {
    const Address end = align(addr + size, STANDARD_PAGE_SIZE);
    addr = align_down(addr, STANDARD_PAGE_SIZE);
    size = end - addr;
}

static bool apply_host_protect(uint8_t *target, size_t size, const MemPerm perm, size_t host_page_size) {
    uint8_t *aligned_start = reinterpret_cast<uint8_t *>(
        align_down(reinterpret_cast<uintptr_t>(target), host_page_size));
    uint8_t *aligned_end = reinterpret_cast<uint8_t *>(align(reinterpret_cast<uintptr_t>(target + size), host_page_size));
    size_t aligned_size = aligned_end - aligned_start;

#ifdef _WIN32
    DWORD old_protect = 0;
    const BOOL ret = VirtualProtect(aligned_start, aligned_size, (perm == MemPerm::None) ? PAGE_NOACCESS : ((perm == MemPerm::ReadOnly) ? PAGE_READONLY : PAGE_READWRITE), &old_protect);
    LOG_CRITICAL_IF(!ret, "VirtualProtect failed: {}", get_error_msg());
    return ret != 0;
#else
    const int ret = mprotect(aligned_start, aligned_size, (perm == MemPerm::None) ? PROT_NONE : ((perm == MemPerm::ReadOnly) ? PROT_READ : (PROT_READ | PROT_WRITE)));
    LOG_CRITICAL_IF(ret == -1, "mprotect failed: {}", get_error_msg());
    return ret != -1;
#endif
}

static void release_external_shadow_pages(uint8_t *target, size_t size, size_t host_page_size) {
    constexpr bool release_shadow_pages_when_mapped = true;
    if (!release_shadow_pages_when_mapped)
        return;
#ifndef _WIN32
    // Windows deliberately excluded here
    uint8_t *inner_start = reinterpret_cast<uint8_t *>(align(reinterpret_cast<uintptr_t>(target), host_page_size));
    uint8_t *inner_end = reinterpret_cast<uint8_t *>(align_down(reinterpret_cast<uintptr_t>(target + size), host_page_size));
    if (inner_end <= inner_start)
        return;

    const size_t inner_size = static_cast<size_t>(inner_end - inner_start);
    if (madvise(inner_start, inner_size, MADV_DONTNEED) == -1) {
        LOG_WARN_ONCE("madvise(MADV_DONTNEED) failed releasing externally mapped pages: {}", get_error_msg());
        return;
    }

    static std::atomic<uint64_t> released_total{ 0 };
    const uint64_t before = released_total.fetch_add(inner_size, std::memory_order_relaxed);
    const uint64_t after = before + inner_size;
    constexpr uint64_t step = MiB(64);
    if ((before / step) != (after / step))
        LOG_INFO("Released {} MiB of arena pages shadowed by external mappings", after / MiB(1));
#else
    (void)target;
    (void)size;
    (void)host_page_size;
#endif
}

void unprotect_inner(MemState &state, Address addr, uint32_t size) {
    if (LOG_PROTECT) {
        fmt::print("Unprotect: {} {}\n", log_hex(addr), size);
    }
    uint8_t *addr_ptr = state.use_page_table ? state.page_table[addr / KiB(4)] : state.memory.get();
    apply_host_protect(&addr_ptr[addr], size, MemPerm::ReadWrite, state.host_page_size);
}

void protect_inner(MemState &state, Address addr, uint32_t size, const MemPerm perm) {
    uint8_t *addr_ptr = state.use_page_table ? state.page_table[addr / KiB(4)] : state.memory.get();
    apply_host_protect(&addr_ptr[addr], size, perm, state.host_page_size);
}

std::string (*g_fault_context_provider)() = nullptr;

void set_fault_context_provider(std::string (*provider)()) {
    g_fault_context_provider = provider;
}

bool handle_access_violation(MemState &state, uint8_t *addr, bool write) noexcept {
    const uintptr_t memory_addr = reinterpret_cast<uintptr_t>(state.memory.get());
    const uintptr_t fault_addr = reinterpret_cast<uintptr_t>(addr);

    Address vaddr = 0;
    const std::unique_lock<std::mutex> lock(state.protect_mutex);
    if (fault_addr < memory_addr || fault_addr >= memory_addr + TOTAL_MEM_SIZE) {
        if (state.use_page_table) {
            // this may come from an external mapping
            uint64_t addr_val = std::bit_cast<uint64_t>(addr);
            auto it = state.external_mapping.lower_bound(addr_val);
            if (it != state.external_mapping.end() && addr_val < it->first + it->second.size) {
                vaddr = static_cast<Address>(addr_val - it->first + it->second.address);
            } else {
                return false;
            }
        } else {
            return false;
        }
    } else {
        vaddr = static_cast<Address>(fault_addr - memory_addr);
    }

    if (!is_valid_addr(state, vaddr)) {
        return false;
    }
    if (LOG_PROTECT) {
        fmt::print("Access: {}\n", log_hex(vaddr));
    }

    // never allow accesses to the null guard page - letting them through silently corrupts low memory
    if (vaddr < 0x1000) {
        return false;
    }

    // HACK: keep going recovery for faults with no covering protect_tree entry
    const auto unhandled_but_valid = [&]() {
        apply_host_protect(reinterpret_cast<uint8_t *>(align_down(fault_addr, state.host_page_size)),
            state.host_page_size, MemPerm::ReadWrite, state.host_page_size);
        static std::atomic<uint32_t> count{ 0 };
        const uint32_t n = count.fetch_add(1, std::memory_order_relaxed);
        if (n < 8) {
            LOG_CRITICAL("Unhandled {} to protected-but-valid region. vaddr=0x{:X} host=0x{:X}{}",
                write ? "write" : "read", vaddr, fault_addr,
                g_fault_context_provider ? g_fault_context_provider() : std::string());
        } else if (n == 8)
            LOG_CRITICAL("Further unhandled protected-region accesses will be suppressed");
    };

    auto it = state.protect_tree.lower_bound(vaddr);
    if (it == state.protect_tree.end()) {
        unhandled_but_valid();
        return true;
    }

    ProtectSegmentInfo &info = it->second;
    if (vaddr < it->first || vaddr >= it->first + info.size) {
        unhandled_but_valid();
        return true;
    }

    Address previous_beg = it->first;
    for (auto &[block_addr, block] : info.blocks) {
        block.callback(vaddr, write);
    }

    unprotect_inner(state, it->first, info.size);
    state.protect_tree.erase(it);

    return true;
}

Address host_to_guest(const MemState &state, const void *host) {
    if (host == nullptr)
        return 0;

    const uint64_t host_val = std::bit_cast<uint64_t>(host);
    const uint64_t memory_base = std::bit_cast<uint64_t>(state.memory.get());

    // Fast path: the pointer is inside the linear guest-RAM window. Covers most conversions and takes no lock
    if (host_val >= memory_base && host_val < memory_base + TOTAL_MEM_SIZE)
        return static_cast<Address>(host_val - memory_base);

    // Otherwise the pointer lives inside an external GPU mapping whose pages were pointed away
    if (state.use_page_table) {
        const std::lock_guard<std::mutex> ext_lock(state.external_mapping_mutex);
        auto it = state.external_mapping.lower_bound(host_val);
        if (it != state.external_mapping.end() && host_val < it->first + it->second.size)
            return it->second.address + static_cast<Address>(host_val - it->first);
    }

    // Fallback: linear (likely a bogus pointer?)
    return static_cast<Address>(host_val - memory_base);
}

bool add_protect(MemState &state, Address addr, const uint32_t size, const MemPerm perm, const ProtectCallback &callback) {
    const std::lock_guard<std::mutex> lock(state.protect_mutex);
    ProtectSegmentInfo protect(size, perm);
    align_to_page(state, addr, protect.size);

    ProtectBlockInfo block;
    block.size = size;
    block.callback = callback;

    protect.blocks.emplace(addr, std::move(block));

    auto it = state.protect_tree.lower_bound(addr);
    if (it == state.protect_tree.end() || it->first + it->second.size <= addr) {
        if (it == state.protect_tree.begin())
            it = state.protect_tree.end();
        else
            --it;
    }

    while (it != state.protect_tree.end() && it->first < addr + size) {
        const Address start = std::min(it->first, addr);
        protect.size = std::max(it->first + it->second.size, addr + protect.size) - start;
        addr = start;
        protect.blocks.merge(it->second.blocks); // transfer blocks to the new protect
        protect.perm = most_restrictive_perm(protect.perm, it->second.perm);

        if (it == state.protect_tree.begin()) {
            state.protect_tree.erase(it);
            break;
        }

        // protect tree is in reverse order, so decrease it
        state.protect_tree.erase(it--);
    }

    protect_inner(state, addr, protect.size, protect.perm);

    state.protect_tree.emplace(addr, std::move(protect));
    return true;
}

bool is_protecting(MemState &state, Address addr, MemPerm *perm) {
    const std::lock_guard<std::mutex> lock(state.protect_mutex);
    auto ite = state.protect_tree.lower_bound(addr);

    if (ite != state.protect_tree.end() && addr < ite->first + ite->second.size) {
        if (perm)
            *perm = ite->second.perm;

        return true;
    }

    return false;
}

void add_external_mapping(MemState &mem, Address addr, uint32_t size, uint8_t *addr_ptr) {
    assert((size & 4095) == 0);
    if (!mem.use_page_table)
        return;

    uint64_t addr_value = std::bit_cast<uint64_t>(addr_ptr);
    uint8_t *page_table_entry = addr_ptr - addr;
    uint8_t *original_address = &mem.memory[addr];

    const std::unique_lock<std::shared_mutex> transition_lock(mem.external_transition_mutex);

    memcpy(addr_ptr, original_address, size);

    int verify_pass = 0;
    for (; verify_pass < 4; verify_pass++) {
        int recopied = 0;
        for (uint32_t off = 0; off < size; off += KiB(4)) {
            if (memcmp(addr_ptr + off, original_address + off, KiB(4)) != 0) {
                memcpy(addr_ptr + off, original_address + off, KiB(4));
                recopied++;
            }
        }
        if (recopied == 0)
            break;
        LOG_WARN("add_external_mapping 0x{:X} size 0x{:X}: verify pass {} re-copied {} page(s) changed by a concurrent writer", addr, size, verify_pass, recopied);
    }

    std::atomic_thread_fence(std::memory_order_release);
    for (uint32_t block = 0; block < size / KiB(4); block++)
        mem.page_table[addr / KiB(4) + block] = page_table_entry;

    apply_host_protect(original_address, size, MemPerm::None, mem.host_page_size);
    release_external_shadow_pages(original_address, size, mem.host_page_size);

    const std::unique_lock<std::mutex> lock(mem.protect_mutex);
    const std::lock_guard<std::mutex> ext_lock(mem.external_mapping_mutex);
    mem.external_mapping[addr_value] = { addr, size };
}

void remove_external_mapping(MemState &mem, uint8_t *addr_ptr, uint32_t size) {
    uint64_t addr_value = std::bit_cast<uint64_t>(addr_ptr);
    MemExternalMapping mapping;
    if (mem.use_page_table) {
        const std::unique_lock<std::mutex> lock(mem.protect_mutex);
        const std::lock_guard<std::mutex> ext_lock(mem.external_mapping_mutex);
        auto it = mem.external_mapping.find(addr_value);
        if (it == mem.external_mapping.end()) {
            LOG_ERROR("[EXTMAP] remove MISS key=0x{:X} size=0x{:X}: entry already gone (was crashing via end() deref); {} live entries:", addr_value, size, mem.external_mapping.size());
            int shown = 0;
            for (const auto &kv : mem.external_mapping) {
                if (shown++ >= 16) {
                    LOG_ERROR("[EXTMAP]   ... ({} more)", mem.external_mapping.size() - 16);
                    break;
                }
                LOG_ERROR("[EXTMAP]   live key=0x{:X} guest=0x{:X} size=0x{:X}", kv.first, kv.second.address, kv.second.size);
            }
            return;
        }

        mapping = it->second;
        mem.external_mapping.erase(it);
    } else {
        mapping.address = static_cast<Address>(addr_ptr - mem.memory.get());
        mapping.size = size;
    }

    // remove all protections on this range
    unprotect_inner(mem, mapping.address, mapping.size);
    {
        const std::unique_lock<std::mutex> lock(mem.protect_mutex);
        auto prot_it = mem.protect_tree.lower_bound(mapping.address);
        if (prot_it->first + prot_it->second.size <= mapping.address) {
            if (prot_it == mem.protect_tree.begin())
                prot_it = mem.protect_tree.end();
            else
                --prot_it;
        }

        while (prot_it != mem.protect_tree.end() && prot_it->first < mapping.address + mapping.size) {
            if (prot_it == mem.protect_tree.begin()) {
                mem.protect_tree.erase(prot_it);
                break;
            }

            mem.protect_tree.erase(prot_it--);
        }
    }

    if (mem.use_page_table) {
        const std::unique_lock<std::shared_mutex> transition_lock(mem.external_transition_mutex);

        uint8_t *arena = &mem.memory[mapping.address];
        // The guest can free this memblock while the unmap is still deferred
        if (apply_host_protect(arena, mapping.size, MemPerm::ReadWrite, mem.host_page_size)) {
            memcpy(arena, addr_ptr, mapping.size);
            for (int verify_pass = 0; verify_pass < 4; verify_pass++) {
                int recopied = 0;
                for (uint32_t off = 0; off < mapping.size; off += KiB(4)) {
                    if (memcmp(arena + off, addr_ptr + off, KiB(4)) != 0) {
                        memcpy(arena + off, addr_ptr + off, KiB(4));
                        recopied++;
                    }
                }
                if (recopied == 0)
                    break;
                LOG_WARN("remove_external_mapping 0x{:X} size 0x{:X}: verify pass {} re-copied {} page(s) changed by a concurrent writer", mapping.address, mapping.size, verify_pass, recopied);
            }
        } else {
            LOG_WARN("remove_external_mapping 0x{:X} size 0x{:X}: arena decommitted (guest freed it under a deferred unmap) — skipping copy-back", mapping.address, mapping.size);
        }

        std::atomic_thread_fence(std::memory_order_release);
        for (uint32_t block = 0; block < mapping.size / KiB(4); block++)
            mem.page_table[mapping.address / KiB(4) + block] = mem.memory.get();
    }
}

Address alloc(MemState &state, uint32_t size, const char *name, Address start_addr) {
    const std::lock_guard<std::mutex> lock(state.generation_mutex);
    const uint32_t page_count = align(size, STANDARD_PAGE_SIZE) / STANDARD_PAGE_SIZE;
    const Address addr = alloc_inner(state, start_addr / STANDARD_PAGE_SIZE, page_count, name, false);
    return addr;
}

Address alloc_at(MemState &state, Address address, uint32_t size, const char *name) {
    auto addr = try_alloc_at(state, address, size, name);
    LOG_CRITICAL_IF(addr == 0, "Failed to allocate at specific page. Memory address:{}, size:{}, name:{}", log_hex(address), log_hex(size), name);
    return addr;
}

Address try_alloc_at(MemState &state, Address address, uint32_t size, const char *name) {
    const std::lock_guard<std::mutex> lock(state.generation_mutex);
    const uint32_t wanted_page = address / STANDARD_PAGE_SIZE;
    size += address % STANDARD_PAGE_SIZE;
    const uint32_t page_count = align(size, STANDARD_PAGE_SIZE) / STANDARD_PAGE_SIZE;
    const Address addr = alloc_inner(state, wanted_page, page_count, name, true);
    return addr ? address : 0;
}

Block alloc_block(MemState &mem, uint32_t size, const char *name, Address start_addr) {
    const Address address = alloc(mem, size, name, start_addr);
    return Block(address, [&mem](Address stack) {
        free(mem, stack);
    });
}

void free(MemState &state, Address address) {
    const std::lock_guard<std::mutex> lock(state.generation_mutex);
    const uint32_t page_num = address / STANDARD_PAGE_SIZE;
    assert(page_num >= 0);

    if (state.alloc_table == nullptr) {
        LOG_CRITICAL("Freeing unallocated alloc_table");
        return;
    }

    AllocMemPage &page = state.alloc_table[page_num];
    if (!page.allocated) {
        // continuing would free page.size stale pages in the bitmap, wiping ranges that may since have been re-allocated
        LOG_CRITICAL("Freeing unallocated page at addr 0x{:X} (stale size {} pages) — ignored", address, static_cast<uint32_t>(page.size));
        return;
    }
    page.allocated = 0;

    state.allocator.free(page_num, page.size);
    if (PAGE_NAME_TRACKING) {
        state.page_name_map.erase(page_num);
    }

    assert(!state.use_page_table || state.page_table[address / KiB(4)] == state.memory.get());
    const Address region_start = page_num * STANDARD_PAGE_SIZE;
    const Address region_end = region_start + page.size * STANDARD_PAGE_SIZE;

    if (!state.preserve_freed_pages) {
        Address host_page = align_down(region_start, state.host_page_size);
        Address batch_start = 0;
        uint32_t batch_size = 0;

        while (host_page < region_end) {
            Address host_page_end = host_page + state.host_page_size;
            uint32_t first_guest = host_page / STANDARD_PAGE_SIZE;
            uint32_t last_guest = host_page_end / STANDARD_PAGE_SIZE;

            if (state.allocator.free_slot_count(first_guest, last_guest) == (last_guest - first_guest)) {
                if (batch_size == 0)
                    batch_start = host_page;
                batch_size += state.host_page_size;
            } else if (batch_size > 0) {
                uint8_t *memory = &state.memory[batch_start];
#ifdef _WIN32
                const BOOL ret = VirtualFree(memory, batch_size, MEM_DECOMMIT);
                LOG_CRITICAL_IF(!ret, "VirtualFree failed: {}", get_error_msg());
#else
                int ret = mprotect(memory, batch_size, PROT_NONE);
                LOG_CRITICAL_IF(ret == -1, "mprotect failed: {}", get_error_msg());
                ret = madvise(memory, batch_size, MADV_DONTNEED);
                LOG_CRITICAL_IF(ret == -1, "madvise failed: {}", get_error_msg());
#endif
                batch_size = 0;
            }
            host_page = host_page_end;
        }

        if (batch_size > 0) {
            uint8_t *memory = &state.memory[batch_start];
#ifdef _WIN32
            const BOOL ret = VirtualFree(memory, batch_size, MEM_DECOMMIT);
            LOG_CRITICAL_IF(!ret, "VirtualFree failed: {}", get_error_msg());
#else
            int ret = mprotect(memory, batch_size, PROT_NONE);
            LOG_CRITICAL_IF(ret == -1, "mprotect failed: {}", get_error_msg());
            ret = madvise(memory, batch_size, MADV_DONTNEED);
            LOG_CRITICAL_IF(ret == -1, "madvise failed: {}", get_error_msg());
#endif
        }
    }
}

uint32_t mem_available(MemState &state) {
    return state.allocator.free_slot_count(0, state.allocator.max_offset) * STANDARD_PAGE_SIZE;
}

const char *mem_name(Address address, MemState &state) {
    if (PAGE_NAME_TRACKING) {
        return state.page_name_map.find(address / STANDARD_PAGE_SIZE)->second.c_str();
    }
    return "";
}

void deinit_mem(MemState &state) {
    const std::lock_guard<std::mutex> gen_lock(state.generation_mutex);

    {
        const std::lock_guard<std::mutex> prot_lock(state.protect_mutex);
        state.protect_tree.clear();
    }

    state.memory.reset();
    state.alloc_table.reset();
    state.allocator.reset();
    state.page_name_map.clear();
    state.page_table.reset();
    state.external_mapping.clear();
    state.use_page_table = false;
    state.host_page_size = 0;
}

#ifdef _WIN32

static LONG WINAPI exception_handler(PEXCEPTION_POINTERS pExp) noexcept {
    if (pExp->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT && IsDebuggerPresent()) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const auto ptr = reinterpret_cast<uint8_t *>(pExp->ExceptionRecord->ExceptionInformation[1]);
    const bool is_writing = pExp->ExceptionRecord->ExceptionInformation[0] == 1;
    const bool is_executing = pExp->ExceptionRecord->ExceptionInformation[0] == 8;

    if (pExp->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && !is_executing) {
        if (access_violation_handler(ptr, is_writing)) {
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

static void register_access_violation_handler(const AccessViolationHandler &handler) {
    access_violation_handler = handler;
    if (!AddVectoredExceptionHandler(1, exception_handler)) {
        LOG_CRITICAL("Failed to register an exception handler");
    }
}

#else

static thread_local sigjmp_buf t_fault_probe_jmp;
static thread_local volatile bool t_fault_probe_active = false;

static uintptr_t extract_fault_pc(ucontext_t *context) {
#if defined(__aarch64__)
#if defined(__APPLE__)
    return static_cast<uintptr_t>(context->uc_mcontext->__ss.__pc);
#else
    return static_cast<uintptr_t>(context->uc_mcontext.pc);
#endif
#elif defined(__x86_64__)
#if defined(__APPLE__)
    return static_cast<uintptr_t>(context->uc_mcontext->__ss.__rip);
#else
    return static_cast<uintptr_t>(context->uc_mcontext.gregs[REG_RIP]);
#endif
#else
    (void)context;
    return 0;
#endif
}

static uintptr_t extract_fault_lr(ucontext_t *context) {
#if defined(__aarch64__)
#if defined(__APPLE__)
    return static_cast<uintptr_t>(context->uc_mcontext->__ss.__lr);
#else
    return static_cast<uintptr_t>(context->uc_mcontext.regs[30]);
#endif
#else
    (void)context;
    return 0;
#endif
}

static void signal_handler(int sig, siginfo_t *info, void *uct) noexcept {
    auto context = static_cast<ucontext_t *>(uct);

    if (t_fault_probe_active)
        siglongjmp(t_fault_probe_jmp, sig);

    static thread_local int handler_depth = 0;
    if (handler_depth >= 1) {
        signal(sig, SIG_DFL);
        return;
    }
    handler_depth++;
    struct DepthGuard {
        int &depth;
        ~DepthGuard() { --depth; }
    } depth_guard{ handler_depth };

#ifdef __ANDROID__
    if (sig == SIGBUS) {
        const uintptr_t pc = extract_fault_pc(context);
        const uintptr_t lr = extract_fault_lr(context);
        const char *pc_module = "?";
        uintptr_t pc_offset = 0;
        Dl_info dl_pc{};
        if (pc != 0 && dladdr(reinterpret_cast<void *>(pc), &dl_pc) != 0 && dl_pc.dli_fname != nullptr) {
            pc_module = dl_pc.dli_fname;
            pc_offset = pc - reinterpret_cast<uintptr_t>(dl_pc.dli_fbase);
        }
        LOG_CRITICAL("[SIGBUS] si_code {} at address 0x{:X} - pc 0x{:X} = {}+0x{:X} lr 0x{:X} - reported BEFORE recovery; if the log ends here, recovery re-faulted",
            info->si_code, reinterpret_cast<uintptr_t>(info->si_addr), pc, pc_module, pc_offset, lr);
        logging::flush();
    }
#endif

    if (sig != SIGABRT && sig != SIGILL) {
#ifdef __aarch64__
#ifdef __APPLE__
        const uint32_t esr = context->uc_mcontext->__es.__esr;
#else
        uint64_t esr = 0;
        bool have_esr = false;
        _aarch64_ctx *ctx = reinterpret_cast<_aarch64_ctx *>(context->uc_mcontext.__reserved);
        while (ctx->magic != 0) {
            if (ctx->magic == ESR_MAGIC) {
                esr = reinterpret_cast<esr_context *>(ctx)->esr;
                have_esr = true;
                break;
            }
            ctx = reinterpret_cast<_aarch64_ctx *>(reinterpret_cast<uint8_t *>(ctx) + ctx->size);
        }
#endif
        // https://developer.arm.com/documentation/ddi0595/2021-03/AArch64-Registers/ESR-EL1--Exception-Syndrome-Register--EL1-
#ifdef __APPLE__
        constexpr bool have_esr = true;
#endif
        const uint32_t exception_class = have_esr ? (static_cast<uint32_t>(esr) >> 26) : 0;
        const bool is_executing = have_esr && ((exception_class == 0b100000) || (exception_class == 0b100001));
        const bool is_data_abort = (exception_class == 0b100100) || (exception_class == 0b100101);
        const bool is_writing = is_data_abort && (esr & (1 << 6));
#else
#ifdef __APPLE__
        const uint64_t err = context->uc_mcontext->__es.__err;
#else
        const uint64_t err = context->uc_mcontext.gregs[REG_ERR];
#endif
        const bool is_executing = err & 0x10;
        const bool is_writing = err & 0x2;
#endif

        if (!is_executing) {
            if (access_violation_handler(reinterpret_cast<uint8_t *>(info->si_addr), is_writing)) {
                return;
            }
        }
    }

    // Genuine crash so record it in our log and flush as users can rarely logcat.
    uintptr_t crash_pc = 0;
#if defined(__aarch64__)
#if defined(__APPLE__)
    crash_pc = static_cast<uintptr_t>(context->uc_mcontext->__ss.__pc);
#else
    crash_pc = static_cast<uintptr_t>(context->uc_mcontext.pc);
#endif
#elif defined(__x86_64__)
#if defined(__APPLE__)
    crash_pc = static_cast<uintptr_t>(context->uc_mcontext->__ss.__rip);
#else
    crash_pc = static_cast<uintptr_t>(context->uc_mcontext.gregs[REG_RIP]);
#endif
#endif

    uintptr_t crash_lr = 0;
#if defined(__aarch64__) && !defined(__APPLE__)
    crash_lr = static_cast<uintptr_t>(context->uc_mcontext.regs[30]);
#elif defined(__aarch64__)
    crash_lr = static_cast<uintptr_t>(context->uc_mcontext->__ss.__lr);
#endif

    auto describe = [](uintptr_t addr) -> std::string {
        if (addr == 0)
            return "null";
        Dl_info info{};
        if (dladdr(reinterpret_cast<void *>(addr), &info) == 0 || info.dli_fname == nullptr)
            return fmt::format("0x{:X} (unmapped)", addr);
        const uintptr_t off = addr - reinterpret_cast<uintptr_t>(info.dli_fbase);
        if (info.dli_sname != nullptr)
            return fmt::format("0x{:X} = {}+0x{:X} ({})", addr, info.dli_fname, off, info.dli_sname);
        return fmt::format("0x{:X} = {}+0x{:X}", addr, info.dli_fname, off);
    };

    LOG_CRITICAL("[CRASH] fatal signal {} (si_code {}) at address 0x{:X} - pc {} - lr {} - flushing log and aborting",
        sig, info->si_code, reinterpret_cast<uintptr_t>(info->si_addr), describe(crash_pc), describe(crash_lr));
#ifndef _WIN32
    {
        struct Bt {
            uintptr_t pcs[28];
            int n = 0;
        } bt;
        _Unwind_Backtrace([](struct _Unwind_Context *uctx, void *arg) -> _Unwind_Reason_Code {
            Bt *b = static_cast<Bt *>(arg);
            const uintptr_t pc = _Unwind_GetIP(uctx);
            if (pc && b->n < 28)
                b->pcs[b->n++] = pc;
            return b->n >= 28 ? _URC_END_OF_STACK : _URC_NO_REASON;
        },
            &bt);
        for (int i = 0; i < bt.n; i++)
            LOG_CRITICAL("[CRASH] frame #{:02}: {}", i, describe(bt.pcs[i]));
    }
#endif
    logging::flush();
    signal(sig, SIG_DFL);
    raise(sig);
    return;
}

static void register_access_violation_handler(const AccessViolationHandler &handler) {
    access_violation_handler = handler;
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = signal_handler;
    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        LOG_CRITICAL("Failed to register an exception handler");
    }
    // SIGBUS: on macOS a PROT_NONE access raises it instead of SIGSEGV.
    // on Android an ARM64 atomic against non-cacheable memory raises it.
    // Handle it on both so it is logged and not silent.
    if (sigaction(SIGBUS, &sa, NULL) == -1) {
        LOG_CRITICAL("Failed to register an exception handler to SIGBUS");
    }
    if (sigaction(SIGABRT, &sa, NULL) == -1) {
        LOG_CRITICAL("Failed to register an exception handler to SIGABRT");
    }
    if (sigaction(SIGILL, &sa, NULL) == -1) {
        LOG_CRITICAL("Failed to register an exception handler to SIGILL");
    }
    if (sigaction(SIGTRAP, &sa, NULL) == -1) {
        LOG_CRITICAL("Failed to register an exception handler to SIGTRAP");
    }
}

bool test_arm64_atomics_on(void *ptr) {
#if defined(__ANDROID__) && defined(__aarch64__)
    volatile uint32_t *word = static_cast<volatile uint32_t *>(ptr);
    t_fault_probe_active = true;
    const int faulted = sigsetjmp(t_fault_probe_jmp, 1);
    if (faulted == 0) {
        __atomic_fetch_add(const_cast<uint32_t *>(word), 0u, __ATOMIC_SEQ_CST);
        uint32_t value, status;
        asm volatile(
            "1: ldaxr %w0, [%2]\n"
            "   stlxr %w1, %w0, [%2]\n"
            "   cbnz  %w1, 1b\n"
            : "=&r"(value), "=&r"(status)
            : "r"(word)
            : "memory");
    }
    t_fault_probe_active = false;
    if (faulted != 0)
        LOG_ERROR("ARM64 atomic probe faulted with signal {} on mapping at {}", faulted, ptr);
    return faulted == 0;
#else
    (void)ptr;
    return true;
#endif
}

#endif
