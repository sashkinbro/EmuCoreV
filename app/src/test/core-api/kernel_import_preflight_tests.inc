#include <emucorev/savestate/kernel_import_io.h>
namespace {
struct ImportAbiFixture {
    std::array<uint8_t, 0x3000> bytes{};
    WaitContinuationSnapshot wait;
    ImportAbiFixture() {
        wait.operation = WaitOperation::callback_dispatch;
        wait.context.set_sp(0x2000);
    }
    void word(Address address, uint32_t value) { std::memcpy(bytes.data() + address - 0x1000, &value, 4); }
    bool valid(std::string &error) {
        const auto contains = [&](uint64_t address, size_t size) { return address >= 0x1000 && address <= 0x4000 && size <= 0x4000 - address; };
        return emucorev::savestate::validate_kernel_import_abi(wait, contains, [&](uint64_t address, void *out, size_t size) {
                if (!contains(address, size)) return false;
                std::memcpy(out, bytes.data() + address - 0x1000, size); return true; }, error);
    }
};
} // namespace
TEST(KernelImportPreflight, event_and_pipe_validate_stacked_outputs_and_buffer_without_dispatch) {
    ImportAbiFixture fixture;
    std::string error;
    fixture.wait.args[0] = 0xA0490795U;
    fixture.wait.context.cpu_registers[2] = 0x1000;
    fixture.wait.context.cpu_registers[3] = 0x1008;
    fixture.word(0x2000, 0x1010);
    EXPECT_TRUE(fixture.valid(error)) << error;
    fixture.word(0x2000, 0xfffffffcU);
    EXPECT_FALSE(fixture.valid(error));
    fixture.wait.args[0] = 0x33AF829BU;
    fixture.wait.context.cpu_registers[1] = 0x3000;
    fixture.wait.context.cpu_registers[2] = 0x1000;
    fixture.word(0x2000, 0x1010);
    fixture.word(0x2004, 0x1020);
    EXPECT_TRUE(fixture.valid(error)) << error;
    fixture.wait.context.cpu_registers[2] = 0x1001;
    EXPECT_FALSE(fixture.valid(error));
    fixture.wait.context.cpu_registers[2] = 1;
    fixture.wait.context.set_sp(0x3ffc);
    EXPECT_FALSE(fixture.valid(error)); // Second stacked word is outside staged RAM.
}
TEST(KernelImportPreflight, creation_rejects_unterminated_name_nested_option_and_bridge_stack_underflow) {
    ImportAbiFixture fixture;
    std::string error;
    fixture.wait.operation = WaitOperation::creation_import;
    fixture.wait.args[0] = 0xC0FAF6A3U;
    fixture.wait.context.cpu_registers[0] = 0x1000;
    fixture.wait.context.cpu_registers[1] = 0x3001;
    fixture.wait.context.cpu_registers[2] = 64;
    fixture.wait.context.cpu_registers[3] = 0x1800;
    fixture.word(0x1804, 0x4000);
    EXPECT_TRUE(fixture.valid(error)) << error;
    fixture.word(0x1810, 0xfffffffcU);
    EXPECT_FALSE(fixture.valid(error));
    fixture.word(0x1810, 0);
    std::fill_n(fixture.bytes.begin(), 4096, 'x');
    EXPECT_FALSE(fixture.valid(error));
    fixture.bytes[0] = 0;
    fixture.wait.args[0] = 0xC5C11EE7U;
    fixture.wait.context.cpu_registers[3] = 0x4000;
    fixture.word(0x2000, 0);
    fixture.word(0x2004, 0);
    fixture.word(0x2008, 0);
    EXPECT_TRUE(fixture.valid(error)) << error;
    fixture.wait.context.set_sp(0x1000);
    EXPECT_FALSE(fixture.valid(error));
}

TEST(KernelImportPreflight, thread_owners_require_exact_stack_and_full_tls_allocations) {
    using namespace emucorev::savestate;
    ThreadState::Snapshot thread;
    thread.stack_addr = 0x1000;
    thread.stack_size = 0x2000;
    thread.tls_addr = 0x4000;
    const std::vector<MemorySpanInfo> spans{ { 0x1000, 2 }, { 0x4000, 2 } };
    std::string error;
    EXPECT_TRUE(validate_kernel_thread_allocations({ thread }, 0x1000, spans, error)) << error;
    EXPECT_FALSE(validate_kernel_thread_allocations({ thread }, 0, spans, error));
    auto interior = thread;
    interior.stack_addr = 0x2000;
    interior.stack_size = 0x1000;
    EXPECT_FALSE(validate_kernel_thread_allocations({ interior }, 0x1000, spans, error));
    auto alias = thread;
    alias.tls_addr = thread.stack_addr;
    EXPECT_FALSE(validate_kernel_thread_allocations({ alias }, 0x1000, spans, error));
    EXPECT_FALSE(validate_kernel_thread_allocations({ thread, thread }, 0x1000, spans, error));
}
