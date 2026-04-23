#include "LocalKeyGenerator.h"

#include <cstdint>

#include <fstream>
#include <string>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <fstream>

#include "LocalKeys.h"
#include "Utils.h"

//check functions are based on code provided by Proxima

int check_sealedkey(std::shared_ptr<ICryptoOperations> cryptops, sealedkey_t& sk)
{
   std::uint8_t result[0x20];

   if(std::string((char*)sk.magic, 8) != SEALEDKEY_MAGIC)
   {
      std::cout << "sealedkey: invalid magic" << std::endl;
      return -1;
   }

   if(sk.type_major != SEALEDKEY_EXPECTED_TYPE_MAJOR)
   {
      std::cout << "sealedkey: invalid type_major" << std::endl;
      return -1;
   }

   if(sk.type_minor != SEALEDKEY_EXPECTED_TYPE_MINOR)
   {
      std::cout << "sealedkey: invalid type_minor" << std::endl;
      return -1;
   }

   if(!isZeroVector(sk.padding, sk.padding + sizeof(sk.padding)))
   {
      std::cout << "sealedkey: invalid padding" << std::endl;
      return -1;
   }

   cryptops->hmac_sha256((unsigned char*)&sk, result, 0x30, sealedkey_retail_key, 0x10);
   if(memcmp(sk.hmac, result, 0x20) == 0)
   {
      std::cout << "sealedkey: matched retail hmac" << std::endl;
      return 0;
   }
   else
   {
      cryptops->hmac_sha256((unsigned char*)&sk, result, 0x30, sealedkey_debug_key, 0x10);
      if(memcmp(sk.hmac, result, 0x20) == 0)
      {
         std::cout << "sealedkey: matched debug hmac" << std::endl;
         return 0;
      }
      else
      {
         std::cout << "sealedkey: failed to match hmac" << std::endl;
         return -1;
      }
   }
}

int check_keystone(std::shared_ptr<ICryptoOperations> cryptops, keystone_t& ks)
{
   std::uint8_t result[0x20];

   if(std::string((char*)ks.magic, 8) != KEYSTONE_MAGIC)
   {
      std::cout << "keystone: invalid magic" << std::endl;
      return -1;
   }

   if(ks.type != KEYSTONE_EXPECTED_TYPE)
   {
      std::cout << "keystone: invalid type" << std::endl;
      return -1;
   }

   if(ks.version != KEYSTONE_EXPECTED_VERSION)
   {
      std::cout << "keystone: invalid version" << std::endl;
      return -1;
   }

   if(!isZeroVector(ks.padding, ks.padding + sizeof(ks.padding)))
   {
      std::cout << "keystone: invalid padding" << std::endl;
      return -1;
   }

   cryptops->hmac_sha256((unsigned char*)&ks, result, 0x40, keystone_hmac_secret, 0x20);
   if(memcmp(ks.keystone_hmac, result, 0x20) == 0)
   {
      std::cout << "keystone: matched retail hmac" << std::endl;
      return 0;
   }
   else
   {
      cryptops->hmac_sha256((unsigned char*)&ks, result, 0x40, keystone_debug_key, 0x20);
      if(memcmp(ks.keystone_hmac, result, 0x20) == 0)
      {
         std::cout << "keystone: matched debug hmac!" << std::endl;
         return 0;
      }
      else
      {
         std::cout << "keystone: failed to match hmac" << std::endl;
         return -1;
      }
   }
}

int check_keystone(std::shared_ptr<ICryptoOperations> cryptops, keystone_t& ks, unsigned char* passcode)
{
   if(check_keystone(cryptops, ks) < 0)
      return -1;

   std::uint8_t result[0x20];

   cryptops->hmac_sha256(passcode, result, 0x20, passcode_hmac_secret, 0x20);
   if(memcmp(ks.passcode_hmac, result, 0x20) == 0)
   {
      std::cout << "keystone: matched passcode hmac" << std::endl;
      return 0;
   }
   else
   {
      cryptops->hmac_sha256(passcode, result, 0x20, passcode_debug_key, 0x20);
      if(memcmp(ks.passcode_hmac, result, 0x20) == 0)
      {
         std::cout << "keystone: matched debug passcode hmac!" << std::endl;
         return 0;
      }
      else
      {
         std::cout << "keystone: failed to match passcode hmac" << std::endl;
         return -1;
      }
   }
}

//public functions

int get_sealedkey(std::shared_ptr<ICryptoOperations> cryptops, const psvpfs::path& titleIdPath, unsigned char* dec_key)
{
   psvpfs::path filepath = titleIdPath / "sce_sys" / "sealedkey";

   if(!psvpfs::exists(filepath))
   {
      std::cout << "sealedkey does not exist" << std::endl;
      return -1;
   }

   sealedkey_t sk;

   std::ifstream inputStream(filepath.generic_string().c_str(), std::ios::in | std::ios::binary);
   inputStream.read((char*)&sk, sizeof(sealedkey_t));

   if(check_sealedkey(cryptops, sk) < 0)
      return -1;

   cryptops->aes_cbc_decrypt(sk.enc_key, dec_key, sizeof(sk.enc_key), PFS_EncKey, 128, sk.iv);

   return 0;
}

int get_keystone(std::shared_ptr<ICryptoOperations> cryptops, const psvpfs::path& titleIdPath, char* passcode)
{
   psvpfs::path filepath = titleIdPath / "sce_sys" / "keystone";

   if(!psvpfs::exists(filepath))
   {
      std::cout << "keystone does not exist" << std::endl;
      return -1;
   }

   keystone_t ks;

   std::ifstream inputStream(filepath.generic_string().c_str(), std::ios::in | std::ios::binary);
   inputStream.read((char*)&ks, 0x60);

   if(passcode == 0)
      return check_keystone(cryptops, ks);
   else
      return check_keystone(cryptops, ks, (unsigned char*)passcode);

   return 0;
}
