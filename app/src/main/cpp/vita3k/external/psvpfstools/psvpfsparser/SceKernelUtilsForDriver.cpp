#include "SceKernelUtilsForDriver.h"

//##### SW HASH FUNCTIONS #####

int SceKernelUtilsForDriver_sceSha1DigestForDriver(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char *source, int size, unsigned char result[0x14])
{
   cryptops->sha1(source, result, size);
   return 0;
}

int SceKernelUtilsForDriver_sceHmacSha1DigestForDriver(std::shared_ptr<ICryptoOperations> cryptops, const unsigned char* key, int key_len, const unsigned char* data, int data_len, unsigned char digest[0x14])
{
   cryptops->hmac_sha1(data, digest, data_len, key, key_len);
   return 0;
}