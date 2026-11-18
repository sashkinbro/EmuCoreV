#include <emucorev/paf/gim_image.h>

#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>

namespace emucorev::paf {
namespace {
using Bytes = std::span<const std::uint8_t>;
Bytes range(Bytes bytes, std::uint64_t offset, std::uint64_t size) {
    if (offset > bytes.size() || size > bytes.size() - offset)
        throw std::runtime_error("GIM range exceeds its block");
    return bytes.subspan(std::size_t(offset), std::size_t(size));
}
std::uint16_t half(Bytes bytes, std::size_t offset) {
    const auto b = range(bytes, offset, 2);
    return std::uint16_t(b[0]) | (std::uint16_t(b[1]) << 8);
}
std::uint32_t word(Bytes bytes, std::size_t offset) {
    return std::uint32_t(half(bytes, offset)) | (std::uint32_t(half(bytes, offset + 2)) << 16);
}
std::uint64_t align(std::uint64_t size, std::uint16_t alignment) {
    if (!alignment || (alignment & (alignment - 1)))
        throw std::runtime_error("Unsupported GIM alignment");
    return (size + alignment - 1) & ~std::uint64_t(alignment - 1);
}
struct Block {
    std::uint16_t kind;
    Bytes bytes, data;
    std::uint32_t next;
};
Block block(Bytes bytes) {
    range(bytes, 0, 16);
    const auto size = word(bytes, 4), next = word(bytes, 8), offset = word(bytes, 12);
    if (size < 16 || offset < 16 || offset > size || next < 16 || next > size)
        throw std::runtime_error("Invalid GIM block header");
    const auto contents = range(bytes, 0, size);
    return {half(bytes, 0), contents, contents.subspan(offset), next};
}
struct Plane {
    std::uint16_t format, width, height, bits;
    std::size_t pitch;
    Bytes pixels;
};
Plane plane(const Block &block) {
    const auto data = block.data;
    range(data, 0, 48);
    const auto header_size = half(data, 0), format = half(data, 4), order = half(data, 6);
    const auto width = half(data, 8), height = half(data, 10), bits = half(data, 12);
    if (header_size != 48 || order != 0 || !width || !height)
        throw std::runtime_error("Unsupported GIM plane header/order");
    if (!((format == 3 && bits == 32) || (format == 4 && bits == 4) || (format == 5 && bits == 8)))
        throw std::runtime_error("Unreconstructed GIM pixel format");
    if (half(data, 42) != 1 || half(data, 44) != 3 || half(data, 46) != 1
        || (half(data, 40) != 1 && half(data, 40) != 2))
        throw std::runtime_error("Unreconstructed GIM levels/frames");
    const auto index = word(data, 24), start = word(data, 28), end = word(data, 32);
    if (index < header_size || std::uint64_t(index) + 4 > start || end < start || word(data, index) != start)
        throw std::runtime_error("Invalid GIM plane offsets");
    const auto pitch = align((std::uint64_t(width) * bits + 7) / 8, half(data, 14));
    const auto stored_height = align(height, half(data, 16));
    const auto pixels = range(data, start, end - start);
    range(pixels, 0, pitch * stored_height);
    return {format, width, height, bits, std::size_t(pitch), pixels};
}

RgbaImage picture(const Block &picture, std::size_t maximum) {
    if (picture.next != picture.bytes.size() - picture.data.size())
        throw std::runtime_error("Invalid GIM picture child offset");
    std::optional<Plane> image, palette;
    for (std::size_t offset = 0; offset < picture.data.size();) {
        const auto child = block(picture.data.subspan(offset));
        if (child.next != child.bytes.size()) throw std::runtime_error("Invalid GIM leaf next offset");
        if (child.kind == 4) {
            if (image) throw std::runtime_error("Multiple GIM image planes in a picture");
            image = plane(child);
        } else if (child.kind == 5) {
            if (palette) throw std::runtime_error("Multiple GIM palettes in a picture");
            palette = plane(child);
        } else throw std::runtime_error("Unreconstructed GIM picture block");
        offset += child.bytes.size();
    }
    if (!image) throw std::runtime_error("Missing GIM image");
    const auto &source = *image;
    const auto output_size = std::uint64_t(source.width) * source.height * 4;
    if (output_size > maximum) throw std::runtime_error("GIM output exceeds allocation limit");
    if (source.format != 3 && (!palette || palette->format != 3 || palette->height != 1))
        throw std::runtime_error("Missing or unsupported GIM RGBA palette");
    RgbaImage result{source.width, source.height, std::vector<std::uint8_t>(std::size_t(output_size))};
    for (std::size_t y = 0; y < source.height; ++y) {
        for (std::size_t x = 0; x < source.width; ++x) {
            std::array<std::uint8_t, 4> color;
            if (source.format == 3) {
                const auto rgba = range(source.pixels, y * source.pitch + x * 4, 4);
                std::copy(rgba.begin(), rgba.end(), color.begin());
            } else {
                const auto packed = source.pixels[y * source.pitch + (source.format == 5 ? x : x / 2)];
                const auto index = source.format == 5 ? packed : ((packed >> ((x % 2) * 4)) & 15);
                if (index >= palette->width) throw std::runtime_error("GIM palette index out of range");
                const auto rgba = range(palette->pixels, std::size_t(index) * 4, 4);
                std::copy(rgba.begin(), rgba.end(), color.begin());
            }
            std::copy(color.begin(), color.end(), result.pixels.begin() + (y * source.width + x) * 4);
        }
    }
    return result;
}
}

std::vector<RgbaImage> decode_gim(Bytes bytes, std::size_t maximum_output_bytes) {
    constexpr std::array<std::uint8_t, 16> magic{'M','I','G','.','0','0','.','1','P','S','P',0,0,0,0,0};
    const auto header = range(bytes, 0, magic.size());
    if (!std::equal(header.begin(), header.end(), magic.begin())) throw std::runtime_error("Unsupported GIM signature");
    const auto root = block(bytes.subspan(16));
    if (root.kind != 2 || root.bytes.size() != bytes.size() - 16
        || root.next != root.bytes.size() - root.data.size())
        throw std::runtime_error("Invalid GIM root");
    std::vector<RgbaImage> images;
    std::size_t remaining = maximum_output_bytes;
    for (std::size_t offset = 0; offset < root.data.size();) {
        const auto child = block(root.data.subspan(offset));
        if (child.kind == 3) {
            auto image = picture(child, remaining);
            remaining -= image.pixels.size();
            images.push_back(std::move(image));
        } else if (child.kind != 0xff) throw std::runtime_error("Unreconstructed GIM root block");
        offset += child.bytes.size();
    }
    if (images.empty()) throw std::runtime_error("GIM has no pictures");
    return images;
}
} // namespace emucorev::paf
