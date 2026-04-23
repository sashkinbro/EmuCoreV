#include "IcvPrimitives.h"
#include <cstring>
#include "SceKernelUtilsForDriver.h"

int icv_set_hmac_sw(std::shared_ptr<ICryptoOperations> cryptops, unsigned char *dst, const unsigned char *key, const unsigned char *src, int size)
{
   SceKernelUtilsForDriver_sceHmacSha1DigestForDriver(cryptops, key, 0x14, src, size, dst);
   return 0;
}

int icv_set_sw(std::shared_ptr<ICryptoOperations> cryptops, unsigned char *dst, const unsigned  char *src, int size)
{
   SceKernelUtilsForDriver_sceSha1DigestForDriver(cryptops, src, size, dst); // calculate digest of klicensee
   return 0;
}

int icv_contract(std::shared_ptr<ICryptoOperations> cryptops, unsigned char *result, const unsigned char *left_hash, const unsigned char *right_hash)
{
   unsigned char combo[0x28] = {0};

   memcpy(combo, left_hash, 0x14);
   memcpy(combo + 0x14, right_hash, 0x14);

   SceKernelUtilsForDriver_sceSha1DigestForDriver(cryptops, combo, 0x28, result);
   return 0;
}