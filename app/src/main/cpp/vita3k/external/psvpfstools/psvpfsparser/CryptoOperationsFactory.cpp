#include <stdexcept>

#include "CryptoOperationsFactory.h"
#include "OpenSSLCryptoOperations.h"

std::shared_ptr<ICryptoOperations> CryptoOperationsFactory::create(CryptoOperationsTypes type)
{
   switch(type)
   {
   case CryptoOperationsTypes::openssl:
      return std::make_shared<OpenSSLCryptoOperations>();
   default:
      throw std::runtime_error("unexpected CryptoOperationsTypes value");
   }
}
