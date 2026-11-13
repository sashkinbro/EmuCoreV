#include <emucorev/savestate/gpu_buffer_stream.h>

#include <gtest/gtest.h>

#include <array>
#include <string>

using namespace emucorev::savestate;

namespace {
fs::path make_test_directory() {
    const fs::path path = fs::temp_directory_path() / fs::unique_path("gpu-buffer-stream-%%%%-%%%%");
    fs::create_directories(path);
    return path;
}

void remove_test_directory(const fs::path &path) {
    boost::system::error_code error;
    fs::remove_all(path, error);
}

void append_u32(std::vector<uint8_t> &bytes, uint32_t value) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
    bytes.push_back(static_cast<uint8_t>(value >> 16));
    bytes.push_back(static_cast<uint8_t>(value >> 24));
}

bool write_test_page_archive(const fs::path &path, const std::vector<uint8_t> &payload, std::string &error) {
    Writer writer;
    if (!writer.open(path, error) || !writer.begin_section(SectionId::GpuBufferPages, false, error))
        return false;
    std::vector<uint8_t> body;
    append_u32(body, 0x31504742); // stream magic
    append_u32(body, 1); // stream version
    append_u32(body, 1); // buffers
    append_u32(body, 0x30000000);
    append_u32(body, kGpuSnapshotPageSize);
    append_u32(body, 1); // pages
    append_u32(body, 0); // page offset
    append_u32(body, kGpuSnapshotPageSize);
    append_u32(body, 0); // raw encoding
    append_u32(body, kGpuSnapshotPageSize);
    append_u32(body, 1); // deliberately invalid CRC for a complete page
    body.insert(body.end(), payload.begin(), payload.end());
    if (!writer.append_section_data(body.data(), body.size(), error) || !writer.end_section(error))
        return false;
    return writer.finalize(error);
}
}

TEST(GpuBufferPageStream, RoundTripsLogicalPayloadOverBufferedSectionLimit) {
    constexpr uint32_t page_count = (256u * 1024u * 1024u) / kGpuSnapshotPageSize + 1;
    constexpr uint32_t mapping_size = page_count * kGpuSnapshotPageSize;
    const fs::path directory = make_test_directory();
    const fs::path save_path = directory / "stream.ecvs";

    std::string error;
    auto source_store = std::make_shared<GpuPageStore>();
    ASSERT_TRUE(source_store->open(directory, error)) << error;
    std::array<uint8_t, kGpuSnapshotPageSize> source_page{};
    for (size_t i = 0; i < source_page.size(); ++i)
        source_page[i] = static_cast<uint8_t>((i * 37u) % 251u);
    uint64_t source_offset = UINT64_MAX;
    ASSERT_TRUE(source_store->append(source_page.data(), source_page.size(), source_offset, error)) << error;

    GpuSnapshotBuffer source{ 0x10000000, mapping_size, {} };
    source.pages.reserve(page_count);
    for (uint32_t page = 0; page < page_count; ++page)
        source.pages.push_back({ page * kGpuSnapshotPageSize, {}, source_offset });

    {
        Writer writer;
        ASSERT_TRUE(writer.open(save_path, error)) << error;
        ASSERT_TRUE(write_gpu_buffer_page_section(writer, { source }, source_store.get(),
            uint64_t(mapping_size) + kGpuSnapshotPageSize, error)) << error;
        ASSERT_TRUE(writer.finalize(error)) << error;
    }

    std::shared_ptr<GpuPageStore> restored_store;
    {
        Reader archive;
        ASSERT_TRUE(archive.open(save_path, error)) << error;
        ASSERT_TRUE(archive.validate_sections(error)) << error;
        auto section = archive.open_section(SectionId::GpuBufferPages, error);
        ASSERT_NE(section, nullptr) << error;

        std::vector<GpuSnapshotBuffer> restored_directory{ GpuSnapshotBuffer{ 0x10000000, mapping_size, std::vector<GpuSnapshotPage>(page_count) } };
        ASSERT_TRUE(read_gpu_buffer_page_section(*section, restored_directory, restored_store, directory,
            uint64_t(mapping_size) + kGpuSnapshotPageSize, error)) << error;
        ASSERT_NE(restored_store, nullptr);
        EXPECT_EQ(restored_store->size(), uint64_t(mapping_size));
        ASSERT_EQ(restored_directory[0].pages.size(), page_count);
        EXPECT_EQ(restored_directory[0].pages.front().offset, 0u);
        EXPECT_EQ(restored_directory[0].pages.back().offset, mapping_size - kGpuSnapshotPageSize);

        std::array<uint8_t, kGpuSnapshotPageSize> restored_page{};
        ASSERT_TRUE(restored_store->read(restored_directory[0].pages.back().staged_offset,
            restored_page.data(), restored_page.size(), error)) << error;
        EXPECT_EQ(restored_page, source_page);
    }

    restored_store.reset();
    source_store.reset();
    remove_test_directory(directory);
}

TEST(GpuBufferPageStream, CanonicalizesDescendingRuntimeMappingOrder) {
    const fs::path directory = make_test_directory();
    const fs::path save_path = directory / "descending.ecvs";
    std::string error;
    const std::vector<uint8_t> zero_page(kGpuSnapshotPageSize);
    std::vector<GpuSnapshotBuffer> buffers{
        GpuSnapshotBuffer{ 0x40000000, kGpuSnapshotPageSize, { { 0, zero_page } } },
        GpuSnapshotBuffer{ 0x30000000, kGpuSnapshotPageSize, { { 0, zero_page } } },
        };
    sort_gpu_snapshot_buffers(buffers);
    ASSERT_EQ(buffers[0].address, 0x30000000u);
    ASSERT_EQ(buffers[1].address, 0x40000000u);

    {
        Writer writer;
        ASSERT_TRUE(writer.open(save_path, error)) << error;
        ASSERT_TRUE(write_gpu_buffer_page_section(writer, buffers, nullptr,
            2ull * kGpuSnapshotPageSize, error)) << error;
        ASSERT_TRUE(writer.finalize(error)) << error;
    }

    std::shared_ptr<GpuPageStore> restored_store;
    {
        Reader archive;
        ASSERT_TRUE(archive.open(save_path, error)) << error;
        ASSERT_TRUE(archive.validate_sections(error)) << error;
        auto section = archive.open_section(SectionId::GpuBufferPages, error);
        ASSERT_NE(section, nullptr) << error;
        std::vector<GpuSnapshotBuffer> restored_directory{
            GpuSnapshotBuffer{ 0x30000000, kGpuSnapshotPageSize, {}, 1 },
            GpuSnapshotBuffer{ 0x40000000, kGpuSnapshotPageSize, {}, 1 },
        };
        ASSERT_TRUE(read_gpu_buffer_page_section(*section, restored_directory, restored_store, directory,
            2ull * kGpuSnapshotPageSize, error)) << error;
        ASSERT_EQ(restored_directory.size(), 2u);
        EXPECT_EQ(restored_directory[0].address, 0x30000000u);
        EXPECT_EQ(restored_directory[1].address, 0x40000000u);
    }

    restored_store.reset();
    remove_test_directory(directory);
}

TEST(GpuBufferPageStream, RejectsMetadataDirectoryMismatchBeforeStagingPages) {
    const fs::path directory = make_test_directory();
    const fs::path save_path = directory / "mismatch.ecvs";
    std::string error;

    auto source_store = std::make_shared<GpuPageStore>();
    ASSERT_TRUE(source_store->open(directory, error)) << error;
    const std::array<uint8_t, kGpuSnapshotPageSize> page{};
    uint64_t staged_offset = UINT64_MAX;
    ASSERT_TRUE(source_store->append(page.data(), page.size(), staged_offset, error)) << error;
    GpuSnapshotBuffer source{ 0x20000000, kGpuSnapshotPageSize, { { 0, {}, staged_offset } } };
    {
        Writer writer;
        ASSERT_TRUE(writer.open(save_path, error)) << error;
        ASSERT_TRUE(write_gpu_buffer_page_section(writer, { source }, source_store.get(),
            2ull * kGpuSnapshotPageSize, error)) << error;
        ASSERT_TRUE(writer.finalize(error)) << error;
    }

    std::shared_ptr<GpuPageStore> restored_store;
    {
        Reader archive;
        ASSERT_TRUE(archive.open(save_path, error)) << error;
        ASSERT_TRUE(archive.validate_sections(error)) << error;
        auto section = archive.open_section(SectionId::GpuBufferPages, error);
        ASSERT_NE(section, nullptr) << error;
        std::vector<GpuSnapshotBuffer> wrong_directory{ GpuSnapshotBuffer{ 0x20000000, kGpuSnapshotPageSize, {} } };
        EXPECT_FALSE(read_gpu_buffer_page_section(*section, wrong_directory, restored_store, directory,
            2ull * kGpuSnapshotPageSize, error));
        EXPECT_EQ(restored_store, nullptr);
        EXPECT_NE(error.find("directory"), std::string::npos);
    }

    source_store.reset();
    remove_test_directory(directory);
}

TEST(GpuBufferPageStream, RejectsTruncatedRawPage) {
    const fs::path directory = make_test_directory();
    const fs::path save_path = directory / "truncated.ecvs";
    std::string error;
    ASSERT_TRUE(write_test_page_archive(save_path, {}, error)) << error;

    {
        Reader archive;
        ASSERT_TRUE(archive.open(save_path, error)) << error;
        ASSERT_TRUE(archive.validate_sections(error)) << error;
        auto section = archive.open_section(SectionId::GpuBufferPages, error);
        ASSERT_NE(section, nullptr) << error;
        std::vector<GpuSnapshotBuffer> directory_table{
            GpuSnapshotBuffer{ 0x30000000, kGpuSnapshotPageSize, {}, 1 }
        };
        std::shared_ptr<GpuPageStore> store;
        EXPECT_FALSE(read_gpu_buffer_page_section(*section, directory_table, store, directory,
            2ull * kGpuSnapshotPageSize, error));
        EXPECT_NE(error.find("raw GPU page size"), std::string::npos);
        EXPECT_EQ(store, nullptr);
    }
    remove_test_directory(directory);
}

TEST(GpuBufferPageStream, RejectsCorruptPageChecksum) {
    const fs::path directory = make_test_directory();
    const fs::path save_path = directory / "corrupt.ecvs";
    std::string error;
    const std::array<uint8_t, kGpuSnapshotPageSize> zero_page{};
    ASSERT_TRUE(write_test_page_archive(save_path,
        std::vector<uint8_t>(zero_page.begin(), zero_page.end()), error)) << error;

    {
        Reader archive;
        ASSERT_TRUE(archive.open(save_path, error)) << error;
        ASSERT_TRUE(archive.validate_sections(error)) << error;
        auto section = archive.open_section(SectionId::GpuBufferPages, error);
        ASSERT_NE(section, nullptr) << error;
        std::vector<GpuSnapshotBuffer> directory_table{
            GpuSnapshotBuffer{ 0x30000000, kGpuSnapshotPageSize, {}, 1 }
        };
        std::shared_ptr<GpuPageStore> store;
        EXPECT_FALSE(read_gpu_buffer_page_section(*section, directory_table, store, directory,
            2ull * kGpuSnapshotPageSize, error));
        EXPECT_NE(error.find("GPU page checksum mismatch"), std::string::npos);
        EXPECT_EQ(store, nullptr);
    }
    remove_test_directory(directory);
}
