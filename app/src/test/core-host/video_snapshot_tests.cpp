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
    EXPECT_EQ(blob.size(), 16u + 3u * 16u);
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
