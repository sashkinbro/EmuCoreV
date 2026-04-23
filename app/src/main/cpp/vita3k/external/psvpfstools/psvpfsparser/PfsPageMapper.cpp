#include "PfsPageMapper.h"

#include "SecretGenerator.h"
#include "UnicvDbParser.h"
#include "FilesDbParser.h"

PfsPageMapper::PfsPageMapper(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, std::ostream& output, const unsigned char* klicensee, const psvpfs::path& titleIdPath)
   : m_cryptops(cryptops), m_iF00D(iF00D), m_output(output), m_titleIdPath(titleIdPath)
{
   memcpy(m_klicensee, klicensee, 0x10);
}

std::shared_ptr<sce_junction> PfsPageMapper::brutforce_hashes(const std::unique_ptr<FilesDbParser>& filesDbParser, std::map<sce_junction, std::vector<std::uint8_t>>& fileDatas, const unsigned char* secret, const unsigned char* signature) const
{
   const sce_ng_pfs_header_t& ngpfs = filesDbParser->get_header();

   unsigned char signature_key[0x14] = {0};

   if(img_spec_to_is_unicv(ngpfs.image_spec))
   {
      //we will be checking only first sector of each file hence we can precalculate a signature_key
      //because both secret and sector_salt will not vary
      int sector_salt = 0; //sector number is most likely a salt which is logically correct in terms of xts-aes
      m_cryptops->hmac_sha1((unsigned char*)&sector_salt, signature_key, 4, secret, 0x14);
   }
   else
   {
      //for icv files sector salt is not used. salt is global and is specified in the name of the file
      //this means that we can just use secret as is
      memcpy(signature_key, secret, 0x14);
   }

   //go through each first sector of the file
   for(auto& f : fileDatas)
   {
      //calculate sector signature
      unsigned char realSignature[0x14] = {0};
      m_cryptops->hmac_sha1(f.second.data(), realSignature, f.second.size(), signature_key, 0x14);

      //try to match the signatures
      if(memcmp(signature, realSignature, 0x14) == 0)
      {
         std::shared_ptr<sce_junction> found_path(new sce_junction(f.first));
         //remove newly found path from the search list to reduce time with each next iteration
         fileDatas.erase(f.first);
         return found_path;
      }
   }

   return std::shared_ptr<sce_junction>();
}

//this is a tree walker function and it should not be a part of the class
int find_zero_sector_index(std::shared_ptr<merkle_tree_node<icv> > node, void* ctx)
{
   std::pair<std::uint32_t, std::uint32_t>* ctx_pair = (std::pair<std::uint32_t, std::uint32_t>*)ctx;

   if(node->isLeaf())
   {
      if(node->m_index == 0)
      {
         ctx_pair->second = ctx_pair->first; //save global counter to result
         return -1;
      }
      else
      {
         ctx_pair->first++; //increase global counter
         return 0;
      }
   }
   else
   {
      ctx_pair->first++; //increase global counter
      return 0;
   }
}

//this is a tree walker function and it should not be a part of the class
int assign_hash(std::shared_ptr<merkle_tree_node<icv> > node, void* ctx)
{
   if(!node->isLeaf())
      return 0;

   std::map<std::uint32_t, icv>* sectorHashMap = (std::map<std::uint32_t, icv>*)ctx;

   auto item = sectorHashMap->find(node->m_index);
   if(item == sectorHashMap->end())
      throw std::runtime_error("Missing sector hash");

   node->m_context.m_data.assign(item->second.m_data.begin(), item->second.m_data.end());

   return 0;
}

//this is a tree walker function and it should not be a part of the class
int combine_hash(std::shared_ptr<merkle_tree_node<icv> > result, std::shared_ptr<merkle_tree_node<icv> > left, std::shared_ptr<merkle_tree_node<icv> > right, void* ctx)
{
   unsigned char bytes28[0x28] = {0};
   memcpy(bytes28, left->m_context.m_data.data(), 0x14);
   memcpy(bytes28 + 0x14, right->m_context.m_data.data(), 0x14);

   std::pair<std::shared_ptr<ICryptoOperations>, unsigned char*>* ctx_cast = (std::pair<std::shared_ptr<ICryptoOperations>, unsigned char*>*)ctx;

   std::shared_ptr<ICryptoOperations> cryptops = ctx_cast->first;
   unsigned char* secret = ctx_cast->second;

   result->m_context.m_data.resize(0x14);
   cryptops->hmac_sha1(bytes28, result->m_context.m_data.data(), 0x28, secret, 0x14);

   return 0;
}

//this is a tree walker function and it should not be a part of the class
int collect_hash(std::shared_ptr<merkle_tree_node<icv> > node, void* ctx)
{
   std::vector<icv>* hashTable = (std::vector<icv>*)ctx;
   hashTable->push_back(node->m_context);
   return 0;
}

int PfsPageMapper::compare_hash_tables(const std::vector<icv>& left, const std::vector<icv>& right)
{
   if(left.size() != right.size())
      return -1;

   for(std::size_t i = 0; i < left.size(); i++)
   {
      if(memcmp(left[i].m_data.data(), right[i].m_data.data(), 0x14) != 0)
         return -1;
   }

   return 0;
}

//pageMap - relates icv salt (icv filename) to junction (real file in filesystem)
//merkleTrees - relates icv table entry (icv file) to merkle tree of real file
//idea is to find icv table entry by icv filename - this way we can relate junction to merkle tree
//then we can read the file and hash it into merkle tree
//then merkle tree is collected into hash table
//then hash table is compared to the hash table from icv table entry
int PfsPageMapper::validate_merkle_trees(const std::unique_ptr<FilesDbParser>& filesDbParser, std::vector<std::pair<std::shared_ptr<sce_iftbl_base_t>, std::shared_ptr<merkle_tree<icv> > > >& merkleTrees)
{
   const sce_ng_pfs_header_t& ngpfs = filesDbParser->get_header();

   m_output << "Validating merkle trees..." << std::endl;

   for(auto entry : merkleTrees)
   {
      //get table
      std::shared_ptr<sce_iftbl_base_t> table = entry.first;

      //calculate secret
      unsigned char secret[0x14];
      scePfsUtilGetSecret(m_cryptops, m_iF00D, secret, m_klicensee, ngpfs.files_salt, img_spec_to_crypto_engine_flag(ngpfs.image_spec), table->get_icv_salt(), 0);

      //find junction
      auto junctionIt = m_pageMap.find(table->get_icv_salt());
      if(junctionIt == m_pageMap.end())
      {
         m_output << "Table item not found in page map" << std::endl;
         return -1;
      }

      const sce_junction& junction = junctionIt->second;

      //read junction into sector map
      std::ifstream inputStream;
      junction.open(inputStream);

      std::uint32_t sectorSize = table->get_header()->get_fileSectorSize();
      std::uintmax_t fileSize = junction.file_size();

      std::uint32_t nSectors = static_cast<std::uint32_t>(fileSize / sectorSize);
      std::uint32_t tailSize = fileSize % sectorSize;

      std::map<std::uint32_t, icv> sectorHashMap;

      std::vector<std::uint8_t> raw_data(sectorSize);
      for(std::uint32_t i = 0; i < nSectors; i++)
      {
         auto currentItem = sectorHashMap.insert(std::make_pair(i, icv()));
         icv& currentIcv = currentItem.first->second;

         inputStream.read((char*)raw_data.data(), sectorSize);

         currentIcv.m_data.resize(0x14);
         m_cryptops->hmac_sha1(raw_data.data(), currentIcv.m_data.data(), sectorSize, secret, 0x14);
      }

      if(tailSize > 0)
      {
         auto currentItem = sectorHashMap.insert(std::make_pair(nSectors, icv()));
         icv& currentIcv = currentItem.first->second;

         inputStream.read((char*)raw_data.data(), tailSize);

         currentIcv.m_data.resize(0x14);
         m_cryptops->hmac_sha1(raw_data.data(), currentIcv.m_data.data(), tailSize, secret, 0x14);
      }

      try
      {
         //get merkle tree (it should already be indexed)
         std::shared_ptr<merkle_tree<icv> > mkt = entry.second;

         //assign hashes to leaves
         walk_tree(mkt, assign_hash, &sectorHashMap);

         //calculate node hashes
         auto combine_ctx = std::make_pair(m_cryptops, secret);
         bottom_top_walk_combine(mkt, combine_hash, &combine_ctx);

         //collect hashes into table
         std::vector<icv> hashTable;
         walk_tree(mkt, collect_hash, &hashTable);

         //compare tables
         if(compare_hash_tables(hashTable, table->m_blocks.front().m_signatures) < 0)
         {
            m_output << "Merkle tree is invalid in file " << junction << std::endl;
            return -1;
         }

         m_output << "File: " << std::hex << table->get_icv_salt() << " [OK]" << std::endl;
      }
      catch(std::runtime_error& e)
      {
         m_output << e.what() << std::endl;
         return -1;
      }
   }

   return 0;
}

//filesDbParser and unicvDbParser are not made part of the context of PfsPageMapper
//the reason is because both filesDbParser and unicvDbParser have to be
//initialized with parse method externally prior to calling bruteforce_map
//having filesDbParser and unicvDbParser as constructor arguments will
//introduce ambiguity in usage of PfsPageMapper
int PfsPageMapper::bruteforce_map(const std::unique_ptr<FilesDbParser>& filesDbParser, const std::unique_ptr<UnicvDbParser>& unicvDbParser)
{
   const sce_ng_pfs_header_t& ngpfs = filesDbParser->get_header();
   const std::unique_ptr<sce_idb_base_t>& unicv = unicvDbParser->get_idatabase();

   if(img_spec_to_is_unicv(ngpfs.image_spec))
      m_output << "Building unicv.db -> files.db relation..." << std::endl;
   else
      m_output << "Building icv.db -> files.db relation..." << std::endl;

   psvpfs::path root(m_titleIdPath);

   //check file fileSectorSize
   std::set<std::uint32_t> fileSectorSizes;
   for(auto& t : unicv->m_tables)
   {
      //skip SCEINULL blocks
      if(t->m_blocks.size() > 0)
         fileSectorSizes.insert(t->get_header()->get_fileSectorSize());
   }

   if(fileSectorSizes.size() > 1)
   {
      m_output << "File sector size is not unique. This bruteforce mode is not supported now" << std::endl;
      return -1;
   }

   std::uint32_t uniqueSectorSize = *fileSectorSizes.begin();

   //get all files and directories
   std::set<psvpfs::path> files;
   std::set<psvpfs::path> directories;
   getFileListNoPfs(root, files, directories);

   //pre read all the files once
   std::map<sce_junction, std::vector<std::uint8_t>> fileDatas;
   for(auto& real_file : files)
   {
      sce_junction sp(real_file);
      sp.link_to_real(real_file);

      std::uintmax_t fsz = sp.file_size();

      // using uniqueSectorSize here.
      // in theory this size may vary per SCEIFTBL - this will make bruteforcing a bit harder.
      // files can not be pre read in this case
      // in practice though it does not change.
      std::uintmax_t fsz_limited = (fsz < uniqueSectorSize) ? fsz : uniqueSectorSize;

      //empty files should be allowed!
      if(fsz_limited == 0)
      {
         m_output << "File " << sp << " is empty" << std::endl;
         m_emptyFiles.insert(sp);
      }
      else
      {
         const auto& fdt = fileDatas.insert(std::make_pair(sp, std::vector<std::uint8_t>(static_cast<std::vector<std::uint8_t>::size_type>(fsz_limited))));

         std::ifstream in;
         if(!sp.open(in))
         {
            m_output << "Failed to open " << sp << std::endl;
            return -1;
         }

         in.read((char*)fdt.first->second.data(), fsz_limited);
         in.close();
      }
   }

   std::vector<std::pair<std::shared_ptr<sce_iftbl_base_t>, std::shared_ptr<merkle_tree<icv> > > > merkleTrees;

   //brutforce each sce_iftbl_t record
   for(auto& t : unicv->m_tables)
   {
      //process only files that are not empty
      if(t->get_header()->get_numSectors() > 0)
      {
         //generate secret - one secret per unicv.db page is required
         unsigned char secret[0x14];
         scePfsUtilGetSecret(m_cryptops, m_iF00D, secret, m_klicensee, ngpfs.files_salt, img_spec_to_crypto_engine_flag(ngpfs.image_spec), t->get_icv_salt(), 0);

         std::shared_ptr<sce_junction> found_path;

         if(img_spec_to_is_unicv(ngpfs.image_spec))
         {
            //in unicv - hash table has same order as sectors in a file
            const unsigned char* zeroSectorIcv = t->m_blocks.front().m_signatures.front().m_data.data();

            //try to find match by hash of zero sector
            found_path = brutforce_hashes(filesDbParser, fileDatas, secret, zeroSectorIcv);
         }
         else
         {
            try
            {
               //create merkle tree for corresponding table
               std::shared_ptr<merkle_tree<icv> > mkt = generate_merkle_tree<icv>(t->get_header()->get_numSectors());
               index_merkle_tree(mkt);

               //save merkle tree
               merkleTrees.push_back(std::make_pair(t, mkt));

               //use merkle tree to find index of zero sector in hash table
               std::pair<std::uint32_t, std::uint32_t> ctx;
               walk_tree(mkt, find_zero_sector_index, &ctx);

               //in icv - hash table is ordered according to merkle tree structure
               //that is why it is required to walk through the tree to find zero sector hash in hash table
               const unsigned char* zeroSectorIcv = t->m_blocks.front().m_signatures.at(ctx.second).m_data.data();

               //try to find match by hash of zero sector
               found_path = brutforce_hashes(filesDbParser, fileDatas, secret, zeroSectorIcv);
            }
            catch(std::runtime_error& e)
            {
               m_output << e.what() << std::endl;
               return -1;
            }
         }

         if(found_path)
         {
            m_output << "Match found: " << std::hex << t->get_icv_salt() << " " << *found_path << std::endl;
            m_pageMap.insert(std::make_pair(t->get_icv_salt(), *found_path));
         }
         else
         {
            m_output << "Match not found: " << std::hex << t->get_icv_salt() << std::endl;
            return -1;
         }
      }
   }

   //in icv - additional step checks that hash table corresponds to merkle tree
   if(!img_spec_to_is_unicv(ngpfs.image_spec))
   {
      if(validate_merkle_trees(filesDbParser, merkleTrees) < 0)
         return -1;
   }

   if(files.size() != (m_pageMap.size() + m_emptyFiles.size()))
   {
      m_output << "Extra files are left after mapping (warning): " << (files.size() - (m_pageMap.size() + m_emptyFiles.size())) << std::endl;
   }

   if(fileDatas.size() != 0)
   {
      for(auto& f : fileDatas)
         m_output << f.first << std::endl;
   }

   return 0;
}

//this is a test method that was supposed to be used for caching
int PfsPageMapper::load_page_map(const psvpfs::path& filepath, std::map<std::uint32_t, std::string>& pageMap) const
{
   const auto& fp = filepath;

   if(!psvpfs::exists(fp))
   {
      m_output << "File " << fp.generic_string() << " does not exist" << std::endl;
      return -1;
   }

   std::ifstream in(fp.generic_string().c_str());
   if(!in.is_open())
   {
      m_output << "Failed to open " << fp.generic_string() << std::endl;
      return -1;
   }

   std::string line;
   while(std::getline(in, line))
   {
      int index = line.find(' ');
      std::string pageStr = line.substr(0, index);
      std::string path = line.substr(index + 1);
      std::uint32_t page = std::atoi(pageStr.c_str());
      pageMap.insert(std::make_pair(page, path));
   }

   in.close();

   return 0;
}

const std::map<std::uint32_t, sce_junction>& PfsPageMapper::get_pageMap() const
{
   return m_pageMap;
}

const std::set<sce_junction>& PfsPageMapper::get_emptyFiles() const
{
   return m_emptyFiles;
}