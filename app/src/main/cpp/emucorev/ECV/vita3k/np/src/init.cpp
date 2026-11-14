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

#include <np/common.h>
#include <np/functions.h>
#include <np/state.h>

#include <io/functions.h>

bool init(NpState &state, const np::CommunicationID *comm_id) {
    const std::lock_guard<std::mutex> lock(state.mutex);
    if (state.inited)
        return false;

    ++state.generation;
    state.inited = true;
    state.comm_id = comm_id ? *comm_id : np::CommunicationID{};

    return true;
}

bool deinit(NpState &state) {
    const std::scoped_lock lock(state.mutex, state.matching2.mutex);
    // App teardown also calls this when only trophies or Matching2 were used.
    ++state.generation;
    deinit(state.trophy_state);

    state.cbs.clear();
    state.state_cb_id = 0;
    state.comm_id = {};
    ++state.matching2.callback_generation;
    state.matching2.dispatching = false;
    state.matching2.context_cb_pc = 0;
    state.matching2.context_cb_arg = 0;
    state.matching2.next_ctx_id = 1;
    state.matching2.pending.clear();
    state.inited = false;
    return true;
}

bool init(NpTrophyState &state) {
    state.inited = true;
    return true;
}

bool deinit(NpTrophyState &state) {
    for (auto &ctx : state.contexts) {
        if (ctx.valid && ctx.io)
            close_file(*ctx.io, ctx.trophy_file_stream, "np_deinit");
    }
    state.contexts.clear();
    state.clear_trophy_unlock_callbacks();
    state.inited = false;
    return true;
}
