#pragma once

#include <cstdint>
#include <string>
#include <memory>

#include "IF00DKeyEncryptor.h"
#include "ICryptoOperations.h"

int scePfsUtilGetSecret(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, unsigned char* secret, const unsigned char* klicensee, std::uint32_t files_salt, std::uint16_t flag, std::uint32_t unicv_page_salt, std::uint16_t key_id);