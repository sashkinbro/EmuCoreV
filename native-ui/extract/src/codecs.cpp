// SHA-256, PNG, DDS, HE-VAG/WAV and lookup generators used by the on-device
// firmware asset extractor. Standalone: no emulator, SDL or guest CPU.

#include "internal.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace emucorev::extract {

// ---- SHA-256 ----------------------------------------------------------------
namespace {
constexpr std::uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
inline std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
}  // namespace

Sha256::Sha256() {
    const std::uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::copy(init, init + 8, h_);
    std::memset(buffer_, 0, sizeof(buffer_));
}

void Sha256::block(const std::uint8_t* p) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = std::uint32_t(p[i * 4]) << 24 | std::uint32_t(p[i * 4 + 1]) << 16 |
            std::uint32_t(p[i * 4 + 2]) << 8 | std::uint32_t(p[i * 4 + 3]);
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
    for (int i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + s1 + ch + kSha256K[i] + w[i];
        const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
}

void Sha256::update(std::span<const std::uint8_t> data) {
    total_ += data.size();
    std::size_t offset = 0;
    if (used_) {
        const std::size_t take = std::min<std::size_t>(64 - used_, data.size());
        std::memcpy(buffer_ + used_, data.data(), take);
        used_ += take;
        offset = take;
        if (used_ == 64) { block(buffer_); used_ = 0; }
    }
    for (; offset + 64 <= data.size(); offset += 64) block(data.data() + offset);
    if (offset < data.size()) {
        used_ = data.size() - offset;
        std::memcpy(buffer_, data.data() + offset, used_);
    }
}

std::string Sha256::hex_digest() {
    const std::uint64_t bits = total_ * 8;
    const std::uint8_t one = 0x80;
    update({&one, 1});
    const std::uint8_t zero = 0;
    while (used_ != 56) update({&zero, 1});
    std::uint8_t length[8];
    for (int i = 0; i < 8; ++i) length[i] = std::uint8_t(bits >> (56 - 8 * i));
    update(length);
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 8; ++i)
        for (int s = 28; s >= 0; s -= 4) out.push_back(digits[(h_[i] >> s) & 15]);
    return out;
}

std::string sha256_hex(std::span<const std::uint8_t> data) {
    Sha256 hash;
    hash.update(data);
    return hash.hex_digest();
}

// ---- PNG --------------------------------------------------------------------
namespace {
void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(std::uint8_t(v >> s));
}
void chunk(std::vector<std::uint8_t>& out, const char type[4], std::span<const std::uint8_t> data) {
    put32(out, std::uint32_t(data.size()));
    const std::size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    const uLong crc = crc32(crc32(0, nullptr, 0), out.data() + start, uInt(out.size() - start));
    put32(out, std::uint32_t(crc));
}
inline int paeth(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}
}  // namespace

std::vector<std::uint8_t> encode_png(std::uint32_t width, std::uint32_t height, PngKind kind,
    std::span<const std::uint8_t> pixels) {
    const std::size_t bpp = kind == PngKind::rgba8 ? 4 : 1;
    const std::size_t stride = std::size_t(width) * bpp;
    if (!width || !height || pixels.size() != stride * height) throw std::runtime_error("Invalid PNG image size");
    // Per-row adaptive filter chosen by the minimum sum of absolute values.
    std::vector<std::uint8_t> filtered;
    filtered.reserve((stride + 1) * height);
    std::vector<std::uint8_t> candidate[5];
    for (auto& row : candidate) row.resize(stride);
    const std::vector<std::uint8_t> zero_row(stride, 0);
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* row = pixels.data() + y * stride;
        const std::uint8_t* up = y ? row - stride : zero_row.data();
        for (std::size_t x = 0; x < stride; ++x) {
            const int a = x >= bpp ? row[x - bpp] : 0, b = up[x], c = x >= bpp ? up[x - bpp] : 0;
            candidate[0][x] = row[x];
            candidate[1][x] = std::uint8_t(row[x] - a);
            candidate[2][x] = std::uint8_t(row[x] - b);
            candidate[3][x] = std::uint8_t(row[x] - ((a + b) >> 1));
            candidate[4][x] = std::uint8_t(row[x] - paeth(a, b, c));
        }
        int best = 0;
        std::uint64_t best_sum = ~0ull;
        for (int f = 0; f < 5; ++f) {
            std::uint64_t sum = 0;
            for (std::size_t x = 0; x < stride; ++x) {
                const int v = candidate[f][x];
                sum += std::uint64_t(v < 128 ? v : 256 - v);
            }
            if (sum < best_sum) { best_sum = sum; best = f; }
        }
        filtered.push_back(std::uint8_t(best));
        filtered.insert(filtered.end(), candidate[best].begin(), candidate[best].end());
    }
    uLongf compressed_size = compressBound(uLong(filtered.size()));
    std::vector<std::uint8_t> compressed(compressed_size);
    if (compress2(compressed.data(), &compressed_size, filtered.data(), uLong(filtered.size()), 9) != Z_OK)
        throw std::runtime_error("PNG compression failed");
    compressed.resize(compressed_size);

    std::vector<std::uint8_t> png{137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<std::uint8_t> header;
    put32(header, width);
    put32(header, height);
    header.insert(header.end(), {8, std::uint8_t(kind == PngKind::rgba8 ? 6 : 0), 0, 0, 0});
    chunk(png, "IHDR", header);
    chunk(png, "IDAT", compressed);
    chunk(png, "IEND", {});
    return png;
}

bool png_size(std::span<const std::uint8_t> png, std::uint32_t& width, std::uint32_t& height) {
    static const std::uint8_t magic[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (png.size() < 33 || std::memcmp(png.data(), magic, 8) != 0 || std::memcmp(png.data() + 12, "IHDR", 4) != 0)
        return false;
    auto be32 = [&](std::size_t o) {
        return std::uint32_t(png[o]) << 24 | std::uint32_t(png[o + 1]) << 16 | std::uint32_t(png[o + 2]) << 8 | png[o + 3];
    };
    width = be32(16);
    height = be32(20);
    return width && height;
}

bool decode_png_to_rgba(std::span<const std::uint8_t> png, DecodedPng& out) {
    std::uint32_t width = 0, height = 0;
    if (!png_size(png, width, height) || width > 16384 || height > 16384) return false;
    const unsigned depth = png[24], color = png[25], interlace = png[28];
    if (interlace != 0 || png[26] != 0 || png[27] != 0) return false;
    unsigned channels;
    switch (color) {
        case 0: channels = 1; if (depth != 8) return false; break;
        case 2: channels = 3; if (depth != 8) return false; break;
        case 3: channels = 1; if (depth != 1 && depth != 2 && depth != 4 && depth != 8) return false; break;
        case 4: channels = 2; if (depth != 8) return false; break;
        case 6: channels = 4; if (depth != 8) return false; break;
        default: return false;
    }
    std::vector<std::uint8_t> idat, palette, trns;
    for (std::size_t pos = 8; pos + 12 <= png.size();) {
        const std::uint32_t length = std::uint32_t(png[pos]) << 24 | std::uint32_t(png[pos + 1]) << 16 |
            std::uint32_t(png[pos + 2]) << 8 | png[pos + 3];
        if (std::size_t(length) > png.size() - pos - 12) return false;
        const char* type = reinterpret_cast<const char*>(png.data() + pos + 4);
        const std::uint8_t* data = png.data() + pos + 8;
        if (!std::memcmp(type, "IDAT", 4)) idat.insert(idat.end(), data, data + length);
        else if (!std::memcmp(type, "PLTE", 4)) palette.assign(data, data + length);
        else if (!std::memcmp(type, "tRNS", 4)) trns.assign(data, data + length);
        else if (!std::memcmp(type, "IEND", 4)) break;
        pos += 12 + std::size_t(length);
    }
    if (color == 3 && (palette.empty() || palette.size() % 3 != 0)) return false;
    if ((color == 0 || color == 2) && !trns.empty()) return false;  // colour-key transparency: keep original
    const std::size_t bits_per_pixel = std::size_t(channels) * depth;
    const std::size_t stride = (std::size_t(width) * bits_per_pixel + 7) / 8;
    const std::size_t bpp = std::max<std::size_t>(1, bits_per_pixel / 8);
    std::vector<std::uint8_t> raw((stride + 1) * height);
    uLongf raw_size = uLongf(raw.size());
    if (uncompress(raw.data(), &raw_size, idat.data(), uLong(idat.size())) != Z_OK || raw_size != raw.size())
        return false;
    std::vector<std::uint8_t> rows(stride * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t filter = raw[y * (stride + 1)];
        std::uint8_t* row = rows.data() + y * stride;
        const std::uint8_t* src = raw.data() + y * (stride + 1) + 1;
        const std::uint8_t* up = y ? row - stride : nullptr;
        for (std::size_t x = 0; x < stride; ++x) {
            const int a = x >= bpp ? row[x - bpp] : 0, b = up ? up[x] : 0, c = (up && x >= bpp) ? up[x - bpp] : 0;
            int predictor;
            switch (filter) {
                case 0: predictor = 0; break;
                case 1: predictor = a; break;
                case 2: predictor = b; break;
                case 3: predictor = (a + b) >> 1; break;
                case 4: predictor = paeth(a, b, c); break;
                default: return false;
            }
            row[x] = std::uint8_t(src[x] + predictor);
        }
    }
    out.width = width;
    out.height = height;
    out.rgba.assign(std::size_t(width) * height * 4, 255);
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* row = rows.data() + y * stride;
        std::uint8_t* dst = out.rgba.data() + std::size_t(y) * width * 4;
        for (std::uint32_t x = 0; x < width; ++x, dst += 4) {
            switch (color) {
                case 0: dst[0] = dst[1] = dst[2] = row[x]; break;
                case 2: dst[0] = row[x * 3]; dst[1] = row[x * 3 + 1]; dst[2] = row[x * 3 + 2]; break;
                case 4: dst[0] = dst[1] = dst[2] = row[x * 2]; dst[3] = row[x * 2 + 1]; break;
                case 6: std::memcpy(dst, row + x * 4, 4); break;
                default: {
                    std::size_t index;
                    if (depth == 8) index = row[x];
                    else {
                        const std::size_t bit = std::size_t(x) * depth;
                        index = (row[bit / 8] >> (8 - depth - bit % 8)) & ((1u << depth) - 1);
                    }
                    if (index * 3 + 2 >= palette.size()) return false;
                    dst[0] = palette[index * 3]; dst[1] = palette[index * 3 + 1]; dst[2] = palette[index * 3 + 2];
                    dst[3] = index < trns.size() ? trns[index] : 255;
                }
            }
        }
    }
    return true;
}

// ---- DDS --------------------------------------------------------------------
std::vector<DdsMip> decode_dds(std::span<const std::uint8_t> data, bool all_mips) {
    auto le32 = [&](std::size_t o) {
        return std::uint32_t(data[o]) | std::uint32_t(data[o + 1]) << 8 | std::uint32_t(data[o + 2]) << 16 |
            std::uint32_t(data[o + 3]) << 24;
    };
    if (data.size() < 128 || std::memcmp(data.data(), "DDS ", 4) != 0) throw std::runtime_error("Incomplete DDS header");
    const std::uint32_t size = le32(4), flags = le32(8);
    std::uint32_t height = le32(12), width = le32(16), pitch = le32(20), depth = le32(24), levels = le32(28);
    std::uint32_t pixel[8];
    for (int i = 0; i < 8; ++i) pixel[i] = le32(76 + i * 4);
    const std::uint32_t caps2 = le32(112), caps3 = le32(116);
    if (size != 124 || !width || !height || width > 16384 || height > 16384 || depth || caps2 || caps3)
        throw std::runtime_error("Unsupported DDS dimensions or surface kind");
    bool alpha8;
    std::size_t bytes_per_pixel;
    static const std::uint32_t bgra[8] = {32, 65, 0, 32, 0xff0000, 0xff00, 0xff, 0xff000000};
    static const std::uint32_t a8[8] = {32, 2, 0, 8, 0, 0, 0, 255};
    if (std::equal(pixel, pixel + 8, bgra)) { alpha8 = false; bytes_per_pixel = 4; }
    else if (std::equal(pixel, pixel + 8, a8)) { alpha8 = true; bytes_per_pixel = 1; }
    else throw std::runtime_error("Unsupported DDS pixel format");
    levels = std::max<std::uint32_t>(1, levels);
    std::uint32_t max_levels = 0;
    for (std::uint32_t v = std::max(width, height); v; v >>= 1) ++max_levels;
    if (levels > max_levels) throw std::runtime_error("DDS has too many mip levels");
    if ((flags & 8) && pitch != width * bytes_per_pixel) throw std::runtime_error("DDS row padding is not supported");
    std::vector<DdsMip> result;
    std::size_t offset = 128;
    for (std::uint32_t level = 0; level < levels; ++level) {
        const std::size_t count = std::size_t(width) * height * bytes_per_pixel;
        if (offset + count > data.size()) throw std::runtime_error("Truncated DDS mip level");
        DdsMip mip{width, height, alpha8, {}};
        mip.pixels.assign(data.begin() + std::ptrdiff_t(offset), data.begin() + std::ptrdiff_t(offset + count));
        if (!alpha8)
            for (std::size_t i = 0; i < count; i += 4) std::swap(mip.pixels[i], mip.pixels[i + 2]);
        result.push_back(std::move(mip));
        offset += count;
        width = std::max<std::uint32_t>(1, width / 2);
        height = std::max<std::uint32_t>(1, height / 2);
        if (!all_mips) return result;
    }
    if (offset != data.size()) throw std::runtime_error("Unconsumed DDS payload");
    return result;
}

// ---- HE-VAG -----------------------------------------------------------------
namespace {
// Sony HE-VAG predictor table (128 rows, 14-bit fixed point), as published by
// vgmstream and Vita3K.
constexpr std::int16_t kHeVagCoefficients[128][4] = {
#include "hevag_table.inc"
};
constexpr int kNibble[16] = {0, 1, 2, 3, 4, 5, 6, 7, -8, -7, -6, -5, -4, -3, -2, -1};
}  // namespace

std::vector<std::int16_t> decode_hevag(std::span<const std::uint8_t> vag, VagInfo& info) {
    if (vag.size() < 0x30 || std::memcmp(vag.data(), "VAGp", 4) != 0) throw std::runtime_error("Not a VAGp sound");
    auto be32 = [&](std::size_t o) {
        return std::uint32_t(vag[o]) << 24 | std::uint32_t(vag[o + 1]) << 16 | std::uint32_t(vag[o + 2]) << 8 | vag[o + 3];
    };
    const std::uint32_t size = be32(12);
    info.sample_rate = be32(16);
    if (!info.sample_rate || size == 0 || size % 16 != 0 || std::size_t(size) > vag.size() - 0x30)
        throw std::runtime_error("Invalid VAG size or rate");
    const std::uint8_t* body = vag.data() + 0x30;
    const std::size_t frames = size / 16;
    std::vector<std::int16_t> out;
    out.reserve(frames * 28);
    std::int32_t history[4] = {0, 0, 0, 0};
    for (std::size_t i = 0; i < frames; ++i) {
        const std::uint8_t* frame = body + i * 16;
        unsigned index = (frame[0] >> 4) | (frame[1] & 0xf0u);
        const int shift = 20 - int(frame[0] & 0xf);
        const unsigned flag = frame[1] & 0xf;
        if (index > 127) index = 0;
        float coefficients[4];
        for (int t = 0; t < 4; ++t) coefficients[t] = float(kHeVagCoefficients[index][t]) / 8192.0f;
        for (int j = 0; j < 28; ++j) {
            std::int32_t sample = 0;
            if (flag < 7) {
                const std::uint8_t pair = frame[2 + j / 2];
                const int nibble = kNibble[(j & 1) ? (pair >> 4) : (pair & 0xf)];
                // Float32 accumulation, left to right (-ffp-contract=off): the original
                // decoder's prediction truncates toward zero after a float sum.
                float prediction = 0.0f;
                for (int t = 0; t < 4; ++t) {
                    const float term = float(history[t]) * coefficients[t];
                    prediction = prediction + term;
                }
                sample = ((nibble * (1 << shift)) >> 8) + static_cast<std::int32_t>(prediction);
            }
            out.push_back(static_cast<std::int16_t>(std::clamp(sample, -32768, 32767)));
            history[3] = history[2]; history[2] = history[1]; history[1] = history[0]; history[0] = sample;
        }
    }
    info.samples = std::uint32_t(out.size());
    return out;
}

std::vector<std::uint8_t> make_wav(std::uint32_t rate, std::uint32_t channels,
    std::span<const std::int16_t> interleaved) {
    const std::uint32_t data_bytes = std::uint32_t(interleaved.size() * 2);
    std::vector<std::uint8_t> wav;
    wav.reserve(44 + data_bytes);
    auto le32 = [&](std::uint32_t v) { for (int s = 0; s < 32; s += 8) wav.push_back(std::uint8_t(v >> s)); };
    auto le16 = [&](std::uint32_t v) { wav.push_back(std::uint8_t(v)); wav.push_back(std::uint8_t(v >> 8)); };
    const auto tag = [&](const char* t) { wav.insert(wav.end(), t, t + 4); };
    tag("RIFF"); le32(36 + data_bytes); tag("WAVE");
    tag("fmt "); le32(16); le16(1); le16(channels); le32(rate); le32(rate * channels * 2); le16(channels * 2); le16(16);
    tag("data"); le32(data_bytes);
    for (const std::int16_t s : interleaved) le16(std::uint16_t(s));
    return wav;
}

// ---- Lookups ------------------------------------------------------------------
// PROVISIONAL provenance: both generators are analytic fits, verified bit-exact
// against every pixel of the original PageTurn generator output (firmware 3.74),
// not decoded from the original instructions.
std::vector<std::uint8_t> exp_minus_lookup() {
    std::vector<std::uint8_t> pixels(128 * 8);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 128; ++x)
            pixels[std::size_t(y) * 128 + x] =
                std::uint8_t(static_cast<int>(255.0 * std::exp(-5.0 * x / 127.0)));
    return pixels;
}

std::vector<std::uint8_t> corner_distance_lookup() {
    std::vector<std::uint8_t> pixels(64 * 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const double v = 255.0 * std::sqrt(double(x * x + y * y)) / 63.0;
            pixels[std::size_t(y) * 64 + x] = std::uint8_t(std::min(255, static_cast<int>(v)));
        }
    return pixels;
}

}  // namespace emucorev::extract
