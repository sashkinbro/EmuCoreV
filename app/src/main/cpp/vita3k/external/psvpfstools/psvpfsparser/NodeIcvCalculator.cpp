#include "NodeIcvCalculator.h"

#include <cstring>
#include <string>
#include <vector>

#include "FilesDbParser.h"
#include "Utils.h"

unsigned char* c_node_icvs(unsigned char* raw_data, std::uint32_t order)
{
  int offset = 0x48 * order + 0x10 * order - 0x38;
  return raw_data + offset;
}

int icv_set_hmac_sw(std::shared_ptr<ICryptoOperations> cryptops, unsigned char* iv, const unsigned char* key, unsigned char* input, int length)
{
  cryptops->hmac_sha1(input, iv, length, key, 0x14);
  return 0;
}

int icv_contract_hmac(std::shared_ptr<ICryptoOperations> cryptops, unsigned char* iv, const unsigned char* key, const unsigned char* base0_src, const unsigned char* base1_src)
{
  unsigned char base[0x28];
  memcpy(base, base0_src, 0x14);
  memcpy(base + 0x14, base1_src, 0x14);
  return icv_set_hmac_sw(cryptops, iv, key, base, 0x28);
}

//should return page size
std::uint32_t node_size(std::uint32_t index)
{
  return 0x6C * index - 0x38;
}

//get order of the page (max number of hashes per page)
std::uint32_t order_max_avail(std::uint32_t pagesize)
{
  std::uint32_t index;
  //calculate max possible index until data size does not fit the page
  for(index = 1; pagesize > node_size(index); index++);
  //substract one entry if last entry overflows the page
  if(pagesize < node_size(index))
    index--;
  return index;
}

int calculate_node_icv(std::shared_ptr<ICryptoOperations> cryptops, sce_ng_pfs_header_t& ngh, const unsigned char* secret, sce_ng_pfs_block_header_t* node_header, unsigned char* raw_data, unsigned char* icv)
{
   std::uint32_t order = order_max_avail(ngh.pageSize); //get order of the page (max number of hashes per page)

   if(ngh.version == 5)
   {
      size_t dataSize = (0x6C * order - 0x3C); //should be page size - 4
      cryptops->hmac_sha1(raw_data + 4, icv, dataSize, secret, 0x14);
      return 0;
   }

   if(node_header == 0)
      return -1;

   std::uint32_t nEntries = node_header->nFiles;
   if (node_header->type > 0)
      nEntries++;

   memset(icv, 0, 0x14);

   for (std::uint32_t index = 0; index < nEntries; index++)
   {
      unsigned char* icvs_base = c_node_icvs(raw_data, order);
      icv_contract_hmac(cryptops, icv, secret, icv, icvs_base + index * 0x14);
   }

   return 0;
}