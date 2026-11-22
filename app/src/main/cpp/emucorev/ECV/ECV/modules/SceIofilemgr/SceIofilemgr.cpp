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

#include "SceIofilemgr.h"

#include <cpu/functions.h>
#include <io/functions.h>
#include <io/io.h>
#include <kernel/state.h>
#include <kernel/thread/thread_state.h>
#include <kernel/types.h>

#include <string_view>

#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

#include <util/tracy.h>
TRACY_MODULE_NAME(SceIofilemgr);

// Async I/O is executed synchronously when it is submitted; the result is parked per fd until the guest
// collects it with sceIoComplete (see io/async_io.h). vita-headers has no prototype for the *Async
// functions or sceIoComplete (only sceIoCancel, "per file descriptor"), so the shapes below follow the
// synchronous counterparts: submit returns 0, the operation result is delivered through sceIoComplete.
static int io_sync_fd(IOState &io, const SceUID fd) {
    std::lock_guard<std::mutex> lock(io.file_mutex);
    const auto file = io.std_files.find(fd);
    if (file != io.std_files.end()) {
        if (FILE *fp = file->second.get_file_pointer())
            fflush(fp);
        return 0;
    }
    if (io.tty_files.find(fd) != io.tty_files.end())
        return 0;
    return SCE_ERROR_ERRNO_EBADFD;
}

EXPORT(int, _sceIoChstat) {
    TRACY_FUNC(_sceIoChstat);
    return UNIMPLEMENTED();
}

EXPORT(int, _sceIoChstatAsync) {
    TRACY_FUNC(_sceIoChstatAsync);
    return UNIMPLEMENTED();
}

EXPORT(int, _sceIoChstatByFd) {
    TRACY_FUNC(_sceIoChstatByFd);
    return UNIMPLEMENTED();
}

EXPORT(int, _sceIoCompleteMultiple) {
    TRACY_FUNC(_sceIoCompleteMultiple);
    return UNIMPLEMENTED();
}

EXPORT(int, _sceIoDevctl) {
    TRACY_FUNC(_sceIoDevctl);
    return UNIMPLEMENTED();
}

EXPORT(int, _sceIoDevctlAsync) {
    TRACY_FUNC(_sceIoDevctlAsync);
    return UNIMPLEMENTED();
}

EXPORT(int, _sceIoDopen, const char *dir) {
    TRACY_FUNC(_sceIoDopen, dir);
    return open_dir(emuenv.io, dir, emuenv.vita_fs_path, export_name);
}

EXPORT(int, _sceIoDread, const SceUID fd, SceIoDirent *dir) {
    TRACY_FUNC(_sceIoDread, fd, dir);
    if (dir == nullptr) {
        return RET_ERROR(SCE_KERNEL_ERROR_ILLEGAL_ADDR);
    }
    return read_dir(emuenv.io, fd, dir, emuenv.vita_fs_path, export_name);
}

EXPORT(int, _sceIoGetstat, const char *file, SceIoStat *stat) {
    TRACY_FUNC(_sceIoGetstat, file, stat);
    return stat_file(emuenv.io, file, stat, emuenv.vita_fs_path, export_name);
}

EXPORT(int, _sceIoGetstatAsync) {
    TRACY_FUNC(_sceIoGetstatAsync);
    return UNIMPLEMENTED();
}

EXPORT(int, _sceIoGetstatByFd, const SceUID fd, SceIoStat *stat) {
    TRACY_FUNC(_sceIoGetstatByFd, fd, stat);
    return stat_file_by_fd(emuenv.io, fd, stat, emuenv.vita_fs_path, export_name);
}

EXPORT(int, _sceIoIoctl) {
    TRACY_FUNC(_sceIoIoctl);
    return UNIMPLEMENTED();
}

EXPORT(int, _sceIoIoctlAsync) {
    TRACY_FUNC(_sceIoIoctlAsync);
    return UNIMPLEMENTED();
}

EXPORT(SceOff, _sceIoLseek, const SceUID fd, Ptr<_sceIoLseekOpt> opt) {
    TRACY_FUNC(_sceIoLseek, fd, opt);
    return seek_file(fd, opt.get(emuenv.mem)->offset, opt.get(emuenv.mem)->whence, emuenv.io, export_name);
}

EXPORT(int, _sceIoLseekAsync) {
    TRACY_FUNC(_sceIoLseekAsync);
    return UNIMPLEMENTED();
}

EXPORT(int, _sceIoMkdir, const char *dir, const SceMode mode) {
    TRACY_FUNC(_sceIoMkdir, dir, mode);
    return create_dir(emuenv.io, dir, mode, emuenv.vita_fs_path, export_name);
}

EXPORT(int, _sceIoMkdirAsync) {
    TRACY_FUNC(_sceIoMkdirAsync);
    return UNIMPLEMENTED();
}

EXPORT(int, _sceIoOpen, const char *file, const int flags, const SceMode mode) {
    TRACY_FUNC(_sceIoOpen, file, flags, mode);
    if (file == nullptr) {
        return RET_ERROR(SCE_ERROR_ERRNO_EINVAL);
    }
    const auto opened_fd = open_file(emuenv.io, file, flags, emuenv.vita_fs_path, export_name);
    LOG_INFO("Opening file: {} -> {}", file, opened_fd < 0 ? fmt::format("FAILED {}", log_hex(static_cast<uint32_t>(opened_fd))) : fmt::format("fd {}", opened_fd));
    return opened_fd;
}

EXPORT(int, _sceIoOpenAsync) {
    TRACY_FUNC(_sceIoOpenAsync);
    return UNIMPLEMENTED();
}

// guest call stack for reads of one filename
static constexpr const char *watched_read_file_fragment = nullptr;

EXPORT(SceSSize, _sceIoPread, SceUID fd, void *buf, SceSize nbyte, SceOff offset) {
    TRACY_FUNC(_sceIoPread, fd, buf, nbyte, offset);
    if (watched_read_file_fragment && *watched_read_file_fragment) {
        std::string vita_path;
        {
            const std::lock_guard<std::mutex> lock(emuenv.io.file_mutex);
            const auto file = emuenv.io.std_files.find(fd);
            if (file != emuenv.io.std_files.end())
                vita_path = file->second.get_vita_loc();
        }
        if (std::string_view(vita_path).find(watched_read_file_fragment) != std::string_view::npos) {
            const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);
            LOG_ERROR("[CUESTACK] pread {} off={} size={} thread='{}' guest stack:\n{}",
                watched_read_file_fragment, offset, nbyte, thread ? thread->name : "?",
                thread ? thread->log_stack_traceback() : std::string("?"));
        }
    }
    return read_file_at(buf, emuenv.io, fd, nbyte, offset, export_name);
}

EXPORT(int, _sceIoPreadAsync) {
    TRACY_FUNC(_sceIoPreadAsync);
    return UNIMPLEMENTED();
}

EXPORT(SceSSize, _sceIoPwrite, SceUID fd, const void *buf, SceSize nbyte, SceOff offset) {
    TRACY_FUNC(_sceIoPwrite, fd, buf, nbyte, offset);
    return write_file_at(fd, buf, nbyte, offset, emuenv.io, export_name);
}

EXPORT(int, _sceIoPwriteAsync) {
    TRACY_FUNC(_sceIoPwriteAsync);
    return UNIMPLEMENTED();
}

EXPORT(int, _sceIoRemove) {
    TRACY_FUNC(_sceIoRemove);
    return UNIMPLEMENTED();
}

EXPORT(int, _sceIoRemoveAsync) {
    TRACY_FUNC(_sceIoRemoveAsync);
    return UNIMPLEMENTED();
}

EXPORT(int, _sceIoRename) {
    TRACY_FUNC(_sceIoRename);
    return UNIMPLEMENTED();
}

EXPORT(int, _sceIoRenameAsync) {
    TRACY_FUNC(_sceIoRenameAsync);
    return UNIMPLEMENTED();
}

EXPORT(int, _sceIoRmdir) {
    TRACY_FUNC(_sceIoRmdir);
    return UNIMPLEMENTED();
}

EXPORT(int, _sceIoRmdirAsync) {
    TRACY_FUNC(_sceIoRmdirAsync);
    return UNIMPLEMENTED();
}

EXPORT(int, _sceIoSync) {
    TRACY_FUNC(_sceIoSync);
    return UNIMPLEMENTED();
}

EXPORT(int, _sceIoSyncAsync) {
    TRACY_FUNC(_sceIoSyncAsync);
    return UNIMPLEMENTED();
}

EXPORT(int, sceIoCancel) {
    TRACY_FUNC(sceIoCancel);
    return UNIMPLEMENTED();
}

EXPORT(int, sceIoChstatByFdAsync) {
    TRACY_FUNC(sceIoChstatByFdAsync);
    return UNIMPLEMENTED();
}

EXPORT(int, sceIoClose, const SceUID fd) {
    TRACY_FUNC(sceIoClose, fd);
    // a synchronous close abandons whatever async result was still parked for the fd
    emuenv.io.async_ops.discard(fd);
    emuenv.io.priorities.forget(fd);
    return close_file(emuenv.io, fd, export_name);
}

EXPORT(int, sceIoCloseAsync, const SceUID fd) {
    TRACY_FUNC(sceIoCloseAsync, fd);
    const int res = close_file(emuenv.io, fd, export_name);
    emuenv.io.priorities.forget(fd);
    emuenv.io.async_ops.push(fd, res);
    return 0;
}

// PROVISIONAL signature: sceIoComplete(SceUID fd, SceInt64 *res, ...) mirrors the PSP sceIoWaitAsync(fd, res)
// shape (the result is 64 bit because sceIoLseekAsync produces an offset). Unproven, vita-headers lacks it.
// Returns 0 and writes the result of the oldest async op of fd; an fd with nothing pending is an error.
EXPORT(int, sceIoComplete, const SceUID fd, int64_t *res) {
    TRACY_FUNC(sceIoComplete, fd, res);
    int64_t result = 0;
    if (!emuenv.io.async_ops.pop(fd, result))
        return RET_ERROR(async_io::ERR_NO_PENDING);
    if (res)
        *res = result;
    return 0;
}

EXPORT(int, sceIoDclose, const SceUID fd) {
    TRACY_FUNC(sceIoDclose, fd);
    return close_dir(emuenv.io, fd, export_name);
}

EXPORT(int, sceIoDcloseAsync) {
    TRACY_FUNC(sceIoDcloseAsync);
    return UNIMPLEMENTED();
}

EXPORT(int, sceIoDopenAsync) {
    TRACY_FUNC(sceIoDopenAsync);
    return UNIMPLEMENTED();
}

EXPORT(int, sceIoDreadAsync) {
    TRACY_FUNC(sceIoDreadAsync);
    return UNIMPLEMENTED();
}

EXPORT(int, sceIoFlockForSystem) {
    TRACY_FUNC(sceIoFlockForSystem);
    return UNIMPLEMENTED();
}

EXPORT(int, sceIoGetPriority, const SceUID fd) {
    TRACY_FUNC(sceIoGetPriority, fd);
    return emuenv.io.priorities.get(fd);
}

EXPORT(int, sceIoGetPriorityForSystem, const SceUID fd) {
    TRACY_FUNC(sceIoGetPriorityForSystem, fd);
    return emuenv.io.priorities.get(fd);
}

EXPORT(int, sceIoGetProcessDefaultPriority) {
    TRACY_FUNC(sceIoGetProcessDefaultPriority);
    return emuenv.io.priorities.process_default();
}

EXPORT(int, sceIoGetThreadDefaultPriority) {
    TRACY_FUNC(sceIoGetThreadDefaultPriority);
    return emuenv.io.priorities.thread_default();
}

EXPORT(int, sceIoGetThreadDefaultPriorityForSystem) {
    TRACY_FUNC(sceIoGetThreadDefaultPriorityForSystem);
    return emuenv.io.priorities.thread_default();
}

EXPORT(int, sceIoGetstatByFdAsync) {
    TRACY_FUNC(sceIoGetstatByFdAsync);
    return UNIMPLEMENTED();
}

EXPORT(int, sceIoLseek32, const SceUID fd, const int32_t offset, const SceIoSeekMode whence) {
    TRACY_FUNC(sceIoLseek32, fd, offset, whence);
    return static_cast<int>(seek_file(fd, offset, whence, emuenv.io, export_name));
}

EXPORT(int, sceIoRead, const SceUID fd, void *data, const SceSize size) {
    TRACY_FUNC(sceIoRead, fd, data, size);
    if (emuenv.cfg.current_config.file_loading_delay > 0) {
        const uint32_t delay_us = emuenv.cfg.current_config.file_loading_delay * 1000 + size / 20;
        guest_sched_release_for_block();
        std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
    }
    return read_file(data, emuenv.io, fd, size, export_name);
}

EXPORT(int, sceIoReadAsync, const SceUID fd, void *data, const SceSize size) {
    TRACY_FUNC(sceIoReadAsync, fd, data, size);
    if (!data)
        return RET_ERROR(SCE_ERROR_ERRNO_EINVAL);
    const int res = CALL_EXPORT(sceIoRead, fd, data, size);
    emuenv.io.async_ops.push(fd, res);
    return 0;
}

EXPORT(int, sceIoSetPriority, const SceUID fd, const int priority) {
    TRACY_FUNC(sceIoSetPriority, fd, priority);
    if (fd < 0)
        return RET_ERROR(SCE_ERROR_ERRNO_EBADFD);
    emuenv.io.priorities.set(fd, priority);
    return 0;
}

EXPORT(int, sceIoSetPriorityForSystem, const SceUID fd, const int priority) {
    TRACY_FUNC(sceIoSetPriorityForSystem, fd, priority);
    if (fd < 0)
        return RET_ERROR(SCE_ERROR_ERRNO_EBADFD);
    emuenv.io.priorities.set(fd, priority);
    return 0;
}

EXPORT(int, sceIoSetProcessDefaultPriority, const int priority) {
    TRACY_FUNC(sceIoSetProcessDefaultPriority, priority);
    emuenv.io.priorities.set_process_default(priority);
    return 0;
}

EXPORT(int, sceIoSetThreadDefaultPriority, const int priority) {
    TRACY_FUNC(sceIoSetThreadDefaultPriority, priority);
    emuenv.io.priorities.set_thread_default(priority);
    return 0;
}

EXPORT(int, sceIoSetThreadDefaultPriorityForSystem, const int priority) {
    TRACY_FUNC(sceIoSetThreadDefaultPriorityForSystem, priority);
    emuenv.io.priorities.set_thread_default(priority);
    return 0;
}

EXPORT(int, sceIoSyncByFd, const SceUID fd, const int flag) {
    TRACY_FUNC(sceIoSyncByFd, fd, flag);
    const int res = io_sync_fd(emuenv.io, fd);
    return res < 0 ? RET_ERROR(res) : res;
}

EXPORT(int, sceIoSyncByFdAsync, const SceUID fd, const int flag) {
    TRACY_FUNC(sceIoSyncByFdAsync, fd, flag);
    emuenv.io.async_ops.push(fd, io_sync_fd(emuenv.io, fd));
    return 0;
}

EXPORT(int, sceIoWrite, const SceUID fd, const void *data, const SceSize size) {
    TRACY_FUNC(sceIoWrite, fd, data, size);
    return write_file(fd, data, size, emuenv.io, export_name);
}

EXPORT(int, sceIoWriteAsync, const SceUID fd, const void *data, const SceSize size) {
    TRACY_FUNC(sceIoWriteAsync, fd, data, size);
    if (!data)
        return RET_ERROR(SCE_ERROR_ERRNO_EINVAL);
    const int res = write_file(fd, data, size, emuenv.io, export_name);
    emuenv.io.async_ops.push(fd, res);
    return 0;
}
