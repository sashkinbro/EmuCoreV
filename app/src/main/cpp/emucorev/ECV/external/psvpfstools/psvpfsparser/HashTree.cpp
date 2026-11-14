#include "HashTree.h"

#include "FilesDbParser.h"

#include "Utils.h"

#include <cstring>

int64_t page2off(std::uint32_t page, std::uint32_t pageSize)
{
   return page * pageSize + pageSize;
}

std::uint32_t off2page(std::int64_t offset, std::uint32_t pageSize)
{
   return static_cast<std::uint32_t>((offset - pageSize) / pageSize);
}

bool validate_hash_tree(int level, std::uint32_t page, const std::vector<sce_ng_pfs_block_t>& blocks, const std::multimap<std::uint32_t, page_icv_data>& page_icvs)
{
   const sce_ng_pfs_block_t& current_block = blocks[page]; //it should be safe to use page directly as index

   if(current_block.page != page)
   {
      std::cout << "Invalid page" << std::endl;
      return false;
   }

   auto children = page_icvs.equal_range(page);
   for (auto it = children.first; it != children.second; it++)
   {
      std::cout << std::string(level, '.') << it->second.page;

      bool found = false;
      for(auto& hash : current_block.hashes)
      {
         if(memcmp(it->second.icv, hash.data, 0x14) == 0)
         {
            std::cout << " - OK : ";

            print_bytes(it->second.icv, 0x14);

            validate_hash_tree(level + 1, it->second.page, blocks, page_icvs);
            found = true;
            break;
         }
      }

      if(!found)
      {
         std::cout << " - Hash does not match" << std::endl;
         return false;
      }
   }

   return true;
}