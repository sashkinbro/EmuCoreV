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

#include "SceLibKernel.h"
#include "clib_chk.h"
#include "guest_atomics.h"
#include <modules/module_parent.h>
#include <v3kprintf.h>

#include <../SceIofilemgr/SceIofilemgr.h>
#include <../SceKernelModulemgr/SceModulemgr.h>
#include <../SceKernelThreadMgr/SceThreadmgr.h>

#include <cpu/functions.h>
#include <dlmalloc.h>
#include <io/functions.h>
#include <kernel/state.h>
#include <kernel/sync_primitives.h>
#include <packages/functions.h>
#include <string_view>

#include <io/device.h>
#include <io/io.h>
#include <io/types.h>
#include <kernel/types.h>
#include <rtc/rtc.h>
#include <set>
#include <util/lock_and_find.h>
#include <util/log.h>
#include <util/tracy.h>

#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string>

enum class TimerFlags : uint32_t {
    FIFO_THREAD = 0x00000000,
    PRIORITY_THREAD = 0x00002000,
    MANUAL_RESET = 0x00000000,
    AUTOMATIC_RESET = 0x00000100,
    ALL_NOTIFY = 0x00000000,
    WAKE_ONLY_NOTIFY = 0x00000800,

    OPENABLE = 0x00000080,
};

TRACY_MODULE_NAME(SceLibKernel);

inline static uint64_t get_current_time() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch())
        .count();
}

VAR_EXPORT(__sce_libcparam) {
    // This variable almost newer used. So I return something that looks good, but not exacly correct.
    return (emuenv.kernel.process_param ? emuenv.kernel.process_param.get(emuenv.mem)->sce_libc_param.address() : 0);
}

VAR_EXPORT(__stack_chk_guard) {
    auto ptr = Ptr<uint32_t>(alloc(emuenv.mem, 4, "__stack_chk_guard"));
    // Can be any random value
    *ptr.get(emuenv.mem) = 0x0DEADBEE;
    return ptr.address();
}

EXPORT(int, __sce_aeabi_idiv0) {
    TRACY_FUNC(__sce_aeabi_idiv0);
    LOG_ERROR("Division by zero");
    return UNIMPLEMENTED();
}

EXPORT(int, __sce_aeabi_ldiv0) {
    TRACY_FUNC(__sce_aeabi_ldiv0);
    LOG_ERROR("Division by zero");
    return UNIMPLEMENTED();
}

EXPORT(int, __stack_chk_fail) {
    TRACY_FUNC(__stack_chk_fail);
    LOG_CRITICAL("Stack corruption on TID: {}", thread_id);

    LOG_ERROR("{}", save_context(*emuenv.kernel.get_thread(thread_id)->cpu).description());

    assert(false); // if this triggers then something is seriously wrong somewhere else

    return UNIMPLEMENTED();
}

EXPORT(int, _sceKernelCreateLwMutex, Ptr<SceKernelLwMutexWork> workarea, const char *name, unsigned int attr, int init_count, Ptr<SceKernelLwMutexOptParam> opt_param) {
    TRACY_FUNC(_sceKernelCreateLwMutex, workarea, name, attr, init_count, opt_param);
    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);

    Ptr<SceKernelCreateLwMutex_opt> options = Ptr<SceKernelCreateLwMutex_opt>(stack_alloc(*thread->cpu, sizeof(SceKernelCreateLwMutex_opt)));
    options.get(emuenv.mem)->init_count = init_count;
    options.get(emuenv.mem)->opt_param = opt_param;
    int res = CALL_EXPORT(__sceKernelCreateLwMutex, workarea, name, attr, options);
    stack_free(*thread->cpu, sizeof(SceKernelCreateLwMutex_opt));
    return res;
}

// Fortify (sceClib*Chk) overflow. Real firmware aborts; here (PROVISIONAL policy) the copy is
// not performed, the error is logged once per export and the caller returns its failure value
// (nullptr / error code). The guest thread is deliberately not halted. Returns nullptr.
static void *clib_chk_overflow(EmuEnvState &, SceUID thread_id, const char *export_name, SceSize len, SceSize dst_size) {
    static std::mutex mutex;
    static std::set<std::string> reported;
    bool first;
    {
        std::lock_guard lock(mutex);
        first = reported.insert(export_name).second;
    }
    if (first)
        LOG_ERROR("{}: buffer overflow detected (len {} / dst size {}) in thread {}, operation skipped", export_name, len, dst_size, thread_id);
    return nullptr;
}

EXPORT(int, sceClibAbort) {
    TRACY_FUNC(sceClibAbort);
    return UNIMPLEMENTED();
}

EXPORT(int, sceClibDprintf) {
    TRACY_FUNC(sceClibDprintf);
    return UNIMPLEMENTED();
}

EXPORT(int, sceClibLookCtypeTable, uint32_t param1) {
    TRACY_FUNC(sceClibLookCtypeTable, param1);
    if (param1 < 128) {
        const uint8_t arr[128] = {
            0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x08, 0x08, 0x08, 0x08, 0x08, 0x20, 0x20,
            0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
            0x18, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
            0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
            0x10, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
            0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x10, 0x10, 0x10, 0x10, 0x10,
            0x10, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
            0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x10, 0x10, 0x10, 0x10, 0x20
        };
        return arr[param1];
    } else {
        return 0;
    }
}

EXPORT(Ptr<void>, sceClibMemchr, const void *src, int ch, SceSize len) {
    TRACY_FUNC(sceClibMemchr, src, ch, len);
    void *res = const_cast<void *>(memchr(src, ch, len));
    return Ptr<void>(res, emuenv.mem);
}

EXPORT(int, sceClibMemcmp, const void *s1, const void *s2, SceSize len) {
    TRACY_FUNC(sceClibMemcmp, s1, s2, len);
    return memcmp(s1, s2, len);
}

EXPORT(int, sceClibMemcmpConstTime) {
    TRACY_FUNC(sceClibMemcmpConstTime);
    return UNIMPLEMENTED();
}

EXPORT(Ptr<void>, sceClibMemcpy, Ptr<void> dst, const void *src, SceSize len) {
    TRACY_FUNC(sceClibMemcpy, dst, src, len);
    memcpy(dst.get(emuenv.mem), src, len);
    return dst;
}

EXPORT(Ptr<void>, sceClibMemcpyChk, Ptr<void> dst, const void *src, SceSize len, SceSize dst_size) {
    TRACY_FUNC(sceClibMemcpyChk, dst, src, len, dst_size);
    if (!clib_chk::fits_bytes(dst_size, len)) {
        clib_chk_overflow(emuenv, thread_id, export_name, len, dst_size);
        return Ptr<void>();
    }
    memcpy(dst.get(emuenv.mem), src, len);
    return dst;
}

EXPORT(Ptr<void>, sceClibMemcpy_safe, Ptr<void> dst, const Ptr<void> src, SceSize len) {
    TRACY_FUNC(sceClibMemcpy_safe, dst, src, len);
    /*
    should be 1:1 to real hw, asserts are in the same position as decompiled code,
    still call memcpy when the breakpoint happens just like in real hw, should practically
    be the same as just calling memcpy, but with a bit more checks for the developers,
    asserts do get annoying if the game tends to do memcpy on overlapping pointers, which is weird,
    if they in fact do get annoying very easily they can just be deleted, we still have the log_error
    */
    if (len == 0)
        return dst;

    if (dst.address() == src.address()) {
        LOG_ERROR("sceClibMemcpy({},{},{}) src == dst", log_hex_full(src.address()), log_hex_full(dst.address()), len);
        assert(false);
        CALL_EXPORT(sceClibMemcpy, dst, src.get(emuenv.mem), len);
        return dst;
    }
    const auto diff = std::abs((int)(src.address() - dst.address()));
    if (len > diff) {
        LOG_ERROR("sceClibMemcpy({},{},{}) src/dst overlap", log_hex_full(src.address()), log_hex_full(dst.address()), len);
        assert(false);
    }
    CALL_EXPORT(sceClibMemcpy, dst, src.get(emuenv.mem), len);
    return dst;
}

EXPORT(Ptr<void>, sceClibMemmove, Ptr<void> dst, const void *src, SceSize len) {
    TRACY_FUNC(sceClibMemmove, dst, src, len);
    memmove(dst.get(emuenv.mem), src, len);
    return dst;
}

EXPORT(Ptr<void>, sceClibMemmoveChk, Ptr<void> dst, const void *src, SceSize len, SceSize dst_size) {
    TRACY_FUNC(sceClibMemmoveChk, dst, src, len, dst_size);
    if (!clib_chk::fits_bytes(dst_size, len)) {
        clib_chk_overflow(emuenv, thread_id, export_name, len, dst_size);
        return Ptr<void>();
    }
    memmove(dst.get(emuenv.mem), src, len);
    return dst;
}

EXPORT(Ptr<void>, sceClibMemset, Ptr<void> dst, int ch, SceSize len) {
    TRACY_FUNC(sceClibMemset, dst, ch, len);
    memset(dst.get(emuenv.mem), ch, len);
    return dst;
}

EXPORT(Ptr<void>, sceClibMemsetChk, Ptr<void> dst, int ch, SceSize len, SceSize dst_size) {
    TRACY_FUNC(sceClibMemsetChk, dst, ch, len, dst_size);
    if (!clib_chk::fits_bytes(dst_size, len)) {
        clib_chk_overflow(emuenv, thread_id, export_name, len, dst_size);
        return Ptr<void>();
    }
    memset(dst.get(emuenv.mem), ch, len);
    return dst;
}

// Guest RAM snapshots also contain dlmalloc's host-addressed bookkeeping.
// Prepare the exact handle lazily so legacy states need no memory scan or
// format change. All callers already hold the kernel mutex.
static bool prepare_clib_mspace(EmuEnvState &emuenv, Ptr<void> space) {
    if (emuenv.kernel.prepared_clib_mspaces.contains(space.address())) return true;
    const auto mapped_region = [](void *pointer, size_t size, void *context) -> int {
        auto &mem = *static_cast<MemState *>(context);
        if (!pointer || !size || size > UINT32_MAX) return 0;
        const Address start = host_to_guest(mem, pointer);
        const uint64_t end = static_cast<uint64_t>(start) + size;
        if (!start || end >= (uint64_t{1} << 32)
            || Ptr<void>(start).get(mem) != pointer) return 0;
        for (uint64_t address = start; address < end; address = (address | 4095u) + 1) {
            if (!is_valid_addr(mem, static_cast<Address>(address))
                || Ptr<void>(static_cast<Address>(address)).get(mem)
                    != static_cast<uint8_t *>(pointer) + (address - start)) return 0;
        }
        return 1;
    };
    if (mspace_prepare_external(space.get(emuenv.mem), mapped_region, &emuenv.mem)) {
        emuenv.kernel.prepared_clib_mspaces.insert(space.address());
        return true;
    }
    LOG_ERROR("Invalid or unsupported guest mspace at {:#x}", space.address());
    return false;
}

EXPORT(Ptr<void>, sceClibMspaceCalloc, Ptr<void> space, uint32_t elements, uint32_t size) {
    TRACY_FUNC(sceClibMspaceCalloc, space, elements, size);
    const std::lock_guard<std::mutex> guard(emuenv.kernel.mutex);

    if (!prepare_clib_mspace(emuenv, space)) return Ptr<void>();
    void *address = mspace_calloc(space.get(emuenv.mem), elements, size);
    return Ptr<void>(address, emuenv.mem);
}

EXPORT(Ptr<void>, sceClibMspaceCreate, Ptr<void> base, uint32_t capacity) {
    TRACY_FUNC(sceClibMspaceCreate, base, capacity);
    const std::lock_guard<std::mutex> guard(emuenv.kernel.mutex);

    mspace space = create_mspace_with_base(base.get(emuenv.mem), capacity, 0);
    const Ptr<void> result(space, emuenv.mem);
    if (space) emuenv.kernel.prepared_clib_mspaces.insert(result.address());
    return result;
}

EXPORT(uint32_t, sceClibMspaceDestroy, Ptr<void> space) {
    TRACY_FUNC(sceClibMspaceDestroy, space);
    const std::lock_guard<std::mutex> guard(emuenv.kernel.mutex);

    if (!prepare_clib_mspace(emuenv, space)) return 0;
    emuenv.kernel.prepared_clib_mspaces.erase(space.address());
    return static_cast<uint32_t>(destroy_mspace(space.get(emuenv.mem)));
}

EXPORT(void, sceClibMspaceFree, Ptr<void> space, Ptr<void> address) {
    TRACY_FUNC(sceClibMspaceFree, space, address);
    const std::lock_guard<std::mutex> guard(emuenv.kernel.mutex);

    if (!prepare_clib_mspace(emuenv, space)) return;
    mspace_free(space.get(emuenv.mem), address.get(emuenv.mem));
}

EXPORT(int, sceClibMspaceIsHeapEmpty) {
    TRACY_FUNC(sceClibMspaceIsHeapEmpty);
    return UNIMPLEMENTED();
}

EXPORT(Ptr<void>, sceClibMspaceMalloc, Ptr<void> space, uint32_t size) {
    TRACY_FUNC(sceClibMspaceMalloc, space, size);
    const std::lock_guard<std::mutex> guard(emuenv.kernel.mutex);

    if (!prepare_clib_mspace(emuenv, space)) return Ptr<void>();
    void *address = mspace_malloc(space.get(emuenv.mem), size);
    return Ptr<void>(address, emuenv.mem);
}

EXPORT(int, sceClibMspaceMallocStats) {
    TRACY_FUNC(sceClibMspaceMallocStats);
    return UNIMPLEMENTED();
}

EXPORT(int, sceClibMspaceMallocStatsFast) {
    TRACY_FUNC(sceClibMspaceMallocStatsFast);
    return UNIMPLEMENTED();
}

EXPORT(int, sceClibMspaceMallocUsableSize) {
    TRACY_FUNC(sceClibMspaceMallocUsableSize);
    return UNIMPLEMENTED();
}

EXPORT(Ptr<void>, sceClibMspaceMemalign, Ptr<void> space, uint32_t alignment, uint32_t size) {
    TRACY_FUNC(sceClibMspaceMemalign, space, alignment, size);
    const std::lock_guard<std::mutex> guard(emuenv.kernel.mutex);

    if (!prepare_clib_mspace(emuenv, space)) return Ptr<void>();
    void *address = mspace_memalign(space.get(emuenv.mem), alignment, size);
    return Ptr<void>(address, emuenv.mem);
}

EXPORT(Ptr<void>, sceClibMspaceRealloc, Ptr<void> space, Ptr<void> address, uint32_t size) {
    TRACY_FUNC(sceClibMspaceRealloc, space, address, size);
    const std::lock_guard<std::mutex> guard(emuenv.kernel.mutex);

    if (!prepare_clib_mspace(emuenv, space)) return Ptr<void>();
    void *new_address = mspace_realloc(space.get(emuenv.mem), address.get(emuenv.mem), size);
    return Ptr<void>(new_address, emuenv.mem);
}

EXPORT(int, sceClibMspaceReallocalign) {
    TRACY_FUNC(sceClibMspaceReallocalign);
    return UNIMPLEMENTED();
}

EXPORT(int, sceClibPrintf, const char *fmt, module::vargs args) {
    TRACY_FUNC(sceClibPrintf, fmt);
    std::vector<char> buffer(KiB(1));

    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);

    if (!thread) {
        return SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID;
    }

    const int result = utils::snprintf(buffer.data(), buffer.size(), fmt, *(thread->cpu), emuenv.mem, args);

    if (!result) {
        return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
    }

    LOG_INFO("{}", buffer.data());

    return SCE_KERNEL_OK;
}

EXPORT(int, sceClibSnprintf, char *dst, SceSize dst_max_size, const char *fmt, module::vargs args) {
    TRACY_FUNC(sceClibSnprintf, dst, dst_max_size, fmt);
    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);

    if (!thread) {
        return SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID;
    }

    int result = utils::snprintf(dst, dst_max_size, fmt, *(thread->cpu), emuenv.mem, args);

    if (!result) {
        return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
    }

    return result;
}

EXPORT(int, sceClibSnprintfChk, char *dst, SceSize dst_max_size, int flag, SceSize dst_size, const char *fmt, module::vargs args) {
    TRACY_FUNC(sceClibSnprintfChk, dst, dst_max_size, flag, dst_size, fmt);
    if (!clib_chk::fits_size(dst_size, dst_max_size)) {
        clib_chk_overflow(emuenv, thread_id, export_name, dst_max_size, dst_size);
        return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
    }
    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);
    if (!thread) {
        return SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID;
    }
    return utils::snprintf(dst, dst_max_size, fmt, *(thread->cpu), emuenv.mem, args);
}

EXPORT(Ptr<char>, sceClibStrcatChk, char *dst, const char *src, SceSize dst_size) {
    TRACY_FUNC(sceClibStrcatChk, dst, src, dst_size);
    if (!clib_chk::fits_strcat(dst, src, dst_size)) {
        clib_chk_overflow(emuenv, thread_id, export_name, 0, dst_size);
        return Ptr<char>();
    }
    strcat(dst, src);
    return Ptr<char>(dst, emuenv.mem);
}

EXPORT(Ptr<char>, sceClibStrchr, const char *str, int c) {
    TRACY_FUNC(sceClibStrchr, str, c);
    char *res = const_cast<char *>(strchr(str, c));
    return Ptr<char>(res, emuenv.mem);
}

EXPORT(int, sceClibStrcmp, const char *s1, const char *s2) {
    TRACY_FUNC(sceClibStrcmp, s1, s2);
    return strcmp(s1, s2);
}

EXPORT(Ptr<char>, sceClibStrcpyChk, char *dst, const char *src, SceSize dst_size) {
    TRACY_FUNC(sceClibStrcpyChk, dst, src, dst_size);
    if (!clib_chk::fits_strcpy(src, dst_size)) {
        clib_chk_overflow(emuenv, thread_id, export_name, 0, dst_size);
        return Ptr<char>();
    }
    strcpy(dst, src);
    return Ptr<char>(dst, emuenv.mem);
}

EXPORT(Ptr<char>, sceClibStrlcat, char *dst, const char *src, SceSize len) {
    TRACY_FUNC(sceClibStrlcat, dst, src, len);
    char *res = strncat(dst, src, len);
    return Ptr<char>(res, emuenv.mem);
}

EXPORT(SceSize, sceClibStrlcatChk, char *dst, const char *src, SceSize len, SceSize dst_size) {
    TRACY_FUNC(sceClibStrlcatChk, dst, src, len, dst_size);
    if (!clib_chk::fits_size(dst_size, len)) {
        clib_chk_overflow(emuenv, thread_id, export_name, len, dst_size);
        return 0;
    }
    return clib_chk::strlcat_impl(dst, src, len);
}

EXPORT(Ptr<char>, sceClibStrlcpy, char *dst, const char *src, SceSize len) {
    TRACY_FUNC(sceClibStrlcpy, dst, src, len);
    char *res = strncpy(dst, src, len);
    return Ptr<char>(res, emuenv.mem);
}

EXPORT(SceSize, sceClibStrlcpyChk, char *dst, const char *src, SceSize len, SceSize dst_size) {
    TRACY_FUNC(sceClibStrlcpyChk, dst, src, len, dst_size);
    if (!clib_chk::fits_size(dst_size, len)) {
        clib_chk_overflow(emuenv, thread_id, export_name, len, dst_size);
        return 0;
    }
    return clib_chk::strlcpy_impl(dst, src, len);
}

EXPORT(int, sceClibStrncasecmp, const char *s1, const char *s2, SceSize len) {
    TRACY_FUNC(sceClibStrncasecmp, s1, s2, len);
#ifdef _WIN32
    return _strnicmp(s1, s2, len);
#else
    return strncasecmp(s1, s2, len);
#endif
}

EXPORT(Ptr<char>, sceClibStrncat, char *dst, const char *src, SceSize len) {
    TRACY_FUNC(sceClibStrncat, dst, src, len);
    char *res = strncat(dst, src, len);
    return Ptr<char>(res, emuenv.mem);
}

EXPORT(Ptr<char>, sceClibStrncatChk, char *dst, const char *src, SceSize len, SceSize dst_size) {
    TRACY_FUNC(sceClibStrncatChk, dst, src, len, dst_size);
    if (!clib_chk::fits_strncat(dst, src, len, dst_size)) {
        clib_chk_overflow(emuenv, thread_id, export_name, len, dst_size);
        return Ptr<char>();
    }
    strncat(dst, src, len);
    return Ptr<char>(dst, emuenv.mem);
}

EXPORT(int, sceClibStrncmp, const char *s1, const char *s2, SceSize len) {
    TRACY_FUNC(sceClibStrncmp, s1, s2, len);
    return strncmp(s1, s2, len);
}

EXPORT(Ptr<char>, sceClibStrncpy, char *dst, const char *src, SceSize len) {
    TRACY_FUNC(sceClibStrncpy, dst, src, len);
    char *res = strncpy(dst, src, len);
    return Ptr<char>(res, emuenv.mem);
}

EXPORT(Ptr<char>, sceClibStrncpyChk, char *dst, const char *src, SceSize len, SceSize dst_size) {
    TRACY_FUNC(sceClibStrncpyChk, dst, src, len, dst_size);
    if (!clib_chk::fits_bytes(dst_size, len)) {
        clib_chk_overflow(emuenv, thread_id, export_name, len, dst_size);
        return Ptr<char>();
    }
    strncpy(dst, src, len);
    return Ptr<char>(dst, emuenv.mem);
}

EXPORT(uint32_t, sceClibStrnlen, const char *s1, SceSize maxlen) {
    TRACY_FUNC(sceClibStrnlen, s1, maxlen);
    return static_cast<uint32_t>(strnlen(s1, maxlen));
}

EXPORT(Ptr<char>, sceClibStrrchr, const char *src, int ch) {
    TRACY_FUNC(sceClibStrrchr, src, ch);
    char *res = const_cast<char *>(strrchr(src, ch));
    return Ptr<char>(res, emuenv.mem);
}

EXPORT(Ptr<char>, sceClibStrstr, const char *s1, const char *s2) {
    TRACY_FUNC(sceClibStrstr, s1, s2);
    char *res = const_cast<char *>(strstr(s1, s2));
    return Ptr<char>(res, emuenv.mem);
}

EXPORT(int64_t, sceClibStrtoll, const char *str, char **endptr, int base) {
    TRACY_FUNC(sceClibStrtoll, str, endptr, base);
    return strtoll(str, endptr, base);
}

EXPORT(int, sceClibTolower, char ch) {
    TRACY_FUNC(sceClibTolower, ch);
    return tolower(ch);
}

EXPORT(int, sceClibToupper, char ch) {
    TRACY_FUNC(sceClibToupper, ch);
    return toupper(ch);
}

EXPORT(int, sceClibVdprintf) {
    TRACY_FUNC(sceClibVdprintf);
    return UNIMPLEMENTED();
}

EXPORT(int, sceClibVprintf, const char *fmt, module::vargs args) {
    TRACY_FUNC(sceClibVprintf, fmt);
    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);
    if (!thread) {
        return SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID;
    }
    constexpr int dst_max_size = 1024;
    char dst[dst_max_size];
    int result = utils::snprintf(dst, dst_max_size, fmt, *(thread->cpu), emuenv.mem, args);
    if (!result) {
        return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
    }
    if (result == dst_max_size) {
        LOG_WARN("Predefined buffer too small. Result truncated");
    }
    LOG_INFO("{}", dst);
    return SCE_KERNEL_OK;
}

EXPORT(int, sceClibVsnprintf, char *dst, SceSize dst_max_size, const char *fmt, Address list) {
    TRACY_FUNC(sceClibVsnprintf, dst, dst_max_size, fmt, list);
    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);

    module::vargs args(list);
    if (!thread) {
        return SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID;
    }

    int result = utils::snprintf(dst, dst_max_size, fmt, *(thread->cpu), emuenv.mem, args);

    if (!result) {
        return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
    }

    return SCE_KERNEL_OK;
}

EXPORT(int, sceClibVsnprintfChk, char *dst, SceSize dst_max_size, int flag, SceSize dst_size, const char *fmt, Address list) {
    TRACY_FUNC(sceClibVsnprintfChk, dst, dst_max_size, flag, dst_size, fmt, list);
    if (!clib_chk::fits_size(dst_size, dst_max_size)) {
        clib_chk_overflow(emuenv, thread_id, export_name, dst_max_size, dst_size);
        return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
    }
    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);
    if (!thread) {
        return SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID;
    }
    module::vargs args(list);
    return utils::snprintf(dst, dst_max_size, fmt, *(thread->cpu), emuenv.mem, args);
}

EXPORT(int, sceIoChstat) {
    TRACY_FUNC(sceIoChstat);
    return UNIMPLEMENTED();
}

EXPORT(int, sceIoChstatAsync) {
    TRACY_FUNC(sceIoChstatAsync);
    return UNIMPLEMENTED();
}

EXPORT(int, sceIoChstatByFd) {
    TRACY_FUNC(sceIoChstatByFd);
    return UNIMPLEMENTED();
}

EXPORT(int, sceIoClose2) {
    TRACY_FUNC(sceIoClose2);
    return UNIMPLEMENTED();
}

EXPORT(int, sceIoCompleteMultiple) {
    TRACY_FUNC(sceIoCompleteMultiple);
    return UNIMPLEMENTED();
}

EXPORT(int, sceIoDevctl, const char *dev, SceInt cmd, const void *indata, SceSize inlen, void *outdata, SceSize outlen) {
    TRACY_FUNC(sceIoDevctl, dev, cmd, indata, inlen, outdata, outlen);
    if (!dev || !outdata)
        return RET_ERROR(SCE_ERROR_ERRNO_EINVAL);

    // TODO: Turn the commands into an enum of commands
    switch (cmd) {
    case 12289: { // Get device capacity info?
        assert(outlen == sizeof(SceIoDevInfo));

        auto device = device::get_device(dev);
        if (device == VitaIoDevice::_INVALID) {
            LOG_ERROR("Cannot find device for path: {}", dev);
            return RET_ERROR(SCE_ERROR_ERRNO_ENOENT);
        }

        fs::path dev_path = boost::describe::enum_to_string(device, "");
        fs::path path = emuenv.vita_fs_path / dev_path;
        fs::space_info space = fs::space(path);

        ((SceIoDevInfo *)outdata)->max_size = space.capacity;
        ((SceIoDevInfo *)outdata)->free_size = space.available;
        STUBBED("cluster size = 4096");
        ((SceIoDevInfo *)outdata)->cluster_size = 4096;
        break;
    }
    default: {
        LOG_WARN("Unhandled case for sceIoDevctl cmd={}", cmd);
        assert(false);
        return 0;
    }
    }

    return 0;
}

EXPORT(int, sceIoDevctlAsync) {
    TRACY_FUNC(sceIoDevctlAsync);
    return UNIMPLEMENTED();
}

EXPORT(int, sceIoDopen, const char *dir) {
    TRACY_FUNC(sceIoDopen, dir);
    return CALL_EXPORT(_sceIoDopen, dir);
}

EXPORT(int, sceIoDread, const SceUID fd, SceIoDirent *dir) {
    TRACY_FUNC(sceIoDread, fd, dir);
    return CALL_EXPORT(_sceIoDread, fd, dir);
}

EXPORT(int, sceIoGetstat, const char *file, SceIoStat *stat) {
    TRACY_FUNC(sceIoGetstat, file, stat);
    return CALL_EXPORT(_sceIoGetstat, file, stat);
}

EXPORT(int, sceIoGetstatAsync) {
    TRACY_FUNC(sceIoGetstatAsync);
    return UNIMPLEMENTED();
}

EXPORT(int, sceIoGetstatByFd, const SceUID fd, SceIoStat *stat) {
    TRACY_FUNC(sceIoGetstatByFd, fd, stat);
    return stat_file_by_fd(emuenv.io, fd, stat, emuenv.vita_fs_path, export_name);
}

EXPORT(int, sceIoIoctl, SceUID fd, int cmd, const void *argp, SceSize arglen, void *bufp, SceSize buflen) {
    TRACY_FUNC(sceIoIoctl, fd, cmd, argp, arglen, bufp, buflen);
    return UNIMPLEMENTED();
}

EXPORT(int, sceIoIoctlAsync) {
    TRACY_FUNC(sceIoIoctlAsync);
    return UNIMPLEMENTED();
}

EXPORT(SceOff, sceIoLseek, const SceUID fd, const SceOff offset, const SceIoSeekMode whence) {
    TRACY_FUNC(sceIoLseek, fd, offset, whence);
    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);

    Ptr<_sceIoLseekOpt> options = Ptr<_sceIoLseekOpt>(stack_alloc(*thread->cpu, sizeof(_sceIoLseekOpt)));
    options.get(emuenv.mem)->offset = offset;
    options.get(emuenv.mem)->whence = whence;
    SceOff res = CALL_EXPORT(_sceIoLseek, fd, options);
    stack_free(*thread->cpu, sizeof(_sceIoLseekOpt));
    return res;
}

EXPORT(int, sceIoLseekAsync) {
    TRACY_FUNC(sceIoLseekAsync);
    return UNIMPLEMENTED();
}

EXPORT(int, sceIoMkdir, const char *dir, const SceMode mode) {
    TRACY_FUNC(sceIoMkdir, dir, mode);
    return CALL_EXPORT(_sceIoMkdir, dir, mode);
}

EXPORT(int, sceIoMkdirAsync) {
    TRACY_FUNC(sceIoMkdirAsync);
    return UNIMPLEMENTED();
}

EXPORT(SceUID, sceIoOpen, const char *file, const int flags, const SceMode mode) {
    TRACY_FUNC(sceIoOpen, file, flags, mode);
    if (file == nullptr) {
        return RET_ERROR(SCE_ERROR_ERRNO_EINVAL);
    }

    if (emuenv.cfg.current_config.file_loading_delay > 0)
        guest_sched_release_for_block();
    std::this_thread::sleep_for(std::chrono::milliseconds(emuenv.cfg.current_config.file_loading_delay));

    const SceUID opened_fd = open_file(emuenv.io, file, flags, emuenv.vita_fs_path, export_name);
    LOG_INFO("Opening file: {} flags=0x{:X} -> {}", file, flags, opened_fd < 0 ? fmt::format("FAILED {}", log_hex(static_cast<uint32_t>(opened_fd))) : fmt::format("fd {}", opened_fd));
    return opened_fd;
}

EXPORT(int, sceIoOpenAsync) {
    TRACY_FUNC(sceIoOpenAsync);
    return UNIMPLEMENTED();
}

EXPORT(SceSSize, sceIoPread, SceUID fd, void *buf, SceSize nbyte, SceOff offset) {
    TRACY_FUNC(sceIoPread, fd, buf, nbyte, offset);

    if (emuenv.cfg.current_config.file_loading_delay > 0) {
        const uint32_t delay_us = emuenv.cfg.current_config.file_loading_delay * 1000 + nbyte / 20;
        guest_sched_release_for_block();
        std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
    }
    return read_file_at(buf, emuenv.io, fd, nbyte, offset, export_name);
}

EXPORT(int, sceIoPreadAsync) {
    TRACY_FUNC(sceIoPreadAsync);
    return UNIMPLEMENTED();
}

EXPORT(SceSSize, sceIoPwrite, SceUID fd, const void *buf, SceSize nbyte, SceOff offset) {
    TRACY_FUNC(sceIoPwrite, fd, buf, nbyte, offset);
    return write_file_at(fd, buf, nbyte, offset, emuenv.io, export_name);
}

EXPORT(int, sceIoPwriteAsync) {
    TRACY_FUNC(sceIoPwriteAsync);
    return UNIMPLEMENTED();
}

EXPORT(int, sceIoRead2) {
    TRACY_FUNC(sceIoRead2);
    return UNIMPLEMENTED();
}

EXPORT(int, sceIoRemove, const char *path) {
    TRACY_FUNC(sceIoRemove, path);
    if (path == nullptr) {
        return RET_ERROR(SCE_ERROR_ERRNO_EINVAL);
    }
    return remove_file(emuenv.io, path, emuenv.vita_fs_path, export_name);
}

EXPORT(int, sceIoRemoveAsync) {
    TRACY_FUNC(sceIoRemoveAsync);
    return UNIMPLEMENTED();
}

EXPORT(int, sceIoRename, const char *oldname, const char *newname) {
    TRACY_FUNC(sceIoRename, oldname, newname);
    if (!oldname || !newname)
        return RET_ERROR(SCE_ERROR_ERRNO_EINVAL);

    LOG_INFO("Renaming: {} to {}", oldname, newname);
    return rename(emuenv.io, oldname, newname, emuenv.vita_fs_path, export_name);
}

EXPORT(int, sceIoRenameAsync) {
    TRACY_FUNC(sceIoRenameAsync);
    return UNIMPLEMENTED();
}

EXPORT(int, sceIoRmdir, const char *path) {
    TRACY_FUNC(sceIoRmdir, path);
    if (path == nullptr) {
        return RET_ERROR(SCE_ERROR_ERRNO_EINVAL);
    }
    return remove_dir(emuenv.io, path, emuenv.vita_fs_path, export_name);
}

EXPORT(int, sceIoRmdirAsync) {
    TRACY_FUNC(sceIoRmdirAsync);
    return UNIMPLEMENTED();
}

EXPORT(int, sceIoSync) {
    TRACY_FUNC(sceIoSync);
    return UNIMPLEMENTED();
}

EXPORT(int, sceIoSyncAsync) {
    TRACY_FUNC(sceIoSyncAsync);
    return UNIMPLEMENTED();
}

EXPORT(int, sceIoWrite2) {
    TRACY_FUNC(sceIoWrite2);
    return UNIMPLEMENTED();
}

EXPORT(int32_t, sceKernelAtomicAddAndGet16, uint16_t *ptr, uint32_t value) {
    TRACY_FUNC(sceKernelAtomicAddAndGet16, ptr, value);
    return static_cast<int32_t>(static_cast<int16_t>(guest_atomic::add_and_get(ptr, static_cast<uint16_t>(value))));
}

EXPORT(int32_t, sceKernelAtomicAddAndGet32, uint32_t *ptr, uint32_t value) {
    TRACY_FUNC(sceKernelAtomicAddAndGet32, ptr, value);
    return static_cast<int32_t>(guest_atomic::add_and_get(ptr, static_cast<uint32_t>(value)));
}

EXPORT(int64_t, sceKernelAtomicAddAndGet64, uint64_t *ptr, uint64_t value) {
    TRACY_FUNC(sceKernelAtomicAddAndGet64, ptr, value);
    return static_cast<int64_t>(guest_atomic::add_and_get(ptr, static_cast<uint64_t>(value)));
}

EXPORT(int32_t, sceKernelAtomicAddAndGet8, uint8_t *ptr, uint32_t value) {
    TRACY_FUNC(sceKernelAtomicAddAndGet8, ptr, value);
    return static_cast<int32_t>(static_cast<int8_t>(guest_atomic::add_and_get(ptr, static_cast<uint8_t>(value))));
}

EXPORT(int32_t, sceKernelAtomicAddUnless16, uint16_t *ptr, uint32_t value, uint32_t cmp) {
    TRACY_FUNC(sceKernelAtomicAddUnless16, ptr, value, cmp);
    return static_cast<int32_t>(static_cast<int16_t>(guest_atomic::add_unless(ptr, static_cast<uint16_t>(value), static_cast<uint16_t>(cmp))));
}

EXPORT(int32_t, sceKernelAtomicAddUnless32, uint32_t *ptr, uint32_t value, uint32_t cmp) {
    TRACY_FUNC(sceKernelAtomicAddUnless32, ptr, value, cmp);
    return static_cast<int32_t>(guest_atomic::add_unless(ptr, static_cast<uint32_t>(value), static_cast<uint32_t>(cmp)));
}

EXPORT(int64_t, sceKernelAtomicAddUnless64, uint64_t *ptr, uint64_t value, uint64_t cmp) {
    TRACY_FUNC(sceKernelAtomicAddUnless64, ptr, value, cmp);
    return static_cast<int64_t>(guest_atomic::add_unless(ptr, static_cast<uint64_t>(value), static_cast<uint64_t>(cmp)));
}

EXPORT(int32_t, sceKernelAtomicAddUnless8, uint8_t *ptr, uint32_t value, uint32_t cmp) {
    TRACY_FUNC(sceKernelAtomicAddUnless8, ptr, value, cmp);
    return static_cast<int32_t>(static_cast<int8_t>(guest_atomic::add_unless(ptr, static_cast<uint8_t>(value), static_cast<uint8_t>(cmp))));
}

EXPORT(int32_t, sceKernelAtomicAndAndGet16, uint16_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicAndAndGet16, ptr, mask);
    return static_cast<int32_t>(static_cast<int16_t>(guest_atomic::and_and_get(ptr, static_cast<uint16_t>(mask))));
}

EXPORT(int32_t, sceKernelAtomicAndAndGet32, uint32_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicAndAndGet32, ptr, mask);
    return static_cast<int32_t>(guest_atomic::and_and_get(ptr, static_cast<uint32_t>(mask)));
}

EXPORT(int64_t, sceKernelAtomicAndAndGet64, uint64_t *ptr, uint64_t mask) {
    TRACY_FUNC(sceKernelAtomicAndAndGet64, ptr, mask);
    return static_cast<int64_t>(guest_atomic::and_and_get(ptr, static_cast<uint64_t>(mask)));
}

EXPORT(int32_t, sceKernelAtomicAndAndGet8, uint8_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicAndAndGet8, ptr, mask);
    return static_cast<int32_t>(static_cast<int8_t>(guest_atomic::and_and_get(ptr, static_cast<uint8_t>(mask))));
}

EXPORT(int32_t, sceKernelAtomicClearAndGet16, uint16_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicClearAndGet16, ptr, mask);
    return static_cast<int32_t>(static_cast<int16_t>(guest_atomic::clear_and_get(ptr, static_cast<uint16_t>(mask))));
}

EXPORT(int32_t, sceKernelAtomicClearAndGet32, uint32_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicClearAndGet32, ptr, mask);
    return static_cast<int32_t>(guest_atomic::clear_and_get(ptr, static_cast<uint32_t>(mask)));
}

EXPORT(int64_t, sceKernelAtomicClearAndGet64, uint64_t *ptr, uint64_t mask) {
    TRACY_FUNC(sceKernelAtomicClearAndGet64, ptr, mask);
    return static_cast<int64_t>(guest_atomic::clear_and_get(ptr, static_cast<uint64_t>(mask)));
}

EXPORT(int32_t, sceKernelAtomicClearAndGet8, uint8_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicClearAndGet8, ptr, mask);
    return static_cast<int32_t>(static_cast<int8_t>(guest_atomic::clear_and_get(ptr, static_cast<uint8_t>(mask))));
}

EXPORT(int32_t, sceKernelAtomicClearMask16, uint16_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicClearMask16, ptr, mask);
    return static_cast<int32_t>(static_cast<int16_t>(guest_atomic::clear_mask(ptr, static_cast<uint16_t>(mask))));
}

EXPORT(int32_t, sceKernelAtomicClearMask32, uint32_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicClearMask32, ptr, mask);
    return static_cast<int32_t>(guest_atomic::clear_mask(ptr, static_cast<uint32_t>(mask)));
}

EXPORT(int64_t, sceKernelAtomicClearMask64, uint64_t *ptr, uint64_t mask) {
    TRACY_FUNC(sceKernelAtomicClearMask64, ptr, mask);
    return static_cast<int64_t>(guest_atomic::clear_mask(ptr, static_cast<uint64_t>(mask)));
}

EXPORT(int32_t, sceKernelAtomicClearMask8, uint8_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicClearMask8, ptr, mask);
    return static_cast<int32_t>(static_cast<int8_t>(guest_atomic::clear_mask(ptr, static_cast<uint8_t>(mask))));
}

EXPORT(int32_t, sceKernelAtomicCompareAndSet16, uint16_t *ptr, uint32_t cmp, uint32_t set_to) {
    TRACY_FUNC(sceKernelAtomicCompareAndSet16, ptr, cmp, set_to);
    return static_cast<int32_t>(static_cast<int16_t>(guest_atomic::compare_and_set(ptr, static_cast<uint16_t>(cmp), static_cast<uint16_t>(set_to))));
}

EXPORT(int32_t, sceKernelAtomicCompareAndSet32, uint32_t *ptr, uint32_t cmp, uint32_t set_to) {
    TRACY_FUNC(sceKernelAtomicCompareAndSet32, ptr, cmp, set_to);
    return static_cast<int32_t>(guest_atomic::compare_and_set(ptr, static_cast<uint32_t>(cmp), static_cast<uint32_t>(set_to)));
}

EXPORT(int64_t, sceKernelAtomicCompareAndSet64, uint64_t *ptr, uint64_t cmp, uint64_t set_to) {
    TRACY_FUNC(sceKernelAtomicCompareAndSet64, ptr, cmp, set_to);
    return static_cast<int64_t>(guest_atomic::compare_and_set(ptr, static_cast<uint64_t>(cmp), static_cast<uint64_t>(set_to)));
}

EXPORT(int32_t, sceKernelAtomicCompareAndSet8, uint8_t *ptr, uint32_t cmp, uint32_t set_to) {
    TRACY_FUNC(sceKernelAtomicCompareAndSet8, ptr, cmp, set_to);
    return static_cast<int32_t>(static_cast<int8_t>(guest_atomic::compare_and_set(ptr, static_cast<uint8_t>(cmp), static_cast<uint8_t>(set_to))));
}

EXPORT(int32_t, sceKernelAtomicDecIfPositive16, uint16_t *ptr) {
    TRACY_FUNC(sceKernelAtomicDecIfPositive16, ptr);
    return static_cast<int32_t>(static_cast<int16_t>(guest_atomic::dec_if_positive(ptr)));
}

EXPORT(int32_t, sceKernelAtomicDecIfPositive32, uint32_t *ptr) {
    TRACY_FUNC(sceKernelAtomicDecIfPositive32, ptr);
    return static_cast<int32_t>(guest_atomic::dec_if_positive(ptr));
}

EXPORT(int64_t, sceKernelAtomicDecIfPositive64, uint64_t *ptr) {
    TRACY_FUNC(sceKernelAtomicDecIfPositive64, ptr);
    return static_cast<int64_t>(guest_atomic::dec_if_positive(ptr));
}

EXPORT(int32_t, sceKernelAtomicDecIfPositive8, uint8_t *ptr) {
    TRACY_FUNC(sceKernelAtomicDecIfPositive8, ptr);
    return static_cast<int32_t>(static_cast<int8_t>(guest_atomic::dec_if_positive(ptr)));
}

EXPORT(int32_t, sceKernelAtomicGetAndAdd16, uint16_t *ptr, uint32_t value) {
    TRACY_FUNC(sceKernelAtomicGetAndAdd16, ptr, value);
    return static_cast<int32_t>(static_cast<int16_t>(guest_atomic::get_and_add(ptr, static_cast<uint16_t>(value))));
}

EXPORT(int32_t, sceKernelAtomicGetAndAdd32, uint32_t *ptr, uint32_t value) {
    TRACY_FUNC(sceKernelAtomicGetAndAdd32, ptr, value);
    return static_cast<int32_t>(guest_atomic::get_and_add(ptr, static_cast<uint32_t>(value)));
}

EXPORT(int64_t, sceKernelAtomicGetAndAdd64, uint64_t *ptr, uint64_t value) {
    TRACY_FUNC(sceKernelAtomicGetAndAdd64, ptr, value);
    return static_cast<int64_t>(guest_atomic::get_and_add(ptr, static_cast<uint64_t>(value)));
}

EXPORT(int32_t, sceKernelAtomicGetAndAdd8, uint8_t *ptr, uint32_t value) {
    TRACY_FUNC(sceKernelAtomicGetAndAdd8, ptr, value);
    return static_cast<int32_t>(static_cast<int8_t>(guest_atomic::get_and_add(ptr, static_cast<uint8_t>(value))));
}

EXPORT(int32_t, sceKernelAtomicGetAndAnd16, uint16_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicGetAndAnd16, ptr, mask);
    return static_cast<int32_t>(static_cast<int16_t>(guest_atomic::get_and_and(ptr, static_cast<uint16_t>(mask))));
}

EXPORT(int32_t, sceKernelAtomicGetAndAnd32, uint32_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicGetAndAnd32, ptr, mask);
    return static_cast<int32_t>(guest_atomic::get_and_and(ptr, static_cast<uint32_t>(mask)));
}

EXPORT(int64_t, sceKernelAtomicGetAndAnd64, uint64_t *ptr, uint64_t mask) {
    TRACY_FUNC(sceKernelAtomicGetAndAnd64, ptr, mask);
    return static_cast<int64_t>(guest_atomic::get_and_and(ptr, static_cast<uint64_t>(mask)));
}

EXPORT(int32_t, sceKernelAtomicGetAndAnd8, uint8_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicGetAndAnd8, ptr, mask);
    return static_cast<int32_t>(static_cast<int8_t>(guest_atomic::get_and_and(ptr, static_cast<uint8_t>(mask))));
}

EXPORT(int32_t, sceKernelAtomicGetAndClear16, uint16_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicGetAndClear16, ptr, mask);
    return static_cast<int32_t>(static_cast<int16_t>(guest_atomic::get_and_clear(ptr, static_cast<uint16_t>(mask))));
}

EXPORT(int32_t, sceKernelAtomicGetAndClear32, uint32_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicGetAndClear32, ptr, mask);
    return static_cast<int32_t>(guest_atomic::get_and_clear(ptr, static_cast<uint32_t>(mask)));
}

EXPORT(int64_t, sceKernelAtomicGetAndClear64, uint64_t *ptr, uint64_t mask) {
    TRACY_FUNC(sceKernelAtomicGetAndClear64, ptr, mask);
    return static_cast<int64_t>(guest_atomic::get_and_clear(ptr, static_cast<uint64_t>(mask)));
}

EXPORT(int32_t, sceKernelAtomicGetAndClear8, uint8_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicGetAndClear8, ptr, mask);
    return static_cast<int32_t>(static_cast<int8_t>(guest_atomic::get_and_clear(ptr, static_cast<uint8_t>(mask))));
}

EXPORT(int32_t, sceKernelAtomicGetAndOr16, uint16_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicGetAndOr16, ptr, mask);
    return static_cast<int32_t>(static_cast<int16_t>(guest_atomic::get_and_or(ptr, static_cast<uint16_t>(mask))));
}

EXPORT(int32_t, sceKernelAtomicGetAndOr32, uint32_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicGetAndOr32, ptr, mask);
    return static_cast<int32_t>(guest_atomic::get_and_or(ptr, static_cast<uint32_t>(mask)));
}

EXPORT(int64_t, sceKernelAtomicGetAndOr64, uint64_t *ptr, uint64_t mask) {
    TRACY_FUNC(sceKernelAtomicGetAndOr64, ptr, mask);
    return static_cast<int64_t>(guest_atomic::get_and_or(ptr, static_cast<uint64_t>(mask)));
}

EXPORT(int32_t, sceKernelAtomicGetAndOr8, uint8_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicGetAndOr8, ptr, mask);
    return static_cast<int32_t>(static_cast<int8_t>(guest_atomic::get_and_or(ptr, static_cast<uint8_t>(mask))));
}

EXPORT(int32_t, sceKernelAtomicGetAndSet16, uint16_t *ptr, uint32_t value) {
    TRACY_FUNC(sceKernelAtomicGetAndSet16, ptr, value);
    return static_cast<int32_t>(static_cast<int16_t>(guest_atomic::get_and_set(ptr, static_cast<uint16_t>(value))));
}

EXPORT(int32_t, sceKernelAtomicGetAndSet32, uint32_t *ptr, uint32_t value) {
    TRACY_FUNC(sceKernelAtomicGetAndSet32, ptr, value);
    return static_cast<int32_t>(guest_atomic::get_and_set(ptr, static_cast<uint32_t>(value)));
}

EXPORT(int64_t, sceKernelAtomicGetAndSet64, uint64_t *ptr, uint64_t value) {
    TRACY_FUNC(sceKernelAtomicGetAndSet64, ptr, value);
    return static_cast<int64_t>(guest_atomic::get_and_set(ptr, static_cast<uint64_t>(value)));
}

EXPORT(int32_t, sceKernelAtomicGetAndSet8, uint8_t *ptr, uint32_t value) {
    TRACY_FUNC(sceKernelAtomicGetAndSet8, ptr, value);
    return static_cast<int32_t>(static_cast<int8_t>(guest_atomic::get_and_set(ptr, static_cast<uint8_t>(value))));
}

EXPORT(int32_t, sceKernelAtomicGetAndSub16, uint16_t *ptr, uint32_t value) {
    TRACY_FUNC(sceKernelAtomicGetAndSub16, ptr, value);
    return static_cast<int32_t>(static_cast<int16_t>(guest_atomic::get_and_sub(ptr, static_cast<uint16_t>(value))));
}

EXPORT(int32_t, sceKernelAtomicGetAndSub32, uint32_t *ptr, uint32_t value) {
    TRACY_FUNC(sceKernelAtomicGetAndSub32, ptr, value);
    return static_cast<int32_t>(guest_atomic::get_and_sub(ptr, static_cast<uint32_t>(value)));
}

EXPORT(int64_t, sceKernelAtomicGetAndSub64, uint64_t *ptr, uint64_t value) {
    TRACY_FUNC(sceKernelAtomicGetAndSub64, ptr, value);
    return static_cast<int64_t>(guest_atomic::get_and_sub(ptr, static_cast<uint64_t>(value)));
}

EXPORT(int32_t, sceKernelAtomicGetAndSub8, uint8_t *ptr, uint32_t value) {
    TRACY_FUNC(sceKernelAtomicGetAndSub8, ptr, value);
    return static_cast<int32_t>(static_cast<int8_t>(guest_atomic::get_and_sub(ptr, static_cast<uint8_t>(value))));
}

EXPORT(int32_t, sceKernelAtomicGetAndXor16, uint16_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicGetAndXor16, ptr, mask);
    return static_cast<int32_t>(static_cast<int16_t>(guest_atomic::get_and_xor(ptr, static_cast<uint16_t>(mask))));
}

EXPORT(int32_t, sceKernelAtomicGetAndXor32, uint32_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicGetAndXor32, ptr, mask);
    return static_cast<int32_t>(guest_atomic::get_and_xor(ptr, static_cast<uint32_t>(mask)));
}

EXPORT(int64_t, sceKernelAtomicGetAndXor64, uint64_t *ptr, uint64_t mask) {
    TRACY_FUNC(sceKernelAtomicGetAndXor64, ptr, mask);
    return static_cast<int64_t>(guest_atomic::get_and_xor(ptr, static_cast<uint64_t>(mask)));
}

EXPORT(int32_t, sceKernelAtomicGetAndXor8, uint8_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicGetAndXor8, ptr, mask);
    return static_cast<int32_t>(static_cast<int8_t>(guest_atomic::get_and_xor(ptr, static_cast<uint8_t>(mask))));
}

EXPORT(int32_t, sceKernelAtomicOrAndGet16, uint16_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicOrAndGet16, ptr, mask);
    return static_cast<int32_t>(static_cast<int16_t>(guest_atomic::or_and_get(ptr, static_cast<uint16_t>(mask))));
}

EXPORT(int32_t, sceKernelAtomicOrAndGet32, uint32_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicOrAndGet32, ptr, mask);
    return static_cast<int32_t>(guest_atomic::or_and_get(ptr, static_cast<uint32_t>(mask)));
}

EXPORT(int64_t, sceKernelAtomicOrAndGet64, uint64_t *ptr, uint64_t mask) {
    TRACY_FUNC(sceKernelAtomicOrAndGet64, ptr, mask);
    return static_cast<int64_t>(guest_atomic::or_and_get(ptr, static_cast<uint64_t>(mask)));
}

EXPORT(int32_t, sceKernelAtomicOrAndGet8, uint8_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicOrAndGet8, ptr, mask);
    return static_cast<int32_t>(static_cast<int8_t>(guest_atomic::or_and_get(ptr, static_cast<uint8_t>(mask))));
}

EXPORT(int32_t, sceKernelAtomicSet16, uint16_t *ptr, uint32_t value) {
    TRACY_FUNC(sceKernelAtomicSet16, ptr, value);
    return static_cast<int32_t>(static_cast<int16_t>(guest_atomic::set(ptr, static_cast<uint16_t>(value))));
}

EXPORT(int32_t, sceKernelAtomicSet32, uint32_t *ptr, uint32_t value) {
    TRACY_FUNC(sceKernelAtomicSet32, ptr, value);
    return static_cast<int32_t>(guest_atomic::set(ptr, static_cast<uint32_t>(value)));
}

EXPORT(int64_t, sceKernelAtomicSet64, uint64_t *ptr, uint64_t value) {
    TRACY_FUNC(sceKernelAtomicSet64, ptr, value);
    return static_cast<int64_t>(guest_atomic::set(ptr, static_cast<uint64_t>(value)));
}

EXPORT(int32_t, sceKernelAtomicSet8, uint8_t *ptr, uint32_t value) {
    TRACY_FUNC(sceKernelAtomicSet8, ptr, value);
    return static_cast<int32_t>(static_cast<int8_t>(guest_atomic::set(ptr, static_cast<uint8_t>(value))));
}

EXPORT(int32_t, sceKernelAtomicSubAndGet16, uint16_t *ptr, uint32_t value) {
    TRACY_FUNC(sceKernelAtomicSubAndGet16, ptr, value);
    return static_cast<int32_t>(static_cast<int16_t>(guest_atomic::sub_and_get(ptr, static_cast<uint16_t>(value))));
}

EXPORT(int32_t, sceKernelAtomicSubAndGet32, uint32_t *ptr, uint32_t value) {
    TRACY_FUNC(sceKernelAtomicSubAndGet32, ptr, value);
    return static_cast<int32_t>(guest_atomic::sub_and_get(ptr, static_cast<uint32_t>(value)));
}

EXPORT(int64_t, sceKernelAtomicSubAndGet64, uint64_t *ptr, uint64_t value) {
    TRACY_FUNC(sceKernelAtomicSubAndGet64, ptr, value);
    return static_cast<int64_t>(guest_atomic::sub_and_get(ptr, static_cast<uint64_t>(value)));
}

EXPORT(int32_t, sceKernelAtomicSubAndGet8, uint8_t *ptr, uint32_t value) {
    TRACY_FUNC(sceKernelAtomicSubAndGet8, ptr, value);
    return static_cast<int32_t>(static_cast<int8_t>(guest_atomic::sub_and_get(ptr, static_cast<uint8_t>(value))));
}

EXPORT(int32_t, sceKernelAtomicXorAndGet16, uint16_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicXorAndGet16, ptr, mask);
    return static_cast<int32_t>(static_cast<int16_t>(guest_atomic::xor_and_get(ptr, static_cast<uint16_t>(mask))));
}

EXPORT(int32_t, sceKernelAtomicXorAndGet32, uint32_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicXorAndGet32, ptr, mask);
    return static_cast<int32_t>(guest_atomic::xor_and_get(ptr, static_cast<uint32_t>(mask)));
}

EXPORT(int64_t, sceKernelAtomicXorAndGet64, uint64_t *ptr, uint64_t mask) {
    TRACY_FUNC(sceKernelAtomicXorAndGet64, ptr, mask);
    return static_cast<int64_t>(guest_atomic::xor_and_get(ptr, static_cast<uint64_t>(mask)));
}

EXPORT(int32_t, sceKernelAtomicXorAndGet8, uint8_t *ptr, uint32_t mask) {
    TRACY_FUNC(sceKernelAtomicXorAndGet8, ptr, mask);
    return static_cast<int32_t>(static_cast<int8_t>(guest_atomic::xor_and_get(ptr, static_cast<uint8_t>(mask))));
}

EXPORT(int, sceKernelBacktrace) {
    TRACY_FUNC(sceKernelBacktrace);
    return UNIMPLEMENTED();
}

struct SceKernelCallFrame {
    SceUInt32 sp;
    SceUInt32 pc;
};

EXPORT(int, sceKernelBacktraceSelf, Ptr<SceKernelCallFrame> pFrames, SceSize maxFrames, Ptr<SceUInt32> pFrameCount, SceUInt32 flags) {
    TRACY_FUNC(sceKernelBacktraceSelf, pFrames, maxFrames, pFrameCount, flags);
    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);
    if (!thread)
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID);

    uint32_t written = 0;
    const uint32_t frame_cap = std::min<uint32_t>(std::max<uint32_t>(maxFrames, 1), 64);
    SceKernelCallFrame *const frames = pFrames.get(emuenv.mem);
    const Address sp = read_sp(*thread->cpu);
    const Address stack_end = thread->stack_top();

    if (frames) {
        for (Address addr = sp; addr + 4 <= stack_end && written < frame_cap; addr += 4) {
            if (!Ptr<uint32_t>(addr).valid(emuenv.mem))
                break;
            const Address value = *Ptr<uint32_t>(addr).get(emuenv.mem);
            if (value < 0x80000000)
                continue;
            if (!emuenv.kernel.find_module_by_addr(value))
                continue;
            frames[written].sp = addr;
            frames[written].pc = value;
            written++;
        }
    }

    if (SceUInt32 *const count = pFrameCount.get(emuenv.mem))
        *count = written;

    return 0;
}

EXPORT(int, sceKernelCallModuleExit) {
    TRACY_FUNC(sceKernelCallModuleExit);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelCallWithChangeStack) {
    TRACY_FUNC(sceKernelCallWithChangeStack);
    return UNIMPLEMENTED();
}

EXPORT(SceInt32, sceKernelCancelEvent, SceUID event_id, SceUInt32 *num_wait_threads) {
    TRACY_FUNC(sceKernelCancelEvent, event_id, num_wait_threads);
    return CALL_EXPORT(_sceKernelCancelEvent, event_id, num_wait_threads);
}

EXPORT(SceInt32, sceKernelCancelEventFlag, SceUID event_id, SceUInt pattern, SceUInt32 *num_wait_thread) {
    TRACY_FUNC(sceKernelCancelEventFlag, event_id, pattern, num_wait_thread);
    return CALL_EXPORT(_sceKernelCancelEventFlag, event_id, pattern, num_wait_thread);
}

EXPORT(int, sceKernelCancelEventWithSetPattern) {
    TRACY_FUNC(sceKernelCancelEventWithSetPattern);
    return UNIMPLEMENTED();
}

EXPORT(SceInt32, sceKernelCancelMsgPipe, SceUID msgpipe_id, SceUInt32 *num_senders, SceUInt32 *num_receivers) {
    TRACY_FUNC(sceKernelCancelMsgPipe, msgpipe_id, num_senders, num_receivers);
    return CALL_EXPORT(_sceKernelCancelMsgPipe, msgpipe_id, num_senders, num_receivers);
}

EXPORT(SceInt32, sceKernelCancelMutex, SceUID mutex_id, SceInt32 new_count, SceUInt32 *num_wait_threads) {
    TRACY_FUNC(sceKernelCancelMutex, mutex_id, new_count, num_wait_threads);
    return CALL_EXPORT(_sceKernelCancelMutex, mutex_id, new_count, num_wait_threads);
}

EXPORT(SceInt32, sceKernelCancelRWLock, SceUID rwlock_id, SceUInt32 *num_readers, SceUInt32 *num_writers, SceInt32 flag) {
    TRACY_FUNC(sceKernelCancelRWLock, rwlock_id, num_readers, num_writers, flag);
    return CALL_EXPORT(_sceKernelCancelRWLock, rwlock_id, num_readers, num_writers, flag);
}

EXPORT(int, sceKernelCancelSema, SceUID semaId, SceInt32 setCount, SceUInt32 *pNumWaitThreads) {
    TRACY_FUNC(sceKernelCancelSema, semaId, setCount, pNumWaitThreads);
    return CALL_EXPORT(_sceKernelCancelSema, semaId, setCount, pNumWaitThreads);
}

EXPORT(SceInt32, sceKernelCancelTimer, SceUID timer_id, SceUInt32 *num_wait_threads) {
    TRACY_FUNC(sceKernelCancelTimer, timer_id, num_wait_threads);
    return CALL_EXPORT(_sceKernelCancelTimer, timer_id, num_wait_threads);
}

EXPORT(int, sceKernelChangeCurrentThreadAttr) {
    TRACY_FUNC(sceKernelChangeCurrentThreadAttr);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelCheckThreadStack) {
    TRACY_FUNC(sceKernelCheckThreadStack);
    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);
    int stack_size = read_sp(*thread->cpu) - (thread->stack.get());
    if (stack_size < 0 || stack_size > thread->stack_size)
        stack_size = 0;
    return stack_size;
}

EXPORT(int, sceKernelCloseModule) {
    TRACY_FUNC(sceKernelCloseModule);
    return UNIMPLEMENTED();
}

EXPORT(SceUID, sceKernelCreateCond, const char *pName, SceUInt32 attr, SceUID mutexId, const SceKernelCondOptParam *pOptParam) {
    TRACY_FUNC(sceKernelCreateCond, pName, attr, mutexId, pOptParam);
    return CALL_EXPORT(_sceKernelCreateCond, pName, attr, mutexId, pOptParam);
}

EXPORT(int, sceKernelCreateEventFlag, const char *name, unsigned int attr, unsigned int flags, SceKernelEventFlagOptParam *opt) {
    TRACY_FUNC(sceKernelCreateEventFlag, name, attr, flags, opt);
    return CALL_EXPORT(_sceKernelCreateEventFlag, name, attr, flags, opt);
}

EXPORT(int, sceKernelCreateLwCond, Ptr<SceKernelLwCondWork> workarea, const char *name, SceUInt attr, Ptr<SceKernelLwMutexWork> workarea_mutex, Ptr<SceKernelLwCondOptParam> opt_param) {
    TRACY_FUNC(sceKernelCreateLwCond, workarea, name, attr, workarea_mutex, opt_param);
    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);

    Ptr<SceKernelCreateLwCond_opt> options = Ptr<SceKernelCreateLwCond_opt>(stack_alloc(*thread->cpu, sizeof(SceKernelCreateLwCond_opt)));
    options.get(emuenv.mem)->workarea_mutex = workarea_mutex;
    options.get(emuenv.mem)->opt_param = opt_param;
    int res = CALL_EXPORT(_sceKernelCreateLwCond, workarea, name, attr, options);
    stack_free(*thread->cpu, sizeof(SceKernelCreateLwCond_opt));
    return res;
}

EXPORT(int, sceKernelCreateLwMutex, Ptr<SceKernelLwMutexWork> workarea, const char *name, SceUInt attr, int init_count, Ptr<SceKernelLwMutexOptParam> opt_param) {
    TRACY_FUNC(sceKernelCreateLwMutex, workarea, name, attr, init_count, opt_param);
    return CALL_EXPORT(_sceKernelCreateLwMutex, workarea, name, attr, init_count, opt_param);
}

EXPORT(int, sceKernelCreateMsgPipe, const char *name, uint32_t type, uint32_t attr, SceSize bufSize, const SceKernelCreateMsgPipeOpt *opt) {
    TRACY_FUNC(sceKernelCreateMsgPipe, name, type, attr, bufSize, opt);
    return msgpipe_create(emuenv.kernel, export_name, name, thread_id, attr, bufSize);
}

EXPORT(int, sceKernelCreateMsgPipeWithLR) {
    TRACY_FUNC(sceKernelCreateMsgPipeWithLR);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelCreateMutex, const char *name, SceUInt attr, int init_count, SceKernelMutexOptParam *opt_param) {
    TRACY_FUNC(sceKernelCreateMutex, name, attr, init_count, opt_param);
    if (attr & SCE_KERNEL_MUTEX_ATTR_CEILING) {
        STUBBED("priority ceiling feature is not supported");
    }

    return mutex_create(emuenv.kernel, emuenv.mem, export_name, name, thread_id, attr, init_count, Ptr<SceKernelLwMutexWork>(0), SyncWeight::Heavy);
}

EXPORT(SceUID, sceKernelCreateRWLock, const char *name, SceUInt32 attr, SceKernelMutexOptParam *opt_param) {
    TRACY_FUNC(sceKernelCreateRWLock, name, attr, opt_param);
    return CALL_EXPORT(_sceKernelCreateRWLock, name, attr, opt_param);
}

EXPORT(SceUID, sceKernelCreateSema, const char *name, SceUInt attr, int initVal, int maxVal, Ptr<SceKernelSemaOptParam> option) {
    TRACY_FUNC(sceKernelCreateSema, name, attr, initVal, maxVal, option);
    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);

    Ptr<SceKernelCreateSema_opt> options = Ptr<SceKernelCreateSema_opt>(stack_alloc(*thread->cpu, sizeof(SceKernelCreateSema_opt)));
    options.get(emuenv.mem)->maxVal = maxVal;
    options.get(emuenv.mem)->option = option;
    int res = CALL_EXPORT(_sceKernelCreateSema, name, attr, initVal, options);
    stack_free(*thread->cpu, sizeof(SceKernelCreateSema_opt));
    return res;
}

EXPORT(int, sceKernelCreateSema_16XX, const char *name, SceUInt attr, int initVal, int maxVal, Ptr<SceKernelSemaOptParam> option) {
    TRACY_FUNC(sceKernelCreateSema_16XX, name, attr, initVal, maxVal, option);
    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);

    Ptr<SceKernelCreateSema_opt> options = Ptr<SceKernelCreateSema_opt>(stack_alloc(*thread->cpu, sizeof(SceKernelCreateSema_opt)));
    options.get(emuenv.mem)->maxVal = maxVal;
    options.get(emuenv.mem)->option = option;
    int res = CALL_EXPORT(_sceKernelCreateSema, name, attr, initVal, options);
    stack_free(*thread->cpu, sizeof(SceKernelCreateSema_opt));
    return res;
}

EXPORT(SceUID, sceKernelCreateSimpleEvent, const char *name, SceUInt32 attr, SceUInt32 init_pattern, const SceKernelSimpleEventOptParam *pOptParam) {
    TRACY_FUNC(sceKernelCreateSimpleEvent, name, attr, init_pattern, pOptParam);
    return CALL_EXPORT(_sceKernelCreateSimpleEvent, name, attr, init_pattern, pOptParam);
}

EXPORT(SceUID, sceKernelCreateThread, const char *name, SceKernelThreadEntry entry, int init_priority, int stack_size, SceUInt attr, int cpu_affinity_mask, Ptr<SceKernelThreadOptParam> option) {
    TRACY_FUNC(sceKernelCreateThread, name, entry, init_priority, stack_size, attr, cpu_affinity_mask, option);
    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);

    auto options = Ptr<SceKernelCreateThread_opt>(stack_alloc(*thread->cpu, sizeof(SceKernelCreateThread_opt))).get(emuenv.mem);
    options->stack_size = stack_size;
    options->attr = attr;
    options->cpu_affinity_mask = cpu_affinity_mask;
    options->option = option;
    SceUID res = CALL_EXPORT(sceKernelCreateThreadForUser, name, entry, init_priority, options);
    stack_free(*thread->cpu, sizeof(SceKernelCreateThread_opt));
    return res;
}

EXPORT(SceUID, sceKernelCreateTimer, const char *name, SceUInt32 attr, const uint32_t *opt_params) {
    TRACY_FUNC(sceKernelCreateTimer, name, attr, opt_params);
    return CALL_EXPORT(_sceKernelCreateTimer, name, attr, opt_params);
}

EXPORT(int, sceKernelDeleteLwCond, Ptr<SceKernelLwCondWork> workarea) {
    TRACY_FUNC(sceKernelDeleteLwCond, workarea);
    SceUID lightweight_condition_id = workarea.get(emuenv.mem)->uid;

    return condvar_delete(emuenv.kernel, export_name, thread_id, lightweight_condition_id, SyncWeight::Light);
}

EXPORT(int, sceKernelDeleteLwMutex, Ptr<SceKernelLwMutexWork> workarea) {
    TRACY_FUNC(sceKernelDeleteLwMutex, workarea);
    return CALL_EXPORT(_sceKernelDeleteLwMutex, workarea);
}

EXPORT(int, sceKernelExitProcess, int res) {
    TRACY_FUNC(sceKernelExitProcess, res);
    emuenv.kernel.request_process_exit(res);
    return SCE_KERNEL_OK;
}

EXPORT(SceInt32, sceKernelGetCallbackInfo, SceUID callbackId, Ptr<SceKernelCallbackInfo> pInfo) {
    TRACY_FUNC(sceKernelGetCallbackInfo, callbackId, pInfo);
    return CALL_EXPORT(_sceKernelGetCallbackInfo, callbackId, pInfo.get(emuenv.mem));
}

EXPORT(SceInt32, sceKernelGetCondInfo, SceUID condId, Ptr<SceKernelCondInfo> pInfo) {
    TRACY_FUNC(sceKernelGetCondInfo, condId, pInfo);
    return CALL_EXPORT(_sceKernelGetCondInfo, condId, pInfo);
}

EXPORT(int, sceKernelGetCurrentThreadVfpException) {
    TRACY_FUNC(sceKernelGetCurrentThreadVfpException);
    return emuenv.kernel.get_thread(thread_id)->tls.get_ptr<int>().get(emuenv.mem)[TLS_VFP_EXCEPTION];
}

EXPORT(SceInt32, sceKernelGetEventFlagInfo, SceUID evfId, Ptr<SceKernelEventFlagInfo> pInfo) {
    TRACY_FUNC(sceKernelGetEventFlagInfo, evfId, pInfo);
    return CALL_EXPORT(_sceKernelGetEventFlagInfo, evfId, pInfo);
}

EXPORT(int, sceKernelGetEventInfo) {
    TRACY_FUNC(sceKernelGetEventInfo);
    return UNIMPLEMENTED();
}

EXPORT(SceInt32, sceKernelGetEventPattern, SceUID event_id, SceUInt32 *get_pattern) {
    TRACY_FUNC(sceKernelGetEventPattern, event_id, get_pattern);
    return CALL_EXPORT(_sceKernelGetEventPattern, event_id, get_pattern);
}

EXPORT(int, sceKernelGetLwCondInfo) {
    TRACY_FUNC(sceKernelGetLwCondInfo);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelGetLwCondInfoById) {
    TRACY_FUNC(sceKernelGetLwCondInfoById);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelGetLwMutexInfoById, SceUID lightweight_mutex_id, Ptr<SceKernelLwMutexInfo> info) {
    TRACY_FUNC(sceKernelGetLwMutexInfoById, lightweight_mutex_id, info);
    SceSize size = 0;
    if (info) {
        size = info.get(emuenv.mem)->size;
    }
    return CALL_EXPORT(_sceKernelGetLwMutexInfoById, lightweight_mutex_id, info, size);
}

EXPORT(int, sceKernelGetLwMutexInfo, Ptr<SceKernelLwMutexWork> workarea, Ptr<SceKernelLwMutexInfo> info) {
    TRACY_FUNC(sceKernelGetLwMutexInfo, workarea, info);
    return CALL_EXPORT(sceKernelGetLwMutexInfoById, workarea.get(emuenv.mem)->uid, info);
}

EXPORT(int, sceKernelGetModuleInfoByAddr, Ptr<void> addr, SceKernelModuleInfo *info) {
    TRACY_FUNC(sceKernelGetModuleInfoByAddr, addr, info);
    const auto mod = emuenv.kernel.find_module_by_addr(addr.address());
    if (mod) {
        *info = *mod;
        return SCE_KERNEL_OK;
    }

    return RET_ERROR(SCE_KERNEL_ERROR_MODULEMGR_NOENT);
}

EXPORT(int, sceKernelGetMsgPipeInfo) {
    TRACY_FUNC(sceKernelGetMsgPipeInfo);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelGetMutexInfo, SceUID mutexId, SceKernelMutexInfo *info) {
    TRACY_FUNC(sceKernelGetMutexInfo, mutexId, info);
    return CALL_EXPORT(_sceKernelGetMutexInfo, mutexId, info);
}

EXPORT(int, sceKernelGetOpenPsId) {
    TRACY_FUNC(sceKernelGetOpenPsId);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelGetPMUSERENR) {
    TRACY_FUNC(sceKernelGetPMUSERENR);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelGetProcessTime, SceUInt64 *time) {
    TRACY_FUNC(sceKernelGetProcessTime, time);
    if (time) {
        *time = emuenv.kernel.process_time_us();
    }
    return 0;
}

EXPORT(SceUInt32, sceKernelGetProcessTimeLow) {
    TRACY_FUNC(sceKernelGetProcessTimeLow);
    return static_cast<SceUInt32>(emuenv.kernel.process_time_us());
}

EXPORT(SceUInt64, sceKernelGetProcessTimeWide) {
    TRACY_FUNC(sceKernelGetProcessTimeWide);
    return emuenv.kernel.process_time_us();
}

EXPORT(int, sceKernelGetRWLockInfo, SceUID rwlockId, SceKernelRWLockInfo *info) {
    TRACY_FUNC(sceKernelGetRWLockInfo, rwlockId, info);
    return CALL_EXPORT(_sceKernelGetRWLockInfo, rwlockId, info);
}

EXPORT(SceInt32, sceKernelGetSemaInfo, SceUID semaId, Ptr<SceKernelSemaInfo> pInfo) {
    TRACY_FUNC(sceKernelGetSemaInfo, semaId, pInfo);
    return CALL_EXPORT(_sceKernelGetSemaInfo, semaId, pInfo);
}

EXPORT(int, sceKernelGetSystemInfo) {
    TRACY_FUNC(sceKernelGetSystemInfo);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelGetSystemTime) {
    TRACY_FUNC(sceKernelGetSystemTime);
    return UNIMPLEMENTED();
}

EXPORT(Ptr<Ptr<void>>, sceKernelGetTLSAddr, int key) {
    TRACY_FUNC(sceKernelGetTLSAddr, key);
    return emuenv.kernel.get_thread_tls_addr(emuenv.mem, thread_id, key);
}

EXPORT(int, sceKernelGetThreadContextForVM, SceUID threadId, Ptr<SceKernelThreadCpuRegisterInfo> pCpuRegisterInfo, Ptr<SceKernelThreadVfpRegisterInfo> pVfpRegisterInfo) {
    TRACY_FUNC(sceKernelGetThreadContextForVM, threadId, pCpuRegisterInfo, pVfpRegisterInfo);
    return CALL_EXPORT(_sceKernelGetThreadContextForVM, threadId, pCpuRegisterInfo, pVfpRegisterInfo);
}

EXPORT(SceInt32, sceKernelGetThreadCpuAffinityMask2, SceUID thid) {
    TRACY_FUNC(sceKernelGetThreadCpuAffinityMask2, thid);
    const SceInt32 affinity = CALL_EXPORT(_sceKernelGetThreadCpuAffinityMask, thid);

    // I guess the difference between this function and sceKernelGetThreadCpuAffinityMask
    // is that if the result is SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT (=0)
    // it gets converted into its real affinity (SCE_KERNEL_CPU_MASK_USER_ALL)
    // either way without this dragon quest builder does not boot
    if (affinity == SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT)
        return SCE_KERNEL_CPU_MASK_USER_ALL;

    return affinity;
}

EXPORT(SceInt32, sceKernelGetThreadCurrentPriority) {
    TRACY_FUNC(sceKernelGetThreadCurrentPriority);
    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);

    if (!thread)
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID);

    return thread->priority;
}

EXPORT(int, sceKernelGetThreadEventInfo) {
    TRACY_FUNC(sceKernelGetThreadEventInfo);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelGetThreadExitStatus, SceUID thid, SceInt32 *pExitStatus) {
    TRACY_FUNC(sceKernelGetThreadExitStatus, thid, pExitStatus);
    const ThreadStatePtr thread = emuenv.kernel.get_thread(thid ? thid : thread_id);
    if (!thread) {
        return SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID;
    }
    if (thread->status != ThreadStatus::dormant) {
        return SCE_KERNEL_ERROR_NOT_DORMANT;
    }
    if (pExitStatus) {
        *pExitStatus = thread->returned_value;
    }
    return 0;
}

EXPORT(int, sceKernelGetThreadId) {
    TRACY_FUNC(sceKernelGetThreadId);
    return thread_id;
}

EXPORT(SceInt32, sceKernelGetThreadInfo, SceUID threadId, Ptr<SceKernelThreadInfo> pInfo) {
    TRACY_FUNC(sceKernelGetThreadInfo, threadId, pInfo);
    return CALL_EXPORT(_sceKernelGetThreadInfo, threadId, pInfo);
}

EXPORT(int, sceKernelGetThreadRunStatus) {
    TRACY_FUNC(sceKernelGetThreadRunStatus);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelGetTimerBase, SceUID timer_handle, SceKernelSysClock *time) {
    TRACY_FUNC(sceKernelGetTimerBase, timer_handle, time);
    const TimerPtr timer_info = lock_and_find(timer_handle, emuenv.kernel.timers, emuenv.kernel.mutex);

    auto object_lock = timer_info ? timer_info->lock() : std::unique_lock<std::mutex>();
    if (!object_lock)
        return SCE_KERNEL_ERROR_UNKNOWN_TIMER_ID;

    *time = timer_info->time;

    return 0;
}

EXPORT(int, sceKernelGetTimerEventRemainingTime) {
    TRACY_FUNC(sceKernelGetTimerEventRemainingTime);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelGetTimerInfo) {
    TRACY_FUNC(sceKernelGetTimerInfo);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelGetTimerTime, SceUID timer_handle, SceKernelSysClock *time) {
    TRACY_FUNC(sceKernelGetTimerTime, timer_handle, time);
    const TimerPtr timer_info = lock_and_find(timer_handle, emuenv.kernel.timers, emuenv.kernel.mutex);

    auto object_lock = timer_info ? timer_info->lock() : std::unique_lock<std::mutex>();
    if (!object_lock)
        return SCE_KERNEL_ERROR_UNKNOWN_TIMER_ID;

    *time = get_current_time() - timer_info->time;

    return 0;
}

EXPORT(SceUID, sceKernelLoadModule, char *path, int flags, SceKernelLMOption *option) {
    TRACY_FUNC(sceKernelLoadModule, path, flags, option);
    return CALL_EXPORT(_sceKernelLoadModule, path, flags, option);
}

EXPORT(SceUID, sceKernelLoadStartModule, const char *moduleFileName, SceSize args, Ptr<const void> argp, SceUInt32 flags, const SceKernelLMOption *pOpt, int *pRes) {
    TRACY_FUNC(sceKernelLoadStartModule, moduleFileName, args, argp, flags, pOpt, pRes);
    return CALL_EXPORT(_sceKernelLoadStartModule, moduleFileName, args, argp, flags, pOpt, pRes);
}

EXPORT(int, sceKernelLockLwMutex, Ptr<SceKernelLwMutexWork> workarea, int lock_count, unsigned int *ptimeout) {
    TRACY_FUNC(sceKernelLockLwMutex, workarea, lock_count, ptimeout);
    return CALL_EXPORT(_sceKernelLockLwMutex, workarea, lock_count, ptimeout);
}

EXPORT(int, sceKernelLockLwMutex_0, Ptr<SceKernelLwMutexWork> workarea, int lock_count, unsigned int *ptimeout) {
    TRACY_FUNC(sceKernelLockLwMutex_0, workarea, lock_count, ptimeout);
    return CALL_EXPORT(_sceKernelLockLwMutex, workarea, lock_count, ptimeout);
}

EXPORT(int, sceKernelLockLwMutexCB, Ptr<SceKernelLwMutexWork> workarea, int lock_count, unsigned int *ptimeout) {
    TRACY_FUNC(sceKernelLockLwMutexCB, workarea, lock_count, ptimeout);
    emuenv.kernel.get_thread(thread_id)->process_callbacks();
    if (!workarea)
        return RET_ERROR(SCE_KERNEL_ERROR_INVALID_ARGUMENT);

    const auto lwmutexid = workarea.get(emuenv.mem)->uid;
    return mutex_lock(emuenv.kernel, emuenv.mem, export_name, thread_id, lwmutexid, lock_count, ptimeout, SyncWeight::Light, true);
}

EXPORT(int, sceKernelLockMutex, SceUID mutexid, int lock_count, unsigned int *timeout) {
    TRACY_FUNC(sceKernelLockMutex, mutexid, lock_count, timeout);
    return CALL_EXPORT(_sceKernelLockMutex, mutexid, lock_count, timeout);
}

EXPORT(SceInt32, sceKernelLockMutexCB, SceUID mutexId, SceInt32 lockCount, SceUInt32 *pTimeout) {
    TRACY_FUNC(sceKernelLockMutexCB, mutexId, lockCount, pTimeout);
    return CALL_EXPORT(_sceKernelLockMutexCB, mutexId, lockCount, pTimeout);
}

EXPORT(SceInt32, sceKernelLockReadRWLock, SceUID lock_id, SceUInt32 *timeout) {
    TRACY_FUNC(sceKernelLockReadRWLock, lock_id, timeout);
    return CALL_EXPORT(_sceKernelLockReadRWLock, lock_id, timeout);
}

EXPORT(SceInt32, sceKernelLockReadRWLockCB, SceUID lock_id, SceUInt32 *timeout) {
    TRACY_FUNC(sceKernelLockReadRWLockCB, lock_id, timeout);
    return CALL_EXPORT(_sceKernelLockReadRWLockCB, lock_id, timeout);
}

EXPORT(SceInt32, sceKernelLockWriteRWLock, SceUID lock_id, SceUInt32 *timeout) {
    TRACY_FUNC(sceKernelLockWriteRWLock, lock_id, timeout);
    return CALL_EXPORT(_sceKernelLockWriteRWLock, lock_id, timeout);
}

EXPORT(SceInt32, sceKernelLockWriteRWLockCB, SceUID lock_id, SceUInt32 *timeout) {
    TRACY_FUNC(sceKernelLockWriteRWLockCB, lock_id, timeout);
    return CALL_EXPORT(_sceKernelLockWriteRWLockCB, lock_id, timeout);
}

EXPORT(int, sceKernelOpenModule) {
    TRACY_FUNC(sceKernelOpenModule);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelPMonThreadGetCounter) {
    TRACY_FUNC(sceKernelPMonThreadGetCounter);
    return UNIMPLEMENTED();
}

EXPORT(SceInt32, sceKernelPollEvent, SceUID event_id, SceUInt32 bit_pattern, SceUInt32 *result_pattern, SceUInt64 *user_data) {
    TRACY_FUNC(sceKernelPollEvent, event_id, bit_pattern, result_pattern, user_data);
    return CALL_EXPORT(_sceKernelPollEvent, event_id, bit_pattern, result_pattern, user_data);
}

EXPORT(int, sceKernelPollEventFlag, SceUID event_id, unsigned int flags, unsigned int wait, unsigned int *outBits) {
    TRACY_FUNC(sceKernelPollEventFlag, event_id, flags, wait, outBits);
    return CALL_EXPORT(_sceKernelPollEventFlag, event_id, flags, wait, outBits);
}

EXPORT(int, sceKernelPrintBacktrace) {
    TRACY_FUNC(sceKernelPrintBacktrace);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelPulseEventWithNotifyCallback) {
    TRACY_FUNC(sceKernelPulseEventWithNotifyCallback);
    return UNIMPLEMENTED();
}

static SceInt32 receive_msg_pipe(EmuEnvState &emuenv, const char *export_name, SceUID thread_id, SceUID msgPipeId, void *pRecvBuf, SceSize recvSize, SceUInt32 waitMode, SceSize *pResult, SceUInt32 *pTimeout, bool callbacks) {
    const auto ret = msgpipe_recv(emuenv.kernel, export_name, thread_id, msgPipeId, waitMode, pRecvBuf, recvSize, pTimeout, callbacks, pResult, true);
    if (static_cast<int>(ret) < 0) {
        return ret;
    }
    if (pResult) {
        *pResult = ret;
    }
    return SCE_KERNEL_OK;
}

EXPORT(SceInt32, sceKernelReceiveMsgPipe, SceUID msgPipeId, void *pRecvBuf, SceSize recvSize, SceUInt32 waitMode, SceSize *pResult, SceUInt32 *pTimeout) {
    TRACY_FUNC(sceKernelReceiveMsgPipe, msgPipeId, pRecvBuf, recvSize, waitMode, pResult, pTimeout);
    return receive_msg_pipe(emuenv, export_name, thread_id, msgPipeId, pRecvBuf, recvSize, waitMode, pResult, pTimeout, false);
}

EXPORT(SceInt32, sceKernelReceiveMsgPipeCB, SceUID msgPipeId, void *pRecvBuf, SceSize recvSize, SceUInt32 waitMode, SceSize *pResult, SceUInt32 *pTimeout) {
    TRACY_FUNC(sceKernelReceiveMsgPipeCB, msgPipeId, pRecvBuf, recvSize, waitMode, pResult, pTimeout);
    emuenv.kernel.get_thread(thread_id)->process_callbacks();
    return receive_msg_pipe(emuenv, export_name, thread_id, msgPipeId, pRecvBuf, recvSize, waitMode, pResult, pTimeout, true);
}

EXPORT(int, sceKernelReceiveMsgPipeVector) {
    TRACY_FUNC(sceKernelReceiveMsgPipeVector);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelReceiveMsgPipeVectorCB) {
    TRACY_FUNC(sceKernelReceiveMsgPipeVectorCB);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelRegisterThreadEventHandler, const char *name, SceUID thread_mask, SceUInt32 mask, Ptr<const void> handler, Address common) {
    TRACY_FUNC(sceKernelRegisterThreadEventHandler, name, thread_mask, mask, handler, common);
    sceKernelRegisterThreadEventHandlerOpt handler_opt = {
        .handler = handler,
        .common = common
    };
    return CALL_EXPORT(_sceKernelRegisterThreadEventHandler, name, thread_mask, mask, &handler_opt);
}

static SceInt32 send_msg_pipe(EmuEnvState &emuenv, const char *export_name, SceUID thread_id, SceUID msgPipeId, const void *pSendBuf, SceSize sendSize, SceUInt32 waitMode, SceSize *pResult, SceUInt32 *pTimeout, bool callbacks) {
    const auto ret = msgpipe_send(emuenv.kernel, export_name, thread_id, msgPipeId, waitMode, pSendBuf, sendSize, pTimeout, callbacks, pResult, true);
    if (static_cast<int>(ret) < 0) {
        return ret;
    }
    if (pResult) {
        *pResult = ret;
    }
    return SCE_KERNEL_OK;
}

EXPORT(SceInt32, sceKernelSendMsgPipe, SceUID msgPipeId, const void *pSendBuf, SceSize sendSize, SceUInt32 waitMode, SceSize *pResult, SceUInt32 *pTimeout) {
    TRACY_FUNC(sceKernelSendMsgPipe, msgPipeId, pSendBuf, sendSize, waitMode, pResult, pTimeout);
    return send_msg_pipe(emuenv, export_name, thread_id, msgPipeId, pSendBuf, sendSize, waitMode, pResult, pTimeout, false);
}

EXPORT(SceInt32, sceKernelSendMsgPipeCB, SceUID msgPipeId, const void *pSendBuf, SceSize sendSize, SceUInt32 waitMode, SceSize *pResult, SceUInt32 *pTimeout) {
    TRACY_FUNC(sceKernelSendMsgPipeCB, msgPipeId, pSendBuf, sendSize, waitMode, pResult, pTimeout);
    emuenv.kernel.get_thread(thread_id)->process_callbacks();
    return send_msg_pipe(emuenv, export_name, thread_id, msgPipeId, pSendBuf, sendSize, waitMode, pResult, pTimeout, true);
}

EXPORT(int, sceKernelSendMsgPipeVector) {
    TRACY_FUNC(sceKernelSendMsgPipeVector);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelSendMsgPipeVectorCB) {
    TRACY_FUNC(sceKernelSendMsgPipeVectorCB);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelSetEventWithNotifyCallback) {
    TRACY_FUNC(sceKernelSetEventWithNotifyCallback);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelSetThreadContextForVM, SceUID threadId, Ptr<SceKernelThreadCpuRegisterInfo> pCpuRegisterInfo, Ptr<SceKernelThreadVfpRegisterInfo> pVfpRegisterInfo) {
    TRACY_FUNC(sceKernelSetThreadContextForVM, threadId, pCpuRegisterInfo, pVfpRegisterInfo);
    return CALL_EXPORT(_sceKernelSetThreadContextForVM, threadId, pCpuRegisterInfo, pVfpRegisterInfo);
}

EXPORT(int, sceKernelSetTimerEvent, SceUID timer_handle, SceInt32 type, SceKernelSysClock *interval, SceInt32 repeats) {
    TRACY_FUNC(sceKernelSetTimerEvent, timer_handle, type, interval, repeats);

    return timer_set(emuenv.kernel, export_name, thread_id, timer_handle, type, interval, repeats);
}

EXPORT(int, sceKernelSetTimerTime) {
    TRACY_FUNC(sceKernelSetTimerTime);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelSignalLwCond, Ptr<SceKernelLwCondWork> workarea) {
    TRACY_FUNC(sceKernelSignalLwCond, workarea);
    SceUID condid = workarea.get(emuenv.mem)->uid;
    return condvar_signal(emuenv.kernel, export_name, thread_id, condid,
        Condvar::SignalTarget(Condvar::SignalTarget::Type::Any), SyncWeight::Light);
}

EXPORT(int, sceKernelSignalLwCondAll, Ptr<SceKernelLwCondWork> workarea) {
    TRACY_FUNC(sceKernelSignalLwCondAll, workarea);
    SceUID condid = workarea.get(emuenv.mem)->uid;
    return condvar_signal(emuenv.kernel, export_name, thread_id, condid,
        Condvar::SignalTarget(Condvar::SignalTarget::Type::All), SyncWeight::Light);
}

EXPORT(int, sceKernelSignalLwCondTo, Ptr<SceKernelLwCondWork> workarea, SceUID thread_target) {
    TRACY_FUNC(sceKernelSignalLwCondTo, workarea, thread_target);
    SceUID condid = workarea.get(emuenv.mem)->uid;
    return condvar_signal(emuenv.kernel, export_name, thread_id, condid,
        Condvar::SignalTarget(Condvar::SignalTarget::Type::Specific, thread_target), SyncWeight::Light);
}

EXPORT(int, sceKernelStackChkFail) {
    TRACY_FUNC(sceKernelStackChkFail);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelStartModule, SceUID uid, SceSize args, Ptr<const void> argp, SceUInt32 flags, const SceKernelStartModuleOpt *pOpt, int *pRes) {
    TRACY_FUNC(sceKernelStartModule, uid, args, argp, flags, pOpt, pRes);
    return CALL_EXPORT(_sceKernelStartModule, uid, args, argp, flags, pOpt, pRes);
}

EXPORT(int, sceKernelStartThread, SceUID thid, SceSize arglen, const Ptr<void> argp) {
    TRACY_FUNC(sceKernelStartThread, thid, arglen, argp);
    return CALL_EXPORT(_sceKernelStartThread, thid, arglen, argp);
}

EXPORT(int, sceKernelStopModule, SceUID uid, SceSize args, Ptr<const void> argp, SceUInt32 flags, const SceKernelStopModuleOpt *pOpt, int *pRes) {
    TRACY_FUNC(sceKernelStopModule, uid, args, argp, flags, pOpt, pRes);
    return CALL_EXPORT(_sceKernelStopModule, uid, args, argp, flags, pOpt, pRes);
}

EXPORT(int, sceKernelStopUnloadModule, SceUID uid, SceSize args, Ptr<const void> argp, SceUInt32 flags, const void *pOpt, int *pRes) {
    TRACY_FUNC(sceKernelStopUnloadModule, uid, args, argp, flags, pOpt, pRes);
    return CALL_EXPORT(_sceKernelStopUnloadModule, uid, args, argp, flags, pOpt, pRes);
}

EXPORT(int, sceKernelTryLockLwMutex, Ptr<SceKernelLwMutexWork> workarea, int lock_count) {
    TRACY_FUNC(sceKernelTryLockLwMutex, workarea, lock_count);
    const auto lwmutexid = workarea.get(emuenv.mem)->uid;
    return mutex_try_lock(emuenv.kernel, emuenv.mem, export_name, thread_id, lwmutexid, lock_count, SyncWeight::Light);
}

EXPORT(int, sceKernelTryLockLwMutex_16XX, Ptr<SceKernelLwMutexWork> workarea, int lock_count) {
    TRACY_FUNC(sceKernelTryLockLwMutex_16XX, workarea, lock_count);
    return CALL_EXPORT(sceKernelTryLockLwMutex, workarea, lock_count);
}

EXPORT(int, sceKernelTryReceiveMsgPipe, SceUID msgpipe_id, char *recv_buf, SceSize msg_size, SceUInt32 wait_mode, SceSize *result) {
    TRACY_FUNC(sceKernelTryReceiveMsgPipe, msgpipe_id, recv_buf, msg_size, wait_mode, result);
    const auto ret = msgpipe_recv(emuenv.kernel, export_name, thread_id, msgpipe_id, wait_mode | SCE_KERNEL_MSG_PIPE_MODE_DONT_WAIT, recv_buf, msg_size, 0, false);
    if (ret == 0) {
        return SCE_KERNEL_ERROR_MPP_EMPTY;
    }
    if (result) {
        *result = ret;
    }
    return SCE_KERNEL_OK;
}

EXPORT(int, sceKernelTryReceiveMsgPipeVector) {
    TRACY_FUNC(sceKernelTryReceiveMsgPipeVector);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelTrySendMsgPipe, SceUID msgPipeId, const void *pSendBuf, SceSize sendSize, SceUInt32 waitMode, SceSize *pResult) {
    TRACY_FUNC(sceKernelTrySendMsgPipe, msgPipeId, pSendBuf, sendSize, waitMode, pResult);
    STUBBED("");
    waitMode |= SCE_KERNEL_MSG_PIPE_MODE_DONT_WAIT;
    SceUInt32 pTimeout = 0;
    const auto ret = msgpipe_send(emuenv.kernel, export_name, thread_id, msgPipeId, waitMode, pSendBuf, sendSize, &pTimeout, false);
    if (static_cast<int>(ret) < 0) {
        return ret;
    }
    if (pResult) {
        *pResult = ret;
    }
    return SCE_KERNEL_OK;
    // return UNIMPLEMENTED();
}

EXPORT(int, sceKernelTrySendMsgPipeVector) {
    TRACY_FUNC(sceKernelTrySendMsgPipeVector);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelUnloadModule, SceUID uid, SceUInt32 flags, const void *pOpt) {
    TRACY_FUNC(sceKernelUnloadModule, uid, flags, pOpt);
    return CALL_EXPORT(_sceKernelUnloadModule, uid, flags, pOpt);
}

EXPORT(int, sceKernelUnlockLwMutex, Ptr<SceKernelLwMutexWork> workarea, int unlock_count) {
    TRACY_FUNC(sceKernelUnlockLwMutex, workarea, unlock_count);
    const auto lwmutexid = workarea.get(emuenv.mem)->uid;
    return mutex_unlock(emuenv.kernel, emuenv.mem, export_name, thread_id, lwmutexid, unlock_count, SyncWeight::Light);
}

EXPORT(int, sceKernelUnlockLwMutex_0, Ptr<SceKernelLwMutexWork> workarea, int unlock_count) {
    TRACY_FUNC(sceKernelUnlockLwMutex_0, workarea, unlock_count);
    return CALL_EXPORT(sceKernelUnlockLwMutex, workarea, unlock_count);
}

EXPORT(int, sceKernelUnlockLwMutex2, Ptr<SceKernelLwMutexWork> workarea, int unlock_count) {
    TRACY_FUNC(sceKernelUnlockLwMutex2, workarea, unlock_count);
    const auto lwmutexid = workarea.get(emuenv.mem)->uid;
    return mutex_unlock(emuenv.kernel, emuenv.mem, export_name, thread_id, lwmutexid, unlock_count, SyncWeight::Light);
}

EXPORT(SceInt32, sceKernelWaitCond, SceUID condId, SceUInt32 *pTimeout) {
    TRACY_FUNC(sceKernelWaitCond, condId, pTimeout);
    return CALL_EXPORT(_sceKernelWaitCond, condId, pTimeout);
}

EXPORT(SceInt32, sceKernelWaitCondCB, SceUID condId, SceUInt32 *pTimeout) {
    TRACY_FUNC(sceKernelWaitCondCB, condId, pTimeout);
    return CALL_EXPORT(_sceKernelWaitCondCB, condId, pTimeout);
}

EXPORT(SceInt32, sceKernelWaitEvent, SceUID event_id, SceUInt32 bit_pattern, SceUInt32 *result_pattern, SceUInt64 *user_data, SceUInt32 *timeout) {
    TRACY_FUNC(sceKernelWaitEvent, event_id, bit_pattern, result_pattern, user_data, timeout);
    return CALL_EXPORT(_sceKernelWaitEvent, event_id, bit_pattern, result_pattern, user_data, timeout);
}

EXPORT(SceInt32, sceKernelWaitEventCB, SceUID event_id, SceUInt32 bit_pattern, SceUInt32 *result_pattern, SceUInt64 *user_data, SceUInt32 *timeout) {
    TRACY_FUNC(sceKernelWaitEventCB, event_id, bit_pattern, result_pattern, user_data, timeout);
    return CALL_EXPORT(_sceKernelWaitEventCB, event_id, bit_pattern, result_pattern, user_data, timeout);
}

EXPORT(SceInt32, sceKernelWaitEventFlag, SceUID evfId, SceUInt32 bitPattern, SceUInt32 waitMode, SceUInt32 *pResultPat, SceUInt32 *pTimeout) {
    TRACY_FUNC(sceKernelWaitEventFlag, evfId, bitPattern, waitMode, pResultPat, pTimeout);
    return eventflag_wait(emuenv.kernel, export_name, thread_id, evfId, bitPattern, waitMode, pResultPat, pTimeout, false);
}

EXPORT(SceInt32, sceKernelWaitEventFlagCB, SceUID evfId, SceUInt32 bitPattern, SceUInt32 waitMode, SceUInt32 *pResultPat, SceUInt32 *pTimeout) {
    TRACY_FUNC(sceKernelWaitEventFlagCB, evfId, bitPattern, waitMode, pResultPat, pTimeout);
    return CALL_EXPORT(_sceKernelWaitEventFlagCB, evfId, bitPattern, waitMode, pResultPat, pTimeout);
}

EXPORT(int, sceKernelWaitException) {
    TRACY_FUNC(sceKernelWaitException);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelWaitExceptionCB) {
    TRACY_FUNC(sceKernelWaitExceptionCB);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelWaitLwCond, Ptr<SceKernelLwCondWork> workarea, SceUInt32 *timeout) {
    TRACY_FUNC(sceKernelWaitLwCond, workarea, timeout);
    const auto cond_id = workarea.get(emuenv.mem)->uid;
    return condvar_wait(emuenv.kernel, emuenv.mem, export_name, thread_id, cond_id, timeout, SyncWeight::Light, false);
}

EXPORT(SceInt32, sceKernelWaitLwCondCB, Ptr<SceKernelLwCondWork> pWork, SceUInt32 *pTimeout) {
    TRACY_FUNC(sceKernelWaitLwCondCB, pWork, pTimeout);
    return CALL_EXPORT(_sceKernelWaitLwCondCB, pWork, pTimeout);
}

EXPORT(int, sceKernelWaitMultipleEvents) {
    TRACY_FUNC(sceKernelWaitMultipleEvents);
    return UNIMPLEMENTED();
}

EXPORT(int, sceKernelWaitMultipleEventsCB) {
    TRACY_FUNC(sceKernelWaitMultipleEventsCB);
    return UNIMPLEMENTED();
}

EXPORT(SceInt32, sceKernelWaitSema, SceUID semaId, SceInt32 needCount, SceUInt32 *pTimeout) {
    TRACY_FUNC(sceKernelWaitSema, semaId, needCount, pTimeout);
    return CALL_EXPORT(_sceKernelWaitSema, semaId, needCount, pTimeout);
}

EXPORT(SceInt32, sceKernelWaitSemaCB, SceUID semaId, SceInt32 needCount, SceUInt32 *pTimeout) {
    TRACY_FUNC(sceKernelWaitSemaCB, semaId, needCount, pTimeout);
    return CALL_EXPORT(_sceKernelWaitSemaCB, semaId, needCount, pTimeout);
}

EXPORT(int, sceKernelWaitSignal, uint32_t unknown, uint32_t delay, uint32_t timeout) {
    TRACY_FUNC(sceKernelWaitSignal, unknown, delay, timeout);
    return CALL_EXPORT(_sceKernelWaitSignal, unknown, delay, timeout);
}

EXPORT(int, sceKernelWaitSignalCB, uint32_t unknown, uint32_t delay, uint32_t timeout) {
    TRACY_FUNC(sceKernelWaitSignalCB, unknown, delay, timeout);
    return CALL_EXPORT(_sceKernelWaitSignalCB, unknown, delay, timeout);
}

EXPORT(int, sceKernelWaitThreadEnd, SceUID thid, int *stat, SceUInt *timeout) {
    TRACY_FUNC(sceKernelWaitThreadEnd, thid, stat, timeout);
    return CALL_EXPORT(_sceKernelWaitThreadEnd, thid, stat, timeout);
}

EXPORT(int, sceKernelWaitThreadEndCB, SceUID thid, int *stat, SceUInt *timeout) {
    TRACY_FUNC(sceKernelWaitThreadEndCB, thid, stat, timeout);
    return CALL_EXPORT(_sceKernelWaitThreadEndCB, thid, stat, timeout);
}

EXPORT(int, sceSblACMgrIsGameProgram) {
    TRACY_FUNC(sceSblACMgrIsGameProgram);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrAdhocBB160Auth1) {
    TRACY_FUNC(sceSblGcAuthMgrAdhocBB160Auth1);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrAdhocBB160Auth2) {
    TRACY_FUNC(sceSblGcAuthMgrAdhocBB160Auth2);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrAdhocBB160Auth3) {
    TRACY_FUNC(sceSblGcAuthMgrAdhocBB160Auth3);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrAdhocBB160Auth4) {
    TRACY_FUNC(sceSblGcAuthMgrAdhocBB160Auth4);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrAdhocBB160Auth5) {
    TRACY_FUNC(sceSblGcAuthMgrAdhocBB160Auth5);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrAdhocBB160BroadCastDecrypt) {
    TRACY_FUNC(sceSblGcAuthMgrAdhocBB160BroadCastDecrypt);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrAdhocBB160BroadCastEncrypt) {
    TRACY_FUNC(sceSblGcAuthMgrAdhocBB160BroadCastEncrypt);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrAdhocBB160GetKeys) {
    TRACY_FUNC(sceSblGcAuthMgrAdhocBB160GetKeys);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrAdhocBB160Init) {
    TRACY_FUNC(sceSblGcAuthMgrAdhocBB160Init);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrAdhocBB160Shutdown) {
    TRACY_FUNC(sceSblGcAuthMgrAdhocBB160Shutdown);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrAdhocBB160UniCastDecrypt) {
    TRACY_FUNC(sceSblGcAuthMgrAdhocBB160UniCastDecrypt);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrAdhocBB160UniCastEncrypt) {
    TRACY_FUNC(sceSblGcAuthMgrAdhocBB160UniCastEncrypt);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrAdhocBB224Auth1) {
    TRACY_FUNC(sceSblGcAuthMgrAdhocBB224Auth1);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrAdhocBB224Auth2) {
    TRACY_FUNC(sceSblGcAuthMgrAdhocBB224Auth2);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrAdhocBB224Auth3) {
    TRACY_FUNC(sceSblGcAuthMgrAdhocBB224Auth3);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrAdhocBB224Auth4) {
    TRACY_FUNC(sceSblGcAuthMgrAdhocBB224Auth4);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrAdhocBB224Auth5) {
    TRACY_FUNC(sceSblGcAuthMgrAdhocBB224Auth5);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrAdhocBB224GetKeys) {
    TRACY_FUNC(sceSblGcAuthMgrAdhocBB224GetKeys);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrAdhocBB224Init) {
    TRACY_FUNC(sceSblGcAuthMgrAdhocBB224Init);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrAdhocBB224Shutdown) {
    TRACY_FUNC(sceSblGcAuthMgrAdhocBB224Shutdown);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrGetMediaIdType01) {
    TRACY_FUNC(sceSblGcAuthMgrGetMediaIdType01);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrMsSaveBBCipherFinal) {
    TRACY_FUNC(sceSblGcAuthMgrMsSaveBBCipherFinal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrMsSaveBBCipherInit) {
    TRACY_FUNC(sceSblGcAuthMgrMsSaveBBCipherInit);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrMsSaveBBCipherUpdate) {
    TRACY_FUNC(sceSblGcAuthMgrMsSaveBBCipherUpdate);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrMsSaveBBMacFinal) {
    TRACY_FUNC(sceSblGcAuthMgrMsSaveBBMacFinal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrMsSaveBBMacInit) {
    TRACY_FUNC(sceSblGcAuthMgrMsSaveBBMacInit);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrMsSaveBBMacUpdate) {
    TRACY_FUNC(sceSblGcAuthMgrMsSaveBBMacUpdate);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrPcactActivation) {
    TRACY_FUNC(sceSblGcAuthMgrPcactActivation);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrPcactGetChallenge) {
    TRACY_FUNC(sceSblGcAuthMgrPcactGetChallenge);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrPkgVry) {
    TRACY_FUNC(sceSblGcAuthMgrPkgVry);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrPsmactCreateC1) {
    TRACY_FUNC(sceSblGcAuthMgrPsmactCreateC1);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrPsmactVerifyR1) {
    TRACY_FUNC(sceSblGcAuthMgrPsmactVerifyR1);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrSclkGetData1) {
    TRACY_FUNC(sceSblGcAuthMgrSclkGetData1);
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblGcAuthMgrSclkSetData2) {
    TRACY_FUNC(sceSblGcAuthMgrSclkSetData2);
    return UNIMPLEMENTED();
}
