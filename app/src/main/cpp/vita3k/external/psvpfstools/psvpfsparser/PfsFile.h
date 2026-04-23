#pragma once

#include <memory>

#include "IF00DKeyEncryptor.h"
#include "ICryptoOperations.h"

#include "Utils.h"

#include "FilesDbParser.h"
#include "UnicvDbParser.h"

#include "PfsCryptEngine.h"

class PfsFile
{
private:
   std::shared_ptr<ICryptoOperations> m_cryptops;
   std::shared_ptr<IF00DKeyEncryptor> m_iF00D;
   std::ostream& m_output;
   unsigned char m_klicensee[0x10];
   const psvpfs::path& m_titleIdPath;

private:
   const sce_ng_pfs_file_t& m_file;
   const sce_junction& m_filepath;
   const sce_ng_pfs_header_t& m_ngpfs;
   std::shared_ptr<sce_iftbl_base_t> m_table;

private:
   mutable CryptEngineData m_data;
   mutable CryptEngineSubctx m_sub_ctx;
   mutable std::vector<std::uint8_t> m_signatureTable;

public:
   PfsFile(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, std::ostream& output,
           const unsigned char* klicensee, const psvpfs::path& titleIdPath,
           const sce_ng_pfs_file_t& file, const sce_junction& filepath, const sce_ng_pfs_header_t& ngpfs, std::shared_ptr<sce_iftbl_base_t> table);

private:
   int init_crypt_ctx(CryptEngineWorkCtx* work_ctx, sig_tbl_t& block, std::uint32_t sector_base, std::uint32_t tail_size, unsigned char* source) const;

   int decrypt_icv_file(const psvpfs::path& destination_root) const;

   int decrypt_unicv_file(const psvpfs::path& destination_root) const;

public:
   int decrypt_file(const psvpfs::path& destination_root) const;
};