#pragma once

#include <memory>

#include "ICryptoOperations.h"

int icv_set_hmac_sw(std::shared_ptr<ICryptoOperations> cryptops, unsigned char *dst, const unsigned char *key, const unsigned char *src, int size);

int icv_set_sw(std::shared_ptr<ICryptoOperations> cryptops, unsigned char *dst, const unsigned  char *src, int size);

int icv_contract(std::shared_ptr<ICryptoOperations> cryptops, unsigned char *result, const unsigned char *left_hash, const unsigned char *right_hash);