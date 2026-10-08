#include <emucorev/savestate/archive.h>
#include <emucorev/savestate/memory_image.h>
#include <emucorev/savestate/state_io.h>
#include <emucorev/savestate/display_state_io.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <tuple>
#include <utility>

using namespace emucorev::savestate;

namespace {

struct SpanFixture {
    uint32_t address;
    uint32_t pages;
};

class MemoryImageTests : public testing::Test {
protected:
    fs::path root;
    fs::path archive_path;
    std::string error;

    void SetUp() override {
        const char *tmp = std::getenv("TMPDIR");
        root = fs::path(tmp ? tmp : ".") / fs::unique_path("memory-image-%%%%-%%%%");
        ASSERT_TRUE(fs::create_directory(root));
        archive_path = root / "state.ecvs";
    }

    void TearDown() override { fs::remove_all(root); }

    void write_archive(const std::vector<uint8_t> &memory_section) {
        Writer writer;
        ASSERT_TRUE(writer.open(archive_path, error)) << error;
        ASSERT_TRUE(writer.write_section(SectionId::Memory, memory_section.data(), memory_section.size(), false, error)) << error;
        ASSERT_TRUE(writer.finalize(error)) << error;
    }

    std::unique_ptr<MemoryImage> preflight(const std::vector<uint8_t> &memory_section) {
        write_archive(memory_section);
        Reader reader;
        EXPECT_TRUE(reader.open(archive_path, error)) << error;
        EXPECT_TRUE(reader.validate_sections(error)) << error;
        return MemoryImage::preflight(reader, root, error);
    }

    static void write_header(BufferWriter &writer, uint32_t chunk_size, const std::vector<SpanFixture> &spans) {
        writer.u32(kMemoryImageFormatVersion);
        writer.u32(chunk_size);
        writer.u32(static_cast<uint32_t>(spans.size()));
        for (const SpanFixture &span : spans) {
            writer.u32(span.address);
            writer.u32(span.pages);
        }
    }

    static std::vector<uint8_t> bytes(size_t size, uint8_t value) {
        return std::vector<uint8_t>(size, value);
    }

    static void write_record(BufferWriter &writer, uint64_t address, uint32_t raw_size,
        MemoryChunkEncoding encoding, uint32_t stored_size, uint32_t checksum) {
        writer.u64(address);
        writer.u32(raw_size);
        writer.u32(static_cast<uint32_t>(encoding));
        writer.u32(stored_size);
        writer.u32(checksum);
    }

    size_t staged_file_count() const {
        size_t count = 0;
        for (const auto &entry : fs::directory_iterator(root)) {
            if (entry.path().filename().string().find("memory-image-") == 0)
                ++count;
        }
        return count;
    }

    static void write_raw_chunk(BufferWriter &writer, uint64_t address, const std::vector<uint8_t> &raw,
        uint32_t checksum_override = UINT32_MAX) {
        const uint32_t checksum = checksum_override == UINT32_MAX ? crc32_buffer(raw.data(), raw.size()) : checksum_override;
        write_record(writer, address, static_cast<uint32_t>(raw.size()), MemoryChunkEncoding::Raw,
            static_cast<uint32_t>(raw.size()), checksum);
        writer.append(raw.data(), raw.size());
    }
};

TEST_F(MemoryImageTests, StagesAndVisitsDecodedChunksForRawDeflatedAndZeroRecords) {
    constexpr uint32_t page = kMemoryImagePageSize;
    BufferWriter writer;
    write_header(writer, page, {{0x1000, 3}});

    const auto raw = bytes(page, 0x31);
    write_raw_chunk(writer, 0x1000, raw);

    std::vector<uint8_t> deflated_raw(page);
    for (size_t i = 0; i < deflated_raw.size(); ++i)
        deflated_raw[i] = static_cast<uint8_t>(i % 17);
    std::vector<uint8_t> deflated;
    ASSERT_TRUE(compress_buffer(deflated_raw.data(), deflated_raw.size(), deflated, error)) << error;
    write_record(writer, 0x2000, page, MemoryChunkEncoding::Deflated,
        static_cast<uint32_t>(deflated.size()), crc32_buffer(deflated_raw.data(), deflated_raw.size()));
    writer.append(deflated.data(), deflated.size());

    write_record(writer, 0x3000, page, MemoryChunkEncoding::Zeros, 0, 0);

    auto image = preflight(writer.data());
    ASSERT_NE(image, nullptr) << error;
    EXPECT_EQ(staged_file_count(), 1u);
    ASSERT_EQ(image->spans().size(), 1u);
    EXPECT_EQ(image->spans()[0].address, 0x1000u);
    EXPECT_EQ(image->spans()[0].page_count, 3u);

    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> visited;
    ASSERT_TRUE(image->visit_validated_chunks(
        [&](uint64_t address, const uint8_t *data, size_t size, std::string &) {
            visited.emplace_back(address, std::vector<uint8_t>(data, data + size));
            return true;
        }, error)) << error;
    ASSERT_EQ(visited.size(), 3u);
    EXPECT_EQ(visited[0].first, 0x1000u);
    EXPECT_EQ(visited[0].second, raw);
    EXPECT_EQ(visited[1].first, 0x2000u);
    EXPECT_EQ(visited[1].second, deflated_raw);
    EXPECT_EQ(visited[2].first, 0x3000u);
    EXPECT_EQ(visited[2].second, bytes(page, 0));
    image.reset();
    EXPECT_EQ(staged_file_count(), 0u);
}

TEST_F(MemoryImageTests, RejectsUnalignedZeroOverlappingAndOutOfRangeSpans) {
    for (const std::vector<SpanFixture> &spans : {
             std::vector<SpanFixture>{{0, 1}},
             std::vector<SpanFixture>{{0x1001, 1}},
             std::vector<SpanFixture>{{0x1000, 0}},
             std::vector<SpanFixture>{{0x1000, 2}, {0x2000, 1}},
             std::vector<SpanFixture>{{0x3000, 1}, {0x1000, 1}},
             std::vector<SpanFixture>{{0xFFFFF000u, 2}},
         }) {
        BufferWriter writer;
        write_header(writer, kMemoryImagePageSize, spans);
        EXPECT_EQ(preflight(writer.data()), nullptr) << error;
    }
}

TEST_F(MemoryImageTests, ReadsSavedBytesAcrossChunkAndAdjacentSpanBoundaries) {
    constexpr uint32_t page = kMemoryImagePageSize;
    BufferWriter writer;
    write_header(writer, page, {{0x1000, 1}, {0x2000, 1}, {0x3000, 1}, {0x5000, 1}});
    write_raw_chunk(writer, 0x1000, bytes(page, 0x71));
    write_record(writer, 0x2000, page, MemoryChunkEncoding::Zeros, 0, 0);
    write_raw_chunk(writer, 0x3000, bytes(page, 0x39));
    write_raw_chunk(writer, 0x5000, bytes(page, 0x52));
    auto image = preflight(writer.data());
    ASSERT_NE(image, nullptr) << error;
    EXPECT_TRUE(image->contains(0x1FFE, page + 4));
    EXPECT_FALSE(image->contains(0x3FFE, 4));
    EXPECT_FALSE(image->contains(UINT64_MAX - 1, 4));
    EXPECT_FALSE(image->contains(0xFFFFFFFE, 4));
    EXPECT_FALSE(image->contains(0, 1));
    EXPECT_TRUE(image->contains(0, 0));
    std::vector<uint8_t> read(page + 4, 0xFF);
    ASSERT_TRUE(image->read_bytes(0x1FFE, read.data(), read.size(), error)) << error;
    EXPECT_EQ(read[0], 0x71);
    EXPECT_EQ(read[1], 0x71);
    EXPECT_EQ(read[2], 0);
    EXPECT_EQ(read[page + 1], 0);
    EXPECT_EQ(read[page + 2], 0x39);
    EXPECT_EQ(read[page + 3], 0x39);
    std::array<uint8_t, 4> untouched{1, 2, 3, 4};
    EXPECT_FALSE(image->read_bytes(0x3FFE, untouched.data(), untouched.size(), error));
    EXPECT_EQ(untouched, (std::array<uint8_t, 4>{1, 2, 3, 4}));
    EXPECT_TRUE(image->read_bytes(0, nullptr, 0, error));
    EXPECT_FALSE(image->read_bytes(0x1000, nullptr, 1, error));
    ASSERT_TRUE(image->read_bytes(0x5000, untouched.data(), untouched.size(), error)) << error;
    EXPECT_EQ(untouched, (std::array<uint8_t, 4>{0x52, 0x52, 0x52, 0x52}));
}

TEST_F(MemoryImageTests, RejectsSpanCountThatCannotFitInSection) {
    BufferWriter writer;
    writer.u32(kMemoryImageFormatVersion);
    writer.u32(kMemoryImagePageSize);
    writer.u32(1);
    EXPECT_EQ(preflight(writer.data()), nullptr);
}

TEST_F(MemoryImageTests, RequiresExactChunkCoverageWithoutGapsOrExtraRecords) {
    constexpr uint32_t page = kMemoryImagePageSize;
    const auto data = bytes(page, 0x72);

    BufferWriter missing;
    write_header(missing, page, {{0x1000, 1}});
    EXPECT_EQ(preflight(missing.data()), nullptr);

    BufferWriter gap;
    write_header(gap, page, {{0x1000, 2}});
    write_raw_chunk(gap, 0x2000, data);
    EXPECT_EQ(preflight(gap.data()), nullptr);

    BufferWriter wrong_size;
    write_header(wrong_size, page, {{0x1000, 1}});
    write_raw_chunk(wrong_size, 0x1000, bytes(page - 1, 0x72));
    EXPECT_EQ(preflight(wrong_size.data()), nullptr);

    BufferWriter extra;
    write_header(extra, page, {{0x1000, 1}});
    write_raw_chunk(extra, 0x1000, data);
    extra.u8(0xAA);
    EXPECT_EQ(preflight(extra.data()), nullptr);
}

TEST_F(MemoryImageTests, ValidatesEveryNonzeroChunkChecksumEvenWhenStoredChecksumIsZero) {
    BufferWriter writer;
    write_header(writer, kMemoryImagePageSize, {{0x1000, 1}});
    write_raw_chunk(writer, 0x1000, bytes(kMemoryImagePageSize, 0x55), 0);
    EXPECT_EQ(preflight(writer.data()), nullptr);
}

TEST_F(MemoryImageTests, RequiresCanonicalZeroRecordsAndValidDeflateBounds) {
    constexpr uint32_t page = kMemoryImagePageSize;
    for (const auto [stored, checksum] : {std::pair<uint32_t, uint32_t>{1, 0}, {0, 1}}) {
        BufferWriter zeros;
        write_header(zeros, page, {{0x1000, 1}});
        write_record(zeros, 0x1000, page, MemoryChunkEncoding::Zeros, stored, checksum);
        EXPECT_EQ(preflight(zeros.data()), nullptr);
    }

    BufferWriter oversized;
    write_header(oversized, page, {{0x1000, 1}});
    write_record(oversized, 0x1000, page, MemoryChunkEncoding::Deflated, page + 65537, 1);
    EXPECT_EQ(preflight(oversized.data()), nullptr);
}

TEST_F(MemoryImageTests, RemovesPartialStagingFileAfterLatePreflightFailure) {
    constexpr uint32_t page = kMemoryImagePageSize;
    BufferWriter writer;
    write_header(writer, page, {{0x1000, 2}});
    write_raw_chunk(writer, 0x1000, bytes(page, 0x22));
    write_raw_chunk(writer, 0x2000, bytes(page, 0x33), 0);

    EXPECT_EQ(preflight(writer.data()), nullptr);
    EXPECT_EQ(staged_file_count(), 0u);
}

TEST_F(MemoryImageTests, RejectsTruncatedRawAndInvalidDeflatePayloads) {
    constexpr uint32_t page = kMemoryImagePageSize;
    BufferWriter truncated;
    write_header(truncated, page, {{0x1000, 1}});
    write_record(truncated, 0x1000, page, MemoryChunkEncoding::Raw, page, 1);
    EXPECT_EQ(preflight(truncated.data()), nullptr);

    BufferWriter invalid_deflate;
    write_header(invalid_deflate, page, {{0x1000, 1}});
    const std::vector<uint8_t> invalid(8, 0xFF);
    write_record(invalid_deflate, 0x1000, page, MemoryChunkEncoding::Deflated,
        static_cast<uint32_t>(invalid.size()), 1);
    invalid_deflate.append(invalid.data(), invalid.size());
    EXPECT_EQ(preflight(invalid_deflate.data()), nullptr);
}

TEST_F(MemoryImageTests, DisplayValidationChecksCompleteFramebufferExtentAndFiniteGeometry) {
    BufferWriter writer;
    write_header(writer, kMemoryImagePageSize, {{0x1000, 1}});
    write_record(writer, 0x1000, kMemoryImagePageSize, MemoryChunkEncoding::Zeros, 0, 0);
    auto image = preflight(writer.data());
    ASSERT_NE(image, nullptr) << error;
    DisplaySnapshot snapshot;
    snapshot.sce_frame.base = Ptr<const void>(0x1FFC);
    snapshot.sce_frame.image_size = {1, 1};
    snapshot.sce_frame.pitch = 1;
    EXPECT_TRUE(validate_display_state(snapshot, *image, error));
    snapshot.sce_frame.image_size.y = 2;
    EXPECT_FALSE(validate_display_state(snapshot, *image, error));
    snapshot.sce_frame.image_size.y = 1;
    snapshot.viewport_x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(validate_display_state(snapshot, *image, error));
    snapshot.viewport_x = 0;
    snapshot.sce_frame.pixelformat = 1;
    EXPECT_FALSE(validate_display_state(snapshot, *image, error));
    snapshot.sce_frame.pixelformat = 0;
    snapshot.sce_frame.image_size = {2, 2};
    snapshot.sce_frame.pitch = UINT32_MAX;
    EXPECT_FALSE(validate_display_state(snapshot, *image, error));
}

TEST_F(MemoryImageTests, DisplayParserRejectsCountBombsDuplicateCallbacksAndTrailingBytes) {
    const auto prefix = [] {
        BufferWriter writer;
        writer.i32(1600); writer.i32(2560);
        for (int i = 0; i < 4; ++i) writer.value(0.0f);
        writer.value(DisplayFrameInfo{}); writer.value(DisplayFrameInfo{});
        writer.u64(500); writer.u64(490);
        writer.boolean(false);
        return writer;
    };
    DisplaySnapshot snapshot;
    auto valid = prefix(); valid.u32(0);
    ASSERT_TRUE(parse_display_state(valid.data(), snapshot, error)) << error;
    EXPECT_EQ(snapshot.vblank_count, 500u);
    valid.u8(0xAA);
    EXPECT_FALSE(parse_display_state(valid.data(), snapshot, error));
    auto bomb = prefix(); bomb.u32(UINT32_MAX);
    EXPECT_FALSE(parse_display_state(bomb.data(), snapshot, error));
    auto duplicate = prefix(); duplicate.u32(2); duplicate.i32(1); duplicate.i32(1);
    EXPECT_FALSE(parse_display_state(duplicate.data(), snapshot, error));
    EXPECT_EQ(snapshot.vblank_count, 500u);
}

} // namespace
