#include <emucorev/paf/gim_image.h>

#include <array>
#include <iostream>
#include <stdexcept>

using namespace emucorev::paf;
using Bytes = std::vector<std::uint8_t>;
namespace {
void half(Bytes &bytes, std::size_t offset, std::uint16_t value) {
    bytes.at(offset) = std::uint8_t(value); bytes.at(offset + 1) = std::uint8_t(value >> 8);
}
void word(Bytes &bytes, std::size_t offset, std::uint32_t value) {
    half(bytes, offset, std::uint16_t(value)); half(bytes, offset + 2, std::uint16_t(value >> 16));
}
void check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
std::array<std::uint8_t, 4> color(unsigned index) {
    return {std::uint8_t(index), std::uint8_t(255-index), std::uint8_t(index ^ 0x55), std::uint8_t(index ^ 0xaa)};
}
Bytes block(unsigned kind, const Bytes &data, bool parent = false) {
    Bytes result(16); result.insert(result.end(), data.begin(), data.end());
    half(result, 0, std::uint16_t(kind)); word(result, 4, std::uint32_t(result.size()));
    word(result, 8, parent ? 16 : std::uint32_t(result.size())); word(result, 12, 16);
    return result;
}
Bytes plane(unsigned format, bool palette) {
    const unsigned width = palette ? 16 : 3, height = palette ? 1 : 2;
    const unsigned bits = format == 3 ? 32 : format == 4 ? 4 : 8;
    const unsigned pitch = ((width * bits + 7) / 8 + 15) & ~15u;
    Bytes data(64 + pitch * height, 0xee);
    half(data, 0, 48); half(data, 2, 0); half(data, 4, std::uint16_t(format)); half(data, 6, 0);
    half(data, 8, std::uint16_t(width)); half(data, 10, std::uint16_t(height));
    half(data, 12, std::uint16_t(bits)); half(data, 14, 16); half(data, 16, 1);
    word(data, 24, 48); word(data, 28, 64); word(data, 32, std::uint32_t(data.size()));
    half(data, 40, palette ? 2 : 1); half(data, 42, 1); half(data, 44, 3); half(data, 46, 1);
    word(data, 48, 64);
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const auto index = palette ? x : y * width + x + 1;
            const auto start = 64 + y * pitch;
            if (format == 3) {
                const auto rgba = color(index);
                for (unsigned c = 0; c < 4; ++c) data[start + x * 4 + c] = rgba[c];
            } else if (format == 5) data[start + x] = std::uint8_t(index);
            else {
                auto &packed = data[start + x / 2];
                packed = std::uint8_t((packed & (x % 2 ? 0x0f : 0xf0)) | (index << ((x % 2) * 4)));
            }
        }
    }
    return block(palette ? 5 : 4, data);
}
Bytes picture(unsigned format, bool palette_first = false) {
    auto image = plane(format, false);
    if (format != 3) {
        auto palette = plane(3, true);
        if (palette_first) { palette.insert(palette.end(), image.begin(), image.end()); image = std::move(palette); }
        else image.insert(image.end(), palette.begin(), palette.end());
    }
    return block(3, image, true);
}
Bytes document(const Bytes &pictures) {
    Bytes image{'M','I','G','.','0','0','.','1','P','S','P',0,0,0,0,0};
    auto root = block(2, pictures, true); image.insert(image.end(), root.begin(), root.end()); return image;
}
void verify(const RgbaImage &image) {
    check(image.width == 3 && image.height == 2 && image.pixels.size() == 24, "Dimensions");
    for (unsigned i = 0; i < 6; ++i) {
        const auto expected = color(i + 1);
        for (unsigned c = 0; c < 4; ++c) check(image.pixels[i * 4 + c] == expected[c], "Exact RGBA pixel");
    }
}
}
int main() {
    try {
        for (unsigned format : {3u,4u,5u}) {
            for (bool before : {false,true}) verify(decode_gim(document(picture(format, before)), 24).at(0));
        }
        auto pictures = picture(4), second = picture(3);
        pictures.insert(pictures.end(), second.begin(), second.end());
        const auto both = document(pictures);
        auto images = decode_gim(both, 48); check(images.size() == 2, "Multiple pictures");
        verify(images[0]); verify(images[1]);
        std::size_t rejected = 0;
        const auto rejects = [&](const Bytes &bad, std::size_t limit = 48) {
            try { (void)decode_gim(bad, limit); }
            catch (const std::runtime_error &) { ++rejected; return; }
            throw std::runtime_error("Invalid GIM accepted");
        };
        rejects(both, 47); rejects(both, 0);
        const auto original = document(picture(5));
        for (std::size_t size = 0; size < original.size(); ++size)
            rejects(Bytes(original.begin(), original.begin() + size));
        const auto mutate_half = [&](std::size_t position, unsigned value) {
            auto bad = original; half(bad, position, std::uint16_t(value)); rejects(bad);
        };
        const auto mutate_word = [&](std::size_t position, unsigned value) {
            auto bad = original; word(bad, position, value); rejects(bad);
        };
        mutate_word(0, 0); mutate_word(20, 0xffffffff); mutate_word(24, 0); mutate_word(28, 0xffffffff);
        mutate_half(32, 99); mutate_half(48, 99);
        constexpr auto plane_start = 64;
        for (const auto [offset,value] : std::array<std::pair<unsigned,unsigned>,12>{{
            {0,47},{4,0},{6,1},{8,0},{10,0},{12,4},{14,0},{14,3},{16,0},{40,3},{42,2},{46,2}}})
            mutate_half(plane_start + offset, value);
        mutate_word(plane_start + 24, 0xffffffff); mutate_word(plane_start + 28, 47);
        mutate_word(plane_start + 32, 64); mutate_word(plane_start + 48, 65);
        auto bad = original; bad[plane_start + 64] = 16; rejects(bad); // Palette has 16 entries.
        std::cout << "PASS GIM INDEX4/INDEX8/RGBA, odd widths, row padding, palette order, multiple pictures and "
            << rejected << " rejected malformed/over-limit inputs\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
