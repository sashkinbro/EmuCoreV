#include <cpu/functions.h>
#include <mem/functions.h>
#include <touch/functions.h>
#include <ctrl/ctrl.h>

DECL_EXPORT(int, sceTouchRead, SceUInt32 port, SceTouchData *data, SceUInt32 count);
DECL_EXPORT(int, sceTouchRead2, SceUInt32 port, SceTouchData *data, SceUInt32 count);
DECL_EXPORT(int, sceCtrlReadBufferNegative, int port, SceCtrlData *data, int count);
DECL_EXPORT(int, sceCtrlReadBufferNegative2, int port, SceCtrlData2 *data, int count);
DECL_EXPORT(int, sceCtrlReadBufferPositive, int port, SceCtrlData *data, int count);
DECL_EXPORT(int, sceCtrlReadBufferPositive2, int port, SceCtrlData2 *data, int count);
DECL_EXPORT(int, sceCtrlReadBufferPositiveExt, int port, SceCtrlData *data, int count);
DECL_EXPORT(int, sceCtrlReadBufferPositiveExt2, int port, SceCtrlData2 *data, int count);

namespace {
class InputReadContinuation : public testing::Test {
protected:
    EmuEnvState env;
    ThreadStatePtr thread;
    Address allocation = 0;
    unsigned imports = 0;
    void SetUp() override {
        ASSERT_TRUE(init(env.mem, false));
        allocation = alloc(env.mem, 4096, "input-continuation-test");
        ASSERT_NE(allocation, 0u);
        thread = std::make_shared<ThreadState>(73, env.kernel, env.mem);
        thread->cpu = init_cpu(false, 73, 0, env.mem);
        ASSERT_TRUE(thread->cpu);
        env.kernel.threads.emplace(73, thread);
        register_display_wait_continuations(env.display, env.kernel);
        env.kernel.call_import = [&](CPUState &cpu, uint32_t nid, SceUID id) {
            ++imports;
            const auto port = read_reg(cpu, 0);
            auto *data = Ptr<SceCtrlData>(read_reg(cpu, 1)).get(env.mem);
            const auto count = read_reg(cpu, 2);
            int result = -1;
            switch (nid) {
            case 0x169A1D58: result = export_sceTouchRead(env, id, "resume-test", port, reinterpret_cast<SceTouchData *>(data), count); break;
            case 0x39401BEA: result = export_sceTouchRead2(env, id, "resume-test", port, reinterpret_cast<SceTouchData *>(data), count); break;
            case 0x15F96FB0: result = export_sceCtrlReadBufferNegative(env, id, "resume-test", port, data, count); break;
            case 0x27A0C5FB: result = export_sceCtrlReadBufferNegative2(env, id, "resume-test", port, reinterpret_cast<SceCtrlData2 *>(data), count); break;
            case 0x67E7AB83: result = export_sceCtrlReadBufferPositive(env, id, "resume-test", port, data, count); break;
            case 0xC4226A3E: result = export_sceCtrlReadBufferPositive2(env, id, "resume-test", port, reinterpret_cast<SceCtrlData2 *>(data), count); break;
            case 0xE2D99296: result = export_sceCtrlReadBufferPositiveExt(env, id, "resume-test", port, data, count); break;
            case 0xA7178860: result = export_sceCtrlReadBufferPositiveExt2(env, id, "resume-test", port, reinterpret_cast<SceCtrlData2 *>(data), count); break;
            default: ADD_FAILURE() << "unexpected replayed import";
            }
            write_reg(cpu, 0, result);
        };
    }
    void TearDown() override {
        env.kernel.threads.clear();
        thread.reset();
        deinit_mem(env.mem);
    }
    int resume(uint32_t nid, uint64_t consumed_vcount = 1) {
        auto *stub = Ptr<uint32_t>(allocation).get(env.mem);
        stub[0] = 0xef000000;
        stub[1] = 0xe1a0f00e;
        stub[2] = nid;
        std::memset(Ptr<void>(allocation + 128).get(env.mem), 0x5a, 256);
        ThreadState::Snapshot snapshot;
        snapshot.id = thread->id;
        snapshot.status = ThreadStatus::running;
        WaitContinuationSnapshot wait;
        wait.frame_sequence = 1;
        wait.operation = WaitOperation::display_vblank;
        wait.args[0] = 2;
        wait.target = {SCE_KERNEL_WAITTYPE_EVENT};
        wait.has_result = true;
        wait.context.set_pc(allocation + 4);
        wait.context.cpu_registers[0] = 0;
        wait.context.cpu_registers[1] = allocation + 128;
        wait.context.cpu_registers[2] = 2;
        snapshot.waits.push_back(wait); // Old archive shape: no caller tag.
        thread->apply_private_snapshot(snapshot);
        env.display.vblank_count = 3;
        env.touch.last_vcount[0] = consumed_vcount;
        env.ctrl.last_vcount[0] = consumed_vcount;
        EXPECT_TRUE(thread->restore_wait_queues());
        thread->activate_restored_continuations();
        return thread->resume_wait_continuation(thread->current_wait_continuation());
    }
};
} // namespace

TEST_F(InputReadContinuation, LegacyTouchWaitCompletesRealSampleCopyAndReturnCount) {
    env.touch.touch_mode[0] = SCE_TOUCH_SAMPLING_STATE_START;
    for (auto &pair : env.touch.touch_buffers) {
        pair[0].reportNum = 1;
        pair[0].report[0].x = 321;
    }
    for (const uint32_t nid : {0x169A1D58u, 0x39401BEAu}) {
        EXPECT_EQ(resume(nid), 2);
        const auto *samples = Ptr<SceTouchData>(allocation + 128).get(env.mem);
        EXPECT_EQ(samples[0].reportNum, 1);
        EXPECT_EQ(samples[1].report[0].x, 321);
        EXPECT_EQ(env.touch.last_vcount[0], 3u);
    }
    EXPECT_EQ(imports, 2u);
}

TEST_F(InputReadContinuation, EveryLegacyCtrlReadWaitCompletesRealOutputAndCount) {
    env.ctrl.overlay_input_intercepted = true; // No SDL device needed.
    for (const uint32_t nid : {0x15F96FB0u, 0x27A0C5FBu, 0x67E7AB83u, 0xC4226A3Eu, 0xE2D99296u, 0xA7178860u}) {
        EXPECT_EQ(resume(nid), 2);
        const auto *samples = Ptr<SceCtrlData2>(allocation + 128).get(env.mem);
        EXPECT_NE(samples[0].timeStamp, UINT64_C(0x5a5a5a5a5a5a5a5a));
        EXPECT_EQ(samples[0].lx, 128);
        EXPECT_EQ(samples[1].ly, 128);
        EXPECT_EQ(env.ctrl.last_vcount[0], 3u);
    }
    EXPECT_EQ(imports, 6u);
}

TEST_F(InputReadContinuation, OrdinaryDisplayWaitDoesNotReplayItsImport) {
    EXPECT_EQ(resume(0x814C90AF), 0);
    EXPECT_EQ(imports, 0u);
}

TEST_F(InputReadContinuation, ReaderThatAlreadyConsumedThisVblankDoesNotEnrollAgain) {
    env.touch.touch_mode[0] = SCE_TOUCH_SAMPLING_STATE_START;
    // Same interleaving as an ordinary reader advancing the shared cursor
    // after the original waiter was granted, before its HLE tail runs.
    EXPECT_EQ(resume(0x169A1D58U, 3), 1);
    env.ctrl.overlay_input_intercepted = true;
    EXPECT_EQ(resume(0x67E7AB83U, 3), 1);
    EXPECT_EQ(imports, 2u);
    EXPECT_FALSE(input_read_resuming_after_vblank());
}

TEST(KernelImportPreflight, InputReadRequiresExactArmStubAndCoveredOutput) {
    ImportAbiFixture fixture;
    fixture.wait.operation = WaitOperation::display_vblank;
    fixture.wait.context.set_pc(0x1004);
    fixture.wait.context.cpu_registers[0] = 0;
    fixture.wait.context.cpu_registers[1] = 0x2000;
    fixture.wait.context.cpu_registers[2] = 2;
    fixture.word(0x1000, 0xef000000U);
    fixture.word(0x1004, 0xe1a0f00eU);
    fixture.word(0x1008, 0x169A1D58U);
    std::string error;
    EXPECT_TRUE(fixture.valid(error)) << error;
    fixture.wait.context.cpu_registers[1] = 0x3ffc;
    EXPECT_FALSE(fixture.valid(error));
    fixture.wait.context.cpu_registers[1] = 0x2000;
    fixture.wait.context.cpu_registers[0] = 2;
    EXPECT_FALSE(fixture.valid(error));
    fixture.wait.context.cpu_registers[0] = 0;
    fixture.wait.context.cpu_registers[2] = MAX_TOUCH_BUFFER_SAVED + 1;
    EXPECT_FALSE(fixture.valid(error));
    fixture.word(0x1008, 0x67E7AB83U);
    fixture.wait.context.cpu_registers[2] = UINT32_MAX;
    EXPECT_FALSE(fixture.valid(error));

    const auto read = [&](uint64_t address, void *out, size_t size) {
        if (address < 0x1000 || address + size > 0x4000) return false;
        std::memcpy(out, fixture.bytes.data() + address - 0x1000, size);
        return true;
    };
    EXPECT_EQ(input_read_import_at_wait(fixture.wait.context, read), 0x67E7AB83U);
    fixture.word(0x1000, 0xef000001U);
    EXPECT_EQ(input_read_import_at_wait(fixture.wait.context, read), 0u);
    fixture.word(0x1000, 0xef000000U);
    fixture.word(0x1004, 0xe12fff1eU);
    EXPECT_EQ(input_read_import_at_wait(fixture.wait.context, read), 0u);
    fixture.word(0x1004, 0xe1a0f00eU);
    fixture.wait.context.cpsr |= 0x20;
    EXPECT_EQ(input_read_import_at_wait(fixture.wait.context, read), 0u);
    fixture.wait.context.cpsr &= ~0x20U;
    fixture.wait.context.set_pc(0xfffffffcU);
    EXPECT_EQ(input_read_import_at_wait(fixture.wait.context, read), 0u);
}
