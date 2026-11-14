//this file is taken form here:
//https://github.com/weaknespase/PkgDecrypt
//Thanks to:
//weaknespase
//St4rk

#pragma once

//---------------------------------------------
// From NoNpDRM by theFlow
//---------------------------------------------

#include <stdint.h>

#define FAKE_AID 0x0123456789ABCDEFLL

typedef struct {
    uint16_t version;              // 0x00
    uint16_t version_flag;         // 0x02
    uint16_t type;                 // 0x04
    uint16_t flags;                // 0x06
    uint64_t aid;                  // 0x08
    char content_id[0x30];         // 0x10
    uint8_t key_table[0x10];       // 0x40
    uint8_t key[0x10];             // 0x50
    uint64_t start_time;           // 0x60
    uint64_t expiration_time;      // 0x68
    uint8_t ecdsa_signature[0x28]; // 0x70

    uint64_t flags2;               // 0x98
    uint8_t key2[0x10];            // 0xA0
    uint8_t unk_B0[0x10];          // 0xB0
    uint8_t openpsid[0x10];        // 0xC0
    uint8_t unk_D0[0x10];          // 0xD0
    uint8_t cmd56_handshake[0x14]; // 0xE0
    uint32_t unk_F4;               // 0xF4
    uint32_t unk_F8;               // 0xF8
    uint32_t sku_flag;             // 0xFC
    uint8_t rsa_signature[0x100];  // 0x100
} SceNpDrmLicense;

const uint32_t SceNpDrmLicenseSize = 0x200;

//---------------------------------------------
// From NoPsmDrm by frangarcj
//---------------------------------------------

typedef struct {
    char magic[0x8];             // 0x00
    uint32_t unk1;               // 0x08
    uint32_t unk2;               // 0x0C
    uint64_t aid;                // 0x10
    uint32_t unk3;               // 0x18
    uint32_t unk4;               // 0x1C
    uint64_t start_time;         // 0x20
    uint64_t expiration_time;    // 0x28
    uint8_t act_digest[0x20];    // 0x30
    char content_id[0x30];       // 0x50
    uint8_t unk5[0x80];          // 0x80
    uint8_t key[0x200];          // 0x100
    uint8_t sha256digest[0x100]; // 0x300
} ScePsmDrmLicense;

const uint32_t ScePsmDrmLicenseSize = 0x400;

//---------------------------------------------