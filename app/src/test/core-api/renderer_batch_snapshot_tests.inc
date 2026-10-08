#include <renderer/functions.h>
#include <renderer/state.h>
#include <renderer/command_snapshot.h>
#include <gxm/savestate.h>
#include <renderer/finish.h>
#include <cpu/functions.h>
#include <kernel/state.h>
#include <app/functions.h>
#include <audio/continuation.h>
#include <audio/impl/cubeb_audio.h>
#include <emucorev/savestate/kernel_state_io.h>
#include <ngs/state.h>
#include <future>

DECL_EXPORT(int, sceAudioOutOutput, int port, const void *buffer);

namespace {
struct SnapshotBatchTestRenderer final : renderer::State {
    std::mutex drain_mutex;
    std::condition_variable drain_changed;
    bool hold_drain = false;
    std::vector<std::function<void()>> drains;
    SnapshotBatchTestRenderer() { context = nullptr; current_backend = renderer::Backend::OpenGL; should_display = false; res_multiplier = 1; }
    bool init() override { return true; }
    void late_init(const Config &, std::string_view, MemState &) override {}
    renderer::TextureCache *get_texture_cache() override { return nullptr; }
    void render_frame(DisplayState &, const GxmState &, MemState &) override {}
    void swap_window() override {}
    std::vector<uint32_t> dump_frame(DisplayState &, uint32_t &, uint32_t &) override { return {}; }
    int get_supported_filters() override { return 0; }
    void set_screen_filter(const std::string_view &) override {}
    int get_max_anisotropic_filtering() override { return 1; }
    void set_anisotropic_filtering(int) override {}
    int get_max_2d_texture_width() override { return 4096; }
    std::string_view get_gpu_name() override { return "batch test"; }
    void precompile_shader(const renderer::ShadersHash &) override {}
    void preclose_action() override {}
    void enqueue_finish_drain(std::function<void()> completion) override {
        { const std::lock_guard lock(drain_mutex);
          if (hold_drain) { drains.push_back(std::move(completion)); drain_changed.notify_all(); return; } }
        completion();
    }
    bool wait_for_drains(size_t count) {
        std::unique_lock lock(drain_mutex);
        return drain_changed.wait_for(lock, std::chrono::seconds(1), [&] { return drains.size() >= count; });
    }
};
}

TEST(RendererBatchSnapshot, BlockedMidBatchWaitRetainsFutureCommandsAndNeverReplaysConsumedPrefix) {
    EmuEnvState env;
    env.mem.memory = Memory(new uint8_t[0x10000]{}, [](uint8_t *p) { delete[] p; });
    env.renderer = std::make_unique<SnapshotBatchTestRenderer>();
    auto &state = *env.renderer;
    auto *sync = new (env.mem.memory.get() + 0x1000) SceGxmSyncObject;
    sync->timestamp_ahead = 1;
    struct Cleanup { EmuEnvState &env; SceGxmSyncObject *sync; ~Cleanup() { renderer::discard_pending_batches(*env.renderer); sync->~SceGxmSyncObject(); } } cleanup{env,sync};
    auto *prefix = renderer::make_command(renderer::generic_command_allocate, renderer::generic_command_free,
        renderer::CommandOpcode::SignalNotification, nullptr, SceGxmNotification{Ptr<uint32_t>(0x3000),7});
    auto *wait = renderer::make_command(renderer::generic_command_allocate, renderer::generic_command_free,
        renderer::CommandOpcode::WaitSyncObject, nullptr, Ptr<SceGxmSyncObject>(0x1000), uint32_t(1));
    auto *future = renderer::make_command(renderer::generic_command_allocate, renderer::generic_command_free,
        renderer::CommandOpcode::SignalNotification, nullptr, SceGxmNotification{Ptr<uint32_t>(0x3004),9});
    ASSERT_NE(prefix, nullptr); ASSERT_NE(wait, nullptr); ASSERT_NE(future, nullptr);
    prefix->next = wait; wait->next = future;
    state.active_batch = renderer::CommandList{prefix,future,nullptr};
    ASSERT_FALSE(renderer::process_batch_until_wait(state, state.features, env.mem, env.cfg, *state.active_batch));
    EXPECT_EQ(*Ptr<uint32_t>(0x3000).get(env.mem), 7u);
    EXPECT_EQ(*Ptr<uint32_t>(0x3004).get(env.mem), 0u);
    EXPECT_EQ(state.active_batch->first, wait);
    EXPECT_EQ(state.progress_counter.load(), 1u);

    std::vector<gxm::ContextSnapshot> contexts;
    std::vector<renderer::CommandSnapshot> commands;
    std::vector<gxm::BatchSnapshot> batches;
    std::string error;
    ASSERT_TRUE(gxm::capture_contexts(env, contexts, commands, batches, error)) << error;
    ASSERT_EQ(commands.size(), 2u); ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(commands[batches[0].first - 1].opcode, renderer::CommandOpcode::WaitSyncObject);
    renderer::discard_pending_batches(state);
    ASSERT_TRUE(gxm::restore_contexts(env, contexts, commands, batches, error)) << error;
    EXPECT_EQ(state.command_buffer_queue.size(), 0u);
    ASSERT_EQ(state.snapshot_pending_batches.size(), 1u);
    *Ptr<uint32_t>(0x3000).get(env.mem) = 55;
    ASSERT_TRUE(gxm::activate_saved_batches(env, 0, error)) << error;
    ASSERT_EQ(state.command_buffer_queue.size(), 1u);
    state.active_batch = *state.command_buffer_queue.pop();
    EXPECT_FALSE(renderer::process_batch_until_wait(state, state.features, env.mem, env.cfg, *state.active_batch));
    EXPECT_EQ(*Ptr<uint32_t>(0x3004).get(env.mem), 0u);
    renderer::subject_done(sync, 1);
    EXPECT_TRUE(renderer::process_batch_until_wait(state, state.features, env.mem, env.cfg, *state.active_batch));
    state.active_batch.reset();
    EXPECT_EQ(*Ptr<uint32_t>(0x3000).get(env.mem), 55u);
    EXPECT_EQ(*Ptr<uint32_t>(0x3004).get(env.mem), 9u);
    EXPECT_EQ(state.progress_counter.load(), 3u);
}

TEST(RendererBatchSnapshot, QueuedBatchAndFrontendCanShareDeferredOwnedNodes) {
    auto cmd = renderer::CommandSnapshot{};
    cmd.opcode = renderer::CommandOpcode::SetState; cmd.deferred_allocation = true;
    auto tag = renderer::GXMState::CullMode; std::memcpy(cmd.data.data(), &tag, sizeof(tag));
    gxm::ContextSnapshot deferred;
    deferred.address = 0x1000; deferred.deferred = true;
    deferred.deferred_lists.push_back({0x3000, 1, 1, 1, false, {{0x4000,0x4040}}});
    gxm::ContextSnapshot immediate; immediate.address = 0x2000;
    immediate.pending_first = immediate.pending_last = 1;
    std::string error;
    EXPECT_TRUE(gxm::validate_command_graph({deferred,immediate}, {cmd}, {{0x2000,1,1}}, error)) << error;
    cmd.deferred_allocation = false;
    EXPECT_FALSE(gxm::validate_command_graph({deferred,immediate}, {cmd}, {{0x2000,1,1}}, error));
}

TEST(RendererBatchSnapshot, DestructiveLoadCleanupDestroysActiveFrontendHostOwners) {
    EmuEnvState env;
    env.mem.memory = Memory(new uint8_t[0x10000]{}, [](uint8_t *p) { delete[] p; });
    env.renderer = std::make_unique<SnapshotBatchTestRenderer>();
    env.renderer->features.enable_memory_mapping = false;
    gxm::ContextSnapshot context;
    context.address = 0x1000; context.deferred = true; context.state.type = SCE_GXM_CONTEXT_TYPE_DEFERRED;
    context.state.active = true; context.pending_first = context.pending_last = 1;
    context.deferred_lists.push_back({0,0,1,1,true,{}});
    renderer::CommandSnapshot command;
    command.opcode = renderer::CommandOpcode::SetState; command.deferred_allocation = true;
    const auto tag = renderer::GXMState::CullMode; std::memcpy(command.data.data(), &tag, sizeof(tag));
    std::string error;
    ASSERT_TRUE(gxm::restore_contexts(env, {context}, {command}, error)) << error;
    auto owner = std::make_shared<int>(1); std::weak_ptr<int> lifetime = owner;
    auto *runtime = gxm::runtime_contexts(env).front().second;
    runtime->alloc_func = [owner] { return static_cast<renderer::Command *>(nullptr); };
    owner.reset();
    ASSERT_FALSE(lifetime.expired());
    renderer::discard_pending_batches(*env.renderer);
    gxm::destroy_runtime_objects(env);
    EXPECT_TRUE(env.gxm.deferred_contexts.empty());
    EXPECT_TRUE(lifetime.expired());
}

namespace {
struct FinishFuture {
    ThreadStatePtr thread;
    std::future<SceInt32> value;
    ~FinishFuture() {
        if (value.valid()) {
            if (value.wait_for(std::chrono::seconds(0)) != std::future_status::ready) thread->exit_delete(false);
            value.wait();
        }
    }
    template <class Duration> auto wait_for(Duration duration) { return value.wait_for(duration); }
};
template <class Function> FinishFuture finish_async(const ThreadStatePtr &thread, Function function) {
    return {thread,std::async(std::launch::async,std::move(function))};
}
SceInt32 finish_result(FinishFuture &future) {
    const auto ready = future.wait_for(std::chrono::seconds(1));
    EXPECT_EQ(ready, std::future_status::ready);
    if (ready != std::future_status::ready) future.thread->exit_delete(false);
    return future.value.get();
}
struct RendererFinishEnv {
    EmuEnvState env;
    ThreadStatePtr thread;
    SnapshotBatchTestRenderer &state() { return static_cast<SnapshotBatchTestRenderer &>(*env.renderer); }
    RendererFinishEnv() {
        env.mem.memory = Memory(new uint8_t[0x10000]{}, [](uint8_t *p) { delete[] p; });
        env.renderer = std::make_unique<SnapshotBatchTestRenderer>();
        renderer::register_finish_wait_handlers(env.kernel, state());
        make_thread();
    }
    void make_thread() {
        thread = std::make_shared<ThreadState>(97, env.kernel, env.mem);
        thread->cpu = init_cpu(false, thread->id, 0, env.mem);
        thread->update_status(ThreadStatus::running);
        env.kernel.threads[thread->id] = thread;
    }
    ~RendererFinishEnv() {
        env.kernel.resume_world();
        renderer::abort_finish_operations(state());
        thread->exit_delete(false);
        renderer::discard_pending_batches(state());
        env.kernel.threads.clear();
    }
    bool waiting(renderer::FinishPhase phase) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (std::chrono::steady_clock::now() < deadline) {
            std::vector<renderer::FinishSnapshot> operations;
            std::string error;
            if (thread->capture_snapshot().status == ThreadStatus::waiting
                && renderer::capture_finish_operations(state(), operations, error)
                && operations.size() == 1 && operations[0].phase == phase) return true;
            std::this_thread::yield();
        }
        return false;
    }
    void process_next() {
        auto batch = state().command_buffer_queue.pop(1);
        if (batch) {
            state().active_batch = *batch;
            if (renderer::process_batch_until_wait(state(), state().features, env.mem, env.cfg, *state().active_batch))
                state().active_batch.reset();
        }
    }
};
}

TEST(RendererFinishContinuation, BlockedDisplaySyncAndNopCanFreezeWithoutDrainingFutureWork) {
    RendererFinishEnv fixture;
    auto &state = fixture.state();
    auto *sync = new (fixture.env.mem.memory.get() + 0x1000) SceGxmSyncObject;
    sync->timestamp_ahead = 1;
    auto *wait = renderer::make_command(renderer::generic_command_allocate, renderer::generic_command_free,
        renderer::CommandOpcode::WaitSyncObject, nullptr, Ptr<SceGxmSyncObject>(0x1000), uint32_t(1));
    renderer::CommandList blocked{wait,wait,nullptr};
    renderer::submit_command_list(state, nullptr, blocked);
    auto future = finish_async(fixture.thread, [&] { return renderer::finish_guest(state, *fixture.thread, nullptr, 0); });
    ASSERT_TRUE(fixture.waiting(renderer::FinishPhase::Fence));
    fixture.process_next();
    ASSERT_TRUE(state.active_batch.has_value());
    EXPECT_EQ(state.active_batch->first->opcode, renderer::CommandOpcode::WaitSyncObject);
    const int not_parked = fixture.env.kernel.stop_world(0, std::chrono::milliseconds(50));
    EXPECT_EQ(not_parked, 0);
    std::vector<gxm::ContextSnapshot> contexts;
    std::vector<renderer::CommandSnapshot> commands;
    std::vector<gxm::BatchSnapshot> batches;
    std::vector<renderer::FinishSnapshot> operations;
    std::string error;
    EXPECT_TRUE(gxm::capture_contexts(fixture.env, contexts, commands, batches, error)) << error;
    EXPECT_TRUE(renderer::capture_finish_operations(state, operations, error)) << error;
    EXPECT_TRUE(renderer::validate_finish_operations(operations, commands, error)) << error;
    ASSERT_EQ(commands.size(), 2u);
    EXPECT_EQ(commands[1].opcode, renderer::CommandOpcode::Nop);
    EXPECT_EQ(commands[1].completion_id, operations[0].id);
    EXPECT_EQ(state.progress_counter.load(), 0u);
    renderer::subject_done(sync, 1);
    EXPECT_TRUE(renderer::process_batch_until_wait(state, state.features, fixture.env.mem, fixture.env.cfg, *state.active_batch));
    state.active_batch.reset();
    fixture.process_next();
    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
    fixture.env.kernel.resume_world();
    EXPECT_EQ(finish_result(future), 0);
    EXPECT_TRUE(state.finish_operations.empty());
    sync->~SceGxmSyncObject();
}

TEST(RendererFinishContinuation, RestorePendingFenceCompletesOriginalTokenWithoutAnotherNop) {
    RendererFinishEnv fixture;
    auto &state = fixture.state();
    auto original = finish_async(fixture.thread, [&] { return renderer::finish_guest(state, *fixture.thread, nullptr, 0); });
    ASSERT_TRUE(fixture.waiting(renderer::FinishPhase::Fence));
    const auto thread_snapshot = fixture.thread->capture_snapshot();
    std::vector<gxm::ContextSnapshot> contexts;
    std::vector<renderer::CommandSnapshot> commands;
    std::vector<gxm::BatchSnapshot> batches;
    std::vector<renderer::FinishSnapshot> operations;
    std::string error;
    ASSERT_TRUE(gxm::capture_contexts(fixture.env, contexts, commands, batches, error));
    ASSERT_TRUE(renderer::capture_finish_operations(state, operations, error));
    renderer::abort_finish_operations(state);
    EXPECT_EQ(finish_result(original), 0);
    renderer::discard_pending_batches(state);
    for (int restore = 0; restore != 2; ++restore) {
        fixture.make_thread();
        fixture.thread->apply_private_snapshot(thread_snapshot);
        ASSERT_TRUE(gxm::restore_contexts(fixture.env, contexts, commands, batches, error)) << error;
        ASSERT_TRUE(renderer::restore_finish_operations(state, operations, error)) << error;
        ASSERT_TRUE(fixture.thread->restore_wait_queues());
        const auto record = fixture.thread->saved_wait_continuations().front();
        auto resumed = finish_async(fixture.thread, [&] { return fixture.thread->resume_wait_continuation(record); });
        ASSERT_TRUE(fixture.waiting(renderer::FinishPhase::Fence));
        EXPECT_EQ(state.command_buffer_queue.size(), 0u);
        EXPECT_EQ(state.snapshot_pending_batches.size(), 1u);
        ASSERT_TRUE(gxm::activate_saved_batches(fixture.env, 0, error));
        fixture.process_next();
        EXPECT_EQ(finish_result(resumed), 0);
        EXPECT_TRUE(state.finish_operations.empty());
        EXPECT_EQ(state.command_buffer_queue.size(), 0u);
    }
    EXPECT_EQ(state.progress_counter.load(), 2u);
}

TEST(RendererFinishContinuation, RestoreDrainPhaseDoesNotRepeatFenceAndOldCallbackCannotCompleteNewToken) {
    RendererFinishEnv fixture;
    auto &state = fixture.state(); state.hold_drain = true;
    auto original = finish_async(fixture.thread, [&] { return renderer::finish_guest(state, *fixture.thread, nullptr, 0); });
    ASSERT_TRUE(fixture.waiting(renderer::FinishPhase::Fence));
    fixture.process_next();
    ASSERT_TRUE(fixture.waiting(renderer::FinishPhase::Drain));
    ASSERT_TRUE(state.wait_for_drains(1));
    const auto thread_snapshot = fixture.thread->capture_snapshot();
    std::vector<renderer::FinishSnapshot> operations;
    std::string error;
    ASSERT_TRUE(renderer::capture_finish_operations(state, operations, error));
    ASSERT_TRUE(renderer::validate_finish_operations(operations, {}, error)) << error;
    renderer::abort_finish_operations(state);
    EXPECT_EQ(finish_result(original), 0);
    fixture.make_thread(); fixture.thread->apply_private_snapshot(thread_snapshot);
    ASSERT_TRUE(renderer::restore_finish_operations(state, operations, error));
    ASSERT_TRUE(fixture.thread->restore_wait_queues());
    const auto record = fixture.thread->saved_wait_continuations().front();
    auto resumed = finish_async(fixture.thread, [&] { return fixture.thread->resume_wait_continuation(record); });
    ASSERT_TRUE(fixture.waiting(renderer::FinishPhase::Drain));
    // Restored WAITING is logical state, not proof the resumed host helper has
    // issued its barrier. Await backend admission before taking the callbacks.
    ASSERT_TRUE(state.wait_for_drains(2));
    std::vector<std::function<void()>> callbacks;
    { const std::lock_guard lock(state.drain_mutex); callbacks = std::move(state.drains); }
    ASSERT_EQ(callbacks.size(), 2u);
    callbacks[0]();
    EXPECT_EQ(resumed.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
    callbacks[1]();
    EXPECT_EQ(finish_result(resumed), 0);
    EXPECT_EQ(state.progress_counter.load(), 1u);
    EXPECT_EQ(state.command_buffer_queue.size(), 0u);
}

TEST(RendererFinishContinuation, MalformedTokenOwnershipAndResultPhasesRejectBeforeRestore) {
    renderer::CommandSnapshot nop;
    nop.opcode = renderer::CommandOpcode::Nop; nop.completion_id = 7;
    int32_t code = 1; std::memcpy(nop.data.data(), &code, sizeof(code));
    renderer::FinishSnapshot saved; saved.id = 7; saved.owner = 97;
    std::string error;
    EXPECT_TRUE(renderer::validate_finish_operations({saved}, {nop}, error));
    EXPECT_FALSE(renderer::validate_finish_operations({}, {nop}, error));
    EXPECT_FALSE(renderer::validate_finish_operations({saved,saved}, {nop}, error));
    EXPECT_FALSE(renderer::validate_finish_operations({saved}, {nop,nop}, error));
    saved.phase = renderer::FinishPhase::Drain;
    EXPECT_FALSE(renderer::validate_finish_operations({saved}, {}, error));
    saved.fence_done = true; saved.result = 1;
    EXPECT_TRUE(renderer::validate_finish_operations({saved}, {}, error));
    EXPECT_FALSE(renderer::validate_finish_operations({saved}, {nop}, error));
    saved.result = 2;
    EXPECT_FALSE(renderer::validate_finish_operations({saved}, {}, error));
    RendererFinishEnv fixture;
    auto &state = fixture.state();
    renderer::FinishSnapshot original; original.id = 5; original.owner = 97;
    ASSERT_TRUE(renderer::restore_finish_operations(state, {original}, error));
    EXPECT_FALSE(renderer::restore_finish_operations(state, {saved}, error));
    std::vector<renderer::FinishSnapshot> after;
    ASSERT_TRUE(renderer::capture_finish_operations(state, after, error));
    ASSERT_EQ(after.size(), 1u);
    EXPECT_EQ(after.front().id, original.id);
    EXPECT_EQ(state.next_finish_id, 6u);
}

TEST(SessionStartupAudioContinuation, NormalLateInitAdmitsLiveOutputWaitWithoutLoadingAState) {
    EmuEnvState env;
    ThreadStatePtr thread;
    std::future<int> output;
    struct Cleanup {
        EmuEnvState &env;
        ThreadStatePtr &thread;
        std::future<int> &output;
        ~Cleanup() {
            env.kernel.resume_world();
            env.audio.stop_all_ports();
            if (thread) thread->exit_delete(false);
            if (output.valid()) output.wait();
            env.kernel.threads.clear();
            thread.reset();
            ngs::deinit(env.ngs, env.mem);
            deinit_mem(env.mem);
        }
    } cleanup{env, thread, output};
    env.renderer = std::make_unique<SnapshotBatchTestRenderer>();
    // Exercise the real startup boundary without opening a host sound device.
    // Audio initialization failure is supported; handler admission must still
    // be established for a backend selected later in this same live session.
    env.cfg.current_config.audio_backend = "startup-admission-test-no-device";
    ASSERT_TRUE(app::late_init(env));
    env.audio.adapter = std::make_unique<CubebAudioAdapter>(env.audio);
    auto port = std::make_shared<CubebAudioOutPort>();
    port->len = 4; port->len_bytes = 16; port->freq = 48000; port->mode = 1;
    port->len_microseconds = 83; port->spec.channels = 2;
    port->audio_buffers.resize(2);
    for (auto &buffer : port->audio_buffers) buffer.buffer.resize(port->len_bytes, 0x17);
    port->nb_buffers_ready = 2;
    env.audio.out_ports.emplace(5, port);
    const Address pcm = alloc(env.mem, port->len_bytes, "startup-audio-pcm");
    ASSERT_NE(pcm, 0u);
    std::fill_n(Ptr<uint8_t>(pcm).get(env.mem), port->len_bytes, 0x53);
    thread = std::make_shared<ThreadState>(73, env.kernel, env.mem);
    thread->cpu = init_cpu(false, thread->id, 0, env.mem);
    thread->update_status(ThreadStatus::running);
    env.kernel.threads.emplace(thread->id, thread);
    output = std::async(std::launch::async, [&] {
        return export_sceAudioOutOutput(env, thread->id, "startup-test", 5, Ptr<void>(pcm).get(env.mem));
    });
    bool waiting = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto snapshot = thread->capture_snapshot();
        if (snapshot.status == ThreadStatus::waiting && snapshot.waits.size() == 1
            && snapshot.waits[0].operation == WaitOperation::audio_output) { waiting = true; break; }
        std::this_thread::yield();
    }
    ASSERT_TRUE(waiting);
    ASSERT_EQ(env.kernel.stop_world(0, std::chrono::milliseconds(50)), 0);
    std::vector<ThreadState::Snapshot> snapshots;
    std::string error;
    EXPECT_TRUE(emucorev::savestate::capture_thread_snapshots(env.kernel, 0, snapshots, error)) << error;
    // Check the behavior actually depends on the startup-installed handler,
    // rather than the test silently admitting unresumable audio continuations.
    env.kernel.wait_resume_handlers.erase(WaitOperation::audio_output);
    EXPECT_FALSE(emucorev::savestate::capture_thread_snapshots(env.kernel, 0, snapshots, error));
    EXPECT_EQ(port->nb_buffers_ready, 2);
    EXPECT_EQ(port->audio_buffers[0].buffer, std::vector<uint8_t>(16, 0x17));
}
