// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <kernel/types.h>

struct KernelState;

// Values reported by sceKernelGetThreadmgrUIDClass (official f44e2aa3b).
enum class UidClass : SceUInt32 {
    thread = 1,
    semaphore = 2,
    event_flag = 3,
    mutex = 4,
    cond = 5,
    timer = 6,
    msg_pipe = 7,
    callback = 8,
    thread_event = 9,
    lw_mutex = 10,
    lw_cond = 11,
    rw_lock = 12,
    simple_event = 13,
};

// Looks up a live registered thread-manager object using the retained per-type maps.
SceInt32 get_threadmgr_uid_class(KernelState &kernel, SceUID uid);
