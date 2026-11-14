#pragma once

#include <cstdint>
#include <memory>

#include "IF00DKeyEncryptor.h"
#include "ICryptoOperations.h"

//#### GROUP 1 (possible keygen aes-cbc-cts dec/aes-cbc-cts enc) ####
//#### GROUP 2 (possible keygen aes-cmac-cts dec/aes-cmac-cts enc) (technically there is no dec/enc - this is pair of same functions since cmac) ####

int pfs_decrypt_unicv(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, const unsigned char* key, const unsigned char* tweak_mask, std::uint64_t tweak_key, std::uint32_t size, std::uint32_t block_size, const unsigned char* src, unsigned char* dst, std::uint16_t crypto_engine_flag, std::uint16_t key_id);

int pfs_encrypt_unicv(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, const unsigned char* key, const unsigned char* tweak_mask, std::uint64_t tweak_key, std::uint32_t size, std::uint32_t block_size, const unsigned char* src, unsigned char* dst, std::uint16_t crypto_engine_flag, std::uint16_t key_id);

//#### GROUP 3 (no keygen xts-aes dec/xts-aes enc) ####
//#### GROUP 4 (no keygen xts-cmac dec/xts-cmac enc) (technically there is no dec/enc - this is pair of same functions since cmac) ####

int pfs_decrypt_icv(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* key, const unsigned char* tweak_enc_key, std::uint32_t keysize, std::uint64_t tweak_key, std::uint32_t size, std::uint32_t block_size, const unsigned char* src, unsigned char* dst, std::uint16_t crypto_engine_flag);

int pfs_encrypt_icv(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* key, const unsigned char* tweak_enc_key, std::uint32_t keysize, std::uint64_t tweak_key, std::uint32_t size, std::uint32_t block_size, const unsigned char* src, unsigned char* dst, std::uint16_t crypto_engine_flag);