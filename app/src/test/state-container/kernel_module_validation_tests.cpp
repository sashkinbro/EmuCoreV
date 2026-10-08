#include <emucorev/savestate/archive.h>
#include <emucorev/savestate/kernel_base_io.h>
#include <emucorev/savestate/memory_image.h>
#include <emucorev/savestate/state_io.h>
#include <gtest/gtest.h>

#define SCE_ELF_DEFS_TARGET
#include <sce-elf-defs.h>
#undef SCE_ELF_DEFS_TARGET

#include <cstring>
#include <cstdlib>

using namespace emucorev::savestate;

namespace {
constexpr uint32_t kBase = 0x1000;
constexpr uint32_t kPage = kMemoryImagePageSize;

class ModuleValidationTests : public testing::Test {
protected:
    fs::path root;
    std::string error;

    void SetUp() override {
        root = fs::temp_directory_path() / fs::unique_path("module-validation-%%%%-%%%%");
        ASSERT_TRUE(fs::create_directory(root));
    }

    void TearDown() override { fs::remove_all(root); }

    std::unique_ptr<MemoryImage> make_image(const std::vector<uint8_t> &guest) {
        if (guest.size() != kPage)
            return nullptr;
        BufferWriter section;
        section.u32(kMemoryImageFormatVersion);
        section.u32(kPage);
        section.u32(1);
        section.u32(kBase);
        section.u32(1);
        section.u64(kBase);
        section.u32(kPage);
        section.u32(static_cast<uint32_t>(MemoryChunkEncoding::Raw));
        section.u32(kPage);
        section.u32(crc32_buffer(guest.data(), guest.size()));
        section.append(guest.data(), guest.size());

        const fs::path archive_path = root / "guest.ecvs";
        Writer writer;
        if (!writer.open(archive_path, error)
            || !writer.write_section(SectionId::Memory, section.data().data(), section.size(), false, error)
            || !writer.finalize(error)) return nullptr;
        Reader reader;
        if (!reader.open(archive_path, error) || !reader.validate_sections(error)) return nullptr;
        return MemoryImage::preflight(reader, root, error);
    }

    static void put(std::vector<uint8_t> &guest, uint32_t address, const void *bytes, size_t size) {
        std::memcpy(guest.data() + (address - kBase), bytes, size);
    }

    static KernelBaseSnapshot module_snapshot(uint32_t info_offset = 0) {
        KernelBaseSnapshot snapshot;
        KernelModule module{};
        module.info_segment_address = Ptr<const uint8_t>(kBase);
        module.info_offset = info_offset;
        module.info.segments[0].vaddr = Ptr<const void>(kBase);
        module.info.segments[0].memsz = kPage;
        snapshot.loaded_modules.emplace(1, std::move(module));
        return snapshot;
    }
};

TEST_F(ModuleValidationTests, AcceptsBoundedEmptyModuleTables) {
    std::vector<uint8_t> guest(kPage, 0);
    sce_module_info_raw raw{};
    put(guest, kBase, &raw, sizeof(raw));
    auto image = make_image(guest);
    ASSERT_NE(image, nullptr) << error;
    auto snapshot = module_snapshot();
    EXPECT_TRUE(validate_kernel_modules(snapshot, *image, error)) << error;
}

TEST_F(ModuleValidationTests, AcceptsValidExportArraysAndShortAndLongImports) {
    for (const uint16_t import_size : {uint16_t{0x24}, uint16_t{0x34}}) {
        std::vector<uint8_t> guest(kPage, 0);
        sce_module_info_raw raw{};
        raw.export_top = 0x100;
        raw.export_end = 0x120;
        raw.import_top = 0x140;
        raw.import_end = raw.import_top + import_size;
        put(guest, kBase, &raw, sizeof(raw));

        sce_module_exports_raw exports{};
        exports.size = sizeof(exports);
        exports.num_syms_funcs = 1;
        exports.library_name = kBase + 0x300;
        exports.nid_table = kBase + 0x320;
        exports.entry_table = kBase + 0x324;
        put(guest, kBase + raw.export_top, &exports, sizeof(exports));
        const char library_name[] = "testlib";
        put(guest, kBase + 0x300, library_name, sizeof(library_name));
        const uint32_t nid = 0x12345678;
        const uint32_t exported_function = kBase + 0x500;
        put(guest, kBase + 0x320, &nid, sizeof(nid));
        put(guest, kBase + 0x324, &exported_function, sizeof(exported_function));

        const uint32_t func_nid_table = kBase + 0x340;
        const uint32_t func_entry_table = kBase + 0x344;
        const uint32_t import_nid = 0x87654321;
        const uint32_t import_stub = kBase + 0x600;
        if (import_size == 0x24) {
            sce_module_imports_short_raw imports{};
            imports.size = import_size;
            imports.library_name = kBase + 0x360;
            imports.num_syms_funcs = 1;
            imports.func_nid_table = func_nid_table;
            imports.func_entry_table = func_entry_table;
            put(guest, kBase + raw.import_top, &imports, sizeof(imports));
        } else {
            sce_module_imports_raw imports{};
            imports.size = import_size;
            imports.library_name = kBase + 0x360;
            imports.num_syms_funcs = 1;
            imports.func_nid_table = func_nid_table;
            imports.func_entry_table = func_entry_table;
            put(guest, kBase + raw.import_top, &imports, sizeof(imports));
        }
        put(guest, kBase + 0x360, library_name, sizeof(library_name));
        put(guest, func_nid_table, &import_nid, sizeof(import_nid));
        put(guest, func_entry_table, &import_stub, sizeof(import_stub));

        auto image = make_image(guest);
        ASSERT_NE(image, nullptr) << error;
        auto snapshot = module_snapshot();
        EXPECT_TRUE(validate_kernel_modules(snapshot, *image, error)) << "size=" << import_size << ": " << error;
    }
}

TEST_F(ModuleValidationTests, RejectsRawHeaderThatOnlyHasOneByteInSavedMemory) {
    std::vector<uint8_t> guest(kPage, 0);
    sce_module_info_raw raw{};
    put(guest, kBase + kPage - 1, &raw, 1);
    auto image = make_image(guest);
    ASSERT_NE(image, nullptr) << error;
    auto snapshot = module_snapshot(kPage - 1);
    EXPECT_FALSE(validate_kernel_modules(snapshot, *image, error));
}

TEST_F(ModuleValidationTests, RejectsRawHeaderThatCrossesItsOwningSegment) {
    std::vector<uint8_t> guest(kPage, 0);
    sce_module_info_raw raw{};
    put(guest, kBase + 0xF0, &raw, sizeof(raw));
    auto image = make_image(guest);
    ASSERT_NE(image, nullptr) << error;
    auto snapshot = module_snapshot(0xF0);
    snapshot.loaded_modules.at(1)->info.segments[0].memsz = 0x100;
    EXPECT_FALSE(validate_kernel_modules(snapshot, *image, error));
}

TEST_F(ModuleValidationTests, RejectsExportDescriptorWithInvalidStrideAndOutOfRangeTables) {
    for (const auto [descriptor_size, table_address] : {
             std::pair<uint16_t, uint32_t>{0, kBase + 0x100},
             std::pair<uint16_t, uint32_t>{0x20, kBase + kPage - 2},
         }) {
        std::vector<uint8_t> guest(kPage, 0);
        sce_module_info_raw raw{};
        raw.export_top = 0x100;
        raw.export_end = 0x120;
        put(guest, kBase, &raw, sizeof(raw));
        sce_module_exports_raw descriptor{};
        descriptor.size = descriptor_size;
        descriptor.num_syms_funcs = 1;
        descriptor.nid_table = table_address;
        descriptor.entry_table = table_address;
        put(guest, kBase + 0x100, &descriptor, sizeof(descriptor));
        auto image = make_image(guest);
        ASSERT_NE(image, nullptr) << error;
        auto snapshot = module_snapshot();
        EXPECT_FALSE(validate_kernel_modules(snapshot, *image, error));
    }
}

TEST_F(ModuleValidationTests, RejectsMalformedShortAndLongImportTables) {
    for (const uint32_t descriptor_size : {0u, 0x24u, 0x34u}) {
        std::vector<uint8_t> guest(kPage, 0);
        sce_module_info_raw raw{};
        raw.import_top = 0x100;
        raw.import_end = 0x134;
        put(guest, kBase, &raw, sizeof(raw));
        if (descriptor_size == 0x24) {
            sce_module_imports_short_raw descriptor{};
            descriptor.size = static_cast<uint16_t>(descriptor_size);
            descriptor.num_syms_funcs = 1;
            descriptor.func_nid_table = kBase + kPage - 4;
            descriptor.func_entry_table = kBase + kPage - 4;
            put(guest, kBase + 0x100, &descriptor, sizeof(descriptor));
        } else {
            sce_module_imports_raw descriptor{};
            descriptor.size = static_cast<uint16_t>(descriptor_size);
            descriptor.num_syms_funcs = 1;
            descriptor.func_nid_table = kBase + kPage - 4;
            descriptor.func_entry_table = kBase + kPage - 4;
            put(guest, kBase + 0x100, &descriptor, sizeof(descriptor));
        }
        auto image = make_image(guest);
        ASSERT_NE(image, nullptr) << error;
        auto snapshot = module_snapshot();
        EXPECT_FALSE(validate_kernel_modules(snapshot, *image, error)) << descriptor_size;
    }
}

TEST_F(ModuleValidationTests, RejectsVariableImportRelocationHeaderThatRunsPastMemory) {
    std::vector<uint8_t> guest(kPage, 0);
    sce_module_info_raw raw{};
    raw.import_top = 0x100;
    raw.import_end = 0x134;
    put(guest, kBase, &raw, sizeof(raw));
    sce_module_imports_raw descriptor{};
    descriptor.size = 0x34;
    descriptor.num_syms_vars = 1;
    descriptor.var_nid_table = kBase + 0x200;
    descriptor.var_entry_table = kBase + 0x204;
    put(guest, kBase + 0x100, &descriptor, sizeof(descriptor));
    const uint32_t nid = 1;
    const uint32_t relocation_pointer = kBase + kPage - 2;
    put(guest, kBase + 0x200, &nid, sizeof(nid));
    put(guest, kBase + 0x204, &relocation_pointer, sizeof(relocation_pointer));
    auto image = make_image(guest);
    ASSERT_NE(image, nullptr) << error;
    auto snapshot = module_snapshot();
    EXPECT_FALSE(validate_kernel_modules(snapshot, *image, error));
}

TEST_F(ModuleValidationTests, RejectsTruncatedVariableImportRelocationRecord) {
    std::vector<uint8_t> guest(kPage, 0);
    sce_module_info_raw raw{};
    raw.import_top = 0x100;
    raw.import_end = 0x134;
    put(guest, kBase, &raw, sizeof(raw));
    sce_module_imports_raw descriptor{};
    descriptor.size = 0x34;
    descriptor.num_syms_vars = 1;
    descriptor.var_nid_table = kBase + 0x200;
    descriptor.var_entry_table = kBase + 0x204;
    put(guest, kBase + 0x100, &descriptor, sizeof(descriptor));
    const uint32_t nid = 1;
    const uint32_t relocation_pointer = kBase + 0x700;
    put(guest, kBase + 0x200, &nid, sizeof(nid));
    put(guest, kBase + 0x204, &relocation_pointer, sizeof(relocation_pointer));
    const uint32_t packed_header = 8u << 4; // Header plus an incomplete format-1 record.
    const uint32_t first_word = 1;
    put(guest, relocation_pointer, &packed_header, sizeof(packed_header));
    put(guest, relocation_pointer + 4, &first_word, sizeof(first_word));
    auto image = make_image(guest);
    ASSERT_NE(image, nullptr) << error;
    auto snapshot = module_snapshot();
    EXPECT_FALSE(validate_kernel_modules(snapshot, *image, error));
}
} // namespace
