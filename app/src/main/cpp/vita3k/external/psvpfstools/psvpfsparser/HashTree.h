#pragma once

#include <cstdint>
#include <vector>
#include <map>

typedef struct page_icv_data
{
   std::int64_t offset;
   std::uint32_t page;
   std::uint8_t icv[0x14];
}page_icv_data;

std::int64_t page2off(std::uint32_t page, std::uint32_t pageSize);

std::uint32_t off2page(std::int64_t offset, std::uint32_t pageSize);

struct sce_ng_pfs_block_t;

bool validate_hash_tree(int level, std::uint32_t page, const std::vector<sce_ng_pfs_block_t>& blocks, const std::multimap<std::uint32_t, page_icv_data>& page_icvs);