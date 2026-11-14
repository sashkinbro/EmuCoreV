#include "PfsCryptEngineSelectors.h"

#include <cstdint>
#include <string>
#include <cstring>

#include "PfsCryptEngineBase.h"
#include "FlagOperations.h"

//this macro unwraps 64 bit sector number into byte array
#define UINT64_TO_BYTEARRAY(x, y)                                              \
   { (y)[7] = (unsigned char)((x) >> 56); (y)[6] = (unsigned char)((x) >> 48); \
     (y)[5] = (unsigned char)((x) >> 40); (y)[4] = (unsigned char)((x) >> 32); \
     (y)[3] = (unsigned char)((x) >> 24); (y)[2] = (unsigned char)((x) >> 16); \
     (y)[1] = (unsigned char)((x) >> 8) ; (y)[0] = (unsigned char)(x); }

//this function increments byte array of size 0x10
int UINT128_BYTEARRAY_INC(unsigned char iv[0x10])
{
   for(int i = 0; i < 0x10; i++)
   {
      if(iv[i] == 0xFF)
      {
         iv[i] = 0;
      }
      else
      {
         iv[i] = iv[i] + 1;
         break;
      }
   }

   return 0;
}

//#### GROUP 1 (possible keygen aes-cbc-cts dec/aes-cbc-cts enc) ####
//#### GROUP 2 (possible keygen aes-cmac-cts dec/aes-cmac-cts enc) (technically there is no dec/enc - this is pair of same functions since cmac) ####

unsigned char g_cmac_buffer[0x10] = {0};

int pfs_decrypt_unicv(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, const unsigned char* key, const unsigned char* tweak_mask, std::uint64_t tweak_key, std::uint32_t size, std::uint32_t block_size, const unsigned char* src, unsigned char* dst, std::uint16_t crypto_engine_flag, std::uint16_t key_id)
{
   unsigned char tweak[0x10] = {0};

   UINT64_TO_BYTEARRAY(tweak_key, tweak); //convert std::uint64_t tweak to byte array

   memset(tweak + 8, 0, 8); //set upper tweak to 0

   for(int i = 0; i < 0x10; i++)
      tweak[i] = tweak[i] ^ tweak_mask[i]; // xor tweak with mask (kinda mimic tweak_enc_value in xts-aes)
  
   if(size != 0)
   {
      std::uint32_t offset = 0;
      std::uint32_t bytes_left = size;

      do
      {
         std::uint64_t tweak_key_ofst = tweak_key + offset;
         UINT64_TO_BYTEARRAY(tweak_key_ofst, tweak); // modify tweak (mimic xts-aes) by adding offset to the tweak

         memset(tweak + 8, 0, 8); //set upper tweak to 0

         for(int i = 0; i < 0x10; i++)
            tweak[i] = tweak[i] ^ tweak_mask[i]; // xor tweak with mask (kinda mimic tweak_enc_value in xts-aes)

         // select block_size if we did not yet reach tail of the data. 
         // or select bytes_left which will be the size of the tail in the end

         int size_arg = 0;
         if(block_size < bytes_left)
            size_arg = block_size;
         else
            size_arg = bytes_left;

         if(crypto_engine_flag & CRYPTO_ENGINE_CRYPTO_USE_KEYGEN)
         {
            if(crypto_engine_flag & CRYPTO_ENGINE_CRYPTO_USE_CMAC)
               AESCMACDecryptWithKeygen_base(cryptops, iF00D, key, tweak, size_arg, src + offset, g_cmac_buffer, key_id);
            else
               AESCBCDecryptWithKeygen_base(cryptops, iF00D, key, tweak, size_arg, src + offset, dst + offset, key_id); //cbc decrypt with tweak as iv
         }
         else
         {
            if(crypto_engine_flag & CRYPTO_ENGINE_CRYPTO_USE_CMAC)
               AESCMACDecrypt_base(cryptops, key, tweak, size_arg, src + offset, g_cmac_buffer);
            else
               AESCBCDecrypt_base(cryptops, key, tweak, size_arg, src + offset, dst + offset); //cbc decrypt with tweak as iv
         }

         offset = offset + block_size;
         bytes_left = bytes_left - block_size;
      }
      while(size > offset);
   }

   //copy result to dest buffer since cmac functions operate with global buffer

   if(crypto_engine_flag & CRYPTO_ENGINE_CRYPTO_USE_CMAC)
   {
      if(dst != src)
      {
         memcpy(dst, src, size);
      }
   }

   return 0;
}

int pfs_encrypt_unicv(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, const unsigned char* key, const unsigned char* tweak_mask, std::uint64_t tweak_key, std::uint32_t size, std::uint32_t block_size, const unsigned char* src, unsigned char* dst, std::uint16_t crypto_engine_flag, std::uint16_t key_id)
{
   unsigned char tweak[0x10] = {0};

   UINT64_TO_BYTEARRAY(tweak_key, tweak); //convert std::uint64_t tweak to byte array

   memset(tweak + 8, 0, 8); //set upper tweak to 0

   for(int i = 0; i < 0x10; i++)
      tweak[i] = tweak[i] ^ tweak_mask[i]; // xor tweak with mask (kinda mimic tweak_enc_value in xts-aes)

   if(size != 0)
   {
      std::uint32_t offset = 0;
      std::uint32_t bytes_left = size;

      do
      {         
         std::uint64_t tweak_key_ofst = tweak_key + offset;
         UINT64_TO_BYTEARRAY(tweak_key_ofst, tweak); // modify tweak (mimic xts-aes) by adding offset to the tweak

         memset(tweak + 8, 0, 8); //set upper tweak to 0

         for(int i = 0; i < 0x10; i++)
            tweak[i] = tweak[i] ^ tweak_mask[i]; // xor tweak with mask (kinda mimic tweak_enc_value in xts-aes)

         // select block_size if we did not yet reach tail of the data. 
         // or select bytes_left which will be the size of the tail in the end

         int size_arg = 0;
         if(block_size < bytes_left)
            size_arg = block_size;
         else
            size_arg = bytes_left;

         if(crypto_engine_flag & CRYPTO_ENGINE_CRYPTO_USE_KEYGEN)
         {
            if(crypto_engine_flag & CRYPTO_ENGINE_CRYPTO_USE_CMAC)
               AESCMACEncryptWithKeygen_base(cryptops, iF00D, key, tweak, size_arg, src + offset, g_cmac_buffer, key_id);
            else
               AESCBCEncryptWithKeygen_base(cryptops, iF00D, key, tweak, size_arg, src + offset, dst + offset, key_id); //cbc encrypt with tweak as iv
         }
         else
         {
            if(crypto_engine_flag & CRYPTO_ENGINE_CRYPTO_USE_CMAC)
               AESCMACEncrypt_base(cryptops, key, tweak, size_arg, src + offset, g_cmac_buffer);
            else
               AESCBCEncrypt_base(cryptops, key, tweak, size_arg, src + offset, dst + offset); //cbc encrypt with tweak as iv
         }

         offset = offset + block_size;
         bytes_left = bytes_left - block_size;
      }
      while(size > offset);
   }

   //copy result to dest buffer since cmac functions operate with global buffer

   if(crypto_engine_flag & CRYPTO_ENGINE_CRYPTO_USE_CMAC)
   {
      if(dst != src)
      {
         memcpy(dst, src, size);
      }
   }

   return 0;
}

//#### GROUP 3 (no keygen xts-aes dec/xts-aes enc) ####
//#### GROUP 4 (no keygen xts-cmac dec/xts-cmac enc) (technically there is no dec/enc - this is pair of same functions since cmac) ####

//looks like this method can decrypt multiple blocks when size > block_size
//assuming that it adds 1 to tweak_key when decrypting each next block
//in practice though it looks like this method is only used to decrypt single block

int pfs_decrypt_icv(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* key, const unsigned char* tweak_enc_key, std::uint32_t keysize, std::uint64_t tweak_key, std::uint32_t size, std::uint32_t block_size, const unsigned char* src, unsigned char* dst, std::uint16_t crypto_engine_flag)
{
   unsigned char tweak[0x10] = {0};

   if((block_size <= 0xF) || (size <= 0xF)) //block_size and size should be at least one block
      return 0x80140609;

   UINT64_TO_BYTEARRAY(tweak_key, tweak); //convert std::uint64_t tweak to byte array

   memset(tweak + 8, 0, 8); //set upper tweak to 0

   std::uint32_t offset = 0;
   std::uint32_t bytes_left = size;

   do
   {
      // select block_size if we did not yet reach tail of the data. 
      // or select bytes_left which will be the size of the tail in the end

      int size_arg = 0;
      if(block_size < bytes_left)
         size_arg = block_size;
      else
         size_arg = bytes_left;

      int result0 = 0;
      if(crypto_engine_flag & CRYPTO_ENGINE_CRYPTO_USE_CMAC)
         result0 = XTSCMACDecrypt_base(cryptops, tweak, key, tweak_enc_key, keysize, size_arg, src + offset, g_cmac_buffer);
      else
         result0 = XTSAESDecrypt_base(cryptops, tweak, key, tweak_enc_key, keysize, size_arg, src + offset, dst + offset); //xts-aes decrypt

      if(result0 != 0)
         return result0;

      UINT128_BYTEARRAY_INC(tweak); // increment tweak by 1 (not relevant ? since this function is only used to decrypt single block of data)

      offset = offset + block_size;
      bytes_left = bytes_left - block_size;
   }
   while(size > offset);

   //copy result to dest buffer since cmac functions operate with global buffer

   if(crypto_engine_flag & CRYPTO_ENGINE_CRYPTO_USE_CMAC)
   {
      if(dst != src)
      {
         memcpy(dst, src, size);
      }
   }

   return 0;
}

//looks like this method can encrypt multiple blocks when size > block_size
//assuming that it adds 1 to tweak_key when encrypting each next block
//in practice though it looks like this method is only used to decrypt single block

int pfs_encrypt_icv(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* key, const unsigned char* tweak_enc_key, std::uint32_t keysize, std::uint64_t tweak_key, std::uint32_t size, std::uint32_t block_size, const unsigned char* src, unsigned char* dst, std::uint16_t crypto_engine_flag)
{
   unsigned char tweak[0x10] = {0};

   if((block_size <= 0xF) || (size <= 0xF)) //block_size and size should be at least one block
      return 0x80140609;

   UINT64_TO_BYTEARRAY(tweak_key, tweak); //block_size and size should be at least one block

   memset(tweak + 8, 0, 8); //set upper tweak to 0
   
   std::uint32_t offset = 0;
   std::uint32_t bytes_left = size;
   
   do
   {
      // select block_size if we did not yet reach tail of the data. 
      // or select bytes_left which will be the size of the tail in the end

      int size_arg = 0;
      if(block_size < bytes_left)
         size_arg = block_size;
      else
         size_arg = bytes_left;

      int result0 = 0;
      if(crypto_engine_flag & CRYPTO_ENGINE_CRYPTO_USE_CMAC)
         result0 = XTSCMACEncrypt_base(cryptops, tweak, key, tweak_enc_key, keysize, size_arg, src + offset, g_cmac_buffer);
      else
         result0 = XTSAESEncrypt_base(cryptops, tweak, key, tweak_enc_key, keysize, size_arg, src + offset, dst + offset); //xts-aes encrypt
      
      if(result0 != 0)
         return result0;

      UINT128_BYTEARRAY_INC(tweak); // increment tweak by 1 (not relevant ? since this function is only used to decrypt single block of data)

      offset = offset + block_size;
      bytes_left = bytes_left - block_size;
   }
   while(size > offset);

   //copy result to dest buffer since cmac functions operate with global buffer

   if(crypto_engine_flag & CRYPTO_ENGINE_CRYPTO_USE_CMAC)
   {
      if(dst != src)
      {
         memcpy(dst, src, size);
      }
   }

   return 0;
}