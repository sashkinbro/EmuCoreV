#pragma once

#include <memory>

#include "IF00DKeyEncryptor.h"
#include "ICryptoOperations.h"

struct CryptEngineData;
struct derive_keys_ctx;

int setup_crypt_packet_keys(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, CryptEngineData* data, const derive_keys_ctx* drv_ctx);