#include "F00DKeyEncryptorFactory.h"

#include "F00DFileKeyEncryptor.h"
#include "F00DNativeKeyEncryptor.h"

template<>
std::shared_ptr<IF00DKeyEncryptor> F00DKeyEncryptorFactory::create<std::string>(F00DEncryptorTypes type, std::string arg)
{
   switch(type)
   {
   case F00DEncryptorTypes::file:
      return std::make_shared<F00DFileKeyEncryptor>(arg);
   default:
      throw std::runtime_error("unexpected F00DEncryptorTypes value");
   }
}

template<>
std::shared_ptr<IF00DKeyEncryptor> F00DKeyEncryptorFactory::create<std::shared_ptr<ICryptoOperations> >(F00DEncryptorTypes type, std::shared_ptr<ICryptoOperations> arg)
{
   switch(type)
   {
   case F00DEncryptorTypes::native:
      return std::make_shared<F00DNativeKeyEncryptor>(arg);
   default:
      throw std::runtime_error("unexpected F00DEncryptorTypes value");
   }
}