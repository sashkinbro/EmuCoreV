#include "PfsCryptEngine.h"

#include <string>
#include <cstring>
#include <stdexcept>

#include "SceSblSsMgrForDriver.h"
#include "SceKernelUtilsForDriver.h"
#include "PfsCryptEngineBase.h"
#include "PfsCryptEngineSelectors.h"

//----------------------

bool is_noicv(CryptEngineWorkCtx* crypt_ctx)
{
   //check that it is not a directory and does have icv
   return (crypt_ctx->subctx->data->fs_attr & ATTR_NICV) || (crypt_ctx->subctx->data->fs_attr & ATTR_DIR);
}

//not sure how to call
bool is_crypto_engine_unk(CryptEngineWorkCtx* crypt_ctx)
{
   return (crypt_ctx->subctx->data->crypto_engine_flag & (CRYPTO_ENGINE_CRYPTO_USE_CMAC | CRYPTO_ENGINE_SKIP_DECRYPT)) == (CRYPTO_ENGINE_CRYPTO_USE_CMAC | CRYPTO_ENGINE_SKIP_DECRYPT);
}

//not sure how to call
bool is_verify_skip(CryptEngineWorkCtx* crypt_ctx)
{
   return (crypt_ctx->subctx->data->crypto_engine_flag & CRYPTO_ENGINE_SKIP_VERIFY) > 0;
}

bool is_fake(CryptEngineWorkCtx* crypt_ctx)
{
   return !(crypt_ctx->subctx->data->crypto_engine_flag & CRYPTO_ENGINE_THROW_ERROR) && (crypt_ctx->subctx->data->crypto_engine_flag & CRYPTO_ENGINE_CRYPTO_USE_CMAC);
}

bool is_noenc(CryptEngineWorkCtx* crypt_ctx)
{
   //check that it is not a directory and is encrypted
   return (crypt_ctx->subctx->data->fs_attr & ATTR_NENC) || (crypt_ctx->subctx->data->fs_attr & ATTR_DIR);
}

//----------------------

int icv_gd_verify(std::shared_ptr<ICryptoOperations> cryptops, CryptEngineWorkCtx* crypt_ctx, unsigned char* source)
{
   if(is_crypto_engine_unk(crypt_ctx))
      return 0;

   std::uint32_t tweak_key = crypt_ctx->subctx->sector_base;
                  
   if(crypt_ctx->subctx->nBlocks != 0)
   {
      std::uint32_t counter = 0;
      std::uint32_t bytes_left = crypt_ctx->subctx->data->block_size * (crypt_ctx->subctx->nBlocks - 1) + (crypt_ctx->subctx->tail_size);

      unsigned char* source_base = source;
      unsigned char* signatures_base = crypt_ctx->subctx->signature_table;

      unsigned char digest[0x14] = {0};
      unsigned char bytes14[0x14] = {0};

      do
      {
         //calculate ICV
         SceKernelUtilsForDriver_sceHmacSha1DigestForDriver(cryptops, crypt_ctx->subctx->data->secret, 0x14, (unsigned char*)&tweak_key, 4, digest);

         int size_arg = (crypt_ctx->subctx->data->block_size < bytes_left) ? crypt_ctx->subctx->data->block_size : bytes_left;
         SceSblSsMgrForDriver_sceSblSsMgrHMACSHA1ForDriver(cryptops, source_base, bytes14, size_arg, digest, 0, 1, 0);
                        
         //compare ICVs
         int ver_res = memcmp(signatures_base, bytes14, 0x14);
                        
         //if verify is not successful and flag is not specified
         if((ver_res != 0) && !is_fake(crypt_ctx))
         {
            crypt_ctx->error = 0x80140F02;
            return -1;
         }
                        
         counter = counter + 1;
         bytes_left = bytes_left - crypt_ctx->subctx->data->block_size;

         source_base = source_base + crypt_ctx->subctx->data->block_size;
         signatures_base = signatures_base + 0x14;

         tweak_key = tweak_key + 1;
      }
      while(counter != crypt_ctx->subctx->nBlocks);
   }
   
   return 0;
}

int icv_sd_verify(std::shared_ptr<ICryptoOperations> cryptops, CryptEngineWorkCtx* crypt_ctx, unsigned char* source)
{
   if(is_crypto_engine_unk(crypt_ctx))
      return 0;

   if(crypt_ctx->subctx->nBlocks != 0)
   {
      std::uint32_t counter = 0;

      unsigned char* source_base = source;
      unsigned char* signatures_base = crypt_ctx->subctx->signature_table;
                     
      unsigned char bytes14[0x14] = {0};

      do
      {
         //calculate ICV
         int size_arg = crypt_ctx->subctx->data->block_size;
         SceSblSsMgrForDriver_sceSblSsMgrHMACSHA1ForDriver(cryptops, source_base, bytes14, size_arg, crypt_ctx->subctx->data->secret, 0, 1, 0);
                     
         //compare ICVs
         int ver_res = memcmp(signatures_base, bytes14, 0x14);

         //if verify is not successful and flag is not specified
         if((ver_res != 0) && !is_fake(crypt_ctx))
         {
            crypt_ctx->error = 0x80140F02;
            return -1;
         }
                        
         counter = counter + 1;

         source_base = source_base + crypt_ctx->subctx->data->block_size;
         signatures_base = signatures_base + 0x14;
      }
      while(counter != crypt_ctx->subctx->nBlocks);
   }
   
   return 0;
}

//[TESTED both branches]
void verify_icv(std::shared_ptr<ICryptoOperations> cryptops, CryptEngineWorkCtx* crypt_ctx, std::uint16_t mode_index, unsigned char* source)
{
   if(is_noicv(crypt_ctx))
      return;

   if(is_verify_skip(crypt_ctx))
      return;

   //check ICV table

   if(is_gamedata(mode_index))
   {
      icv_gd_verify(cryptops, crypt_ctx, source);
   }
   else
   {
      icv_sd_verify(cryptops, crypt_ctx, source);
   }
}

//----------------------

int cbc_dec(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, CryptEngineWorkCtx* crypt_ctx, unsigned char* buffer)
{
   // variable mapping

   unsigned const char* key = crypt_ctx->subctx->data->dec_key;
   unsigned const char* tweak_enc_key = crypt_ctx->subctx->data->tweak_enc_key;

   //remove encryption layer

   int offset = 0;
   std::uint32_t counter = 0;

   std::uint64_t tweak_key = crypt_ctx->subctx->data->block_size * crypt_ctx->subctx->sector_base;

   std::uint32_t bytes_left = crypt_ctx->subctx->data->block_size * (crypt_ctx->subctx->nBlocks - 1) + (crypt_ctx->subctx->tail_size);
   
   do
   {
      int size_arg = ((crypt_ctx->subctx->data->block_size < bytes_left) ? crypt_ctx->subctx->data->block_size : bytes_left);
      pfs_decrypt_unicv(cryptops, iF00D, key, tweak_enc_key, tweak_key + offset, size_arg, crypt_ctx->subctx->data->block_size, buffer + offset, buffer + offset, crypt_ctx->subctx->data->crypto_engine_flag, crypt_ctx->subctx->data->key_id);

      bytes_left = bytes_left - crypt_ctx->subctx->data->block_size;
      offset = offset + crypt_ctx->subctx->data->block_size;
      counter = counter + 1;
   }
   while(counter != crypt_ctx->subctx->nBlocks);

   return 0;
}

int xts_dec(std::shared_ptr<ICryptoOperations> cryptops, CryptEngineWorkCtx* crypt_ctx, unsigned char* buffer)
{
   // variable mapping

   unsigned const char* key = crypt_ctx->subctx->data->dec_key;
   unsigned const char* tweak_enc_key = crypt_ctx->subctx->data->tweak_enc_key;

   //remove encryption layer

   int offset = 0;
   std::uint32_t counter = 0;

   std::uint64_t tweak_key = crypt_ctx->subctx->data->block_size * crypt_ctx->subctx->sector_base;

   do
   {
      pfs_decrypt_icv(cryptops, key, tweak_enc_key, 0x80, tweak_key + offset, crypt_ctx->subctx->data->block_size, crypt_ctx->subctx->data->block_size, buffer + offset, buffer + offset, crypt_ctx->subctx->data->crypto_engine_flag);

      counter = counter + 1;
      offset = offset + crypt_ctx->subctx->data->block_size;
   }
   while(counter != crypt_ctx->subctx->nBlocks);

   return 0;
}

//[TESTED both branches]
void decrypt_simple(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, CryptEngineWorkCtx* crypt_ctx, std::uint16_t mode_index, unsigned char* buffer)
{
   if(is_noenc(crypt_ctx))
   {
      crypt_ctx->error = 0;
      return;
   }

   if(is_crypto_engine_unk(crypt_ctx))
   {
      crypt_ctx->error = 0;
      return;
   }

   if(crypt_ctx->subctx->nBlocks == 0)
   {
      crypt_ctx->error = 0;
      return;
   }

   if(is_gamedata(mode_index))
   {
      cbc_dec(cryptops, iF00D, crypt_ctx, buffer);
   }
   else
   {
      xts_dec(cryptops, crypt_ctx, buffer);
   }

   crypt_ctx->error = 0;
   return;
}

//----------------------

void decrypt_complex(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, CryptEngineWorkCtx* crypt_ctx, std::uint16_t mode_index, unsigned char* buffer)
{
   throw std::runtime_error("Untested decryption branch work_3_step1");

   // variable mapping

   unsigned const char* key = crypt_ctx->subctx->data->dec_key;
   unsigned const char* tweak_enc_key = crypt_ctx->subctx->data->tweak_enc_key;

   unsigned char* output_dst = crypt_ctx->subctx->work_buffer_ofst + ((crypt_ctx->subctx->data->block_size * crypt_ctx->subctx->nBlocksOffset) - crypt_ctx->subctx->dest_offset);
   unsigned char* output_src = buffer + (crypt_ctx->subctx->data->block_size * crypt_ctx->subctx->nBlocksOffset);
   int output_size = crypt_ctx->subctx->data->block_size * crypt_ctx->subctx->nBlocksTail;

   //decrypt data at offset - single block

   if(crypt_ctx->subctx->nBlocksOffset > 0)
   {
      //check that is not a directory and is encrypted
      if(((crypt_ctx->subctx->data->fs_attr & ATTR_NENC) == 0) && ((crypt_ctx->subctx->data->fs_attr & ATTR_DIR) == 0))
      {
         if((crypt_ctx->subctx->data->crypto_engine_flag & (CRYPTO_ENGINE_CRYPTO_USE_CMAC | CRYPTO_ENGINE_SKIP_DECRYPT)) != (CRYPTO_ENGINE_CRYPTO_USE_CMAC | CRYPTO_ENGINE_SKIP_DECRYPT))
         {
            std::uint64_t head_tweak_key = crypt_ctx->subctx->sector_base * crypt_ctx->subctx->data->block_size;

            if(!is_gamedata(mode_index))
            {
               pfs_decrypt_icv(cryptops, key, tweak_enc_key, 0x80, head_tweak_key, crypt_ctx->subctx->data->block_size, crypt_ctx->subctx->data->block_size, buffer, buffer, crypt_ctx->subctx->data->crypto_engine_flag);
            }
            else
            {
               pfs_decrypt_unicv(cryptops, iF00D, key, tweak_enc_key, head_tweak_key, crypt_ctx->subctx->data->block_size, crypt_ctx->subctx->data->block_size, buffer, buffer, crypt_ctx->subctx->data->crypto_engine_flag, crypt_ctx->subctx->data->key_id);
            }
         }
      }  
   }

   //copy to result if nBlocksTail + nBlocksOffset > nBlocks

   if(((crypt_ctx->subctx->nBlocksOffset + crypt_ctx->subctx->nBlocksTail) >= crypt_ctx->subctx->nBlocks))
   {
      if(output_src != output_dst)
         memcpy(output_dst, output_src, output_size);
      crypt_ctx->error = 0;
      return; // this should terminate crypto task (global exit)
   }

   //copy result if data is not encrypted

   if(crypt_ctx->subctx->data->fs_attr & ATTR_NENC)
   {   
      if(output_src != output_dst)
         memcpy(output_dst, output_src, output_size);
      crypt_ctx->error = 0;
      return; // this should terminate crypto task (global exit)
   }

   //decrypt tail data - single block
   
   if((crypt_ctx->subctx->data->fs_attr & ATTR_DIR) == 0)
   {   
      if((crypt_ctx->subctx->data->crypto_engine_flag & (CRYPTO_ENGINE_CRYPTO_USE_CMAC | CRYPTO_ENGINE_SKIP_DECRYPT)) != (CRYPTO_ENGINE_CRYPTO_USE_CMAC | CRYPTO_ENGINE_SKIP_DECRYPT))
      {
         std::uint64_t tail_tweak_key = crypt_ctx->subctx->data->block_size * (crypt_ctx->subctx->sector_base + (crypt_ctx->subctx->nBlocks - 1));
         unsigned char* tail_buffer = buffer + crypt_ctx->subctx->data->block_size * (crypt_ctx->subctx->nBlocks - 1);

         if(!is_gamedata(mode_index))
         {
            pfs_decrypt_icv(cryptops, key, tweak_enc_key, 0x80, tail_tweak_key, crypt_ctx->subctx->data->block_size, crypt_ctx->subctx->data->block_size, tail_buffer, tail_buffer, crypt_ctx->subctx->data->crypto_engine_flag);
         }
         else
         {
            int size_arg = (crypt_ctx->subctx->data->block_size <= crypt_ctx->subctx->tail_size) ? crypt_ctx->subctx->data->block_size : crypt_ctx->subctx->tail_size;
            pfs_decrypt_unicv(cryptops, iF00D, key, tweak_enc_key, tail_tweak_key, size_arg, crypt_ctx->subctx->data->block_size, tail_buffer, tail_buffer, crypt_ctx->subctx->data->crypto_engine_flag, crypt_ctx->subctx->data->key_id);
         }
      }
   }

   //copy result if data is dir
   
   if(crypt_ctx->subctx->data->fs_attr & ATTR_DIR)
   {
      if(output_src != output_dst)
         memcpy(output_dst, output_src, output_size);
      crypt_ctx->error = 0;
      return; // this should terminate crypto task (global exit)
   }

   //copy result if pmi flags are not correct

   if((crypt_ctx->subctx->data->crypto_engine_flag & (CRYPTO_ENGINE_CRYPTO_USE_CMAC | CRYPTO_ENGINE_SKIP_DECRYPT)) == (CRYPTO_ENGINE_CRYPTO_USE_CMAC | CRYPTO_ENGINE_SKIP_DECRYPT))
   {
      if(output_src != output_dst)
         memcpy(output_dst, output_src, output_size);
      crypt_ctx->error = 0;
      return; // this should terminate crypto task (global exit)
   }

   //exit if no blocks in tail

   if(crypt_ctx->subctx->nBlocksTail == 0)
   {
      crypt_ctx->error = 0;
      return; // this should terminate crypto task (global exit)
   }

   //decrypt main data - N blocks

   std::uint64_t tweak_key = crypt_ctx->subctx->data->block_size * (crypt_ctx->subctx->nBlocksOffset + crypt_ctx->subctx->sector_base);

   int offset = 0;
   std::uint32_t counter = 0;

   if(!is_gamedata(mode_index))
   {
      do
      {
         pfs_decrypt_icv(cryptops, key, tweak_enc_key, 0x80, tweak_key + offset, crypt_ctx->subctx->data->block_size, crypt_ctx->subctx->data->block_size, output_src + offset, output_dst + offset, crypt_ctx->subctx->data->crypto_engine_flag);

         offset = offset + crypt_ctx->subctx->data->block_size;
         counter = counter + 1;
      }
      while(counter != crypt_ctx->subctx->nBlocksTail);

      crypt_ctx->error = 0;
      return; // this should terminate crypto task (global exit)
   }
   else
   {
      std::uint32_t bytes_left = output_size;
      
      do
      {
         int size_arg = (crypt_ctx->subctx->data->block_size <= bytes_left) ? crypt_ctx->subctx->data->block_size : bytes_left;
         pfs_decrypt_unicv(cryptops, iF00D, key, tweak_enc_key, tweak_key + offset, size_arg, crypt_ctx->subctx->data->block_size, output_src + offset, output_dst + offset, crypt_ctx->subctx->data->crypto_engine_flag, crypt_ctx->subctx->data->key_id);

         offset = offset + crypt_ctx->subctx->data->block_size;
         bytes_left = bytes_left - crypt_ctx->subctx->data->block_size;
         counter = counter + 1;
      }
      while(counter != crypt_ctx->subctx->nBlocksTail);
   
      crypt_ctx->error = 0;
      return; // this should terminate crypto task (global exit)
   }
}

//----------------------

void crypt_for_read(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, CryptEngineWorkCtx* crypt_ctx)
{
   unsigned char* work_buffer;
   if(is_gamedata(crypt_ctx->subctx->data->mode_index))
      work_buffer = crypt_ctx->subctx->work_buffer1;
   else
      work_buffer = crypt_ctx->subctx->work_buffer0;

   //verifies icv table
   verify_icv(cryptops, crypt_ctx, crypt_ctx->subctx->data->mode_index, work_buffer);

   //check verification error
   if(crypt_ctx->error < 0)
      return;

   if(crypt_ctx->subctx->nBlocksTail == 0)
   {
      //single decryption loop - decrypts area of nBlocks blocks
      decrypt_simple(cryptops, iF00D, crypt_ctx, crypt_ctx->subctx->data->mode_index, work_buffer);
   }
   else
   {
      //two decryption calls and one decryption loop - looks like decrypts nBlocks of data from offset. not sure
      decrypt_complex(cryptops, iF00D, crypt_ctx, crypt_ctx->subctx->data->mode_index, work_buffer);
   }
}

void crypt_for_write(CryptEngineWorkCtx * crypt_ctx, CryptEngineSubctx* r10)
{
   throw std::runtime_error("Untested decryption branch crypt_engine_work_2_4");
}

void pfs_decrypt(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, CryptEngineWorkCtx *work_ctx)
{
   switch(work_ctx->subctx->opt_code)
   {
   case CRYPT_ENGINE_WRITE:
      crypt_for_write(work_ctx, work_ctx->subctx);
      break;
   case CRYPT_ENGINE_READ:
      crypt_for_read(cryptops, iF00D, work_ctx);
      break;
   case CRYPT_ENGINE_TRUNC:
      crypt_for_write(work_ctx, work_ctx->subctx);
      break;
   default:
      break;
   }
}
