#include <emucorev/savestate/gxm_state_io.h>
#include <emucorev/savestate/sysmem_state_io.h>
#include <emucorev/savestate/memory_image.h>
#include <emucorev/savestate/state_io.h>

namespace {
class GxmSysmemPreflight : public testing::Test {
protected:
    std::vector<uint8_t> ram = std::vector<uint8_t>(0x20000);
    fs::path root;
    std::string error;
    emucorev::savestate::GxmSnapshot gxm;
    template <typename T> void put(Address address, const T &value) {
        std::memcpy(ram.data() + address - 0x1000, &value, sizeof(value));
    }
    void SetUp() override {
        root = fs::temp_directory_path() / fs::unique_path("gxm-preflight-%%%%-%%%%");
        ASSERT_TRUE(fs::create_directory(root));
        gxm::ContextSnapshot context;
        context.address = 0x1000;
        context.state.type = SCE_GXM_CONTEXT_TYPE_IMMEDIATE;
        context.state.vertex_program = Ptr<const SceGxmVertexProgram>(0x4000);
        context.render_target_address = 0x5000;
        context.state.vdm_buffer = Ptr<void>(0xa000);
        context.state.vdm_buffer_size = 4096;
        context.alloc_space = Ptr<void>(0xa000);
        context.alloc_space_end = Ptr<void>(0xb000);
        context.command_allocator_size = 4096 / sizeof(renderer::Command);
        gxm.contexts.push_back(context);
        gxm.last_immediate_context = context.address;
        gxm::RenderTargetSnapshot target;
        target.address = 0x5000;
        target.width = target.height = target.params.width = target.params.height = 32;
        target.scenes_per_frame = target.params.scenesPerFrame = 1;
        gxm.render_targets.push_back(target);
        gxm::VertexProgramSnapshot vertex;
        vertex.address = 0x4000;
        vertex.program = Ptr<const SceGxmProgram>(0x8000);
        gxm.vertex_programs.push_back(vertex);
        SceGxmProgram program{};
        program.magic = 0x00505847;
        program.size = sizeof(program) + sizeof(SceGxmProgramVertexVaryings);
        program.varyings_offset = sizeof(program) - offsetof(SceGxmProgram, varyings_offset);
        put(0x8000, program);
    }
    void TearDown() override { fs::remove_all(root); }
    std::unique_ptr<emucorev::savestate::MemoryImage> image() {
        using namespace emucorev::savestate;
        BufferWriter bytes;
        bytes.u32(kMemoryImageFormatVersion); bytes.u32(ram.size()); bytes.u32(1);
        bytes.u32(0x1000); bytes.u32(ram.size() / kMemoryImagePageSize);
        bytes.u64(0x1000); bytes.u32(ram.size()); bytes.u32(uint32_t(MemoryChunkEncoding::Raw));
        bytes.u32(ram.size()); bytes.u32(crc32_buffer(ram.data(), ram.size())); bytes.append(ram.data(), ram.size());
        Writer writer;
        const auto file = root / "memory.ecvs";
        if (!writer.open(file, error) || !writer.write_section(SectionId::Memory, bytes.data().data(), bytes.size(), false, error)
            || !writer.finalize(error)) return {};
        Reader reader;
        if (!reader.open(file, error) || !reader.validate_sections(error)) return {};
        return MemoryImage::preflight(reader, root, error);
    }
};
}

TEST_F(GxmSysmemPreflight, ValidatesStagedProgramsAndRoundedCommandArena) {
    auto memory = image(); ASSERT_TRUE(memory) << error;
    EXPECT_TRUE(emucorev::savestate::validate_gxm_state(gxm, *memory, error)) << error;
    gxm.contexts[0].render_target_address = 0x6000;
    EXPECT_FALSE(emucorev::savestate::validate_gxm_state(gxm, *memory, error));
    gxm.contexts[0].render_target_address = 0x5000;
    gxm.vertex_programs[0].address = gxm.contexts[0].address + 8;
    EXPECT_FALSE(emucorev::savestate::validate_gxm_state(gxm, *memory, error));
}

TEST_F(GxmSysmemPreflight, RejectsStagedGxpOffsetOverflowBeforeCompilation) {
    SceGxmProgram program{};
    std::memcpy(&program, ram.data() + 0x7000, sizeof(program));
    program.parameters_offset = 0xfffffff0;
    program.parameter_count = 1;
    put(0x8000, program);
    auto memory = image(); ASSERT_TRUE(memory);
    EXPECT_FALSE(emucorev::savestate::validate_gxm_state(gxm, *memory, error));
}

TEST_F(GxmSysmemPreflight, VmTableMayAliasSameOwnerButCannotClaimDifferentAllocation) {
    using namespace emucorev::savestate;
    auto memory = image(); ASSERT_TRUE(memory);
    SysmemSnapshot state;
    state.next_uid = 2;
    KernelMemBlock block{};
    block.size = sizeof(SceKernelMemBlockInfo);
    block.mappedBase = Ptr<void>(0x1000); block.mappedSize = 0x2000;
    state.blocks.emplace(1, block); state.vm_blocks.emplace(1, block);
    EXPECT_TRUE(validate_sysmem_state(state, *memory, error)) << error;
    state.vm_blocks[1].mappedBase = Ptr<void>(0x4000);
    EXPECT_FALSE(validate_sysmem_state(state, *memory, error));
    state.vm_blocks.clear();
    state.blocks.emplace(2, block); state.next_uid = 3;
    EXPECT_FALSE(validate_sysmem_state(state, *memory, error));
}

TEST(SysmemPreflightParser, RejectsDuplicateOwnersTrailingBytesAndOversizedCountsAtomically) {
    using namespace emucorev::savestate;
    BufferWriter writer;
    writer.i32(3); writer.u32(0); writer.u32(0); writer.u32(0);
    KernelMemBlock block{};
    writer.u32(2); writer.i32(1); writer.value(block); writer.i32(1); writer.value(block); writer.u32(0);
    SysmemSnapshot output; output.next_uid = 42;
    std::string error;
    EXPECT_FALSE(parse_sysmem_state(writer.data(), output, error));
    EXPECT_EQ(output.next_uid, 42);
    writer.clear(); writer.i32(1); writer.u32(0); writer.u32(0); writer.u32(0); writer.u32(0); writer.u32(0);
    EXPECT_TRUE(parse_sysmem_state(writer.data(), output, error)) << error;
    writer.u8(0);
    EXPECT_FALSE(parse_sysmem_state(writer.data(), output, error));
    writer.clear(); writer.i32(1); writer.u32(0); writer.u32(0); writer.u32(0); writer.u32(0xffffffff);
    EXPECT_FALSE(parse_sysmem_state(writer.data(), output, error));
}

TEST(GxmPreflightParser, RejectsTruncatedTablesAndTrailingBytesWithoutPublishingDto) {
    using namespace emucorev::savestate;
    BufferWriter writer;
    writer.value(SceGxmInitializeParams{}); writer.u32(0); writer.u32(0); writer.u32(0); writer.u32(0); writer.u32(0);
    writer.i32(37); writer.u32(4); writer.u32(0x1234);
    for (int i = 0; i < 12; ++i) writer.u32(0);
    GxmSnapshot output;
    std::string error;
    ASSERT_TRUE(parse_gxm_state(writer.data(), output, error)) << error;
    EXPECT_EQ(output.display_queue_thread, 37);
    EXPECT_EQ(output.display_worker_phase, 4u);
    EXPECT_EQ(output.display_previous_entry, 0x1234u);
    auto invalid_phase = writer.data();
    const uint32_t unsupported_phase = 6;
    std::memcpy(invalid_phase.data() + sizeof(SceGxmInitializeParams) + 6 * sizeof(uint32_t), &unsupported_phase, sizeof(unsupported_phase));
    output.global_timestamp = 42;
    EXPECT_FALSE(parse_gxm_state(invalid_phase, output, error));
    EXPECT_EQ(output.global_timestamp, 42u);
    writer.u8(9);
    EXPECT_FALSE(parse_gxm_state(writer.data(), output, error));
    writer.clear(); writer.value(SceGxmInitializeParams{}); writer.u32(0); writer.u32(0); writer.u32(0); writer.u32(0); writer.u32(0);
    writer.i32(0); writer.u32(0); writer.u32(0); writer.u32(0xffffffff);
    output.global_timestamp = 42;
    EXPECT_FALSE(parse_gxm_state(writer.data(), output, error));
    EXPECT_EQ(output.global_timestamp, 42u);
}

TEST(GxmPreflightParser, CanonicalizesUnusedLegacyMaskButRejectsMeaningfulInvalidBools) {
    using namespace emucorev::savestate;
    BufferWriter writer;
    writer.value(SceGxmInitializeParams{});
    for (int i = 0; i < 8; ++i) writer.u32(0);
    writer.u32(1); // context count
    writer.u32(0x1000); writer.boolean(true); writer.u32(0);
    writer.value(renderer::GxmRecordState{});
    const size_t state_offset = writer.size();
    writer.value(GxmContextState{});
    writer.boolean(false); writer.u64(0);
    for (int i = 0; i < 6; ++i) writer.u32(0);
    writer.boolean(false); writer.boolean(false); writer.u32(0); // deferred lists
    for (int i = 0; i < 11; ++i) writer.u32(0); // remaining tables
    auto bytes = writer.data();
    bytes[state_offset + offsetof(GxmContextState, writing_mask)] = 0xf0;
    GxmSnapshot output;
    std::string error;
    ASSERT_TRUE(parse_gxm_state(bytes, output, error)) << error;
    ASSERT_EQ(output.contexts.size(), 1u);
    EXPECT_FALSE(output.contexts[0].state.writing_mask);
    for (const size_t offset : {offsetof(GxmContextState, visibility_enable), offsetof(GxmContextState, visibility_is_increment), offsetof(GxmContextState, active)}) {
        auto malformed = bytes;
        malformed[state_offset + offset] = 0xf0;
        output.global_timestamp = 42;
        EXPECT_FALSE(parse_gxm_state(malformed, output, error));
        EXPECT_EQ(output.global_timestamp, 42u);
    }
}

TEST_F(GxmSysmemPreflight, PendingVisibilityAndSurfaceExtentsAreCheckedBeforeRestore) {
    auto memory = image(); ASSERT_TRUE(memory) << error;
    renderer::Command live{}; live.opcode = renderer::CommandOpcode::SetState;
    renderer::CommandHelper helper(&live);
    auto tag = renderer::GXMState::VisibilityBuffer;
    auto address = Ptr<uint32_t>(0x20ff0); uint32_t stride = 32;
    ASSERT_TRUE(renderer::do_command_push_data(helper, tag, address, stride));
    renderer::CommandSnapshot command;
    ASSERT_TRUE(renderer::capture_command_snapshot(live, {}, command, error)) << error;
    gxm.commands = {command}; gxm.contexts[0].pending_first = gxm.contexts[0].pending_last = 1;
    EXPECT_FALSE(emucorev::savestate::validate_gxm_state(gxm, *memory, error));
    stride = 16; std::memcpy(gxm.commands[0].data.data() + 6, &stride, sizeof(stride));
    EXPECT_TRUE(emucorev::savestate::validate_gxm_state(gxm, *memory, error)) << error;
    stride = 0xfffffffcu; std::memcpy(gxm.commands[0].data.data() + 6, &stride, sizeof(stride));
    EXPECT_FALSE(emucorev::savestate::validate_gxm_state(gxm, *memory, error));

    command = {}; command.opcode = renderer::CommandOpcode::SetContext;
    command.render_target_address = 0x5000; command.has_color_surface = true;
    command.color_surface.data = Ptr<void>(0x20ff0);
    command.color_surface.width = 4; command.color_surface.height = 2; command.color_surface.strideInPixels = 4;
    command.color_surface.colorFormat = SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR;
    gxm.commands = {command};
    EXPECT_FALSE(emucorev::savestate::validate_gxm_state(gxm, *memory, error));
    gxm.commands[0].color_surface.height = 1;
    EXPECT_TRUE(emucorev::savestate::validate_gxm_state(gxm, *memory, error)) << error;
    gxm.commands[0].has_color_surface = false; gxm.commands[0].color_surface = {};
    gxm.commands[0].has_depth_surface = true;
    auto &depth = gxm.commands[0].depth_surface;
    depth.set_type(SCE_GXM_DEPTH_STENCIL_SURFACE_LINEAR);
    depth.set_format(SCE_GXM_DEPTH_STENCIL_FORMAT_DF32); depth.set_stride(32);
    depth.depth_data = Ptr<void>(0x20ffc);
    EXPECT_FALSE(emucorev::savestate::validate_gxm_state(gxm, *memory, error));
}

TEST_F(GxmSysmemPreflight, MacroblockTargetCountsCannotDivideByZeroOrProduceEmptyGeometry) {
    auto memory = image(); ASSERT_TRUE(memory) << error;
    auto &target = gxm.render_targets[0];
    target.params.flags = SCE_GXM_RENDER_TARGET_MACROTILE_SYNC;
    EXPECT_FALSE(emucorev::savestate::validate_gxm_state(gxm, *memory, error));
    target.params.flags |= 1u << 8; // Rows are still zero.
    EXPECT_FALSE(emucorev::savestate::validate_gxm_state(gxm, *memory, error));
    target.params.flags |= 1u << 12;
    EXPECT_TRUE(emucorev::savestate::validate_gxm_state(gxm, *memory, error)) << error;
    target.params.flags = SCE_GXM_RENDER_TARGET_MACROTILE_SYNC | (5u << 8) | (1u << 12);
    EXPECT_FALSE(emucorev::savestate::validate_gxm_state(gxm, *memory, error));
    target.params.flags = SCE_GXM_RENDER_TARGET_MACROTILE_SYNC | (4u << 8) | (1u << 12);
    target.width = target.params.width = 3;
    EXPECT_FALSE(emucorev::savestate::validate_gxm_state(gxm, *memory, error));
}
