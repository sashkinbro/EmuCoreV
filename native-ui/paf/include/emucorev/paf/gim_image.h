#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace emucorev::paf {

struct RgbaImage {
    std::uint32_t width, height;
    // Logical rows, top to bottom; stored RGBA channel bytes, no palette
    // quantization, alpha conversion, resampling or color-space conversion.
    std::vector<std::uint8_t> pixels;
};

// Decode the observed single-level, single-frame PSP GIM variants. Unsupported
// variants fail explicitly; they must never be rendered through guessed rules.
// The limit applies to the combined RGBA output of all pictures in the document.
std::vector<RgbaImage> decode_gim(std::span<const std::uint8_t> bytes,
    std::size_t maximum_output_bytes);

} // namespace emucorev::paf
