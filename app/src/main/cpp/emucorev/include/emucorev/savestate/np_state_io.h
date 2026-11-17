#pragma once

#include <emucorev/savestate/state_io.h>
#include <np/state.h>

namespace emucorev::savestate {

struct NpSnapshot {
    bool inited = false;
    np::CommunicationID comm_id{};
    np_callbacks callbacks;
    int32_t state_cb_id = 0;
    Address context_pc = 0, context_arg = 0;
    uint32_t next_ctx_id = 1;
    std::vector<SceNpMatching2ContextEvent> pending;
};

inline bool capture_np(NpState &state, BufferWriter &writer, std::string &error) {
    const std::scoped_lock lock(state.mutex, state.matching2.mutex);
    // An active callback batch also owns events on its host stack. Never emit
    // a snapshot that silently loses those events.
    if (state.matching2.dispatching || state.cbs.size() > 4096 || state.matching2.pending.size() > 4096) {
        error = "NP callback state cannot be captured";
        return false;
    }
    writer.u32(1);
    writer.boolean(state.inited);
    writer.value(state.comm_id);
    writer.i32(state.state_cb_id);
    writer.u32(static_cast<uint32_t>(state.cbs.size()));
    for (const auto &[id, callback] : state.cbs) {
        writer.i32(id);
        writer.u32(callback.pc);
        writer.u32(callback.data);
        writer.boolean(callback.delivered);
        writer.u32(callback.last_state);
    }
    writer.u32(state.matching2.context_cb_pc);
    writer.u32(state.matching2.context_cb_arg);
    writer.u32(state.matching2.next_ctx_id);
    writer.u32(static_cast<uint32_t>(state.matching2.pending.size()));
    for (const auto &event : state.matching2.pending) {
        writer.u32(event.ctx_id);
        writer.u32(event.event);
        writer.u32(event.cause);
        writer.u32(event.error_code);
    }
    return true;
}

inline bool parse_np(const std::vector<uint8_t> &data, NpSnapshot &out, std::string &error) {
    BufferReader reader(data.data(), data.size());
    NpSnapshot parsed;
    const auto fail = [&] { error = "invalid NP callback state"; return false; };
    if (reader.u32() != 1) return fail();
    parsed.inited = reader.boolean();
    parsed.comm_id = reader.value<np::CommunicationID>();
    parsed.state_cb_id = reader.i32();
    const uint32_t count = reader.u32();
    if (!reader.ok() || count > 4096 || count > reader.remaining() / 17) return fail();
    for (uint32_t i = 0; i < count; ++i) {
        const int id = reader.i32();
        SceNpServiceStateCallback cb{};
        cb.pc = reader.u32(); cb.data = reader.u32();
        cb.delivered = reader.boolean(); cb.last_state = reader.u32();
        if (id <= 0 || !cb.pc || cb.last_state > SCE_NP_SERVICE_STATE_ONLINE ||
            !parsed.callbacks.emplace(id, cb).second) return fail();
    }
    parsed.context_pc = reader.u32(); parsed.context_arg = reader.u32();
    parsed.next_ctx_id = reader.u32();
    const uint32_t events = reader.u32();
    if (!reader.ok() || events > 4096 || events > reader.remaining() / 16) return fail();
    for (uint32_t i = 0; i < events; ++i)
        parsed.pending.push_back({ reader.u32(), reader.u32(), reader.u32(), reader.u32() });
    if (!reader.ok() || reader.remaining() || !parsed.next_ctx_id ||
        (parsed.state_cb_id && !parsed.callbacks.contains(parsed.state_cb_id)) ||
        (!parsed.inited && (!parsed.callbacks.empty() || parsed.state_cb_id))) return fail();
    out = std::move(parsed);
    return true;
}

template <typename ValidRange>
bool validate_np_callbacks(const NpSnapshot &state, ValidRange valid, std::string &error) {
    const auto callback_valid = [&](Address pc) {
        // User data is an opaque value; games may use non-pointer tokens.
        return !pc || valid(pc & ~1u, 2);
    };
    for (const auto &[id, cb] : state.callbacks) {
        if (!callback_valid(cb.pc)) {
            error = "NP callback refers to unmapped guest memory";
            return false;
        }
    }
    if (!callback_valid(state.context_pc)) {
        error = "NP matching callback refers to unmapped guest memory";
        return false;
    }
    return true;
}

inline void apply_np(NpState &state, NpSnapshot snapshot) {
    const std::scoped_lock lock(state.mutex, state.matching2.mutex);
    // Generations belong to the host dispatch lifetime, not the saved timeline.
    ++state.generation;
    ++state.matching2.callback_generation;
    state.inited = snapshot.inited;
    state.comm_id = snapshot.comm_id;
    state.cbs = std::move(snapshot.callbacks);
    state.state_cb_id = snapshot.state_cb_id;
    state.matching2.dispatching = false;
    state.matching2.context_cb_pc = snapshot.context_pc;
    state.matching2.context_cb_arg = snapshot.context_arg;
    state.matching2.next_ctx_id = snapshot.next_ctx_id;
    state.matching2.pending = std::move(snapshot.pending);
}

} // namespace emucorev::savestate
