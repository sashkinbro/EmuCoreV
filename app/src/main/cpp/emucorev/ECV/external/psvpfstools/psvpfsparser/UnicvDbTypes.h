#pragma once

#include <cstdint>
#include <vector>
#include <iostream>
#include <memory>
#include <cstring>
#include <string>

#include "LocalFilesystem.h"

//some terms
//SCEIRODB (magic word) - sony computer entertainment interface readonly database (unicv file)
//SCEIFTBL (magic word) - sce interface file table (file record in unicv)
//SCEICVDB (magic word) - sce interface C vector database (icv file corresponding to real file)
//SCEINULL (magic word) - sce interface NULL (icv file corresponding to real directory)
//SCEUNICV (this is not a magic word) - sce unified interface C vector (icv files packed into one binary)

#define DB_MAGIC_WORD "SCEIRODB"

#define FT_MAGIC_WORD "SCEIFTBL"

#define CV_DB_MAGIC_WORD "SCEICVDB"

#define NULL_MAGIC_WORD "SCEINULL"

#define EXPECTED_PAGE_SIZE 0x400
#define EXPECTED_SIGNATURE_SIZE 0x14
#define EXPECTED_FILE_SECTOR_SIZE 0x8000

#define UNICV_EXPECTED_VERSION_1 1
#define UNICV_EXPECTED_VERSION_2 2

#define ICV_EXPECTED_VERSION_2 2

#define NULL_EXPECTED_VERSION 1

#define ICV_NUM_ENTRIES 0x2D

#pragma pack(push, 1)

//=================================================

struct sce_irodb_header_t
{
   std::uint8_t magic[8]; //SCEIRODB
   std::uint32_t version; // this is probably version? value is always 2
   std::uint32_t blockSize; //expected 0x400
   std::uint32_t unk2; //0xFFFFFFFF
   std::uint32_t unk3; //0xFFFFFFFF
   std::uint64_t dataSize; //size of data beginning from next chunk
};

class sce_irodb_header_proxy_t
{
private:
   sce_irodb_header_t m_header;

private:
   std::ostream& m_output;

public:
   sce_irodb_header_proxy_t(std::ostream& output)
      : m_output(output)
   {
   }

public:
   bool validate(std::uint64_t fileSize) const;

   bool read(std::ifstream& inputStream, std::uint64_t fileSize);

public:
   std::string get_magic() const
   {
      return std::string((char*)m_header.magic, 8);
   }

   std::uint32_t get_version() const
   {
      return m_header.version;
   }

   std::uint32_t get_blockSize() const
   {
      return m_header.blockSize;
   }

   std::uint64_t get_dataSize() const
   {
      return m_header.dataSize;
   }
};

//=================================================

//this file table corresponds to files.db
//it has exactly same number of sce_iftbl_header_t records with nSectors == 0 as there are directories
//and same number of sce_iftbl_header_t records with nSectors != 0 as there are files
struct sce_iftbl_header_t
{
   std::uint8_t magic[8]; //SCEIFTBL
   std::uint32_t version; // this is probably version? value is always 2
   std::uint32_t pageSize; //expected 0x400
   std::uint32_t binTreeNumMaxAvail; // this is probably max number of sectors in a single sig_tbl_t. expected value is 0x32
   std::uint32_t nSectors; //this shows how many sectors of data in total will follow this block. one sig_tbl_t can contain 0x32 sectors at max
                     //multiple sig_tbl_t group into single file

   //This is sector size for files.db
   std::uint32_t fileSectorSize; // expected 0x8000

   std::uint32_t padding; //this is probably padding? always zero

   //these records are empty if sce_iftbl_header_t corresponds to directory
   std::uint8_t data1[20];
   std::uint8_t dbseed[20]; // this is a base key that is used to derive tweak_enc_key - one of the keys required for decryption
};

struct sce_icvdb_header_t
{
   std::uint8_t magic[8]; //SCEICVDB
   std::uint32_t version; // this is probably version? value is always 2
   std::uint32_t fileSectorSize;
   std::uint32_t pageSize; //expected 0x400
   std::uint32_t padding;
   std::uint32_t unk0; //0xFFFFFFFF
   std::uint32_t unk1; //0xFFFFFFFF
   std::uint64_t dataSize; // from next chunk maybe? or block size
   std::uint32_t nSectors;
   std::uint8_t merkleTreeRoot[20];
};

struct sce_inull_header_t
{
   std::uint8_t magic[8]; //SCEINULL
   std::uint32_t version; // 1
   std::uint32_t unk1;
   std::uint32_t unk2;
   std::uint32_t unk3;
};

//=================================================

//sce_iftbl_header_t.nSectors indicates total number of sectors
//if it is greater then 0x32 that means that multiple signature blocks will follow
struct sig_tbl_header_t
{
   std::uint32_t binTreeSize; // for unicv.db for blocksize 0x400 this would be 0x3f8 = sizeof(sig_tbl_header_t) + (0x32 * 0x14) : which are maxNSectors * sigSize (0x8 bytes are unused)
                         // for icv.db for blocksize 0x400 this would be 0x394 = sizeof(sig_icv_tbl_header_t) + (0x2D * 0x14) : which are 2D * sigSize (0x6C bytes are unused)
   std::uint32_t sigSize; //expected 0x14 - size of hmac-sha1
   std::uint32_t nSignatures; //number of chunks in this block
   std::uint32_t padding; //most likely padding ? always zero
};

class icv
{
public:
   std::vector<std::uint8_t> m_data;
};

class sce_iftbl_t;
class sce_iftbl_base_t;

class sig_tbl_header_base_t
{
protected:
   sig_tbl_header_t m_header;

protected:
   std::ostream& m_output;

public:
   sig_tbl_header_base_t(std::ostream& output)
      : m_output(output)
   {
   }

   virtual ~sig_tbl_header_base_t()
   {
   }

public:
   std::uint32_t get_binTreeSize() const
   {
      return m_header.binTreeSize;
   }

   std::uint32_t get_sigSize() const
   {
      return m_header.sigSize;
   }

   std::uint32_t get_nSignatures() const
   {
      return m_header.nSignatures;
   }

   std::uint32_t get_padding() const
   {
      return m_header.padding;
   }

public:
   bool validate(std::shared_ptr<sce_iftbl_base_t> fft, std::uint32_t sizeCheck) const;

   virtual bool read(std::ifstream& inputStream, std::shared_ptr<sce_iftbl_base_t> fft, std::uint32_t sizeCheck, std::vector<icv>& signatures);

   virtual bool validate_tail(std::shared_ptr<sce_iftbl_base_t> fft, const std::vector<std::uint8_t>& data) const = 0;
};

class sig_tbl_header_normal_t : public sig_tbl_header_base_t
{
public:
   sig_tbl_header_normal_t(std::ostream& output)
      : sig_tbl_header_base_t(output)
   {
   }

public:
   bool validate_tail(std::shared_ptr<sce_iftbl_base_t> fft, const std::vector<std::uint8_t>& data) const override;
};

class sig_tbl_header_merkle_t : public sig_tbl_header_base_t
{
public:
   sig_tbl_header_merkle_t(std::ostream& output)
      : sig_tbl_header_base_t(output)
   {
   }

public:
   bool read(std::ifstream& inputStream, std::shared_ptr<sce_iftbl_base_t> fft, std::uint32_t sizeCheck, std::vector<icv>& signatures) override;

   bool validate_tail(std::shared_ptr<sce_iftbl_base_t> fft, const std::vector<std::uint8_t>& data) const override;
};

//this is a signature table structure - it contains header and list of signatures
//in more generic terms - this is also a data block of size 0x400
//signature table that is used to verify file hashes
//it can hold 0x32 signatures at max
//each signature corresponds to block in a real file. block should have size fileSectorSize (0x8000)
class sig_tbl_t
{
private:
   std::shared_ptr<sig_tbl_header_base_t> m_header;

public:
   sig_tbl_t(std::shared_ptr<sig_tbl_header_base_t> header)
      : m_header(header)
   {
   }

public:
   std::vector<icv> m_signatures;

   std::shared_ptr<sig_tbl_header_base_t> get_header() const
   {
      return m_header;
   }

   bool read(std::ifstream& inputStream, std::shared_ptr<sce_iftbl_base_t> fft, std::uint32_t sizeCheck)
   {
      return m_header->read(inputStream, fft, sizeCheck, m_signatures);
   }
};

//=================================================

class sce_iftbl_header_base_t
{
public:
   virtual ~sce_iftbl_header_base_t()
   {
   }

public:
   virtual std::uint32_t get_numSectors() const = 0;

   virtual std::uint32_t get_numHashes() const = 0;

   virtual std::uint32_t get_fileSectorSize() const = 0;

   virtual const std::uint8_t* get_dbseed() const = 0;

   virtual std::uint32_t get_binTreeNumMaxAvail() const = 0;

   virtual std::uint32_t get_pageSize() const = 0;

   virtual std::uint32_t get_version() const = 0;

   virtual std::string get_magic() const = 0;

public:
   virtual bool validate() const = 0;

   virtual bool read(std::ifstream& inputStream) = 0;

   virtual bool post_validate(const std::vector<sig_tbl_t>& blocks) const = 0;
};

class sce_iftbl_header_proxy_t : public sce_iftbl_header_base_t
{
private:
   sce_iftbl_header_t m_header;

private:
   std::ostream& m_output;

public:
   sce_iftbl_header_proxy_t(std::ostream& output)
      : m_output(output)
   {
   }

public:
   std::uint32_t get_numSectors() const override
   {
      return m_header.nSectors;
   }

   std::uint32_t get_numHashes() const override
   {
      return m_header.nSectors;
   }

   std::uint32_t get_fileSectorSize() const override
   {
      return m_header.fileSectorSize;
   }

   const std::uint8_t* get_dbseed() const override
   {
      return m_header.dbseed;
   }

   std::uint32_t get_binTreeNumMaxAvail() const override
   {
      return m_header.binTreeNumMaxAvail;
   }

   std::uint32_t get_pageSize() const override
   {
      return m_header.pageSize;
   }

   std::uint32_t get_version() const override
   {
      return m_header.version;
   }

   std::string get_magic() const override
   {
      return std::string((char*)m_header.magic, 8);
   }

public:
   bool validate() const override;

   bool read(std::ifstream& inputStream) override;

   bool post_validate(const std::vector<sig_tbl_t>& blocks) const override
   {
      return true;
   }
};

class sce_icvdb_header_proxy_t : public sce_iftbl_header_base_t
{
private:
   sce_icvdb_header_t m_header;

   uint64_t m_realDataSize;

private:
   std::ostream& m_output;

public:
   sce_icvdb_header_proxy_t(std::ostream& output)
      : m_output(output)
   {
   }

public:
   std::uint32_t get_numSectors() const override
   {
      return m_header.nSectors;
   }

   std::uint32_t get_numHashes() const override
   {
      //this is a formula for the number of hashes that will be calculated in merkle tree
      return m_header.nSectors * 2 - 1;
   }

   std::uint32_t get_fileSectorSize() const override
   {
      return m_header.fileSectorSize;
   }

   const std::uint8_t* get_dbseed() const override
   {
      throw std::runtime_error("not implemented");
   }

   std::uint32_t get_binTreeNumMaxAvail() const override
   {
      return ICV_NUM_ENTRIES;
   }

   std::uint32_t get_pageSize() const override
   {
      return m_header.pageSize;
   }

   std::uint32_t get_version() const override
   {
      return m_header.version;
   }

   std::string get_magic() const override
   {
      return std::string((char*)m_header.magic, 8);
   }

public:
   bool validate() const override;

   bool read(std::ifstream& inputStream) override;

   bool post_validate(const std::vector<sig_tbl_t>& blocks) const override
   {
      const unsigned char* rootSig = blocks.front().m_signatures.front().m_data.data();
      if(memcmp(rootSig, m_header.merkleTreeRoot, 0x14) != 0)
      {
         m_output << "Root icv is invalid" << std::endl;
         return false;
      }
      return true;
   }
};

class sce_inull_header_proxy_t : public sce_iftbl_header_base_t
{
private:
   sce_inull_header_t m_header;

private:
   std::ostream& m_output;

public:
   sce_inull_header_proxy_t(std::ostream& output)
      : m_output(output)
   {
   }

public:
   std::uint32_t get_numSectors() const override
   {
      return 0;
   }

   std::uint32_t get_numHashes() const override
   {
      return 0;
   }

   std::uint32_t get_fileSectorSize() const override
   {
      return 0;
   }

   const std::uint8_t* get_dbseed() const override
   {
      throw std::runtime_error("not implemented");
   }

   std::uint32_t get_binTreeNumMaxAvail() const override
   {
      throw std::runtime_error("not implemented");
   }

   std::uint32_t get_pageSize() const override
   {
      throw std::runtime_error("not implemented");
   }

   std::uint32_t get_version() const override
   {
      throw std::runtime_error("not implemented");
   }

   std::string get_magic() const override
   {
      return std::string((char*)m_header.magic, 8);
   }

public:
   bool validate() const override;

   bool read(std::ifstream& inputStream) override;

   bool post_validate(const std::vector<sig_tbl_t>& blocks) const override
   {
      return true;
   }
};

//this is a file table structure - it contais SCEIFTBL/SCEICVDB/SCEINULL header and list of file signature blocks
//in more generic terms - this is also a data block of size 0x400
//which is followed by signature blocks
class sce_iftbl_base_t : public std::enable_shared_from_this<sce_iftbl_base_t>
{
protected:
   std::shared_ptr<sce_iftbl_header_base_t> m_header;

protected:
   std::uint32_t m_page;

public:
   std::vector<sig_tbl_t> m_blocks;

protected:
   std::ostream& m_output;

public:
   sce_iftbl_base_t(std::shared_ptr<sce_iftbl_header_base_t> header, std::ostream& output)
      : m_header(header),
        m_page(-1),
        m_output(output)
   {
   }

   virtual ~sce_iftbl_base_t()
   {
   }

public:
   std::shared_ptr<sce_iftbl_header_base_t> get_header() const
   {
      return m_header;
   }

public:
   virtual bool read(std::ifstream& inputStream, std::uint64_t& index, std::uint32_t icv_salt);

protected:
   bool read_block(std::ifstream& inputStream, std::uint64_t& index, std::uint32_t sizeCheck);

public:
   virtual std::uint32_t get_icv_salt() const = 0;
};

class sce_iftbl_cvdb_proxy_t : public sce_iftbl_base_t
{
public:
   sce_iftbl_cvdb_proxy_t(std::shared_ptr<sce_iftbl_header_base_t> header, std::ostream& output)
      : sce_iftbl_base_t(header, output)
   {
   }

public:
   bool read(std::ifstream& inputStream, std::uint64_t& index, std::uint32_t icv_salt) override;
};

//for now these types do not implement any additional logic that is different from base classes
//however clear separation in 3 different types is essential
class sce_iftbl_proxy_t : public sce_iftbl_cvdb_proxy_t
{
public:
   sce_iftbl_proxy_t(std::shared_ptr<sce_iftbl_header_base_t> header, std::ostream& output)
      : sce_iftbl_cvdb_proxy_t(header, output)
   {
   }

public:
   std::uint32_t get_icv_salt() const override;
};

class sce_icvdb_proxy_t : public sce_iftbl_cvdb_proxy_t
{
private:
   std::uint32_t m_icv_salt;

public:
   sce_icvdb_proxy_t(std::shared_ptr<sce_iftbl_header_base_t> header, std::ostream& output)
      : sce_iftbl_cvdb_proxy_t(header, output)
   {
   }

public:
   std::uint32_t get_icv_salt() const override;

public:
   bool read(std::ifstream& inputStream, std::uint64_t& index, std::uint32_t icv_salt) override;
};

class sce_inull_proxy_t : public sce_iftbl_base_t
{
private:
   std::uint32_t m_icv_salt;

public:
   sce_inull_proxy_t(std::shared_ptr<sce_iftbl_header_base_t> header, std::ostream& output)
      : sce_iftbl_base_t(header, output)
   {
   }

public:
   std::uint32_t get_icv_salt() const override;

public:
   bool read(std::ifstream& inputStream, std::uint64_t& index, std::uint32_t icv_salt) override;
};

//=================================================

class sce_idb_base_t
{
public:
   std::vector<std::shared_ptr<sce_iftbl_base_t> > m_tables;

protected:
   std::ostream& m_output;

public:
   sce_idb_base_t(std::ostream& output)
      : m_output(output)
   {
   }

   virtual ~sce_idb_base_t()
   {
   }

public:
   virtual bool read(psvpfs::path filepath) = 0;

protected:
   bool read_table_item(std::ifstream& inputStream, std::uint64_t& index, std::uint32_t icv_salt);
};

//this is a root object for unicv.db - it contains SCEIRODB header and list of SCEIFTBL file table blocks
class sce_irodb_t : public sce_idb_base_t
{
private:
   std::unique_ptr<sce_irodb_header_proxy_t> m_dbHeader;

public:
   sce_irodb_t(std::ostream& output)
      : sce_idb_base_t(output)
   {
      m_dbHeader = std::unique_ptr<sce_irodb_header_proxy_t>(new sce_irodb_header_proxy_t(output));
   }

public:
   bool read(psvpfs::path filepath);
};

//this is a root object for icv.db - it contains list of SCEICVDB and SCEINULL blocks. there is no additional header
class sce_icvdb_t : public sce_idb_base_t
{
public:
   sce_icvdb_t(std::ostream& output)
      : sce_idb_base_t(output)
   {
   }

public:
   bool read(psvpfs::path filepath);
};

#pragma pack(pop)

std::shared_ptr<sig_tbl_header_base_t> magic_to_sig_tbl(std::string type, std::ostream& output);

std::shared_ptr<sce_iftbl_header_base_t> magic_to_ftbl_header(std::string type, std::ostream& output);

std::shared_ptr<sce_iftbl_base_t> magic_to_ftbl(std::string type, std::ostream& output);
