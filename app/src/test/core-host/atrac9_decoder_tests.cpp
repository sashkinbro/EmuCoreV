// B152: invalid-input paths of the ATRAC9 decoder wrapper and the guest input validation of the
// NGS ATRAC9 voice. The decoder must reject bad config words and corrupt frames with an error and
// recover; it must never dereference a missing Huffman codebook.

#include <gtest/gtest.h>

#include <codec/state.h>
#include <ngs/modules/atrac9_input.h>

extern "C" {
#include <huffCodes.h>
#include <structures.h>
}

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

// Base class members (codec/src/decoder.cpp needs FFmpeg); the ATRAC9 decoder overrides what it uses.
uint32_t DecoderState::get(DecoderQuery) { return 0; }
uint32_t DecoderState::get_es_size() { return 0; }
void DecoderState::flush() {}
DecoderState::~DecoderState() = default;

namespace {

// ATRAC9 config word (4 bytes in stream order): header 0xFE, sample rate index (4), channel config
// index (3), validation bit (1), frame bytes - 1 (11), superframe index (2). Returned as the
// little-endian guest word.
uint32_t make_config(uint32_t rate_index, uint32_t channel_config, uint32_t frame_bytes, uint32_t superframe_index, uint32_t validation = 0, uint32_t header = 0xFE) {
    const uint32_t b0 = header;
    const uint32_t b1 = (rate_index << 4) | (channel_config << 1) | validation;
    const uint32_t frame = frame_bytes - 1;
    const uint32_t b2 = (frame >> 3) & 0xFF;
    const uint32_t b3 = ((frame & 7) << 5) | (superframe_index << 3);
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

// The value NFS Most Wanted uses (stereo, 48 kHz, 80 byte frames, one frame per superframe).
constexpr uint32_t NFS_CONFIG = 0xe00974fe;

// padded: the bit reader looks a few bytes ahead of the read position
std::vector<uint8_t> padded(std::initializer_list<uint8_t> head, size_t size = 4096) {
    std::vector<uint8_t> v(size, 0);
    std::copy(head.begin(), head.end(), v.begin());
    return v;
}

} // namespace

TEST(Atrac9Config, HelperMatchesRealConfig) {
    EXPECT_EQ(make_config(7, 2, 80, 0), NFS_CONFIG);
}

TEST(Atrac9Decoder, RealConfigIsValid) {
    Atrac9DecoderState dec(NFS_CONFIG);
    EXPECT_TRUE(dec.valid);
    EXPECT_EQ(dec.get(DecoderQuery::AT9_SUPERFRAME_SIZE), 80u);
    EXPECT_EQ(dec.get(DecoderQuery::CHANNELS), 2u);
}

TEST(Atrac9Decoder, BadHeaderIsRejected) {
    Atrac9DecoderState dec(make_config(7, 2, 80, 0, 0, 0x12));
    EXPECT_FALSE(dec.valid);
    EXPECT_EQ(dec.get(DecoderQuery::AT9_SUPERFRAME_SIZE), 0u);
    const auto data = padded({ 0 });
    EXPECT_FALSE(dec.send(data.data(), 0));
}

TEST(Atrac9Decoder, ValidationBitIsRejected) {
    Atrac9DecoderState dec(make_config(7, 2, 80, 0, 1));
    EXPECT_FALSE(dec.valid);
}

TEST(Atrac9Decoder, ChannelConfigsThatDoNotExistAreRejected) {
    // the channel config table has six entries; 6 and 7 used to index past its end
    for (uint32_t cfg = 6; cfg < 8; cfg++) {
        Atrac9DecoderState dec(make_config(7, cfg, 80, 0));
        EXPECT_FALSE(dec.valid) << "channel config " << cfg;
        const auto data = padded({ 0xC0 });
        EXPECT_FALSE(dec.send(data.data(), 0));
    }
}

TEST(Atrac9Decoder, AllChannelConfigsAndRatesInitOrFailCleanly) {
    for (uint32_t rate = 0; rate < 16; rate++)
        for (uint32_t cfg = 0; cfg < 8; cfg++)
            for (uint32_t sf = 0; sf < 4; sf++) {
                Atrac9DecoderState dec(make_config(rate, cfg, 100, sf));
                const auto data = padded({ 0x00 });
                dec.send(data.data(), 0); // must not crash, the result is irrelevant
                EXPECT_EQ(dec.valid, cfg < 6);
            }
}

TEST(Atrac9Decoder, NullInputIsRejected) {
    Atrac9DecoderState dec(NFS_CONFIG);
    EXPECT_FALSE(dec.send(nullptr, 0));
    EXPECT_TRUE(dec.valid);
}

// Regression for the B152 crash. A frame that fails to unpack leaves the library's block parameters
// half updated. The next frame, which reuses them, asks for a spectrum code book that does not
// exist (precision 1) and ReadHuffmanValue dereferenced a null table.
TEST(Atrac9Decoder, StaleBlockParametersDoNotReachMissingCodebook) {
    // the slot the stale parameters select really is empty
    ASSERT_EQ(HuffmanSpectrum[0][1][0].Lookup, nullptr);

    Atrac9DecoderState dec(NFS_CONFIG);
    ASSERT_TRUE(dec.valid);
    Atrac9Handle *handle = static_cast<Atrac9Handle *>(dec.decoder_handle);
    Block &block = handle->Frame.Blocks[0];
    block.QuantizationUnitCount = 4;
    block.StereoQuantizationUnit = 20; // more units coded in the second channel than have precisions
    block.ExtensionUnit = 4;
    block.BandCount = 3;
    block.StereoBand = 3;

    // not first in superframe, reuse band parameters, everything else zero
    const auto data = padded({ 0xC0 });
    EXPECT_FALSE(dec.send(data.data(), 0));

    // the failure discarded the half updated state
    EXPECT_EQ(dec.superframe_frame_idx, 0);
    EXPECT_EQ(dec.superframe_data_left, 80);
    const Block &fresh = static_cast<Atrac9Handle *>(dec.decoder_handle)->Frame.Blocks[0];
    EXPECT_EQ(fresh.QuantizationUnitCount, 0);
    EXPECT_EQ(fresh.StereoQuantizationUnit, 0);
}

TEST(Atrac9Decoder, DecoderRecoversAfterFailure) {
    Atrac9DecoderState dec(NFS_CONFIG);
    ASSERT_TRUE(dec.valid);
    // reusing band parameters in the first frame of a superframe is invalid
    const auto bad = padded({ 0x40 });
    EXPECT_FALSE(dec.send(bad.data(), 0));
    EXPECT_TRUE(dec.valid);
    EXPECT_EQ(dec.superframe_frame_idx, 0);
    EXPECT_EQ(dec.superframe_data_left, 80);
    const auto silent = padded({ 0x00 });
    for (int i = 0; i < 4; i++)
        dec.send(silent.data(), 0); // the result depends on the bits, only the state machine matters
    EXPECT_TRUE(dec.valid);
}

TEST(Atrac9Decoder, RandomInputNeverCrashes) {
    uint64_t rng = 0x9E3779B97F4A7C15ull;
    auto next = [&rng]() {
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        return rng;
    };
    const uint32_t configs[] = {
        NFS_CONFIG, make_config(7, 0, 100, 1), make_config(7, 1, 120, 2), make_config(9, 2, 200, 0),
        make_config(8, 3, 300, 1), make_config(7, 4, 400, 2), make_config(13, 5, 150, 0), make_config(3, 2, 64, 3)
    };
    for (const uint32_t config : configs) {
        Atrac9DecoderState dec(config);
        ASSERT_TRUE(dec.valid) << std::hex << config;
        std::vector<uint8_t> data(8192);
        for (int iter = 0; iter < 400; iter++) {
            for (auto &b : data)
                b = static_cast<uint8_t>(next());
            // every other input starts like a plausible block header
            if (iter & 1)
                data[0] &= 0x3F;
            dec.send(data.data(), 0);
            ASSERT_TRUE(dec.valid);
            ASSERT_GE(dec.superframe_data_left, 0);
            ASSERT_LE(dec.superframe_frame_idx, static_cast<int>(dec.get(DecoderQuery::AT9_FRAMES_IN_SUPERFRAME)));
        }
    }
}

// ---- NGS voice input validation --------------------------------------------------------------

TEST(NgsAt9Input, BufferChainIndexOutsideTheTableEndsTheStream) {
    EXPECT_EQ(ngs::at9_valid_buffer_index(0, 4), 0);
    EXPECT_EQ(ngs::at9_valid_buffer_index(3, 4), 3);
    EXPECT_EQ(ngs::at9_valid_buffer_index(4, 4), -1);
    EXPECT_EQ(ngs::at9_valid_buffer_index(-1, 4), -1);
    EXPECT_EQ(ngs::at9_valid_buffer_index(-300, 4), -1);
    EXPECT_EQ(ngs::at9_valid_buffer_index(0x7FFFFFFF, 4), -1);
}

namespace {
void put32(std::vector<uint8_t> &v, int32_t x) {
    for (int i = 0; i < 4; i++)
        v.push_back(static_cast<uint8_t>(static_cast<uint32_t>(x) >> (8 * i)));
}
void put_tag(std::vector<uint8_t> &v, const char *tag) { v.insert(v.end(), tag, tag + 4); }
} // namespace

TEST(NgsAt9Input, RiffHeaderIsSkippedToTheDataChunk) {
    std::vector<uint8_t> v;
    put_tag(v, "RIFF");
    put32(v, 100);
    put_tag(v, "WAVE");
    put_tag(v, "fmt ");
    put32(v, 6);
    v.insert(v.end(), 6, 0xAA);
    put_tag(v, "data");
    put32(v, 40);
    v.insert(v.end(), 40, 0x55);
    EXPECT_EQ(ngs::at9_riff_data_offset(v.data(), static_cast<uint32_t>(v.size())), 12 + 8 + 6 + 8);
}

TEST(NgsAt9Input, NotRiffIsNotAHeader) {
    const std::vector<uint8_t> v(64, 0x11);
    EXPECT_EQ(ngs::at9_riff_data_offset(v.data(), static_cast<uint32_t>(v.size())), -1);
    EXPECT_EQ(ngs::at9_riff_data_offset(v.data(), 4), -1);
}

TEST(NgsAt9Input, MalformedRiffNeverReadsPastTheBuffer) {
    auto header = [](std::vector<uint8_t> &v) {
        v.clear();
        put_tag(v, "RIFF");
        put32(v, 0);
        put_tag(v, "WAVE");
    };
    std::vector<uint8_t> v;

    // negative chunk size: used to move the read pointer backwards
    header(v);
    put_tag(v, "junk");
    put32(v, -5000);
    v.resize(64, 0);
    EXPECT_EQ(ngs::at9_riff_data_offset(v.data(), static_cast<uint32_t>(v.size())), -1);

    // chunk running past the end
    header(v);
    put_tag(v, "junk");
    put32(v, 0x10000);
    v.resize(64, 0);
    EXPECT_EQ(ngs::at9_riff_data_offset(v.data(), static_cast<uint32_t>(v.size())), -1);

    // no data chunk at all: the walk stops at the end of the buffer
    header(v);
    for (int i = 0; i < 4; i++) {
        put_tag(v, "skip");
        put32(v, 4);
        put32(v, 0);
    }
    EXPECT_EQ(ngs::at9_riff_data_offset(v.data(), static_cast<uint32_t>(v.size())), -1);

    // header cut in the middle of a chunk header
    EXPECT_EQ(ngs::at9_riff_data_offset(v.data(), 16), -1);

    // chunk size that would overflow a 32 bit sum
    header(v);
    put_tag(v, "junk");
    put32(v, 0x7FFFFFFF);
    v.resize(64, 0);
    EXPECT_EQ(ngs::at9_riff_data_offset(v.data(), static_cast<uint32_t>(v.size())), -1);
}
