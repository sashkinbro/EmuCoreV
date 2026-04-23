#pragma once

#include <cstdint>
#include <memory>

#include "ICryptoOperations.h"

#include "LocalFilesystem.h"

#pragma pack(push, 1)

#define SEALEDKEY_MAGIC "pfsSKKey"
#define SEALEDKEY_EXPECTED_TYPE_MAJOR 2
#define SEALEDKEY_EXPECTED_TYPE_MINOR 0

typedef struct sealedkey_t
{
   std::uint8_t magic[8];
   std::uint8_t type_major;
   std::uint8_t type_minor;
   std::uint8_t padding[6];
   std::uint8_t iv[0x10];
   std::uint8_t enc_key[0x10];
   std::uint8_t hmac[0x20];
} sealedkey_t;

#define KEYSTONE_MAGIC "keystone"
#define KEYSTONE_EXPECTED_TYPE 2
#define KEYSTONE_EXPECTED_VERSION 1

typedef struct keystone_t
{
   std::uint8_t magic[8];
   std::uint16_t type;
   std::uint16_t version;
   std::uint8_t padding[0x14];
   std::uint8_t passcode_hmac[0x20];
   std::uint8_t keystone_hmac[0x20];
} keystone_t;

#pragma pack(pop)

int get_sealedkey(std::shared_ptr<ICryptoOperations> cryptops, const psvpfs::path& titleIdPath, unsigned char* dec_key);

int get_keystone(std::shared_ptr<ICryptoOperations> cryptops, const psvpfs::path& titleIdPath, char* passcode = 0);