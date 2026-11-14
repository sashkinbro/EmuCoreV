#include "F00DNativeKeyEncryptor.h"

#include "Utils.h"

#include <cstring>

unsigned char contract_key0[0x10] = {0xE1, 0x22, 0x13, 0xB4, 0x80, 0x16, 0xB0, 0xE9, 0x9A, 0xB8, 0x1F, 0x8E, 0xC0, 0x2A, 0xD4, 0xA2};

F00DNativeKeyEncryptor::F00DNativeKeyEncryptor(std::shared_ptr<ICryptoOperations> cryptops)
   : m_cryptops(cryptops)
{
}

int F00DNativeKeyEncryptor::kprx_auth_service_0x50001(const unsigned char* key, int key_size, unsigned char* drv_key, int key_id) const
{
   memset(drv_key, 0, key_size);

   unsigned char key_dest[0x20];

   //check that size is aligned

   if((key_size & 0xF) != 0)
      return -1;

   memset(key_dest, 0, 0x20);

   //execute command

   if(key_size <= 0 || key_size > 0x20)
      return -1;

   memcpy(key_dest, key, key_size);

   //execute command according to specific key id

   //originally F00D service handles different key_ids
   //however PFS can only use key_id 0

   if(key_id != 0x00000000)
      return -1;

   if(m_cryptops->aes_ecb_decrypt(key_dest, key_dest, key_size, contract_key0, 0x80) < 0)
      return -1;

   memcpy(drv_key, key_dest, key_size);

   return 0;
}

int F00DNativeKeyEncryptor::encrypt_key(const unsigned char* key, int key_size, unsigned char* drv_key)
{
   if(key_size != 0x80 &&
      // key_size != 0xC0 && //TODO: need to implement padding
      key_size != 0x100)
      return -1;

   std::uint32_t nbytes = key_size / 8;
   std::string keyStr = byte_array_to_string(key, nbytes);

   auto kit = m_keyCache.find(keyStr);
   if(kit != m_keyCache.end())
   {
      string_to_byte_array(kit->second, nbytes, drv_key);
      return 0;
   }
   else
   {
      if(kprx_auth_service_0x50001(key, nbytes, drv_key, 0) < 0)
         return -1;

      std::string drv_keyStr = byte_array_to_string(drv_key, nbytes);

      m_keyCache.insert(std::make_pair(keyStr, drv_keyStr));

      return 0;
   }
}

void F00DNativeKeyEncryptor::print_cache(std::ostream& os, std::string sep) const
{
   os << "Number of items in cache: " << m_keyCache.size() << std::endl;

   //its ok to print whole cache since we only expect one item anyway

   for(auto& item : m_keyCache)
   {
      os << item.first << sep << item.second << std::endl;
   }
}