#include <iostream>

#include "UnicvDbTypes.h"

#include "UnicvDbUtils.h"
#include "Utils.h"
#include "HashTree.h"

bool sce_irodb_header_proxy_t::validate(std::uint64_t fileSize) const
{
   //check file size field
   if(fileSize != (m_header.dataSize + m_header.blockSize)) //do not forget to count header
   {
      m_output << "Incorrect block size or data size" << std::endl;
      return false;
   }

   //check magic word
   if(std::string((const char*)m_header.magic, 8) != DB_MAGIC_WORD)
   {
      m_output << "Invalid magic word" << std::endl;
      return false;
   }

   //check version
   if(m_header.version != UNICV_EXPECTED_VERSION_1 && m_header.version != UNICV_EXPECTED_VERSION_2)
   {
      m_output << "Unexpected version" << std::endl;
      return false;
   }

   if(m_header.unk2 != 0xFFFFFFFF)
   {
      m_output << "Unexpected unk2" << std::endl;
      return false;
   }

   if(m_header.unk3 != 0xFFFFFFFF)
   {
      m_output << "Unexpected unk3" << std::endl;
      return false;
   }

   //debug check only for now to see if there are any other sizes
   if(m_header.blockSize != EXPECTED_PAGE_SIZE)
   {
      m_output << "Unexpected page size" << std::endl;
      return false;
   }

   return true;
}

bool sce_irodb_header_proxy_t::read(std::ifstream& inputStream, std::uint64_t fileSize)
{
   //read header
   inputStream.read((char*)&m_header, sizeof(sce_irodb_header_t));

   if(!validate(fileSize))
      return false;

   inputStream.seekg(m_header.blockSize, std::ios::beg); //skip header

   return true;
}

//============

bool sig_tbl_header_base_t::validate(std::shared_ptr<sce_iftbl_base_t> fft, std::uint32_t sizeCheck) const
{
   if(m_header.binTreeSize != binTreeSize(0x14, fft->get_header()->get_binTreeNumMaxAvail()))
   {
      m_output << "Unexpected tableSize" << std::endl;
      return false;
   }

   //check to see if there are any other sizes
   if(m_header.sigSize != EXPECTED_SIGNATURE_SIZE)
   {
      m_output << "Unexpected chunk size" << std::endl;
      return false;
   }

   //check padding
   if(m_header.padding != 0)
   {
      m_output << "Unexpected data instead of padding" << std::endl;
      return false;
   }

   //this check is usefull for validating file structure
   if(m_header.nSignatures != sizeCheck)
   {
      m_output << "unexpected number of chunks" << std::endl;
      return false;
   }

   return true;
}

bool sig_tbl_header_base_t::read(std::ifstream& inputStream, std::shared_ptr<sce_iftbl_base_t> fft, std::uint32_t sizeCheck, std::vector<icv>& signatures)
{
   //read header
   inputStream.read((char*)&m_header, sizeof(sig_tbl_header_t));

   //validate header
   if(!validate(fft, sizeCheck))
      return false;

   //read signatures
   for(std::uint32_t c = 0; c < m_header.nSignatures; c++)
   {
      signatures.push_back(icv());
      icv& dte = signatures.back();
      dte.m_data.resize(m_header.sigSize);
      inputStream.read((char*)dte.m_data.data(), m_header.sigSize);
   }

   //calculate size of tail data - this data should be zero padding
   //instead of skipping it is validated here that it contains only zeroes
   std::uint64_t cp = inputStream.tellg();
   std::uint64_t dsize = cp % fft->get_header()->get_pageSize(); //calc size of data that was read
   std::uint64_t tail = fft->get_header()->get_pageSize() - dsize; //calc size of tail data

   //read tail data
   std::vector<std::uint8_t> data(static_cast<std::vector<std::uint8_t>::size_type>(tail));
   inputStream.read((char*)data.data(), tail);

   //fast way would be to use seek
   //inputStream.seekg(tail, std::ios::cur);

   //validate tail in specific class
   return validate_tail(fft, data);
}


bool sig_tbl_header_normal_t::validate_tail(std::shared_ptr<sce_iftbl_base_t> fft, const std::vector<std::uint8_t>& data) const
{
   //validate tail data
   if(!isZeroVector(data))
   {
      m_output << "Unexpected data instead of padding" << std::endl;
      return false;
   }

   return true;
}

bool sig_tbl_header_merkle_t::read(std::ifstream& inputStream, std::shared_ptr<sce_iftbl_base_t> fft, std::uint32_t sizeCheck, std::vector<icv>& signatures)
{
   //read weird 0x10 byte zero header which makes the data not being aligned on page boder
   unsigned char zero_header[0x10];
   inputStream.read((char*)zero_header, 0x10);
   if(!isZeroVector(zero_header, zero_header + 0x10))
   {
      m_output << "Invalid zero vector" << std::endl;
      return false;
   }

   return sig_tbl_header_base_t::read(inputStream, fft, sizeCheck, signatures);
}

bool sig_tbl_header_merkle_t::validate_tail(std::shared_ptr<sce_iftbl_base_t> fft, const std::vector<std::uint8_t>& data) const
{
   std::vector<std::uint8_t> data_copy(data);

   uint32_t* unk_value_base = (uint32_t*)(data_copy.data() + (data_copy.size() - 0x5C));

   //there should be one 0xFFFFFFFF value per sector
   for(std::uint32_t i = 0; i < fft->get_header()->get_numSectors(); i++)
   {
      if(*(unk_value_base + i) != 0xFFFFFFFF)
      {
         m_output << "Unexpected value in signature table tail" << std::endl;
         return false;
      }

      *(unk_value_base + i) = 0; //clear that value
   }

   //after 0xFFFFFFFF values are cleared - everything should be zeroes (including all other data)
   if(!isZeroVector(data_copy))
   {
      m_output << "Invalid zero vector" << std::endl;
      return false;
   }

   return true;
}

//===========

bool sce_iftbl_header_proxy_t::validate() const
{
   //check that block size is expected
   //this will allow to fail if there are any other unexpected block sizes
   if(m_header.pageSize != EXPECTED_PAGE_SIZE)
   {
      m_output << "Unexpected block size" << std::endl;
      return false;
   }

   //check magic word
   if(std::string((const char*)m_header.magic, 8) != FT_MAGIC_WORD)
   {
      m_output << "Invalid magic word" << std::endl;
      return false;
   }

   //check version
   if(m_header.version != UNICV_EXPECTED_VERSION_1 && m_header.version != UNICV_EXPECTED_VERSION_2)
   {
      m_output << "Unexpected version" << std::endl;
      return false;
   }

   //check maxNSectors
   if(m_header.binTreeNumMaxAvail != binTreeNumMaxAvail(0x14, m_header.pageSize))
   {
      m_output << "Unexpected binTreeNumMaxAvail" << std::endl;
      return false;
   }

   //check file sector size
   if(m_header.fileSectorSize != EXPECTED_FILE_SECTOR_SIZE)
   {
      m_output << "Unexpected fileSectorSize" << std::endl;
      return false;
   }

   //check padding
   if(m_header.padding != 0)
   {
      m_output << "Unexpected padding" << std::endl;
      return false;
   }

   return true;
}

bool sce_iftbl_header_proxy_t::read(std::ifstream& inputStream)
{
   inputStream.read((char*)&m_header, sizeof(sce_iftbl_header_t));
   return true;
}


bool sce_icvdb_header_proxy_t::validate() const
{
   if(m_header.pageSize != EXPECTED_PAGE_SIZE)
   {
      m_output << "Unexpected block size" << std::endl;
      return false;
   }

   if(std::string((const char*)m_header.magic, 8) != CV_DB_MAGIC_WORD)
   {
      m_output << "Invalid magic word" << std::endl;
      return false;
   }

   if(m_header.version != ICV_EXPECTED_VERSION_2)
   {
      m_output << "Unexpected version" << std::endl;
      return false;
   }

   if(m_header.fileSectorSize != EXPECTED_FILE_SECTOR_SIZE)
   {
      m_output << "Unexpected fileSectorSize" << std::endl;
      return false;
   }

   if((m_realDataSize - m_header.pageSize) != m_header.dataSize)
   {
      m_output << "Unexpected dataSize" << std::endl;
      return false;
   }

   if(m_header.unk0 != 0xFFFFFFFF)
   {
      m_output << "Unexpected unk2" << std::endl;
      return false;
   }

   if(m_header.unk1 != 0xFFFFFFFF)
   {
      m_output << "Unexpected unk3" << std::endl;
      return false;
   }

   if(m_header.padding != 0)
   {
      m_output << "Unexpected padding" << std::endl;
      return false;
   }

   return true;
}

bool sce_icvdb_header_proxy_t::read(std::ifstream& inputStream)
{
   inputStream.seekg(0, std::ios::end);
   m_realDataSize = inputStream.tellg();
   inputStream.seekg(0, std::ios::beg);

   inputStream.read((char*)&m_header, sizeof(sce_icvdb_header_t));

   return true;
}


bool sce_inull_header_proxy_t::validate() const
{
   if(std::string((const char*)m_header.magic, 8) != NULL_MAGIC_WORD)
   {
      m_output << "Invalid magic word" << std::endl;
      return false;
   }

   if(m_header.version != NULL_EXPECTED_VERSION)
   {
      m_output << "Unexpected version" << std::endl;
      return false;
   }

   if(m_header.unk1 != 0)
   {
      m_output << "Unexpected unk1" << std::endl;
      return false;
   }

   if(m_header.unk2 != 0)
   {
      m_output << "Unexpected unk2" << std::endl;
      return false;
   }

   if(m_header.unk3 != 0)
   {
      m_output << "Unexpected unk3" << std::endl;
      return false;
   }

   return true;
}

bool sce_inull_header_proxy_t::read(std::ifstream& inputStream)
{
   inputStream.read((char*)&m_header, sizeof(sce_inull_header_t));
   return true;
}

//===========

std::shared_ptr<sig_tbl_header_base_t> magic_to_sig_tbl(std::string type, std::ostream& output)
{
   if(type == FT_MAGIC_WORD)
      return std::make_shared<sig_tbl_header_normal_t>(output);
   else if(type == CV_DB_MAGIC_WORD)
      return std::make_shared<sig_tbl_header_merkle_t>(output);
   else if(type == NULL_MAGIC_WORD)
      throw std::runtime_error("wrong magic");
   else
      throw std::runtime_error("wrong magic");
}

std::shared_ptr<sce_iftbl_header_base_t> magic_to_ftbl_header(std::string type, std::ostream& output)
{
   if(type == FT_MAGIC_WORD)
      return std::make_shared<sce_iftbl_header_proxy_t>(output);
   else if(type == CV_DB_MAGIC_WORD)
      return std::make_shared<sce_icvdb_header_proxy_t>(output);
   else if(type == NULL_MAGIC_WORD)
      return std::make_shared<sce_inull_header_proxy_t>(output);
   else
      throw std::runtime_error("wrong magic");
}

std::shared_ptr<sce_iftbl_base_t> magic_to_ftbl(std::string type, std::ostream& output)
{
   if(type == FT_MAGIC_WORD)
      return std::make_shared<sce_iftbl_proxy_t>(magic_to_ftbl_header(type, output), output);
   else if(type == CV_DB_MAGIC_WORD)
      return std::make_shared<sce_icvdb_proxy_t>(magic_to_ftbl_header(type, output), output);
   else if(type == NULL_MAGIC_WORD)
      return std::make_shared<sce_inull_proxy_t>(magic_to_ftbl_header(type, output), output);
   else
      throw std::runtime_error("wrong magic");
}

//===========

bool sce_iftbl_base_t::read(std::ifstream& inputStream, std::uint64_t& index, std::uint32_t icv_salt)
{
   //read header
   if(!m_header->read(inputStream))
      return false;

   //validate header
   if(!m_header->validate())
      return false;

   return true;
}

bool sce_iftbl_base_t::read_block(std::ifstream& inputStream, std::uint64_t& index, std::uint32_t sizeCheck)
{
   //create new signature block
   m_blocks.push_back(sig_tbl_t(magic_to_sig_tbl(m_header->get_magic(), m_output)));
   sig_tbl_t& fdt = m_blocks.back();

   //read and valiate signature block
   if(!fdt.read(inputStream, shared_from_this(), sizeCheck))
      return false;

   index++;

   return true;
}

bool sce_iftbl_cvdb_proxy_t::read(std::ifstream& inputStream, std::uint64_t& index, std::uint32_t icv_salt)
{
   if(!sce_iftbl_base_t::read(inputStream, index, icv_salt))
      return false;

   //calculate size of tail data - this data should be zero padding
   //instead of skipping it is validated here that it contains only zeroes
   std::uint64_t cp = inputStream.tellg(); //get current pos
   std::uint64_t dsize = cp % m_header->get_pageSize(); //calc size of data that was read
   std::uint64_t tail = m_header->get_pageSize() - dsize; //calc size of tail data

   //read tail
   std::vector<std::uint8_t> tailData(static_cast<std::vector<std::uint8_t>::size_type>(tail));
   inputStream.read((char*)tailData.data(), tail);

   //validate tail
   if(!isZeroVector(tailData))
   {
      m_output << "Unexpected data instead of padding" << std::endl;
      return false;
   }

   //fast way would be to use seek
   //inputStream.seekg(tail, std::ios::cur);

   int64_t currentBlockPos = inputStream.tellg();

   m_page = off2page(currentBlockPos, m_header->get_pageSize());

   //check if there are any data blocks after current entry
   if(m_header->get_numSectors() == 0)
      return true;

   //check if there is single block read required or multiple
   if(m_header->get_numHashes() < m_header->get_binTreeNumMaxAvail())
   {
      if(!read_block(inputStream, index, m_header->get_numHashes()))
         return false;

      return m_header->post_validate(m_blocks);
   }
   else
   {
      std::uint32_t nDataBlocks = m_header->get_numHashes() / m_header->get_binTreeNumMaxAvail();
      std::uint32_t nDataTail = m_header->get_numHashes() % m_header->get_binTreeNumMaxAvail();

      for(std::uint32_t dbi = 0; dbi < nDataBlocks; dbi++)
      {
         if(!read_block(inputStream, index, m_header->get_binTreeNumMaxAvail()))
            return false;
      }

      if(nDataTail > 0)
      {
         if(!read_block(inputStream, index, nDataTail))
            return false;
      }

      return m_header->post_validate(m_blocks);
   }
}


std::uint32_t sce_iftbl_proxy_t::get_icv_salt() const
{
   return m_page; // unicv.db uses page number as salt
}


std::uint32_t sce_icvdb_proxy_t::get_icv_salt() const
{
   return m_icv_salt; // icv.db uses file name as salt
}

bool sce_icvdb_proxy_t::read(std::ifstream& inputStream, std::uint64_t& index, std::uint32_t icv_salt)
{
   m_icv_salt = icv_salt;
   return sce_iftbl_cvdb_proxy_t::read(inputStream, index, icv_salt);
}


std::uint32_t sce_inull_proxy_t::get_icv_salt() const
{
   return m_icv_salt; // icv.db uses file name as salt
}

bool sce_inull_proxy_t::read(std::ifstream& inputStream, std::uint64_t& index, std::uint32_t icv_salt)
{
   m_icv_salt = icv_salt;
   return sce_iftbl_base_t::read(inputStream, index, icv_salt);
}

//===========

bool sce_idb_base_t::read_table_item(std::ifstream& inputStream, std::uint64_t& index, std::uint32_t icv_salt)
{
   std::uint8_t magic[8];
   inputStream.read((char*)magic, sizeof(magic));
   inputStream.seekg(-8, std::ios::cur);

   m_tables.push_back(magic_to_ftbl(std::string((char*)magic, sizeof(magic)), m_output));
   std::shared_ptr<sce_iftbl_base_t>& fft = m_tables.back();

   if(!fft->read(inputStream, index, icv_salt))
      return false;

   return true;
}

bool sce_irodb_t::read(psvpfs::path filepath)
{
   std::ifstream inputStream(filepath.generic_string().c_str(), std::ios::in | std::ios::binary);

   if(!inputStream.is_open())
   {
      m_output << "failed to open unicv.db file" << std::endl;
      return false;
   }

   //get stream size
   inputStream.seekg(0, std::ios::end);
   std::uint64_t fileSize = inputStream.tellg();
   inputStream.seekg(0, std::ios::beg);

   //read header
   if(!m_dbHeader->read(inputStream, fileSize))
      return false;

   //it looks like unicv file is split into groups of SCEIFTBL chunks (blocks)
   //where each group corresponds to file or directory
   //however there is no obvious way to determine number of chunks in each group

   //the only way is to calculate total number of chunks (blocks)
   //and read them as stream splitting it into groups in the process

   std::uint64_t nBlocks = m_dbHeader->get_dataSize() / m_dbHeader->get_blockSize();
   std::uint64_t tailSize = m_dbHeader->get_dataSize() % m_dbHeader->get_blockSize();

   //check tail size just in case
   if(tailSize > 0)
   {
      m_output << "Block misalign" << std::endl;
      return false;
   }

   m_output << "Total blocks: " << std::dec << nBlocks << std::endl;

   std::vector<std::uint8_t> blankPage(m_dbHeader->get_blockSize());

   //read all blocks
   for(std::uint64_t index = 0; index < nBlocks; index++)
   {
      //try to skip blank pages
      inputStream.read((char*)blankPage.data(), m_dbHeader->get_blockSize());
      if(isZeroVector(blankPage))
         continue;

      inputStream.seekg(-(std::int32_t)m_dbHeader->get_blockSize(), std::ios::cur);

      //read single block
      if(!read_table_item(inputStream, index, 0))
         return false;
   }

   //check that there is no data left
   std::uint64_t endp = inputStream.tellg();
   if(fileSize != endp)
   {
      m_output << "Data misalign" << std::endl;
      return false;
   }

   return true;
}

bool sce_icvdb_t::read(psvpfs::path filepath)
{
   std::uint64_t index = 0;
   for(auto& entry : psvpfs::directory_iterator(filepath))
   {
      std::ifstream inputStream(entry.path().generic_string().c_str(), std::ios::in | std::ios::binary);

      std::string saltStr = entry.path().stem().generic_string();
      std::uint32_t saltNum =  std::stoul(saltStr, nullptr, 16);

      //read single block
      if(!read_table_item(inputStream, index, saltNum))
         return false;
   }
   return true;
}