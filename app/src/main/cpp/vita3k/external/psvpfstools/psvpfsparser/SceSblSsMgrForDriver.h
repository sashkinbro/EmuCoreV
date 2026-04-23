#pragma once

#include <cstdint>
#include <memory>

#include "IF00DKeyEncryptor.h"
#include "ICryptoOperations.h"

int SceSblSsMgrForDriver_sceSblSsMgrAESCBCDecryptWithKeygenForDriver(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, const unsigned char* src, unsigned char* dst, int size, const unsigned char* key, int key_size, unsigned char* iv, std::uint16_t key_id, int mask_enable);

int SceSblSsMgrForDriver_sceSblSsMgrAESCBCEncryptWithKeygenForDriver(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, const unsigned char* src, unsigned char* dst, int size, const unsigned char* key, int key_size, unsigned char* iv, std::uint16_t key_id, int mask_enable);

int SceSblSsMgrForDriver_sceSblSsMgrAESECBEncryptWithKeygenForDriver(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, const unsigned char* src, unsigned char* dst, int size, const unsigned char* key, int key_size, std::uint16_t key_id, int mask_enable);

int SceSblSsMgrForDriver_sceSblSsMgrAESCBCDecryptForDriver(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* src, unsigned char* dst, int size, const unsigned char* key, int key_size, unsigned char* iv, int mask_enable);

int SceSblSsMgrForDriver_sceSblSsMgrAESCBCEncryptForDriver(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* src, unsigned char* dst, int size, const unsigned char* key, int key_size, unsigned char* iv, int mask_enable);

int SceSblSsMgrForDriver_sceSblSsMgrAESECBEncryptForDriver(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* src, unsigned char* dst, int size, const unsigned char* key, int key_size, int mask_enable);

int SceSblSsMgrForDriver_sceSblSsMgrAESECBDecryptForDriver(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* src, unsigned char* dst, int size, const unsigned char* key, int key_size, int mask_enable);

int SceSblSsMgrForDriver_sceSblSsMgrAESCMACForDriver(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* src, unsigned char dst[0x10], int size, const unsigned char* key, int key_size, unsigned char* iv, int mask_enable, int command_bit);

int SceSblSsMgrForDriver_sceSblSsMgrAESCMACWithKeygenForDriver(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, const unsigned char* src, unsigned char dst[0x10], int size, const unsigned char* key, int key_size, unsigned char* iv, std::uint16_t key_id, int mask_enable, int command_bit);

int SceSblSsMgrForDriver_sceSblSsMgrHMACSHA1ForDriver(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* src, unsigned char* dst, int size, const unsigned char* key, unsigned char* iv, int mask_enable, int command_bit);