#include "PfsKeyGenerator.h"

#include <string>
#include <cstring>
#include <stdexcept>

#include "PfsKeys.h"
#include "IcvPrimitives.h"
#include "PfsCryptEngine.h"
#include "SecretGenerator.h"
#include "FlagOperations.h"

//[TESTED]
//this function can be used both for gamedata and savedata
int generate_enckeys(std::shared_ptr<ICryptoOperations> cryptops, unsigned char* dec_key, unsigned char* tweak_enc_key, const unsigned char* klicensee, std::uint32_t icv_salt)
{
   int saltin[2] = {0};
   unsigned char base0[0x14] = {0};
   unsigned char base1[0x14] = {0};
   
   unsigned char drvkey[0x14] = {0};

   icv_set_sw(cryptops, base0, klicensee, 0x10); //calculate hash of klicensee

   saltin[0] = icv_salt;

   // derive key 0

   saltin[1] = 1;
   
   icv_set_sw(cryptops, base1, (unsigned char *)saltin, 8); //calculate hash of salt 0

   icv_contract(cryptops, drvkey, base0, base1); //calculate hash from combination of salt 0 hash and klicensee hash

   memcpy(dec_key, drvkey, 0x10);  //copy derived key

   // derive key 1
   
   saltin[1] = 2;

   icv_set_sw(cryptops, base1, (unsigned char*)saltin, 8); //calculate hash of salt 1

   icv_contract(cryptops, drvkey, base0, base1); //calculate hash from combination of salt 1 hash and klicensee hash

   memcpy(tweak_enc_key, drvkey, 0x10); //copy derived key

   return 0;
}

//[TESTED both branches]
//this function is used only for gamedata with icv_version <= 1
//files_salt is not empty starting from FILES_EXPECTED_VERSION_4
int gen_iv(std::shared_ptr<ICryptoOperations> cryptops, unsigned char* tweak_enc_key, std::uint32_t files_salt, std::uint32_t icv_salt)
{
   unsigned char drvkey[0x14] = {0};

   if(files_salt == 0)
   {
      int saltin0[1] = {0};
      saltin0[0] = icv_salt;

      icv_set_hmac_sw(cryptops, drvkey, hmac_key0, (unsigned char*)saltin0, 4); // derive key with one salt
   }
   else
   {
      int saltin1[2] = {0};
      saltin1[0] = files_salt;
      saltin1[1] = icv_salt;
      
      icv_set_hmac_sw(cryptops, drvkey, hmac_key0, (unsigned char*)saltin1, 8); // derive key with two salts
   }

   memcpy(tweak_enc_key, drvkey, 0x10); //copy derived key

   return 0;
}

//---------------------

//[TESTED]
//this function is used for savedata
int scePfsUtilGetSDKeys(std::shared_ptr<ICryptoOperations> cryptops, unsigned char* dec_key, unsigned char* tweak_enc_key, const unsigned char* klicensee, std::uint32_t files_salt, std::uint32_t icv_salt)
{
   //files_salt is ignored
   return generate_enckeys(cryptops, dec_key, tweak_enc_key, klicensee, icv_salt);
}

//[TESTED one branch]
//this function is used for gamedata with icv_version <= 1
int scePfsUtilGetGDKeys(std::shared_ptr<ICryptoOperations> cryptops, unsigned char* dec_key, unsigned char* tweak_enc_key, const unsigned char* klicensee, std::uint32_t files_salt, std::uint16_t crypto_engine_flag, std::uint32_t icv_salt)
{
   if(crypto_engine_flag & CRYPTO_ENGINE_CRYPTO_USE_KEYGEN)
   {
      memcpy(dec_key, klicensee, 0x10);

      return gen_iv(cryptops, tweak_enc_key, files_salt, icv_salt);
   }
   else
   {
      throw std::runtime_error("Untested branch in scePfsUtilGetGDKeys");

      return generate_enckeys(cryptops, dec_key, tweak_enc_key, klicensee, icv_salt);
   }
}

//[TESTED]
//this function is used for gamedata with icv_version > 1
int scePfsUtilGetGDKeys2(std::shared_ptr<ICryptoOperations> cryptops, unsigned char* dec_key, unsigned char* tweak_enc_key, const unsigned char* klicensee, const unsigned char* dbseed, std::uint32_t dbseed_len)
{
   unsigned char drvkey[0x14] = {0};

   icv_set_hmac_sw(cryptops, drvkey, hmac_key0, dbseed, dbseed_len);

   memcpy(dec_key, klicensee, 0x10);

   memcpy(tweak_enc_key, drvkey, 0x10);

   return 0;
}

//---------------------

//[TESTED]
//this function is used to derive keys for gamedata and savedata
int setup_crypt_packet_keys(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, CryptEngineData* data, const derive_keys_ctx* drv_ctx)
{
   if(is_gamedata(data->mode_index))
   {
      if(has_dbseed(drv_ctx->db_type, drv_ctx->icv_version))
      {  
         // only ro db with version > 1 
         scePfsUtilGetGDKeys2(cryptops, data->dec_key, data->tweak_enc_key, data->klicensee, drv_ctx->dbseed, 0x14);
      }
      else
      {
         scePfsUtilGetGDKeys(cryptops, data->dec_key, data->tweak_enc_key, data->klicensee, data->files_salt, data->crypto_engine_flag, data->icv_salt);
      }
   }
   else
   {
      scePfsUtilGetSDKeys(cryptops, data->dec_key, data->tweak_enc_key, data->klicensee, data->files_salt, data->icv_salt);
   }

   return scePfsUtilGetSecret(cryptops, iF00D, data->secret, data->klicensee, data->files_salt, data->crypto_engine_flag, data->icv_salt, data->key_id);
}