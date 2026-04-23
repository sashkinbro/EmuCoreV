#include "SecretGenerator.h"

#include <cstdint>
#include <string>
#include <stdexcept>
#include <cstring>

#include "PfsKeys.h"
#include "IcvPrimitives.h"

#include "PfsCryptEngineBase.h"
#include "FlagOperations.h"

//[TESTED both branches]
//this function is used only for gamedata
//files_salt is not empty starting from FILES_EXPECTED_VERSION_4
int gen_secret(std::shared_ptr<ICryptoOperations> cryptops, unsigned char* combo, std::uint32_t files_salt, std::uint32_t icv_salt)
{
   unsigned char base[0x14] = {0};

   if(files_salt == 0)
   {
      int saltin0[1] = {0};
      saltin0[0] = icv_salt;
      icv_set_hmac_sw(cryptops, base, hmac_key1, (unsigned char*)saltin0, 4); // derive base with one salt
   }
   else
   {
      int saltin1[2] = {0};
      saltin1[0] = files_salt;
      saltin1[1] = icv_salt;
      icv_set_hmac_sw(cryptops, base, hmac_key1, (unsigned char*)saltin1, 8); // derive base with two salts
   }

   memcpy(combo, base, 0x14); // calculated digest will be src data

   return 0;
}

//[TESTED]
//this function is used only for gamedata
int generate_secret_np(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, unsigned char* secret, const unsigned char* klicensee, std::uint32_t files_salt, std::uint32_t icv_salt, std::uint16_t key_id)
{
   unsigned char drvkey[0x14] = {0};
   unsigned char iv[0x10] = {0};
   unsigned char combo[0x14] = {0};

   gen_secret(cryptops, combo, files_salt, icv_salt);

   memcpy(iv, iv0, 0x10); //initialize iv

   AESCBCEncryptWithKeygen_base(cryptops, iF00D, klicensee, iv, 0x14, combo, drvkey, key_id);

   memcpy(secret, drvkey, 0x14); // copy derived key
   
   return 0;
}

//[TESTED]
//this function is used only for savedata
int generate_secret(std::shared_ptr<ICryptoOperations> cryptops, unsigned char* secret, const unsigned char* klicensee,  std::uint32_t icv_salt)
{
   int saltin[2] = {0};
   unsigned char base0[0x14] = {0};
   unsigned char base1[0x14] = {0};
   unsigned char drvkey[0x14] = {0};

   icv_set_sw(cryptops, base0, klicensee, 0x10); // calculate digest of klicensee

   saltin[0] = 0xA;
   saltin[1] = icv_salt;

   icv_set_sw(cryptops, base1, (unsigned char*)saltin, 8); // calculate digest of salt

   icv_contract(cryptops, drvkey, base0, base1); // calculate digest of combination of klicensee and salt digests
               
   memcpy(secret, drvkey, 0x14); // copy derived key

   return 0;
}

//[TESTED 2 branches]
//this function is used to derive secret for gamedata and savedata
int scePfsUtilGetSecret(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, unsigned char* secret, const unsigned char* klicensee, std::uint32_t files_salt, std::uint16_t crypto_engine_flag, std::uint32_t icv_salt, std::uint16_t key_id)
{
   if(crypto_engine_flag & CRYPTO_ENGINE_CRYPTO_USE_CMAC)
   {
      throw std::runtime_error("Untested branch in scePfsUtilGetSecret");

      memset(secret, 0, 0x14);
      return 0;
   }
   else if(crypto_engine_flag & CRYPTO_ENGINE_CRYPTO_USE_KEYGEN)
   {
      return generate_secret_np(cryptops, iF00D, secret, klicensee, files_salt, icv_salt, key_id);
   }
   else
   {
      return generate_secret(cryptops, secret, klicensee, icv_salt);
   }
}