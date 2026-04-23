#pragma once

#include <cstdint>
#include <memory>

#include "IF00DKeyEncryptor.h"
#include "ICryptoOperations.h"

//#### FUNCTIONS OF GROUP 1/2 are used to encrypt/decrypt unicv.db ####
//group 1 is relevant - it is implementation of aes-cbc-cts used to encrypt/ decrypt unicv.db
//it is important that: tweak is used as iv and aes-cbc implements cts

//group 2 is not relevant in particular since it is a cmac that outputs only 0x10 bytes
//these functions operate with global g_cmac_buffer buffer and not with destination buffer
//true purpose of cmac functions is still not known

//#### GROUP 1 (possible keygen aes-cbc-cts dec/aes-cbc-cts enc) ####

int AESCBCEncrypt_base(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* key, unsigned char* tweak, std::uint32_t size, const unsigned char* src, unsigned char* dst);

int AESCBCDecrypt_base(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* key, unsigned char* tweak, std::uint32_t size, const unsigned char* src, unsigned char* dst);

int AESCBCEncryptWithKeygen_base(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, const unsigned char* key, unsigned char* tweak, std::uint32_t size, const unsigned char* src, unsigned char* dst, std::uint16_t key_id);

int AESCBCDecryptWithKeygen_base(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, const unsigned char* key, unsigned char* tweak, std::uint32_t size, const unsigned char* src, unsigned char* dst, std::uint16_t key_id);

//#### GROUP 2 (possible keygen aes-cmac-cts dec/aes-cmac-cts enc) (technically there is no dec/enc - this is pair of same functions since cmac) ####

//should use g_cmac_buffer global buffer
int AESCMACEncrypt_base(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* key, unsigned char* tweak, std::uint32_t size, const unsigned char* src, unsigned char dst[0x10]);

//should use g_cmac_buffer global buffer
int AESCMACDecrypt_base(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* key, unsigned char* tweak, std::uint32_t size, const unsigned char* src, unsigned char dst[0x10]);

//should use g_cmac_buffer global buffer
int AESCMACEncryptWithKeygen_base(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, const unsigned char* key, unsigned char* tweak, std::uint32_t size, const unsigned char* src, unsigned char dst[0x10], std::uint16_t key_id);

//should use g_cmac_buffer global buffer
int AESCMACDecryptWithKeygen_base(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, const unsigned char* key, unsigned char* tweak, std::uint32_t size, const unsigned char* src, unsigned char dst[0x10], std::uint16_t key_id);

//#### FUNCTIONS OF GROUP 3/4 are used to encrypt/decrypt icv.db ####
//group 3 is relevant - it is implementation of xts-aes used to encrypt/ decrypt icv.db

//group 4 is not relevant in particular since it is a cmac that outputs only 0x10 bytes
//these functions operate with global g_cmac_buffer buffer and not with destination buffer
//true purpose of cmac functions is still not known

//#### GROUP 3 (no keygen xts-aes dec/xts-aes enc) ####

int XTSAESEncrypt_base(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* tweak, const unsigned char* dst_key, const unsigned char* tweak_enc_key, std::uint32_t key_size, std::uint32_t size, const unsigned char* src, unsigned char* dst);

int XTSAESDecrypt_base(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* tweak, const unsigned char* dst_key, const unsigned char* tweak_enc_key, std::uint32_t key_size, std::uint32_t size, const unsigned char* src, unsigned char* dst);

//#### GROUP 4 (no keygen xts-cmac dec/xts-cmac enc) (technically there is no dec/enc - this is pair of same functions since cmac) ####

//should use g_cmac_buffer global buffer
int XTSCMACEncrypt_base(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* tweak, const unsigned char* dst_key, const unsigned char* tweak_enc_key, std::uint32_t key_size, std::uint32_t size, const unsigned char* src, unsigned char dst[0x10]);

//should use g_cmac_buffer global buffer
int XTSCMACDecrypt_base(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* tweak, const unsigned char* dst_key, const unsigned char* tweak_enc_key, std::uint32_t key_size, std::uint32_t size, const unsigned char* src, unsigned char dst[0x10]);