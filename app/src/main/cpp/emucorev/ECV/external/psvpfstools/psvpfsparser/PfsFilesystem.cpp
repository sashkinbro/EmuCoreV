#include "PfsFilesystem.h"

#include "PfsFile.h"

#include <cstring>

PfsFilesystem::PfsFilesystem(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, std::ostream& output,
                 const unsigned char* klicensee, const psvpfs::path& titleIdPath)
   : m_cryptops(cryptops), m_iF00D(iF00D), m_output(output), m_titleIdPath(titleIdPath)
{
   memcpy(m_klicensee, klicensee, 0x10);

   m_filesDbParser = std::unique_ptr<FilesDbParser>(new FilesDbParser(cryptops, iF00D, output, klicensee, titleIdPath));

   m_unicvDbParser = std::unique_ptr<UnicvDbParser>(new UnicvDbParser(titleIdPath, output));

   m_pageMapper = std::unique_ptr<PfsPageMapper>(new PfsPageMapper(cryptops, iF00D, output, klicensee, titleIdPath));
}

int PfsFilesystem::mount()
{
   if(m_filesDbParser->parse() < 0)
      return -1;

   if(m_unicvDbParser->parse() < 0)
      return -1;

   if(m_pageMapper->bruteforce_map(m_filesDbParser, m_unicvDbParser) < 0)
      return -1;

   return 0;
}

static void to_uppercase(std::string &str) {
   std::transform(str.begin(), str.end(), str.begin(), static_cast<int (*)(int)>(std::toupper));
}

int PfsFilesystem::decrypt_files(const psvpfs::path& destTitleIdPath, PfsProgressCallback progress) const
{
   const sce_ng_pfs_header_t& ngpfs = m_filesDbParser->get_header();
   const std::vector<sce_ng_pfs_file_t>& files = m_filesDbParser->get_files();
   const std::vector<sce_ng_pfs_dir_t>& dirs = m_filesDbParser->get_dirs();

   const std::unique_ptr<sce_idb_base_t>& unicv = m_unicvDbParser->get_idatabase();

   const std::map<std::uint32_t, sce_junction>& pageMap = m_pageMapper->get_pageMap();
   const std::set<sce_junction>& emptyFiles = m_pageMapper->get_emptyFiles();

   std::map<std::string, const sce_ng_pfs_file_t *> file_map;
   for (const auto &file : files) {
      std::string path = file.path().get_value().string();
      to_uppercase(path);

      file_map[path] = &file;
   }

   m_output << "Creating directories..." << std::endl;

   for(auto& d : dirs)
   {
      if(!d.path().create_empty_directory(m_titleIdPath, destTitleIdPath))
      {
         m_output << "Failed to create: " << d.path() << std::endl;
         return -1;
      }
      else
      {
         m_output << "Created: " << d.path() << std::endl;
      }
   }

   m_output << "Creating empty files..." << std::endl;

   for(auto& f : emptyFiles)
   {
      std::string path = f.get_value().string();
      to_uppercase(path);
      auto file = file_map.find(path);
      if (file == file_map.end())
      {
         m_output << "Ignored: " << f << std::endl;
      }
      else
      {
         if(!f.create_empty_file(m_titleIdPath, destTitleIdPath))
         {
            m_output << "Failed to create: " << f << std::endl;
            return -1;
         }
         else
         {
            m_output << "Created: " << f << std::endl;
         }
      }
   }

   m_output << "Decrypting files..." << std::endl;

   std::uint64_t total_bytes = 0;
   for (const auto& t : unicv->m_tables)
   {
     if (t->get_header()->get_numSectors() == 0)
       continue;

     auto me = pageMap.find(t->get_icv_salt());
     if (me == pageMap.end())
       continue;

     std::string p = me->second.get_value().string();
     to_uppercase(p);
     auto fp = file_map.find(p);
     if (fp != file_map.end())
       total_bytes += fp->second->file.m_info.header.size;
   }

   std::uint64_t processed_bytes = 0;

   for(auto& t : unicv->m_tables)
   {
      //skip empty files and directories
      if(t->get_header()->get_numSectors() == 0)
         continue;

      //find filepath by salt (filename for icv.db or page for unicv.db)
      auto map_entry = pageMap.find(t->get_icv_salt());
      if(map_entry == pageMap.end())
      {
         m_output << "failed to find page " << t->get_icv_salt() << " in map" << std::endl;
         return -1;
      }

      //find file in files.db by filepath
      sce_junction filepath = map_entry->second;

      if (progress)
        progress(processed_bytes, total_bytes, filepath.get_value().string());

      std::string path = filepath.get_value().string();
      to_uppercase(path);
      auto file_ptr = file_map.find(path);
      if (file_ptr == file_map.end())
      {
         m_output << "failed to find file " << filepath << " in flat file list" << std::endl;
         return -1;
      }
      auto &file = file_ptr->second;

      //directory and unexisting file are unexpected
      if(is_directory(file->file.m_info.header.type) || is_unexisting(file->file.m_info.header.type))
      {
         m_output << "Unexpected file type" << std::endl;
         return -1;
      }
      //copy unencrypted files
      else if(is_unencrypted(file->file.m_info.header.type))
      {
         if(!filepath.copy_existing_file(m_titleIdPath, destTitleIdPath, file->file.m_info.header.size))
         {
            m_output << "Failed to copy: " << filepath << std::endl;
            return -1;
         }
         else
         {
            m_output << "Copied: " << filepath << std::endl;
         }
      }
      //decrypt encrypted files
      else if(is_encrypted(file->file.m_info.header.type))
      {
         PfsFile pfsFile(m_cryptops, m_iF00D, m_output, m_klicensee, m_titleIdPath, *file, filepath, ngpfs, t);

         if(pfsFile.decrypt_file(destTitleIdPath) < 0)
         {
            m_output << "Failed to decrypt: " << filepath << std::endl;
            return -1;
         }
         else
         {
            m_output << "Decrypted: " << filepath << std::endl;
         }
      }
      else
      {
         m_output << "Unexpected file type" << std::endl;
         return -1;
      }
      processed_bytes += file->file.m_info.header.size;
      if (progress)
        progress(processed_bytes, total_bytes, filepath.get_value().string());
   }

   return 0;
}