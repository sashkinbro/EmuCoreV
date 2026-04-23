#pragma once

#include <cstdint>
#include <string>

#include <vector>

namespace Fat16 {
    #pragma pack(push, 1)
    struct BootBlock {
        std::uint8_t jump_code[3];
        std::uint8_t manufacturer_description[8];
        std::uint16_t bytes_per_block;
        std::uint8_t num_blocks_per_allocation_unit;
        std::uint16_t num_reserved_blocks;
        std::uint8_t num_fat;                       ///< Number of FAT (File Allocation Table).
        std::uint16_t num_root_dirs;                ///< Number of root directories.
        std::uint16_t num_blocks_in_image_op1;      ///< Total number of block in this images. Option 1
        std::uint8_t media_descriptor;
        std::uint16_t num_blocks_per_fat;           ///< Number of blocks occupied by one FAT.
        std::uint16_t num_blocks_per_track;
        std::uint16_t num_heads;
        std::uint32_t num_hidden_blocks;
        std::uint32_t num_blocks_in_image_op2;      ///< Total number of block in this images. Option 2
        std::uint16_t physical_driver_num;
        std::uint8_t extended_boot_record_signature;
        std::uint32_t volume_sig_num;
        char volume_label[11];
        char file_sys_id[8];
        std::uint8_t bootstrap_code[0x1C0];
        std::uint16_t boot_block_sig;

        std::uint32_t fat_region_start() const;
        std::uint32_t root_directory_region_start() const;
        std::uint32_t data_region_start() const;
    };
    #pragma pack(pop)
    
    enum class EntryType {
        FILE = 0,
        DIRECTORY = 1,
        DELETED = 2,
        UNUSED = 3
    };

    enum class EntryAttribute {
        NONE = 0,
        READONLY = 0x1,
        HIDDEN = 0x2,
        SYSFILE = 0x4,
        SPECIAL = 0x8,
        DIRECTORY = 0x10,
        ARCHIVE = 0x20,
        LFN = READONLY | HIDDEN | SYSFILE | SPECIAL
    };

    #pragma pack(push, 1)
    struct FundamentalEntry {
        std::uint8_t filename[8];
        char filename_ext[3];
        std::uint8_t file_attributes;
        std::uint8_t reserved[10];
        std::uint16_t last_modified_time;
        std::uint16_t last_modified_date;
        std::uint16_t starting_cluster;
        std::uint32_t file_size;

        std::string get_filename();
        EntryType get_entry_type_from_filename();
    };
    #pragma pack(pop)
    
    #pragma pack(push, 1)
    struct LongFileNameEntry {
        std::uint8_t position;
        char16_t name_part_1[5];
        std::uint8_t attrib;
        std::uint8_t type;
        std::uint8_t checksum;
        char16_t name_part_2[6];
        std::uint16_t padding;
        char16_t name_part_3[2];
    };
    #pragma pack(pop)

    // Numbered from 2
    using ClusterID = std::uint16_t;

    struct Entry {
    private:
        friend struct Image;
        std::uint32_t cursor_record;
        ClusterID root;

    public:
        FundamentalEntry entry;
        std::vector<LongFileNameEntry> extended_entries;

        explicit Entry()
            : cursor_record(0)
            , root(0) {
        }

        std::u16string get_filename();
    };

    // These functions all required return value to be little-endian.
    typedef std::uint32_t (*ImageReadFunc)(void *userdata, void *buffer, std::uint32_t bytes);
    typedef std::uint32_t (*ImageSeekFunc)(void *userdata, std::uint32_t offset, int mode);

    enum ImageSeekMode {
        IMAGE_SEEK_MODE_BEG,
        IMAGE_SEEK_MODE_CUR,
        IMAGE_SEEK_MODE_END
    };

    struct Image {
        BootBlock boot_block;
        ImageReadFunc read_func;
        ImageSeekFunc seek_func;
        void *userdata;

        explicit Image(void *userdata, ImageReadFunc read_func, ImageSeekFunc seek_func);

        /**
         * \brief Get the next entry to given entry.
         * 
         * \returns True if the number is valid and the get performs success.
         */
        bool get_next_entry(Entry &entry);

        /**
         * \brief       Get first entry of a directory.
         * \returns     True on success.
         */
        bool get_first_entry_dir(Entry &parent, Entry &first);

        /**
         * \brief Get total of bytes a cluster consists of.
         */
        std::uint32_t bytes_per_cluster() const;

        /**
         * \brief   Reading data from the FAT image, starting at given cluster.
         * 
         * \param   dest_buffer       The buffer contains read result.
         * \param   starting_cluster  The first cluster that contains the data.
         * \param   size              The size of data to read.
         * 
         * \returns Number of bytes read.
         */
        std::uint32_t read_from_cluster(std::uint8_t *dest_buffer, const std::uint32_t offset,
            const ClusterID starting_cluster, const std::uint32_t size);

        /**
         * \brief Get the read cursor of current image.
         * \internal
         */
        std::uint32_t get_current_image_offset();

        /**
         * \brief   Get the successor cluster ID of the given cluster.
         * \param   target The ID of the cluster.
         * \returns Successor cluster ID.
         */
        ClusterID get_successor_cluster(const ClusterID target);
    };

    static_assert(sizeof(BootBlock) == 512, "Boot block size doesn't match to what expected.");
    static_assert(sizeof(FundamentalEntry) == 32, "Fundamental entry size doesn't match to what expected.");
    static_assert(sizeof(LongFileNameEntry) == 32, "LFN entry size doesn't match to what expected.");
}