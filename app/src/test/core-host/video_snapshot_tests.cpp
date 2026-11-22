// Host tests for the "video codecs" save-state section (Videodec/AVC decoders + MJpeg flag).
// The wire format is header-only and independent of FFmpeg, so it is exercised directly.
#include <video_snapshot.h>

#include <gtest/gtest.h>

using namespace video_snapshot;

namespace {
Snapshot sample() {
    Snapshot s;
    s.videodec_initialized = true;
    s.mjpeg_initialized = true;
    s.decoders.push_back({ 0x40001234, 1280, 720, true, false });
    s.decoders.push_back({ 0x40001240, 640, 368, false, true });
    s.decoders.push_back({ 0x7fffffff, 0, 0, false, true });
    // version 2 fields
    s.decoders[0].pixel_type = 0x00;
    s.decoders[0].decode_mode = 1;
    s.decoders[0].recovery_point_sei_mode = 2;
    s.decoders[0].interlaced_stream_mode = 3;
    s.decoders[1].codec = 1; // MPEG-4
    s.decoders[1].pixel_type = 0x10;
    return s;
}
std::vector<uint8_t> encode(const Snapshot &s) {
    std::vector<uint8_t> blob;
    std::string error;
    EXPECT_TRUE(serialize(s, blob, error)) << error;
    return blob;
}
} // namespace

TEST(VideoSnapshot, RoundTrip) {
    const Snapshot in = sample();
    const auto blob = encode(in);
    EXPECT_EQ(blob.size(), 16u + 3u * 36u);
    Snapshot out;
    std::string error;
    ASSERT_TRUE(parse(blob, out, error)) << error;
    EXPECT_EQ(in, out);
}

TEST(VideoSnapshot, EmptyStateRoundTrips) {
    // A game that never initialised the libraries: nothing must be created on restore.
    Snapshot in;
    Snapshot out;
    out.videodec_initialized = true;
    std::string error;
    ASSERT_TRUE(parse(encode(in), out, error)) << error;
    EXPECT_FALSE(out.videodec_initialized);
    EXPECT_FALSE(out.mjpeg_initialized);
    EXPECT_TRUE(out.decoders.empty());
}

TEST(VideoSnapshot, InitialisedLibraryWithoutDecoders) {
    Snapshot in;
    in.videodec_initialized = true;
    Snapshot out;
    std::string error;
    ASSERT_TRUE(parse(encode(in), out, error)) << error;
    EXPECT_EQ(in, out);
}

TEST(VideoSnapshot, EveryTruncationIsRejected) {
    const auto blob = encode(sample());
    for (size_t n = 0; n < blob.size(); ++n) {
        std::vector<uint8_t> cut(blob.begin(), blob.begin() + n);
        Snapshot out;
        std::string error;
        EXPECT_FALSE(parse(cut, out, error)) << "length " << n;
        EXPECT_FALSE(error.empty());
        EXPECT_EQ(out, Snapshot{});
    }
}

TEST(VideoSnapshot, TrailingBytesRejected) {
    auto blob = encode(sample());
    blob.push_back(0);
    Snapshot out;
    std::string error;
    EXPECT_FALSE(parse(blob, out, error));
}

TEST(VideoSnapshot, HeaderCorruptionRejected) {
    const auto good = encode(sample());
    Snapshot out;
    std::string error;
    for (size_t index : { size_t(0), size_t(4), size_t(8), size_t(9), size_t(10), size_t(11) }) {
        auto bad = good;
        bad[index] ^= 0x7f; // magic, version, bool flags, reserved bytes
        EXPECT_FALSE(parse(bad, out, error)) << "byte " << index;
    }
}

TEST(VideoSnapshot, InvalidRecordsRejected) {
    Snapshot out;
    std::string error;
    const auto base = sample();

    auto s = base;
    s.decoders[0].handle = 0;
    EXPECT_FALSE(parse(encode(s), out, error));

    s = base;
    s.decoders[0].handle = -5;
    EXPECT_FALSE(parse(encode(s), out, error));

    s = base;
    s.decoders[1].handle = s.decoders[0].handle; // duplicate UID
    EXPECT_FALSE(parse(encode(s), out, error));

    s = base;
    s.decoders[0].width = 1921; // above sceAvcdecCreateDecoder limit
    EXPECT_FALSE(parse(encode(s), out, error));

    s = base;
    s.decoders[0].width = 1920;
    s.decoders[0].height = 1088; // above the 1280x720 pixel budget
    EXPECT_FALSE(parse(encode(s), out, error));

    s = base;
    s.videodec_initialized = false; // decoders cannot exist without the library
    EXPECT_FALSE(parse(encode(s), out, error));
}

TEST(VideoSnapshot, DecoderCountBoundIsEnforcedWithoutAllocation) {
    auto blob = encode(Snapshot{});
    // Claim an absurd decoder count in an otherwise tiny blob.
    blob[12] = 0xff;
    blob[13] = 0xff;
    blob[14] = 0xff;
    blob[15] = 0x7f;
    Snapshot out;
    std::string error;
    EXPECT_FALSE(parse(blob, out, error));
}

TEST(VideoSnapshot, SerializeRejectsTooManyDecoders) {
    Snapshot s;
    s.videodec_initialized = true;
    for (uint32_t i = 0; i < kMaxDecoders + 1; ++i)
        s.decoders.push_back({ static_cast<int32_t>(i + 1), 16, 16, false, true });
    std::vector<uint8_t> blob;
    std::string error;
    EXPECT_FALSE(serialize(s, blob, error));
}

namespace {
// A blob exactly as the version 1 writer produced it.
std::vector<uint8_t> v1_blob() {
    std::vector<uint8_t> b = { 'V', 'C', 'O', 'D', 1, 0, 0, 0, 1, 1, 0, 0, 2, 0, 0, 0 };
    auto record = [&](uint32_t handle, uint32_t w, uint32_t h, uint8_t yuvp3, uint8_t stopped) {
        put_u32(b, handle);
        put_u32(b, w);
        put_u32(b, h);
        b.insert(b.end(), { yuvp3, stopped, 0, 0 });
    };
    record(0x40001234, 1280, 720, 1, 0);
    record(0x40001240, 640, 368, 0, 1);
    return b;
}
} // namespace

TEST(VideoSnapshot, Version1StillParses) {
    Snapshot out;
    std::string error;
    ASSERT_TRUE(parse(v1_blob(), out, error)) << error;
    ASSERT_EQ(out.decoders.size(), 2u);
    EXPECT_TRUE(out.videodec_initialized);
    EXPECT_TRUE(out.mjpeg_initialized);
    EXPECT_EQ(out.decoders[0].handle, 0x40001234);
    EXPECT_TRUE(out.decoders[0].output_yuvp3);
    EXPECT_EQ(out.decoders[0].pixel_type, 0x10u); // derived from the flag
    EXPECT_FALSE(out.decoders[0].stopped);
    EXPECT_EQ(out.decoders[1].pixel_type, 0x20u);
    EXPECT_TRUE(out.decoders[1].stopped);
    for (const auto &d : out.decoders) {
        EXPECT_EQ(d.codec, 0);
        EXPECT_EQ(d.decode_mode, 0u);
        EXPECT_EQ(d.recovery_point_sei_mode, 0u);
        EXPECT_EQ(d.interlaced_stream_mode, 0u);
    }
    // Re-serialising upgrades it to the current version.
    std::vector<uint8_t> upgraded;
    ASSERT_TRUE(serialize(out, upgraded, error));
    EXPECT_EQ(upgraded[4], 2);
    Snapshot again;
    ASSERT_TRUE(parse(upgraded, again, error)) << error;
    EXPECT_EQ(out, again);
}

TEST(VideoSnapshot, Version1TruncationAndTrailingRejected) {
    const auto good = v1_blob();
    Snapshot out;
    std::string error;
    for (size_t n = 0; n < good.size(); ++n) {
        std::vector<uint8_t> cut(good.begin(), good.begin() + n);
        EXPECT_FALSE(parse(cut, out, error)) << "length " << n;
    }
    auto extra = good;
    extra.push_back(0);
    EXPECT_FALSE(parse(extra, out, error));
}

TEST(VideoSnapshot, FutureAndZeroVersionsRejected) {
    Snapshot out;
    std::string error;
    auto blob = encode(sample());
    blob[4] = 3;
    EXPECT_FALSE(parse(blob, out, error));
    blob[4] = 0;
    EXPECT_FALSE(parse(blob, out, error));
}

TEST(VideoSnapshot, Version2FieldValidation) {
    Snapshot out;
    std::string error;
    auto s = sample();
    s.decoders[0].pixel_type = 0x03; // not an SceAvcdecPixelFormat
    EXPECT_FALSE(parse(encode(s), out, error));
    s = sample();
    s.decoders[0].codec = 2;
    EXPECT_FALSE(parse(encode(s), out, error));
    auto blob = encode(sample());
    blob[16 + 16 + 17] = 1; // reserved byte after codec of the first record
    EXPECT_FALSE(parse(blob, out, error));
}
