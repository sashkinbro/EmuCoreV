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

#pragma once

#include <mem/allocator.h>
#include <mem/functions.h>
#include <mem/util.h>

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>

struct AllocMemPage {
    uint32_t allocated : 4;
    uint32_t size : 28;
};

static_assert(sizeof(AllocMemPage) == 4);

typedef uint8_t *PagePtr;
typedef std::unique_ptr<uint8_t[], std::function<void(uint8_t *)>> Memory;
typedef std::unique_ptr<AllocMemPage[]> AllocPageTable;
typedef std::unique_ptr<PagePtr[]> PageTable;
typedef std::map<int, std::string> PageNameMap;

struct ProtectBlockInfo {
    uint32_t size = 0;
    ProtectCallback callback;
};

struct ProtectSegmentInfo {
    std::multimap<Address, ProtectBlockInfo> blocks;
    uint32_t size = 0;
    MemPerm perm = MemPerm::None;

    explicit ProtectSegmentInfo() = default;
    explicit ProtectSegmentInfo(uint32_t size, MemPerm perm)
        : size(size)
        , perm(perm) {
    }
};

typedef std::map<Address, ProtectSegmentInfo, std::greater<>> ProtectSegmentTrees;

struct MemExternalMapping {
    Address address;
    uint32_t size;
};

struct MemState {
    std::mutex generation_mutex;
    std::mutex protect_mutex;

    uint32_t host_page_size = 0;
    // keep freed guest pages committed instead of decommitting
    bool preserve_freed_pages = false;
    Memory memory;
    AllocPageTable alloc_table;
    BitmapAllocator allocator;
    ProtectSegmentTrees protect_tree;

    // Release tracking for caches that trust write traps (a trap cannot see its memory being freed and
    // reallocated): free() stamps the 64 KiB granules of the released region with a new epoch.
    static constexpr uint32_t release_granule_shift = 16;
    std::atomic<uint64_t> release_epoch{ 0 };
    std::array<std::atomic<uint64_t>, (1ull << 32 >> release_granule_shift)> release_stamp{};

    PageNameMap page_name_map;

    bool use_page_table = false;
    PageTable page_table;
    std::map<uint64_t, MemExternalMapping, std::greater<>> external_mapping;
    mutable std::mutex external_mapping_mutex;
    mutable std::shared_mutex external_transition_mutex;
};
