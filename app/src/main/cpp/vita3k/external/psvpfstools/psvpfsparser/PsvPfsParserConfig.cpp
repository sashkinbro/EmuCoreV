#include <string>
#include <iostream>
#include <fstream>
#include <stdio.h>
#include <iomanip>

#include <zRIF/rif.h>
#include <zRIF/licdec.h>

#include "PfsFilesystem.h"

#include "F00DKeyEncryptorFactory.h"
#include "CryptoOperationsFactory.h"
#include "PsvPfsParserConfig.h"
#include "LocalKeyGenerator.h"

int execute(std::shared_ptr<ICryptoOperations> cryptops, std::shared_ptr<IF00DKeyEncryptor> iF00D, const unsigned char *klicensee, const psvpfs::path& titleIdPath, const psvpfs::path& destTitleIdPath) {
    PfsFilesystem pfs(cryptops, iF00D, std::cout, klicensee, titleIdPath);

    if (pfs.mount() < 0)
        return -1;

    if (pfs.decrypt_files(destTitleIdPath) < 0)
        return -1;

    std::cout << "keystone sanity check..." << std::endl;

    if (get_keystone(cryptops, destTitleIdPath) < 0)
        return -1;

    std::cout << "F00D cache:" << std::endl;
    iF00D->print_cache(std::cout);

    return 0;
}

int extract_klicensee(const PsvPfsParserConfig &cfg, std::shared_ptr<ICryptoOperations> cryptops, unsigned char *klicensee) {
    if (cfg.klicensee.length() > 0) {
        if (string_to_byte_array(cfg.klicensee, 0x10, klicensee) < 0) {
            std::cout << "Failed to parse klicensee" << std::endl;
            return -1;
        }
    } else if (cfg.zRIF.length() > 0) {
        std::shared_ptr<SceNpDrmLicense> lic = decode_license_np(cfg.zRIF);
        if (!lic) {
            std::cout << "Failed to decode zRIF string" << std::endl;
            return -1;
        }
        memcpy(klicensee, lic->key, 0x10);
    } else {
        std::cout << "using sealedkey..." << std::endl;

        if (get_sealedkey(cryptops, cfg.title_id_src, klicensee) < 0)
            return -1;
    }

    return 0;
}

std::shared_ptr<IF00DKeyEncryptor> create_F00D_encryptor(const PsvPfsParserConfig &cfg, std::shared_ptr<ICryptoOperations> cryptops) {
    std::shared_ptr<IF00DKeyEncryptor> iF00D;

    switch (cfg.f00d_enc_type) {
    case F00DEncryptorTypes::file:
        iF00D = F00DKeyEncryptorFactory::create(cfg.f00d_enc_type, cfg.f00d_arg);
        break;
    case F00DEncryptorTypes::native:
        iF00D = F00DKeyEncryptorFactory::create(cfg.f00d_enc_type, cryptops);
        break;
    default:
        throw std::runtime_error("unexpected F00DEncryptorTypes value");
    }

    return iF00D;
}

int execute(std::string &zrif, std::string &title_src, std::string &title_dst, F00DEncryptorTypes type, std::string &f00d_arg) {
    PsvPfsParserConfig cfg;

    cfg.zRIF = zrif;
    cfg.title_id_src = title_src;
    cfg.title_id_dst = title_dst;
    cfg.f00d_enc_type = type;
    cfg.f00d_arg = f00d_arg;
    std::shared_ptr<ICryptoOperations> cryptops = CryptoOperationsFactory::create(CryptoOperationsTypes::openssl);
    std::shared_ptr<IF00DKeyEncryptor> iF00D = create_F00D_encryptor(cfg, cryptops);

    unsigned char klicensee[0x10] = { 0 };
    if (extract_klicensee(cfg, cryptops, klicensee) < 0)
        return -1;

    return execute(cryptops, iF00D, klicensee, psvpfs::path{cfg.title_id_src}, psvpfs::path{cfg.title_id_dst});
}
