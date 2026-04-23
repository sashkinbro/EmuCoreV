#pragma once

#include <iostream>

class IF00DKeyEncryptor
{
public:
   virtual ~IF00DKeyEncryptor(){}

public:
   virtual int encrypt_key(const unsigned char* key, int key_size, unsigned char* drv_key) = 0;

   virtual void print_cache(std::ostream& os, std::string sep = "\t") const = 0;
};