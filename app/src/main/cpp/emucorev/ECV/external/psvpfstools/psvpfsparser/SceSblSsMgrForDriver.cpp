#include "SceSblSsMgrForDriver.h"

#include <stdexcept>

//##### WITH KEYGEN CRYPTO FUNCTIONS #####

//this function is tested and works
int SceSblSsMgrForDriver_sceSblSsMgrAESCBCDecryptWithKeygenForDriver(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, const unsigned char* src, unsigned char* dst, int size, const unsigned char* key, int key_size, unsigned char* iv, std::uint16_t key_id, int mask_enable)
{
   if(mask_enable != 1)
      throw std::runtime_error("Unexpected mask_enable");

   if(key_id != 0)
      throw std::runtime_error("Unexpected key_id");

   unsigned char drv_key[0x20] = {0}; //use max possible buffer
   if(iF00D->encrypt_key(key, key_size, drv_key) < 0)
      return -1;

   return cryptops->aes_cbc_decrypt(src, dst, size, drv_key, key_size, iv);
}

//this function is tested and works
int SceSblSsMgrForDriver_sceSblSsMgrAESCBCEncryptWithKeygenForDriver(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, const unsigned char* src, unsigned char* dst, int size, const unsigned char* key, int key_size, unsigned char* iv, std::uint16_t key_id, int mask_enable)
{
   if(mask_enable != 1)
      throw std::runtime_error("Unexpected mask_enable");

   if(key_id != 0)
      throw std::runtime_error("Unexpected key_id");

   unsigned char drv_key[0x20] = {0}; //use max possible buffer
   if(iF00D->encrypt_key(key, key_size, drv_key) < 0)
      return -1;

   return cryptops->aes_cbc_encrypt(src, dst, size, drv_key, key_size, iv);
}

//this function is tested and works
int SceSblSsMgrForDriver_sceSblSsMgrAESECBEncryptWithKeygenForDriver(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, const unsigned char* src, unsigned char* dst, int size, const unsigned char* key, int key_size, std::uint16_t key_id, int mask_enable)
{
   if(mask_enable != 1)
      throw std::runtime_error("Unexpected mask_enable");

   if(key_id != 0)
      throw std::runtime_error("Unexpected key_id");

   unsigned char drv_key[0x20] = {0}; //use max possible buffer
   if(iF00D->encrypt_key(key, key_size, drv_key) < 0)
      return -1;

   return cryptops->aes_ecb_encrypt(src, dst, size, drv_key, key_size);
}

//##### NORMAL CRYPTO FUNCTIONS #####

// aes-cbc

//not tested
int SceSblSsMgrForDriver_sceSblSsMgrAESCBCDecryptForDriver(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* src, unsigned char* dst, int size, const unsigned char* key, int key_size, unsigned char* iv, int mask_enable)
{
   throw std::runtime_error("not tested");

   if(mask_enable != 1)
      throw std::runtime_error("Unexpected mask_enable");

   return cryptops->aes_cbc_decrypt(src, dst, size, key, key_size, iv);
}

//not tested
int SceSblSsMgrForDriver_sceSblSsMgrAESCBCEncryptForDriver(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* src, unsigned char* dst, int size, const unsigned char* key, int key_size, unsigned char* iv, int mask_enable)
{
   throw std::runtime_error("not tested");

   if(mask_enable != 1)
      throw std::runtime_error("Unexpected mask_enable");

   return cryptops->aes_cbc_encrypt(src, dst, size, key, key_size, iv);
}

// aes-ecb

//not tested
int SceSblSsMgrForDriver_sceSblSsMgrAESECBEncryptForDriver(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* src, unsigned char* dst, int size, const unsigned char* key, int key_size, int mask_enable)
{
   throw std::runtime_error("not tested");

   if(mask_enable != 1)
      throw std::runtime_error("Unexpected mask_enable");

   return cryptops->aes_ecb_encrypt(src, dst, size, key, key_size);
}

//ECB works on block data - which means that data has to be padded (most likely with zeroes)
//Maybe that is why all files in icv.db are padded with zeroes to fileSectorSize border?
//Maybe that is why crypto primitives for icv files in CryptEngine only process data in blocks and not handling tail?

//this function is tested and works
int SceSblSsMgrForDriver_sceSblSsMgrAESECBDecryptForDriver(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* src, unsigned char* dst, int size, const unsigned char* key, int key_size, int mask_enable)
{
   if(mask_enable != 1)
      throw std::runtime_error("Unexpected mask_enable");

   return cryptops->aes_ecb_decrypt(src, dst, size, key, key_size);
}

//##### CMAC FUNCTIONS #####

//not tested
int SceSblSsMgrForDriver_sceSblSsMgrAESCMACForDriver(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* src, unsigned char dst[0x10], int size, const unsigned char* key, int key_size, unsigned char* iv, int mask_enable, int command_bit)
{
   throw std::runtime_error("not tested");

   if(iv != 0)
      throw std::runtime_error("iv must be 0");

   if(mask_enable != 1)
      throw std::runtime_error("unsupported mask_enable");

   if(command_bit != 0)
      throw std::runtime_error("unsupported command_bit");

   return cryptops->aes_cmac(src, dst, size, key, key_size);
}

//not tested
int SceSblSsMgrForDriver_sceSblSsMgrAESCMACWithKeygenForDriver(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, const unsigned char* src, unsigned char dst[0x10], int size, const unsigned char* key, int key_size, unsigned char* iv, std::uint16_t key_id, int mask_enable, int command_bit)
{
   throw std::runtime_error("not tested");

   if(key_id != 0)
      throw std::runtime_error("Unexpected key_id");

   if(iv != 0)
      throw std::runtime_error("iv must be 0");

   if(mask_enable != 1)
      throw std::runtime_error("unsupported mask_enable");

   if(command_bit != 0)
      throw std::runtime_error("unsupported command_bit");

   unsigned char drv_key[0x20] = {0}; //use max possible buffer
   if(iF00D->encrypt_key(key, key_size, drv_key) < 0)
      return -1;

   return cryptops->aes_cmac(src, dst, size, drv_key, key_size);
}

//##### NORMAL HASH FUNCTIONS #####

//this function is tested and works
int SceSblSsMgrForDriver_sceSblSsMgrHMACSHA1ForDriver(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* src, unsigned char* dst, int size, const unsigned char* key, unsigned char* iv, int mask_enable, int command_bit)
{
   if(iv != 0)
      throw std::runtime_error("unsupported iv");

   if(mask_enable != 1)
      throw std::runtime_error("unsupported mask_enable");

   if(command_bit != 0)
      throw std::runtime_error("unsupported command_bit");

   return cryptops->hmac_sha1(src, dst, size, key, 0x14);
}