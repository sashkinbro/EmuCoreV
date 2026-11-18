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

#include "../SceDisplay/SceDisplay.h"
#include <module/module.h>

#include <gxm/types.h>

#include <kernel/state.h>

#include <util/tracy.h>
#ifndef NDEBUG
#include <shell_firmware_research.h>
#endif

TRACY_MODULE_NAME(SceSharedFB);

typedef struct SceSharedFbInfo { // size is 0x58
    Ptr<void> base1; // cdram base
    int memsize;
    Ptr<void> base2; // cdram base
    int unk_0C;
    Ptr<void> unk_10;
    int unk_14;
    int unk_18;
    int unk_1C;
    int unk_20;
    int pitch; // 960
    int width; // 960
    int height; // 544
    SceGxmColorFormat color_format; // SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR
    int curbuf;
    int unk_38;
    int unk_3C;
    int unk_40;
    int unk_44;
    int vsync;
    int unk_4C;
    int unk_50;
    int unk_54;
} SceSharedFbInfo;

// VitaSDK psp2/sharedfb.h: render information supplied by the native PAF handoff.
struct SceSharedFbRenderInfo {
    Ptr<void> frame_buffer_base;
    uint32_t reserved04;
    Ptr<void> depth_stencil_buffer;
    uint32_t reserved0C[4];
    uint32_t stride;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    int32_t buffer_index;
    uint32_t reserved30[4];
};
static_assert(sizeof(SceSharedFbRenderInfo) == 0x40);

struct SharedFbState {
    SceSharedFbInfo info;
    std::mutex handoff_mutex;
    SceSharedFbRenderInfo pending{};
    bool pending_valid = false;
};

LIBRARY_INIT(SceSharedFb) {
    emuenv.kernel.obj_store.create<SharedFbState>();
}

DECL_EXPORT(int, sceSharedFbCreate, int smth);

EXPORT(int, _sceSharedFbOpen, int smth) {
    TRACY_FUNC(_sceSharedFbOpen, smth);
    STUBBED("sceSharedFbCreate");
    return CALL_EXPORT(sceSharedFbCreate, smth);
}

EXPORT(int, sceSharedFbBegin, int id, SceSharedFbInfo *info) {
    TRACY_FUNC(sceSharedFbBegin, id, info);
    SharedFbState *state = emuenv.kernel.obj_store.get<SharedFbState>();
    state->info.curbuf = 1;
    *info = state->info;
    return 0;
}

EXPORT(int, sceSharedFbClose) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSharedFbCreate, int smth) {
    TRACY_FUNC(sceSharedFbCreate, smth);
    SharedFbState *state = emuenv.kernel.obj_store.get<SharedFbState>();
    if (state->info.memsize == 0) {
        // enough memory for 2 956x544 buffers
        constexpr uint32_t alloc_size = 4 * 1024 * 512 * 2;
        Ptr<uint8_t> data = Ptr<uint8_t>(alloc(emuenv.mem, alloc_size, "sharedFB"));
        state->info = SceSharedFbInfo{
            .base1 = data,
            .memsize = alloc_size,
            .base2 = data + alloc_size / 2,
            .pitch = 960,
            .width = 960,
            .height = 544,
            .color_format = SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR,
            .curbuf = 1,
        };
    }
    return 1;
}

EXPORT(int, sceSharedFbDelete) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSharedFbEnd) {
    TRACY_FUNC(sceSharedFbEnd);
    SharedFbState *state = emuenv.kernel.obj_store.get<SharedFbState>();
    Ptr<void> data = (state->info.curbuf == 0) ? state->info.base2 : state->info.base1;
#ifndef NDEBUG
    static std::atomic_uint trace_count{0};
    if (trace_count.fetch_add(1) < 6)
        LOG_DEBUG("SharedFbEnd buffer=0x{:X} index={}", data.address(), state->info.curbuf);
#endif
    // tell the display a new buffer is ready
    SceDisplayFrameBuf frame_buf{
        .size = sizeof(SceDisplayFrameBuf),
        .base = data,
        .pitch = 960,
        .width = 960,
        .height = 544
    };
    return CALL_EXPORT(_sceDisplaySetFrameBuf, &frame_buf, SCE_DISPLAY_SETBUF_NEXTFRAME, nullptr);
}

EXPORT(int, sceSharedFbGetInfo, int id, SceSharedFbInfo *info) {
    TRACY_FUNC(sceSharedFbGetInfo);
    SharedFbState *state = emuenv.kernel.obj_store.get<SharedFbState>();
    *info = state->info;
    return 0;
}

EXPORT(int, sceSharedFbGetRenderingInfo) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSharedFbGetShellRenderPort) {
    // One emulated display: the shell renders to port zero.
    return 0;
}

EXPORT(int, sceSharedFbUpdateProcess) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSharedFbUpdateProcessBegin, SceUID id, const SceSharedFbRenderInfo *render_info, uint32_t *update_count) {
#ifndef NDEBUG
    if (const auto thread = emuenv.kernel.get_thread(thread_id))
        poll_shell_catalog_research(emuenv, *thread);
#endif
    if (!render_info || !update_count)
        return RET_ERROR(SCE_KERNEL_ERROR_ILLEGAL_ADDR);
    auto *state = emuenv.kernel.obj_store.get<SharedFbState>();
    if (id != 1 || state->info.memsize == 0)
        return RET_ERROR(SCE_KERNEL_ERROR_INVALID_UID);
    if (!render_info->frame_buffer_base || render_info->width == 0 || render_info->height == 0
        || render_info->stride < render_info->width)
        return RET_ERROR(SCE_KERNEL_ERROR_INVALID_ARGUMENT);
    const std::lock_guard lock(state->handoff_mutex);
    state->pending = *render_info;
    state->pending_valid = true;
#ifndef NDEBUG
    static std::atomic_uint trace_count{0};
    if (trace_count.fetch_add(1) < 6)
        LOG_DEBUG("SharedFb handoff buffer=0x{:X} index={} {}x{} stride={} format=0x{:X}",
            render_info->frame_buffer_base.address(), render_info->buffer_index,
            render_info->width, render_info->height, render_info->stride, render_info->pixel_format);
#endif
    state->info.pitch = render_info->stride;
    state->info.width = render_info->width;
    state->info.height = render_info->height;
    state->info.color_format = static_cast<SceGxmColorFormat>(render_info->pixel_format);
    state->info.curbuf = render_info->buffer_index;
    // There is one guest process. No secondary process needs a handoff.
    *update_count = 0;
    return 0;
}

EXPORT(int, sceSharedFbUpdateProcessEnd) {
    auto *state = emuenv.kernel.obj_store.get<SharedFbState>();
    const std::lock_guard lock(state->handoff_mutex);
    if (!state->pending_valid)
        return RET_ERROR(SCE_KERNEL_ERROR_INVALID_ARGUMENT);
    state->pending_valid = false;
    // This ends the inter-process handoff, not the shell's frame. PAF calls
    // it before drawing its own UI. Its display-queue callback subsequently
    // presents the completed composition through sceDisplaySetFrameBufInternal.
    return 0;
}
