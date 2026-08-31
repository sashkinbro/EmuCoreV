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

#include <util/log.h>

#include <cstdarg>
#include <cstdio>

// Route VMA's leak report into our log
#ifndef NDEBUG
static void vma_debug_log(const char *format, ...) {
    char buf[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    LOG_ERROR("[VMA] {}", buf);
}
#define VMA_DEBUG_LOG_FORMAT(format, ...) vma_debug_log(format, __VA_ARGS__)
#endif

// Build allocator implementations.
#define VMA_IMPLEMENTATION
#include <renderer/vulkan/types.h>

// Provide storage for the dynamic loader
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
