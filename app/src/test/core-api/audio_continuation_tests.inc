#include <audio/continuation.h>
#include <kernel/state.h>
#include <audio/impl/cubeb_audio.h>
#include <chrono>
#include <future>

DECL_EXPORT(int, sceAudioOutOutput, int port, const void *buffer);

namespace {
class AudioContinuation : public testing::Test {
protected:
    EmuEnvState env;
    ThreadStatePtr thread;
    std::shared_ptr<CubebAudioOutPort> port;
    std::future<int> worker;
    void SetUp() override {
        env.mem.memory = Memory(new uint8_t[0x20000]{}, [](uint8_t *p) { delete[] p; });
        env.audio.adapter = std::make_unique<CubebAudioAdapter>(env.audio);
        thread = std::make_shared<ThreadState>(71, env.kernel, env.mem);
        env.kernel.threads.emplace(thread->id, thread);
        thread->update_status(ThreadStatus::running);
        port = make_port(4);
        env.audio.out_ports.emplace(5, port);
        std::fill_n(Ptr<uint8_t>(0x1000).get(env.mem), port->len_bytes, 0x53);
        register_audio_wait_handlers(env.kernel, env.audio, env.mem);
    }
    void TearDown() override { stop_worker(); }
    std::shared_ptr<CubebAudioOutPort> make_port(int samples) {
        auto result = std::make_shared<CubebAudioOutPort>();
        result->len = samples;
        result->len_bytes = samples * 4;
        result->freq = 48000;
        result->mode = 1;
        result->len_microseconds = uint64_t(samples) * 1000000 / 48000;
        result->spec.channels = 2;
        result->audio_buffers.resize(2);
        for (auto &buffer : result->audio_buffers) {
            buffer.buffer.resize(result->len_bytes, 0x17);
            buffer.buffer_position = 0;
        }
        return result;
    }
    void stop_worker() {
        if (worker.valid()) {
            port->stopping = true;
            thread->exit_delete(false);
            port->cond_var.notify_all(); // Also safely cleans up a baseline raw Cubeb wait.
            port->notify_output_ready();
            worker.wait();
        }
    }
    void output() {
        worker = std::async(std::launch::async, [&] {
            return export_sceAudioOutOutput(env, thread->id, "test", 5, Ptr<void>(0x1000).get(env.mem));
        });
    }
    bool waiting(unsigned phase) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        do {
            const auto snapshot = thread->capture_snapshot();
            if (snapshot.status == ThreadStatus::waiting && snapshot.waits.size() == 1
                && snapshot.waits[0].operation == WaitOperation::audio_output && snapshot.waits[0].args[2] == phase) return true;
            std::this_thread::yield();
        } while (std::chrono::steady_clock::now() < until);
        return false;
    }
    void restore_wait(const ThreadState::Snapshot &snapshot, const std::vector<uint8_t> &queue, int samples) {
        stop_worker();
        worker = {};
        env.kernel.threads.erase(thread->id);
        thread = std::make_shared<ThreadState>(71, env.kernel, env.mem);
        env.kernel.threads.emplace(thread->id, thread);
        port = make_port(samples);
        ASSERT_TRUE(env.audio.adapter->restore_port_state(*port, queue));
        env.audio.out_ports[5] = port;
        thread->apply_private_snapshot(snapshot);
        ASSERT_TRUE(thread->restore_wait_queues());
        thread->activate_restored_continuations();
        const auto record = thread->current_wait_continuation();
        worker = std::async(std::launch::async, [&, record] { return thread->resume_wait_continuation(record); });
    }
};

TEST_F(AudioContinuation, FullRingWaitRestoresBeforeSubmittingGuestPcmExactlyOnce) {
    port->nb_buffers_ready = 2;
    output();
    ASSERT_TRUE(waiting(0));
    const auto snapshot = thread->capture_snapshot();
    EXPECT_EQ(snapshot.waits[0].args[0], 5u);
    EXPECT_EQ(snapshot.waits[0].args[1], 0x1000u);
    std::vector<uint8_t> queue;
    ASSERT_TRUE(env.audio.adapter->save_port_state(*port, queue));
    restore_wait(snapshot, queue, 4);
    ASSERT_TRUE(waiting(0));
    {
        const std::lock_guard lock(port->mutex);
        EXPECT_EQ(port->nb_buffers_ready, 2);
        port->nb_buffers_ready = 1;
        port->next_audio_buffer = 1;
    }
    port->notify_output_ready();
    ASSERT_EQ(worker.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    EXPECT_EQ(worker.get(), 4);
    EXPECT_EQ(port->nb_buffers_ready, 2);
    EXPECT_EQ(port->audio_buffers[0].buffer, std::vector<uint8_t>(16, 0x53));
}

TEST_F(AudioContinuation, PacingWaitRestoreDoesNotSubmitTheSamePcmTwice) {
    port = make_port(4800);
    env.audio.out_ports[5] = port;
    std::fill_n(Ptr<uint8_t>(0x1000).get(env.mem), port->len_bytes, 0x69);
    port->last_output = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    output();
    ASSERT_TRUE(waiting(1));
    const auto snapshot = thread->capture_snapshot();
    std::vector<uint8_t> queue;
    ASSERT_TRUE(env.audio.adapter->save_port_state(*port, queue));
    restore_wait(snapshot, queue, 4800);
    ASSERT_EQ(worker.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    EXPECT_EQ(worker.get(), 4800);
    EXPECT_EQ(port->nb_buffers_ready, 1);
    EXPECT_EQ(port->audio_buffers[0].buffer, std::vector<uint8_t>(4800 * 4, 0x69));
}

TEST_F(AudioContinuation, ShutdownWakesFullProducerWithoutReadingOrQueuingGuestBuffer) {
    port->nb_buffers_ready = 2;
    output();
    ASSERT_TRUE(waiting(0));
    env.audio.stop_all_ports();
    ASSERT_EQ(worker.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    EXPECT_LT(worker.get(), 0);
    EXPECT_EQ(port->nb_buffers_ready, 2);
    EXPECT_EQ(port->audio_buffers[0].buffer, std::vector<uint8_t>(16, 0x17));
}
} // namespace

TEST(AudioContinuationPreflight, RequiresSavedPortConfigAndPhaseSpecificGuestCoverage) {
    WaitContinuationSnapshot wait;
    wait.operation = WaitOperation::audio_output;
    wait.args = {5, 0x1000, 0, 4, 16, 48000, 1, 1};
    wait.target = {SCE_KERNEL_WAITTYPE_EVENT, 5};
    const AudioWaitConfiguration config{5, 4, 16, 48000, 1, 83, 2};
    const auto mapped = [](Address address, size_t size) { return address == 0x1000 && size == 16; };
    std::string error;
    EXPECT_TRUE(validate_audio_wait_configuration(wait, config, mapped, error)) << error;
    auto changed = config;
    changed.len = 8;
    EXPECT_FALSE(validate_audio_wait_configuration(wait, changed, mapped, error));
    changed = config;
    changed.mode = 0;
    EXPECT_FALSE(validate_audio_wait_configuration(wait, changed, mapped, error));
    const auto retired = [](Address, size_t) { return false; };
    EXPECT_FALSE(validate_audio_wait_configuration(wait, config, retired, error));
    // PCM has already been copied in phase1; guest may have retired the buffer.
    wait.args[2] = 1;
    wait.infinite = false;
    wait.remaining_us = 40;
    EXPECT_TRUE(validate_audio_wait_configuration(wait, config, retired, error)) << error;
    wait.remaining_us = 42;
    EXPECT_FALSE(validate_audio_wait_configuration(wait, config, retired, error));
    wait.remaining_us = 40;
    wait.infinite = true;
    EXPECT_FALSE(validate_audio_wait_configuration(wait, config, retired, error));
}
