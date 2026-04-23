#pragma once

#include <memory>

#include "IF00DKeyEncryptor.h"
#include "ICryptoOperations.h"

#include "FilesDbParser.h"
#include "UnicvDbParser.h"
#include "PfsPageMapper.h"

class PfsFilesystem
{
private:
   std::shared_ptr<ICryptoOperations> m_cryptops;
   std::shared_ptr<IF00DKeyEncryptor> m_iF00D;
   std::ostream& m_output;
   unsigned char m_klicensee[0x10];
   const psvpfs::path& m_titleIdPath;

private:
   std::unique_ptr<FilesDbParser> m_filesDbParser;
   std::unique_ptr<UnicvDbParser> m_unicvDbParser;
   std::unique_ptr<PfsPageMapper> m_pageMapper;

public:
   PfsFilesystem(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, std::ostream& output,
                 const unsigned char* klicensee, const psvpfs::path& titleIdPath);

public:
   int mount();

   int decrypt_files(const psvpfs::path& destTitleIdPath) const;
};
