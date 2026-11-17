#pragma once

#include <module/module.h>
#include <packages/firmware_info.h>

DECL_EXPORT(int, sceSblUsGetSpkgInfo, int package_type, Ptr<SceSblUsSpkgInfo> info);
