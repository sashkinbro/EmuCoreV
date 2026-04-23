#pragma once

#include <cstdint>
#include <memory>

#include "ICryptoOperations.h"

int SceKernelUtilsForDriver_sceSha1DigestForDriver(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char *source, int size, unsigned char result[0x14]);

int SceKernelUtilsForDriver_sceHmacSha1DigestForDriver(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* key, int key_len, const unsigned char* data, int data_len, unsigned char digest[0x14]);