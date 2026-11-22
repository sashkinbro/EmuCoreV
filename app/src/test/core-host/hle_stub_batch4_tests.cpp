// Host tests for the pure helpers behind the fourth HLE stub batch: the system software version
// reported by sceKernelGetSystemSwVersion and the AppUtil boot parameter version.
#include <app_util_helpers.h>
#include <sw_version.h>

#include <gtest/gtest.h>

#include <cstring>
#include <string>

TEST(SwVersion, PackingMatchesFirmwareStyle) {
    EXPECT_EQ(sw_version::pack(3, 740, 11), 0x03740011u);
    EXPECT_EQ(sw_version::pack(3, 600, 11), 0x03600011u);
    EXPECT_EQ(sw_version::pack(1, 690, 1), 0x01690001u);
    EXPECT_EQ(sw_version::VERSION, 0x03740011u);
}

TEST(SwVersion, FillWritesSizeStringAndVersion) {
    sw_version::FwInfo info;
    std::memset(&info, 0xAA, sizeof(info));
    sw_version::fill(info);
    EXPECT_EQ(info.size, 0x28u);
    EXPECT_STREQ(info.version_string, "3.740.011");
    EXPECT_EQ(info.version, 0x03740011u);
    EXPECT_EQ(info.unk_24, 0u);
    // the whole string field is zero padded after the terminator
    for (size_t i = sizeof("3.740.011"); i < sizeof(info.version_string); i++)
        EXPECT_EQ(info.version_string[i], 0) << i;
}

TEST(AppUtilBootParam, AppVersionFromAppVer) {
    EXPECT_EQ(app_util::boot_param_app_version("01.00"), 0x0100u);
    EXPECT_EQ(app_util::boot_param_app_version("01.05"), 0x0105u);
    EXPECT_EQ(app_util::boot_param_app_version("12.34"), 0x1234u);
}

TEST(AppUtilBootParam, MalformedAppVerIsZero) {
    EXPECT_EQ(app_util::boot_param_app_version(""), 0u);
    EXPECT_EQ(app_util::boot_param_app_version("0105"), 0u);
    EXPECT_EQ(app_util::boot_param_app_version("01."), 0u);
    EXPECT_EQ(app_util::boot_param_app_version(".05"), 0u);
    EXPECT_EQ(app_util::boot_param_app_version("0a.05"), 0u);
    EXPECT_EQ(app_util::boot_param_app_version("001.05"), 0u);
}
