//this file is based on the code from:
//https://github.com/weaknespase/PkgDecrypt
//Thanks to:
//weaknespase
//St4rk

#pragma once

#include <stdint.h>
#include <string>
#include <memory>

#include "rif.h"

std::shared_ptr<SceNpDrmLicense> decode_license_np(std::string zRIF);
std::shared_ptr<ScePsmDrmLicense> decode_license_psm(std::string zRIF);