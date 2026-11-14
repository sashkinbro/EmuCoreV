#include <b64/cdecode.h>
#include <zRIF/keyflate.h>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

void zrif2rif(const std::string &zrif, std::ofstream &outfile) {
    constexpr auto MAX_KEY_SIZE = 2048;
    constexpr auto MIN_KEY_SIZE = 512;

    unsigned char out[MIN_KEY_SIZE];
    unsigned char license_data[MIN_KEY_SIZE];
    base64_decodestate state;
    base64_init_decodestate(&state);
    base64_decode_block(zrif.c_str(), zrif.size(), (char *)out, &state);
    inflateKey(out, MIN_KEY_SIZE, license_data, MIN_KEY_SIZE);

    outfile.write((char *)license_data, sizeof(license_data));
    outfile.close();
}
