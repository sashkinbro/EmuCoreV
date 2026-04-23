//this file is based on the code from:
//https://github.com/weaknespase/PkgDecrypt
//Thanks to:
//weaknespase
//St4rk

#include <zRIF/licdec.h>

#include <stdint.h>

#include <string>
#include <cstring>
#include <memory>
#include <vector>
#include <iostream>

#include <zRIF/keyflate.h>
#include <zRIF/rif.h>

#include <b64/cdecode.h>

#define MIN_KEY_SIZE 512
#define MAX_KEY_SIZE 2048

int decode_license_base64(const char* encoded, uint8_t* target)
{
   //First check encoded buffer
   int deflated = 0;
   for (const char *ptr = encoded; *ptr != 0; ptr++) 
   {
      if ( !( ( *ptr >= '0' && *ptr <= '9' ) || ( *ptr >= 'a' && *ptr <= 'f' ) || ( *ptr >= 'A' && *ptr <= 'F' ) ) ) 
      {
         deflated = 1;
         break;
      }
   }
   if (deflated) 
   {
      char buf[MAX_KEY_SIZE];
      base64_decodestate state;
      base64_init_decodestate(&state);
      size_t len = base64_decode_block(encoded, strlen( encoded ), buf, &state);

      len = inflateKey((unsigned char *)buf, len, target, MAX_KEY_SIZE);
      if(len < MIN_KEY_SIZE) 
      {
         return -1;
      }

      return 0;
   }
   else
   {
      fprintf( stderr, "License is not deflated.\n" );
      return -1;
   }
}

int get_license_type(char *lic) 
{
   if ( *( (uint16_t *) ( lic + 4 ) ) == 0 ) 
   {
      return 1;
   }
   return 0;
}

std::shared_ptr<SceNpDrmLicense> decode_license_np(std::string zRIF)
{
   int is_psm = 0;

   std::vector<uint8_t> vltext(MAX_KEY_SIZE, 0);

   int result = decode_license_base64(zRIF.c_str(), vltext.data());
   if(result < 0)
   {
      std::cout << "Provided license string doesn't encode valid key or zRIF." << std::endl;
      return std::shared_ptr<SceNpDrmLicense>();
   }
   else if(result > 0)
   {
      std::cout << "Unexpected result while decoding license." << std::endl;
      return std::shared_ptr<SceNpDrmLicense>();
   }
   else
   {
      //Check content id
      if(get_license_type((char *) vltext.data()) != is_psm)
      {
         std::cout << "Incorrect license type provided, " << (is_psm ? "PsmDrm" : "NpDrm") << " expected, but got " << (is_psm ? "NpDrm" : "PsmDrm") << ".\n" << std::endl;
         return std::shared_ptr<SceNpDrmLicense>();
      } 
      else 
      {
         SceNpDrmLicense* lic = (SceNpDrmLicense*)vltext.data();
         
         /*
         if(strcmp(lic->content_id, content_id) != 0)
         {
            std::cout << "Provided zRIF is not applicable to specified package." << std::endl << "Package content id: " << std::string(content_id) << std::endl << "License content id: " << std::string(lic->content_id) << std::endl;
            std::cout << "RIF file will not be written." << std::endl;
            return std::shared_ptr<SceNpDrmLicense>();
         } 
         */

         std::cout << "Successfully decompressed zRIF from provided license string." << std::endl;

         std::shared_ptr<SceNpDrmLicense> res(new SceNpDrmLicense());
         memcpy(res.get(), lic, sizeof(SceNpDrmLicense));
         return res;
      }
   }
}

std::shared_ptr<ScePsmDrmLicense> decode_license_psm(std::string zRIF)
{
   int is_psm = 1;

   std::vector<uint8_t> vltext(MAX_KEY_SIZE, 0);

   int result = decode_license_base64(zRIF.c_str(), vltext.data());
   if(result < 0)
   {
      std::cout << "Provided license string doesn't encode valid key or zRIF." << std::endl;
      return std::shared_ptr<ScePsmDrmLicense>();
   }
   else if(result > 0)
   {
      std::cout << "Unexpected result while decoding license." << std::endl;
      return std::shared_ptr<ScePsmDrmLicense>();
   }
   else
   {
      //Check content id
      if(get_license_type((char *) vltext.data()) != is_psm)
      {
         std::cout << "Incorrect license type provided, " << (is_psm ? "PsmDrm" : "NpDrm") << " expected, but got " << (is_psm ? "NpDrm" : "PsmDrm") << ".\n" << std::endl;
         return std::shared_ptr<ScePsmDrmLicense>();
      } 
      else 
      {
         ScePsmDrmLicense* lic = (ScePsmDrmLicense*)vltext.data();

         /*
         if(strcmp(lic->content_id, content_id) != 0)
         {
            std::cout << "Provided zRIF is not applicable to specified package." << std::endl << "Package content id: " << std::string(content_id) << std::endl << "License content id: " << std::string(lic->content_id) << std::endl;
            std::cout << "RIF file will not be written." << std::endl;
            return std::shared_ptr<ScePsmDrmLicense>();
         } 
         */

         std::cout << "Successfully decompressed zRIF from provided license string." << std::endl;

         std::shared_ptr<ScePsmDrmLicense> res(new ScePsmDrmLicense());
         memcpy(res.get(), lic, sizeof(ScePsmDrmLicense));
         return res;
      }
   }
}