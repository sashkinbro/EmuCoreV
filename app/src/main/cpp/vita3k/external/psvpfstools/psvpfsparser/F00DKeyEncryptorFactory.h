#pragma once

#include <string>
#include <memory>

#include "IF00DKeyEncryptor.h"
#include "ICryptoOperations.h"

enum class F00DEncryptorTypes
{
   file,
   native
};

class F00DKeyEncryptorFactory
{
public:
   template<typename Targ>
   static std::shared_ptr<IF00DKeyEncryptor> create(F00DEncryptorTypes type, Targ arg);
};

template<>
std::shared_ptr<IF00DKeyEncryptor> F00DKeyEncryptorFactory::create<std::string>(F00DEncryptorTypes type, std::string arg);

template<>
std::shared_ptr<IF00DKeyEncryptor> F00DKeyEncryptorFactory::create<std::shared_ptr<ICryptoOperations> >(F00DEncryptorTypes type, std::shared_ptr<ICryptoOperations> arg);