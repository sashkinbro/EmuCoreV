#pragma once

#include <cstdint>
#include <vector>

bool scePfsIsRoImage(std::uint16_t image_spec);

bool scePfsIsRwImage(std::uint16_t image_spec);

std::uint16_t scePfsGetImageSpec(std::uint16_t mode_index);

int scePfsCheckImage(std::uint16_t mode_index, std::uint16_t expected_image_spec);

//----------------------

struct pfs_mode_settings
{
   std::uint32_t unk_0;
   std::uint32_t db_type;
   std::uint32_t unk_8;
   std::uint32_t unk_C;

   std::uint32_t unk_10;
   std::uint32_t unk_14;
   std::uint32_t unk_18;
   std::uint32_t unk_1C;

   std::uint32_t unk_20;
   std::uint32_t unk_24;
   std::uint32_t unk_28;
   std::uint32_t unk_2C;

   std::uint32_t unk_30;
   std::uint32_t unk_34;
   std::uint32_t unk_38;
   std::uint32_t unk_3C;
};

pfs_mode_settings* scePfsGetModeSetting(std::uint16_t mode_index);

//----------------------

enum pfs_image_types : std::uint16_t
{
   gamedata = 0,
   savedata = 1,
   ac_root = 2, // ADDCONT
   acid_dir = 3 // DLC
};

pfs_image_types img_spec_to_img_type(std::uint16_t image_spec);

std::uint16_t img_type_to_mode_index(pfs_image_types img_type);

//----------------------

struct mode_to_attr_entry_t
{
  std::uint32_t mode;
  std::uint16_t attr;
  std::uint16_t unk6;
};

#define MODE_RW_OR_NONE  0x180
#define MODE_RO  0x100
#define MORE_WO  0x080 //not sure
#define MODE_SYS 0x000

#define MODE_MASK1 (MODE_RW_OR_NONE | MODE_RO | MORE_WO | MODE_SYS)

#define MODE_AC    0x1000
#define MODE_DIR     0x8000
#define MODE_ACIDDIR (MODE_AC | MODE_DIR) // 0x9000

#define MODE_MASK2 (MODE_AC | MODE_DIR)

#define MODE_NENC    0x100000
#define MODE_NICV    0x200000
#define MODE_NPFS    (MODE_NENC | MODE_NICV) // 0x300000

#define MODE_MASK3 (MODE_NENC | MODE_NICV)

//

#define ATTR_RW_OR_NONE   0x0000
#define ATTR_WO   0x0000 //not sure
#define ATTR_RO   0x0001

#define ATTR_SYS1 0x0002
#define ATTR_SYS2 0x0004
#define ATTR_SYS  (ATTR_SYS1 | ATTR_SYS2) //0x0006

#define ATTR_UNK3 0x0400

#define ATTR_AC 0x1000

#define ATTR_NICV 0x2000 // does not have icv
#define ATTR_NENC 0x4000 // not encrypted
#define ATTR_NPFS (ATTR_NENC | ATTR_NICV) // 0x6000 - not pfs

#define ATTR_DIR  0x8000

//

int scePfsACSetFSAttrByMode(std::uint32_t mode, std::uint16_t* fs_attr);

int is_dir(char* string_id);

std::uint32_t get_file_mode(char* type_string, char* string_id);

std::uint16_t mode_to_attr(std::uint32_t mode, bool is_dir, std::uint16_t mode_index, std::uint32_t node_index);

//----------------------

bool is_gamedata(std::uint16_t mode_index);

//----------------------

enum db_types : std::uint32_t
{
   SCEIFTBL_RO = 0,      // isec_restart_ro
   SCEICVDB_RW = 1,      // isec_restart_rw
   SCEINULL_NULL_RW = 2, // isec_restart_null
   SCEIFTBL_NULL_RO = 3, // isec_restart_nullro
};

db_types db_type_value_to_db_type(std::uint32_t value);

//----------------------

db_types settings_to_db_type(std::uint16_t mode_index, std::uint16_t fs_attr, bool restart = false);

//----------------------

bool has_dbseed(db_types db_type, std::uint32_t icv_version);

//----------------------

std::uint16_t img_spec_to_crypto_engine_flag(std::uint16_t image_spec);

std::uint16_t img_spec_to_mode_index(std::uint16_t image_spec);

void is_unicv_to_img_types(bool isUnicv, std::vector<pfs_image_types>& possibleTypes);

bool db_type_to_is_unicv(db_types type);

bool img_spec_to_is_unicv(std::uint16_t image_spec);

//----------------------

#define CRYPTO_ENGINE_CRYPTO_USE_CMAC   0x0001 //setting this flag will force decryption calls to use cmac functions instead of decryption
#define CRYPTO_ENGINE_CRYPTO_USE_KEYGEN 0x0002 //setting this flag will force decryption calls to use "with keygen" dmac5 functions
#define CRYPTO_ENGINE_THROW_ERROR       0x0008 //setting this flag allows to throw errors if they happen during icv (signature) verification step

#define CRYPTO_ENGINE_SKIP_VERIFY       0x0020 //setting this flag allows to skip icv (signature) verification step when doing pfs decryption
#define CRYPTO_ENGINE_SKIP_DECRYPT      0x0040 //setting this flag together with CRYPTO_ENGINE_CRYPTO_USE_CMAC will skip decryption calls
