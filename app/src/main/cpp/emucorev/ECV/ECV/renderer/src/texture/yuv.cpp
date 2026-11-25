// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include <renderer/functions.h>
#include <renderer/texture_cache.h>
#include <renderer/yuv.h>

namespace renderer::texture {

// CPU YUV -> RGBA8 conversion. This is only the last fallback: the Vulkan renderer converts on the GPU
// (see vulkan/yuv_convert.cpp) and uses these when no GPU route is available (and for the OpenGL backend).
void yuv420_texture_to_rgb(uint8_t profile, bool chroma_swapped, uint8_t *dst, const uint8_t *src, uint32_t width, uint32_t height, uint32_t layout_width, uint32_t layout_height, bool is_p3) {
    // dst is a tightly packed width x height image whose pitch equals the source pitch (width = pixels per stride)
    yuv::convert_420_to_rgba8(profile, is_p3, chroma_swapped, dst, width, src, width, height, width, layout_width, layout_height);
}

void yuv422_texture_to_rgb(uint8_t profile, uint32_t swizzle, uint8_t *dst, const uint8_t *src, uint32_t width, uint32_t height) {
    yuv::convert_422_to_rgba8(profile, swizzle, dst, width, src, width, height, width);
}

} // namespace renderer::texture

renderer::TextureCache::~TextureCache() = default;
