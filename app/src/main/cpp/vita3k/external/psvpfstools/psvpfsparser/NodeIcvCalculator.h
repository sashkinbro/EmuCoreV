#pragma once

#include <cstdint>
#include <memory>

#include "ICryptoOperations.h"

struct sce_ng_pfs_header_t;
struct sce_ng_pfs_block_header_t;

std::uint32_t order_max_avail(std::uint32_t pagesize);

int calculate_node_icv(std::shared_ptr<ICryptoOperations> cryptops, sce_ng_pfs_header_t& ngh, const unsigned char* secret, sce_ng_pfs_block_header_t* node_header, unsigned char* raw_data, unsigned char *icv);