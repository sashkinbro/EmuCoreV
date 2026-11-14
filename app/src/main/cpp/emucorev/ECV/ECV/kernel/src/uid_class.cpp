// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later

#include <kernel/state.h>
#include <kernel/uid_class.h>

SceInt32 get_threadmgr_uid_class(KernelState &kernel, SceUID uid) {
    // Registry membership is the lifetime boundary: deletion extracts an object
    // under this same lock before marking/waking it under its own lock.
    const std::lock_guard lock(kernel.mutex);
    if (kernel.threads.contains(uid))
        return static_cast<SceInt32>(UidClass::thread);
    if (kernel.semaphores.contains(uid))
        return static_cast<SceInt32>(UidClass::semaphore);
    if (kernel.eventflags.contains(uid))
        return static_cast<SceInt32>(UidClass::event_flag);
    if (kernel.mutexes.contains(uid))
        return static_cast<SceInt32>(UidClass::mutex);
    if (kernel.condvars.contains(uid))
        return static_cast<SceInt32>(UidClass::cond);
    if (kernel.timers.contains(uid))
        return static_cast<SceInt32>(UidClass::timer);
    if (kernel.msgpipes.contains(uid))
        return static_cast<SceInt32>(UidClass::msg_pipe);
    if (kernel.callbacks.contains(uid))
        return static_cast<SceInt32>(UidClass::callback);
    if (kernel.lwmutexes.contains(uid))
        return static_cast<SceInt32>(UidClass::lw_mutex);
    if (kernel.lwcondvars.contains(uid))
        return static_cast<SceInt32>(UidClass::lw_cond);
    if (kernel.rwlocks.contains(uid))
        return static_cast<SceInt32>(UidClass::rw_lock);
    if (kernel.simple_events.contains(uid))
        return static_cast<SceInt32>(UidClass::simple_event);
    return SCE_KERNEL_ERROR_UNKNOWN_UID;
}
