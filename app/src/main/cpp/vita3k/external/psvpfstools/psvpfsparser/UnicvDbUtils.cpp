#include "UnicvDbUtils.h"

#include "UnicvDbTypes.h"

std::uint32_t binTreeNumMaxAvail(std::uint32_t signatureSize, std::uint32_t pageSize)
{
  return (pageSize - sizeof(sig_tbl_header_t)) / signatureSize;
}

std::uint32_t binTreeSize(std::uint32_t signatureSize, std::uint32_t binTreeNumMaxAvail)
{
  return binTreeNumMaxAvail * signatureSize + sizeof(sig_tbl_header_t);
}