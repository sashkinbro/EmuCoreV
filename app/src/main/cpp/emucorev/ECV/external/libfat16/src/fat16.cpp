#include <cstdio>
#include <fat16/fat16.h>
#include <cstddef>
#include <iostream>
#include <algorithm>

namespace Fat16 {
    // https://www.win.tue.nl/~aeb/linux/fs/fat/fat-1.html
    // reserved blocks -> fat -> root directory -> data area
    std::uint32_t BootBlock::fat_region_start() const {
        return num_reserved_blocks * bytes_per_block;
    }

    std::uint32_t BootBlock::root_directory_region_start() const {
        return fat_region_start() + (num_fat * num_blocks_per_fat) * bytes_per_block;
    }

    std::uint32_t BootBlock::data_region_start() const {
        return root_directory_region_start() + (num_root_dirs * sizeof(FundamentalEntry));
    }

    std::string FundamentalEntry::get_filename() {
        EntryType etype = get_entry_type_from_filename();
        std::string fname(reinterpret_cast<char*>(filename));

        if (fname.length() > sizeof(filename) / sizeof(char)) {
            fname = fname.substr(0, sizeof(filename) / sizeof(char));
        }

        if (etype != EntryType::FILE && fname.length() > 0) {
            fname.erase(0, 1);
        }

        if (fname.length() > 0 && fname[0] == 0x05) {
            // Transform it to E5, since that's the actual name
            fname[0] = 0xE5;
        }

        // Delete padding spaces
        while (fname.length() > 0 && fname.back() == ' ') {
            fname.pop_back();
        }

        return fname;
    }

    EntryType FundamentalEntry::get_entry_type_from_filename() {
        switch (filename[0]) {
        case 0x00: return EntryType::UNUSED;
        case 0xE5: return EntryType::DELETED;
        case 0x2E: return EntryType::DIRECTORY;
        default: break;
        }

        return EntryType::FILE;
    }

    std::uint32_t Image::get_current_image_offset() {
        return seek_func(userdata, 0, IMAGE_SEEK_MODE_CUR);
    }
    
    ClusterID Image::get_successor_cluster(const ClusterID target) {
        const std::uint32_t current = get_current_image_offset();

        // Seek to beginning of the FAT
        seek_func(userdata, boot_block.fat_region_start() + (target * 2), IMAGE_SEEK_MODE_BEG);
        ClusterID next = 0;
    
        if (read_func(userdata, &next, sizeof(ClusterID)) != sizeof(ClusterID)) {
            return 0;
        }
        
        seek_func(userdata, current, IMAGE_SEEK_MODE_BEG);
        return next;
    }

    std::uint32_t Image::bytes_per_cluster() const {
        return boot_block.bytes_per_block * boot_block.num_blocks_per_allocation_unit;
    }

    std::uint32_t Image::read_from_cluster(std::uint8_t *dest_buffer, const std::uint32_t offset, const ClusterID starting_cluster,
        const std::uint32_t size) {
        // Calculate total number of cluster we need to traverse
        const std::uint32_t total_block_to_read = (size + boot_block.bytes_per_block - 1) / boot_block.bytes_per_block;
        const std::uint32_t total_cluster_to_read = (total_block_to_read + boot_block.num_blocks_per_allocation_unit - 1)
            / boot_block.num_blocks_per_allocation_unit;

        std::uint32_t from_start_cluster_dist = offset / boot_block.bytes_per_block;
        from_start_cluster_dist = (from_start_cluster_dist) / boot_block.num_blocks_per_allocation_unit;

        std::uint32_t offset_in_that_cluster = offset % bytes_per_cluster();

        ClusterID current_cluster = starting_cluster;
        std::uint32_t offset_start_data_area = 0;

        while (from_start_cluster_dist != 0) {
            current_cluster = get_successor_cluster(current_cluster);
            from_start_cluster_dist--;
        }

        // Add the FAT with the boot block, then add the root directory entries size
        offset_start_data_area = boot_block.data_region_start();
        offset_start_data_area += offset_in_that_cluster;

        // Let's seek to that place.
        seek_func(userdata, offset_start_data_area, IMAGE_SEEK_MODE_BEG);

        std::uint32_t total_bytes_left_to_read = size;

        for (std::uint8_t i = 0; i < total_cluster_to_read; i++) {
            // Seek to the given cluster.
            seek_func(userdata, offset_start_data_area + (current_cluster - 2) * bytes_per_cluster(), IMAGE_SEEK_MODE_BEG);
            const std::uint32_t size_to_read_this_take = std::min<std::uint32_t>(bytes_per_cluster(), total_bytes_left_to_read);

            // Read it
            read_func(userdata, dest_buffer, size_to_read_this_take);

            total_bytes_left_to_read -= size_to_read_this_take;
            dest_buffer += size_to_read_this_take;

            // LINK... WAKE UP!!!! WE GOT A VILLAGE TO BURN
            current_cluster = get_successor_cluster(current_cluster);
        }

        // Return the total of bytes read. Calculated by this formula.
        return size - total_bytes_left_to_read;
    }

    bool Image::get_next_entry(Entry &entry) {
        if (entry.cursor_record / 32 >= boot_block.num_root_dirs) {
            return false;
        }

        std::uint32_t offset_root_dir = 0;

        // Add the FAT with the boot block, then add the root directory entries size
        offset_root_dir = boot_block.root_directory_region_start();
        seek_func(userdata, offset_root_dir + entry.cursor_record, IMAGE_SEEK_MODE_BEG);

        LongFileNameEntry extended_entry;
        entry.extended_entries.clear();
        
        do {
            if (entry.root) {
                if (read_from_cluster(reinterpret_cast<std::uint8_t*>(&extended_entry), entry.cursor_record,
                        entry.root, sizeof(LongFileNameEntry)) != sizeof(LongFileNameEntry)) {
                    return false;
                }
            } else if (read_func(userdata, &extended_entry, sizeof(LongFileNameEntry)) != sizeof(LongFileNameEntry)) {
                return false;
            }

            if (extended_entry.attrib == 0x0F && extended_entry.padding == 0) {
                // Definitely is
                entry.cursor_record += sizeof(LongFileNameEntry);
                entry.extended_entries.push_back(extended_entry);
            } else {
                // Seek back
                if (!entry.root)
                    seek_func(userdata, offset_root_dir + entry.cursor_record, IMAGE_SEEK_MODE_BEG);
                
                break;
            }
        } while (true && (entry.cursor_record / 32 != boot_block.num_root_dirs));

        // Try to do fundamental entry read
        if (entry.root) {
            if (read_from_cluster(reinterpret_cast<std::uint8_t*>(&entry.entry), entry.cursor_record,
                    entry.root, sizeof(FundamentalEntry)) != sizeof(FundamentalEntry)) {
                return false;
            }
        } else if (read_func(userdata, &entry.entry, sizeof(FundamentalEntry)) != sizeof(FundamentalEntry)) {
            return false;
        }
        
        entry.cursor_record += sizeof(FundamentalEntry);

        return true;
    }

    bool Image::get_first_entry_dir(Entry &parent, Entry &first) {
        if ((parent.entry.file_attributes & (int)EntryAttribute::DIRECTORY) == 0) {
            return false;
        }

        first.root = parent.entry.starting_cluster;
        first.cursor_record = 0;

        return true;
    }
    
    Image::Image(void *userdata, ImageReadFunc read_func, ImageSeekFunc seek_func)
        : read_func(read_func)
        , seek_func(seek_func)
        , userdata(userdata) {
        seek_func(userdata, 0, IMAGE_SEEK_MODE_BEG);
        if (read_func(userdata, &boot_block, sizeof(BootBlock)) != sizeof(BootBlock)) {
            // TODO:
        }
    }

    std::u16string Entry::get_filename() {
        if (extended_entries.size() != 0) {
            // Use name from extended entries
            std::u16string final_name;

            for (std::intptr_t j = extended_entries.size() - 1; j >= 0; j--) {
                volatile int i = 0;

                while (extended_entries[j].name_part_1[i] != 0 && i < 5) {
                    final_name += extended_entries[j].name_part_1[i++];
                }

                if (i < 5 && extended_entries[j].name_part_1[i] == 0) {
                    break;
                }
                
                i = 0;

                while (extended_entries[j].name_part_2[i] != 0 && i < 6) {
                    final_name += extended_entries[j].name_part_2[i++];
                }
                
                if (i < 6 && extended_entries[j].name_part_2[i] == 0) {
                    break;
                }

                i = 0;

                while (extended_entries[j].name_part_3[i] != 0 && i < 2) {
                    final_name += extended_entries[j].name_part_3[i++];
                }
                
                if (i < 2 && extended_entries[j].name_part_3[i] == 0) {
                    break;
                }
            }

            return final_name;
        }

        // Use fundamental name.
        std::string final_name = entry.get_filename() + std::string(entry.filename_ext, 3);
        while (final_name.length() > 0 && final_name.back() == ' ') final_name.pop_back();

        return std::u16string(final_name.begin(), final_name.end());
    }
}
