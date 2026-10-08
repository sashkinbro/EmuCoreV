// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#include <audio/continuation.h>
#include <audio/state.h>
#include <kernel/state.h>
#include <kernel/thread/thread_state.h>
#include <mem/ptr.h>

namespace {
constexpr int32_t invalid_port = static_cast<int32_t>(0x80260003U);
uint64_t wall_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
AudioOutPortPtr find_port(AudioState &audio, int id) {
    const std::lock_guard lock(audio.mutex);
    const auto found = audio.out_ports.find(id);
    return found == audio.out_ports.end() ? nullptr : found->second;
}
bool matching_port(const AudioOutPort &port, const std::array<uint32_t, 8> &args) {
    return !port.stopping && args[7] == 1 && args[2] <= 2
        && args[3] == port.len && args[4] == port.len_bytes
        && args[5] == port.freq && args[6] == port.mode;
}
struct OutputSubscription {
    AudioOutPort &port;
    int id;
    OutputSubscription(AudioOutPort &port, const ThreadStatePtr &thread) : port(port), id(thread->id) {
        std::weak_ptr<ThreadState> weak = thread;
        const std::lock_guard lock(port.output_wakers_mutex);
        port.output_wakers[id] = [weak] { if (const auto thread = weak.lock()) thread->wake(); };
    }
    ~OutputSubscription() {
        const std::lock_guard lock(port.output_wakers_mutex);
        port.output_wakers.erase(id);
    }
};
} // namespace

int32_t audio_output_wait(KernelState &kernel, AudioState &audio, MemState &mem,
    ThreadState &thread, int id, Address buffer) {
    const auto port = find_port(audio, id);
    if (!port || !audio.adapter)
        return invalid_port;
    if (!buffer)
        return 0;
    const auto self = kernel.get_thread(thread.id);
    if (!self)
        return invalid_port;
    const uint64_t capacity_timeout = audio.adapter->output_wait_timeout_us(*port);
    const auto initial_deadline = capacity_timeout ? std::chrono::steady_clock::now()
        + std::chrono::microseconds(capacity_timeout) : Deadline::max();
    WaitContinuationScope scope(thread, WaitOperation::audio_output,
        {uint32_t(id), buffer, 0, uint32_t(port->len), uint32_t(port->len_bytes),
            uint32_t(port->freq), uint32_t(port->mode), 1}, nullptr, false, initial_deadline);
    auto &record = *scope.record;
    uint32_t phase;
    {
        const std::lock_guard lock(record.mutex);
        if (!matching_port(*port, record.state.args))
            return invalid_port;
        record.object = port;
        record.state.target = {SCE_KERNEL_WAITTYPE_EVENT, id};
        phase = record.state.args[2];
    }
    // Install before checking capacity. A callback between the check and wait
    // leaves a sticky kernel wake, rather than a lost condition-variable signal.
    OutputSubscription subscription(*port, self);
    while (phase == 0) {
        AudioSubmitResult result;
        {
            const std::lock_guard producer_lock(port->mutex);
            const bool expired = record.deadline != Deadline::max()
                && std::chrono::steady_clock::now() >= record.deadline;
            result = audio.adapter->try_audio_output(*port, Ptr<const void>(buffer).get(mem), expired);
            if (result == AudioSubmitResult::submitted) {
                const uint64_t now = wall_us();
                const uint64_t diff = now >= port->last_output ? now - port->last_output : 0;
                const uint64_t pacing = diff < port->len_microseconds
                    && port->len_microseconds - diff > 1000 ? (port->len_microseconds - diff) / 2 : 0;
                // Submission and phase commit cannot be captured separately:
                // the guest thread is still running, and the producer is locked.
                const std::lock_guard lock(record.mutex);
                phase = record.state.args[2] = pacing ? 1 : 2;
                record.deadline = pacing ? std::chrono::steady_clock::now()
                    + std::chrono::microseconds(pacing) : std::chrono::steady_clock::now();
                record.state.infinite = false;
                if (!pacing)
                    port->last_output = now;
            }
        }
        if (result == AudioSubmitResult::stopped || result == AudioSubmitResult::error)
            return invalid_port;
        if (result == AudioSubmitResult::would_block) {
            const auto waited = thread.wait({SCE_KERNEL_WAITTYPE_EVENT, id}, record.deadline, false);
            if (!waited)
                return guest_result(waited);
        }
    }
    while (phase == 1) {
        if (port->stopping)
            return invalid_port;
        if (std::chrono::steady_clock::now() >= record.deadline)
            break;
        const auto waited = thread.wait({SCE_KERNEL_WAITTYPE_EVENT, id}, record.deadline, false);
        if (!waited)
            return guest_result(waited);
    }
    if (phase == 1) {
        const std::lock_guard lock(port->mutex);
        port->last_output = wall_us();
    }
    return port->len;
}

void register_audio_wait_handlers(KernelState &kernel, AudioState &audio, MemState &mem) {
    kernel.wait_resume_handlers[WaitOperation::audio_output] = {
        [&audio](ThreadState &, const std::shared_ptr<WaitContinuation> &record) {
            std::array<uint32_t, 8> args;
            {
                const std::lock_guard lock(record->mutex);
                args = record->state.args;
            }
            const auto port = find_port(audio, int(args[0]));
            if (!port || !matching_port(*port, args))
                return false;
            const std::lock_guard lock(record->mutex);
            record->object = port;
            return true;
        },
        [&kernel, &audio, &mem](ThreadState &thread, const std::shared_ptr<WaitContinuation> &record) {
            return audio_output_wait(kernel, audio, mem, thread,
                int(record->state.args[0]), record->state.args[1]);
        }
    };
}

bool validate_audio_wait_configuration(const WaitContinuationSnapshot &wait,
    const AudioWaitConfiguration &port, const std::function<bool(Address, size_t)> &contains,
    std::string &error) {
    const auto &a = wait.args;
    const auto fail = [&] { error = "audio output continuation disagrees with saved playback port"; return false; };
    if (wait.operation != WaitOperation::audio_output || port.id <= 0 || port.len <= 0
        || port.len_bytes <= 0 || port.freq <= 0 || port.mode < 0 || port.mode > 1
        || (port.codec != 1 && port.codec != 2) || a[7] != 1 || a[2] > 2
        || a[0] != uint32_t(port.id) || a[3] != uint32_t(port.len)
        || a[4] != uint32_t(port.len_bytes) || a[5] != uint32_t(port.freq)
        || a[6] != uint32_t(port.mode) || a[1] == 0 || wait.timeout_address
        || wait.callbacks || wait.queued || wait.has_result || wait.sequence
        || wait.target.type != SCE_KERNEL_WAITTYPE_EVENT || wait.target.id != port.id)
        return fail();
    if (a[2] == 0) {
        if (!contains(a[1], a[4]))
            return fail();
        if (port.codec == 2 ? !wait.infinite
                            : wait.infinite || wait.remaining_us > port.len_microseconds * 2)
            return fail();
    } else if (wait.infinite || wait.remaining_us > (a[2] == 1 ? port.len_microseconds / 2 : 0)) {
        return fail();
    }
    return true;
}
