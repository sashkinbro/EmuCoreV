#pragma once
// Internal helpers of the on-device firmware asset extractor. Not a public API.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace emucorev::extract {

class Sha256 {
public:
    Sha256();
    void update(std::span<const std::uint8_t> data);
    std::string hex_digest();  // finalizes; the object must not be reused

private:
    void block(const std::uint8_t* p);
    std::uint32_t h_[8];
    std::uint8_t buffer_[64];
    std::uint64_t total_ = 0;
    std::size_t used_ = 0;
};

std::string sha256_hex(std::span<const std::uint8_t> data);

// ---- PNG ------------------------------------------------------------------
enum class PngKind { gray8, rgba8 };
std::vector<std::uint8_t> encode_png(std::uint32_t width, std::uint32_t height, PngKind kind,
    std::span<const std::uint8_t> pixels);

struct DecodedPng {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};
// Non-interlaced 1/2/4/8-bit palette and 8-bit gray/gray-alpha/RGB/RGBA only.
// Returns false for anything else (the caller keeps the original bytes).
bool decode_png_to_rgba(std::span<const std::uint8_t> png, DecodedPng& out);
// IHDR size of any PNG; false when the signature/header is invalid.
bool png_size(std::span<const std::uint8_t> png, std::uint32_t& width, std::uint32_t& height);

// ---- DDS ------------------------------------------------------------------
struct DdsMip {
    std::uint32_t width, height;
    bool alpha8;  // single original alpha byte plane; otherwise RGBA (from BGRA8)
    std::vector<std::uint8_t> pixels;
};
// Uncompressed BGRA8 / A8 surfaces with tightly packed rows (the shell's variants).
// Throws std::runtime_error for anything else.
std::vector<DdsMip> decode_dds(std::span<const std::uint8_t> dds, bool all_mips);

// ---- Audio ----------------------------------------------------------------
struct VagInfo {
    std::uint32_t sample_rate = 0;
    std::uint32_t samples = 0;
};
// Sony HE-VAG 4-bit ADPCM in a "VAGp" container, mono. Throws on malformed data.
std::vector<std::int16_t> decode_hevag(std::span<const std::uint8_t> vag, VagInfo& info);
// 44-byte-header PCM16 WAV; samples are interleaved for 1 or 2 channels.
std::vector<std::uint8_t> make_wav(std::uint32_t rate, std::uint32_t channels,
    std::span<const std::int16_t> interleaved);

// ---- Lookups --------------------------------------------------------------
std::vector<std::uint8_t> exp_minus_lookup();       // 128 x 8, scalar8
std::vector<std::uint8_t> corner_distance_lookup(); // 64 x 64, scalar8

}  // namespace emucorev::extract
