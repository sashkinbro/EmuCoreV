#include <gtest/gtest.h>
#include <gxm/types.h>
#include <shader/usse_program_analyzer.h>
#include <shader/thread_buffer_bounds.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

namespace {

struct ProgramFixture {
    alignas(8) std::array<uint8_t, 1024> storage{};
    SceGxmProgram *program = nullptr;
    static constexpr size_t container_offset = 256;
    static constexpr size_t buffer_info_offset = 320;
    static constexpr size_t primary_code_offset = 512;
    static constexpr size_t secondary_code_offset = 640;

    ProgramFixture(const uint16_t reside_buffer = 2, const uint16_t ldst_base_offset = 7, const uint16_t base_sa_offset = 0) {
        program = ::new (storage.data()) SceGxmProgram{};
        program->container_count = 1;
        program->container_offset = static_cast<uint32_t>(container_offset - offsetof(SceGxmProgram, container_offset));
        program->uniform_buffer_count = 1;
        program->uniform_buffer_offset = static_cast<uint32_t>(buffer_info_offset - offsetof(SceGxmProgram, uniform_buffer_offset));
        program->primary_program_offset = static_cast<uint32_t>(primary_code_offset - offsetof(SceGxmProgram, primary_program_offset));
        program->secondary_program_offset = static_cast<uint32_t>(secondary_code_offset - offsetof(SceGxmProgram, secondary_program_offset));
        program->secondary_program_offset_end = static_cast<uint32_t>(secondary_code_offset - offsetof(SceGxmProgram, secondary_program_offset_end));

        SceGxmProgramParameterContainer container{};
        container.container_index = 19;
        container.base_sa_offset = base_sa_offset;
        std::memcpy(storage.data() + container_offset, &container, sizeof(container));

        SceGxmUniformBufferInfo buffer{};
        buffer.reside_buffer = reside_buffer;
        buffer.ldst_base_offset = ldst_base_offset;
        std::memcpy(storage.data() + buffer_info_offset, &buffer, sizeof(buffer));
    }

    void set_primary(std::initializer_list<uint64_t> instructions) {
        program->primary_program_instr_count = static_cast<uint32_t>(instructions.size());
        std::memcpy(storage.data() + primary_code_offset, instructions.begin(), instructions.size() * sizeof(uint64_t));
    }

    void set_secondary(std::initializer_list<uint64_t> instructions) {
        program->secondary_program_instr_count = static_cast<uint32_t>(instructions.size());
        program->secondary_program_offset = static_cast<uint32_t>(secondary_code_offset - offsetof(SceGxmProgram, secondary_program_offset));
        program->secondary_program_offset_end = static_cast<uint32_t>(secondary_code_offset + instructions.size() * sizeof(uint64_t)
            - offsetof(SceGxmProgram, secondary_program_offset_end));
        std::memcpy(storage.data() + secondary_code_offset, instructions.begin(), instructions.size() * sizeof(uint64_t));
    }

    void set_other_buffer(const uint16_t reside_buffer, const uint16_t ldst_base_offset) {
        SceGxmUniformBufferInfo buffer{};
        buffer.reside_buffer = reside_buffer;
        buffer.ldst_base_offset = ldst_base_offset;
        std::memcpy(storage.data() + buffer_info_offset + sizeof(buffer), &buffer, sizeof(buffer));
        program->uniform_buffer_count = 2;
    }
};

uint64_t make_ldr(const uint8_t base_num, const bool uniform_base, const bool register_offset) {
    uint64_t inst = uint64_t{ 0b11101 } << 59;
    inst |= static_cast<uint64_t>(base_num) << 14;
    if (uniform_base) {
        inst |= uint64_t{ 1 } << 50; // extended src0 bank
        inst |= uint64_t{ 1 } << 34; // SECATTR
    } else {
        inst |= uint64_t{ 0 } << 50; // TEMP
    }
    if (register_offset) {
        inst |= uint64_t{ 1 } << 7; // src1 register 1
        inst |= uint64_t{ 0 } << 49; // unextended TEMP bank
        inst |= uint64_t{ 0 } << 30;
    } else {
        inst |= uint64_t{ 2 } << 30; // extended IMMEDIATE bank
        inst |= uint64_t{ 1 } << 49;
    }
    inst |= uint64_t{ 2 } << 28; // load offset is immediate
    inst |= uint64_t{ 1 } << 48;
    return inst;
}

uint64_t make_vmov_sa_to_temp(const uint8_t sa_num, const uint8_t temp_num) {
    uint64_t inst = uint64_t{ 0b00111 } << 59;
    inst |= static_cast<uint64_t>(temp_num) << 18; // TEMP destination
    inst |= static_cast<uint64_t>(sa_num) << 6; // src1 register
    inst |= uint64_t{ 3 } << 30; // SECATTR source
    inst |= uint64_t{ 2 } << 40; // INT32, avoiding vector register doubling
    return inst;
}

} // namespace

TEST(UsseDynamicUniforms, ConstantOffsetDoesNotRequestSlack) {
    ProgramFixture fixture;
    fixture.set_primary({ make_ldr(7, true, false) });
    EXPECT_EQ(shader::usse::get_dynamic_uniform_buffers(*fixture.program), 0u);
}

TEST(UsseDynamicUniforms, RegisterOffsetMarksOnlyTheReferencedUniformBlock) {
    ProgramFixture fixture;
    fixture.set_primary({ make_ldr(7, true, true) });
    EXPECT_EQ(shader::usse::get_dynamic_uniform_buffers(*fixture.program), 1u << 3); // buffer 2 maps to block 3
}

TEST(UsseDynamicUniforms, DefaultUniformBufferMapsToDefaultContainerBit) {
    ProgramFixture fixture(SCE_GXM_DEFAULT_UNIFORM_BUFFER, 7);
    fixture.set_primary({ make_ldr(7, true, true) });
    EXPECT_EQ(shader::usse::get_dynamic_uniform_buffers(*fixture.program), 1u);
}

TEST(UsseDynamicUniforms, PropagatedUniformBaseAndUnknownPointerAreConservative) {
    ProgramFixture propagated;
    propagated.set_primary({ make_vmov_sa_to_temp(7, 1), make_ldr(1, false, true) });
    EXPECT_EQ(shader::usse::get_dynamic_uniform_buffers(*propagated.program), 1u << 3);

    ProgramFixture unknown;
    unknown.set_primary({ make_ldr(1, false, true) });
    EXPECT_EQ(shader::usse::get_dynamic_uniform_buffers(*unknown.program), 1u << 3);
}

TEST(UsseDynamicUniforms, SecondaryProgramAddressPropagationReachesPrimaryReads) {
    ProgramFixture fixture;
    fixture.set_secondary({ make_vmov_sa_to_temp(7, 1) });
    fixture.set_primary({ make_ldr(1, true, true) });
    EXPECT_EQ(shader::usse::get_dynamic_uniform_buffers(*fixture.program), 1u << 3);
}

TEST(UsseDynamicUniforms, ThreadBufferBaseIsExcludedFromUniformSlackMask) {
    ProgramFixture fixture;
    fixture.set_other_buffer(SCE_GXM_THREAD_BUFFER, 8);
    fixture.set_primary({ make_ldr(8, true, true) });
    EXPECT_EQ(shader::usse::get_dynamic_uniform_buffers(*fixture.program), 0u);
}

TEST(UsseDynamicUniforms, OutOfRangeBaseSaDoesNotShiftOrIndexPastTheAnalysisTables) {
    ProgramFixture fixture(2, 1, 300);
    fixture.set_primary({ make_ldr(7, true, true) });
    EXPECT_EQ(shader::usse::get_dynamic_uniform_buffers(*fixture.program), 0u);
}

TEST(UsseThreadBuffer, DeclaredArrayBoundsHaveAValidClampedIndex) {
    EXPECT_EQ(shader::usse::thread_buffer_f32_count(0), 1u);
    EXPECT_EQ(shader::usse::thread_buffer_f32_count(63), 1u);
    EXPECT_EQ(shader::usse::thread_buffer_f32_count(64), 1u);
    EXPECT_EQ(shader::usse::thread_buffer_f32_count(128), 2u);
    EXPECT_EQ(shader::usse::thread_buffer_last_index(1), 0u);
    EXPECT_EQ(shader::usse::thread_buffer_last_index(2), 1u);
    EXPECT_EQ(shader::usse::thread_buffer_last_index(0), 0u);
}
