#pragma once

//very basics of the format can be found here
//http://www.vitadevwiki.com/index.php?title=Files.db

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <map>
#include <iomanip>
#include <memory>

#include "Utils.h"
#include "FlagOperations.h"
#include "IF00DKeyEncryptor.h"
#include "ICryptoOperations.h"

#pragma pack(push, 1)

#define MAGIC_WORD "SCENGPFS"

#define MAX_FILES_IN_BLOCK 9

#define EXPECTED_BLOCK_SIZE 0x400

#define FILES_EXPECTED_VERSION_3 3
#define FILES_EXPECTED_VERSION_4 4 //looks like files.db salt appeared in this version. before that it was 0
#define FILES_EXPECTED_VERSION_5 5

struct sce_ng_pfs_header_t
{
   std::uint8_t magic[8];
   std::uint32_t version;
   std::uint16_t image_spec; // allows to distinguish unicv.db and icv.db - check is_unicv_to_img_type
   std::uint16_t key_id;
   std::uint32_t pageSize;
   std::uint32_t bt_order; // order value of the binary tree - derived from btree_order
   std::uint32_t root_icv_page_number; // derived from off2pgn or btree_top
   std::uint32_t files_salt; // first salt value used for key derrivation
   std::uint64_t unk6; // is 0xFFFFFFFFFFFFFFFF or rarely may have other unknown value
   std::uint64_t tailSize; // size of data after this header
   std::uint64_t total_sz; // is 0
   std::uint8_t root_icv[0x14]; // 0x38 hmac-sha1 of (pageSize - 4) of page (pointed by root_icv_page_number) with secret derived from klicensee
   std::uint8_t header_icv[0x14]; // 0x4C hmac-sha1 of 0x16 bytes of header with secret derived from klicensee
   std::uint8_t rsa_sig0[0x100];
   std::uint8_t rsa_sig1[0x100];
   std::uint8_t padding[0x1A0];
};

//still have to figure out. not quite clear
enum sce_ng_pfs_block_types : std::uint32_t
{
   child = 0,
   root = 1 // if page number is -1 then this should be root. otherwise - unknown
};

struct sce_ng_pfs_block_header_t
{
   std::uint32_t parent_page_number;
   sce_ng_pfs_block_types type;
   std::uint32_t nFiles;
   std::uint32_t padding; // probably padding ? always 0
};

//there can be 9 files at max in one block
struct sce_ng_pfs_file_header_t
{
   std::uint32_t index; //parent index
   std::uint8_t fileName[68];
};

enum sce_ng_pfs_file_types : std::uint16_t
{
   unexisting =                 ATTR_RW_OR_NONE,  //(0x0000)
   normal_file =                ATTR_RO, //(0x0001)
   normal_directory =           ATTR_DIR,  //(0x8000)

   sys_directory =              ATTR_DIR | ATTR_SYS1 | ATTR_SYS2, //(0x8006)

   unencrypted_system_file_rw = ATTR_NENC | ATTR_SYS1 | ATTR_SYS2, //(0x4006)
   encrypted_system_file_rw =   ATTR_SYS1 | ATTR_SYS2, //(0x0006)

   unencrypted_system_file_ro = ATTR_NENC | ATTR_SYS1 | ATTR_SYS2 | ATTR_RO, //(0x4007)
   encrypted_system_file_ro =   ATTR_SYS1 | ATTR_SYS2 | ATTR_RO, //(0x0007)

   acid_directory =             ATTR_DIR | ATTR_AC | ATTR_SYS2, //(0x9004) encountered in ADDCONT
};

#define INVALID_FILE_INDEX 0xFFFFFFFF

struct sce_ng_pfs_file_info_t
{
   std::uint32_t idx; // this file index. can be INVALID_FILE_INDEX
   sce_ng_pfs_file_types type;
   std::uint16_t padding0; //probably padding ? always 0
   std::uint32_t size;
   std::uint32_t padding1; //probably padding ? always 0
};

struct sce_ng_pfs_file_info_proxy_t
{
   sce_ng_pfs_file_info_t header;
   sce_ng_pfs_file_types original_type;
   bool hasFixedType;

   sce_ng_pfs_file_info_proxy_t()
      : hasFixedType(false)
   {
   }

   sce_ng_pfs_file_types get_original_type() const
   {
      if(hasFixedType)
         return original_type;
      else
         return header.type;
   }
};

struct sce_ng_pfs_hash_t
{
   std::uint8_t data[20];
};

struct sce_ng_pfs_block_t
{
   sce_ng_pfs_block_header_t header; //size = 16
   std::vector<sce_ng_pfs_file_header_t> files; // size = 72 * 9 = 648

   //infos may contain non INVALID_FILE_INDEX as last element
   //still dont know the purpose of this
   std::vector<sce_ng_pfs_file_info_proxy_t> m_infos; // size = 16 * 10 = 160
   std::vector<sce_ng_pfs_hash_t> hashes; // size = 20 * 10 = 200

   std::uint32_t page;
};

struct sce_ng_pfs_flat_block_t
{
   sce_ng_pfs_block_header_t header;
   sce_ng_pfs_file_header_t file;
   sce_ng_pfs_file_info_proxy_t m_info;
   sce_ng_pfs_hash_t hash;
};

struct sce_ng_pfs_file_t
{
private:
   sce_junction m_path;

public:
   sce_ng_pfs_flat_block_t file;
   std::vector<sce_ng_pfs_flat_block_t> dirs;

   sce_ng_pfs_file_t(const sce_junction& p)
      : m_path(p)
   {

   }

public:
   const sce_junction& path() const
   {
      return m_path;
   }
};

struct sce_ng_pfs_dir_t
{
private:
   sce_junction m_path;

public:
   sce_ng_pfs_flat_block_t dir;
   std::vector<sce_ng_pfs_flat_block_t> dirs;

   sce_ng_pfs_dir_t(const sce_junction& p)
      : m_path(p)
   {

   }

public:
   const sce_junction& path() const
   {
      return m_path;
   }
};

#pragma pack(pop)

bool is_directory(sce_ng_pfs_file_types type);

bool is_valid_file_type(sce_ng_pfs_file_types type);

bool is_encrypted(sce_ng_pfs_file_types type);

bool is_unencrypted(sce_ng_pfs_file_types type);

bool is_unexisting(sce_ng_pfs_file_types type);

class FilesDbParser
{
private:
   std::shared_ptr<ICryptoOperations> m_cryptops;
   std::shared_ptr<IF00DKeyEncryptor> m_iF00D;
   std::ostream& m_output;
   unsigned char m_klicensee[0x10];
   psvpfs::path m_titleIdPath;

private:
   sce_ng_pfs_header_t m_header;
   std::vector<sce_ng_pfs_file_t> m_files;
   std::vector<sce_ng_pfs_dir_t> m_dirs;

public:
   FilesDbParser(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, std::ostream& output,
                 const unsigned char* klicensee, psvpfs::path titleIdPath);

private:
   bool verify_header_icv(std::ifstream& inputStream, const unsigned char* secret);

   bool get_isUnicv(bool& isUnicv);

   bool validate_header(uint32_t dataSize);

   bool parseFilesDb(std::ifstream& inputStream, std::vector<sce_ng_pfs_block_t>& blocks);

private:
   bool constructDirmatrix(const std::vector<sce_ng_pfs_block_t>& blocks, std::map<std::uint32_t, std::uint32_t>& dirMatrix);

   bool constructFileMatrix(std::vector<sce_ng_pfs_block_t>& blocks, std::map<std::uint32_t, std::uint32_t>& fileMatrix);

   bool flattenBlocks(const std::vector<sce_ng_pfs_block_t>& blocks, std::vector<sce_ng_pfs_flat_block_t>& flatBlocks);

   const std::vector<sce_ng_pfs_flat_block_t>::const_iterator findFlatBlockDir(const std::vector<sce_ng_pfs_flat_block_t>& flatBlocks, std::uint32_t index);

   const std::vector<sce_ng_pfs_flat_block_t>::const_iterator findFlatBlockFile(const std::vector<sce_ng_pfs_flat_block_t>& flatBlocks, std::uint32_t index);

   bool constructDirPaths(const std::map<std::uint32_t, std::uint32_t>& dirMatrix, const std::vector<sce_ng_pfs_flat_block_t>& flatBlocks);

   bool constructFilePaths(const std::map<std::uint32_t, std::uint32_t>& dirMatrix, const std::map<std::uint32_t, std::uint32_t>& fileMatrix, const std::vector<sce_ng_pfs_flat_block_t>& flatBlocks);

private:
   bool linkDirpaths(const std::set<psvpfs::path>& real_directories);

   bool linkFilepaths(const std::set<psvpfs::path>& real_files, std::uint32_t fileSectorSize);

   int matchFileLists(const std::set<psvpfs::path>& files);

public:
   int parse();

public:
   const sce_ng_pfs_header_t& get_header() const
   {
      return m_header;
   }

   const std::vector<sce_ng_pfs_file_t>& get_files() const
   {
      return m_files;
   }

   const std::vector<sce_ng_pfs_dir_t>& get_dirs() const
   {
      return m_dirs;
   }
};