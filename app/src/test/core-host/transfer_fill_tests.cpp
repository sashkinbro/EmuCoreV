#include <gtest/gtest.h>

#include <renderer/transfer_fill.h>

#include <cstring>
#include <vector>

using namespace renderer;

TEST(TransferFill, PixelBytesRoundUp) {
    EXPECT_EQ(transfer_fill_pixel_bytes(8), 1u);
    EXPECT_EQ(transfer_fill_pixel_bytes(16), 2u);
    EXPECT_EQ(transfer_fill_pixel_bytes(24), 3u);
    EXPECT_EQ(transfer_fill_pixel_bytes(32), 4u);
    EXPECT_EQ(transfer_fill_pixel_bytes(64), 8u);
    EXPECT_EQ(transfer_fill_pixel_bytes(128), 16u);
    EXPECT_EQ(transfer_fill_pixel_bytes(4), 1u);
}

TEST(TransferFill, PatternForEveryFormatWidth) {
    const uint32_t color = 0xA1B2C3D4u; // little endian bytes: D4 C3 B2 A1
    for (const uint32_t bits : { 8u, 16u, 24u, 32u, 64u, 128u }) {
        const uint32_t bytes = transfer_fill_pixel_bytes(bits);
        uint8_t pixel[transfer_fill_max_pixel_bytes];
        std::memset(pixel, 0xEE, sizeof(pixel));
        ASSERT_EQ(build_transfer_fill_pixel(color, bytes, pixel), bytes) << bits;
        for (uint32_t i = 0; i < bytes; ++i) {
            const uint8_t expected = static_cast<uint8_t>(color >> (8 * (i % 4)));
            EXPECT_EQ(pixel[i], expected) << "bits " << bits << " byte " << i;
        }
        // nothing beyond the pixel is touched
        for (uint32_t i = bytes; i < transfer_fill_max_pixel_bytes; ++i)
            EXPECT_EQ(pixel[i], 0xEE) << "bits " << bits << " byte " << i;
    }
}

TEST(TransferFill, UnsupportedWidthWritesNothing) {
    uint8_t pixel[transfer_fill_max_pixel_bytes];
    std::memset(pixel, 0x11, sizeof(pixel));
    EXPECT_EQ(build_transfer_fill_pixel(0xFFFFFFFFu, 0, pixel), 0u);
    EXPECT_EQ(build_transfer_fill_pixel(0xFFFFFFFFu, 17, pixel), 0u);
    for (uint8_t b : pixel)
        EXPECT_EQ(b, 0x11);
}

TEST(TransferFill, RowFillMatchesPixelPattern) {
    uint8_t pixel[transfer_fill_max_pixel_bytes];
    ASSERT_EQ(build_transfer_fill_pixel(0x01020304u, 16, pixel), 16u);
    std::vector<uint8_t> row(5 * 16, 0);
    fill_transfer_row(row.data(), 5, pixel, 16);
    for (uint32_t x = 0; x < 5; ++x)
        EXPECT_EQ(0, std::memcmp(&row[x * 16], pixel, 16));
}

TEST(TransferFill, SpanForPositiveStride) {
    uint64_t first = 0, end = 0;
    ASSERT_TRUE(transfer_fill_span(0x1000, 2, 1, 4, 3, 64, 4, first, end));
    EXPECT_EQ(first, 0x1000u + 64 + 8);
    EXPECT_EQ(end, 0x1000u + 3 * 64 + 6 * 4);
}

TEST(TransferFill, SpanForNegativeStride) {
    uint64_t first = 0, end = 0;
    ASSERT_TRUE(transfer_fill_span(0x10000, 0, 2, 4, 3, -64, 4, first, end));
    // rows are at base-128, base-192, base-256
    EXPECT_EQ(first, 0x10000u - 256);
    EXPECT_EQ(end, 0x10000u - 128 + 16);
}

TEST(TransferFill, SpanRejectsOverflowAndEmpty) {
    uint64_t first = 0, end = 0;
    EXPECT_FALSE(transfer_fill_span(0x1000, 0, 0, 0, 4, 64, 4, first, end));
    EXPECT_FALSE(transfer_fill_span(0x1000, 0, 0, 4, 0, 64, 4, first, end));
    // below address 0
    EXPECT_FALSE(transfer_fill_span(0x100, 0, 4, 4, 2, -128, 4, first, end));
    // past the 32 bit guest address space
    EXPECT_FALSE(transfer_fill_span(0xFFFFF000u, 0, 0, 0x1000, 2, 0x1000, 16, first, end));
    EXPECT_FALSE(transfer_fill_span(0xFFFFFF00u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x7FFFFFFF, 16, first, end));
}
