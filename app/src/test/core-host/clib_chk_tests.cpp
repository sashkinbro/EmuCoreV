// Host tests for the sceClib*Chk destination-size logic (bound boundaries) and the BSD
// strlcpy/strlcat implementations used by the Chk exports.
#include <clib_chk.h>

#include <gtest/gtest.h>

#include <cstring>

namespace cc = clib_chk;

TEST(ClibChk, BytesBoundary) {
    EXPECT_TRUE(cc::fits_bytes(8, 0));
    EXPECT_TRUE(cc::fits_bytes(8, 8));
    EXPECT_FALSE(cc::fits_bytes(8, 9));
    EXPECT_TRUE(cc::fits_bytes(0, 0));
    EXPECT_FALSE(cc::fits_bytes(0, 1));
}

TEST(ClibChk, StrcpyNeedsRoomForTerminator) {
    EXPECT_TRUE(cc::fits_strcpy("abc", 4));
    EXPECT_FALSE(cc::fits_strcpy("abc", 3));
    EXPECT_TRUE(cc::fits_strcpy("", 1));
    EXPECT_FALSE(cc::fits_strcpy("", 0));
}

TEST(ClibChk, StrcpyDoesNotOverreadUnterminatedSource) {
    const char src[3] = { 'a', 'b', 'c' }; // no terminator
    EXPECT_FALSE(cc::fits_strcpy(src, 3)); // scan is capped at dst_size
}

TEST(ClibChk, StrcatBoundary) {
    char dst[8] = "ab";
    EXPECT_TRUE(cc::fits_strcat(dst, "cde", 6)); // 2+3+1 == 6
    EXPECT_FALSE(cc::fits_strcat(dst, "cdef", 6)); // 2+4+1 == 7
    EXPECT_TRUE(cc::fits_strcat(dst, "", 3));
    EXPECT_FALSE(cc::fits_strcat(dst, "", 2)); // no room for the terminator behind "ab"
}

TEST(ClibChk, StrcatUnterminatedDestination) {
    const char dst[4] = { 'a', 'b', 'c', 'd' };
    EXPECT_FALSE(cc::fits_strcat(dst, "", 4));
}

TEST(ClibChk, StrncatBoundary) {
    char dst[8] = "ab";
    EXPECT_TRUE(cc::fits_strncat(dst, "cdefgh", 3, 6)); // only 3 of src count: 2+3+1
    EXPECT_FALSE(cc::fits_strncat(dst, "cdefgh", 4, 6)); // 2+4+1
    EXPECT_TRUE(cc::fits_strncat(dst, "c", 100, 4)); // src shorter than n
    EXPECT_FALSE(cc::fits_strncat(dst, "cd", 100, 4));
    EXPECT_TRUE(cc::fits_strncat(dst, "xyz", 0, 3)); // n == 0 appends only the terminator
}

TEST(ClibChk, SizeClaimBoundary) {
    EXPECT_TRUE(cc::fits_size(16, 16));
    EXPECT_FALSE(cc::fits_size(16, 17));
    EXPECT_TRUE(cc::fits_size(16, 0));
}

TEST(ClibChk, StrlcpyBsdSemantics) {
    char buf[8];
    memset(buf, 'x', sizeof(buf));
    EXPECT_EQ(cc::strlcpy_impl(buf, "hello", 8), 5u);
    EXPECT_STREQ(buf, "hello");
    EXPECT_EQ(cc::strlcpy_impl(buf, "hello world", 4), 11u); // truncated, terminated
    EXPECT_STREQ(buf, "hel");
    memset(buf, 'x', sizeof(buf));
    EXPECT_EQ(cc::strlcpy_impl(buf, "abc", 0), 3u); // size 0: nothing written
    EXPECT_EQ(buf[0], 'x');
}

TEST(ClibChk, StrlcatBsdSemantics) {
    char buf[8] = "ab";
    EXPECT_EQ(cc::strlcat_impl(buf, "cd", 8), 4u);
    EXPECT_STREQ(buf, "abcd");
    EXPECT_EQ(cc::strlcat_impl(buf, "efghij", 8), 10u); // truncated to 7 chars
    EXPECT_STREQ(buf, "abcdefg");
    char full[4] = { 'a', 'b', 'c', 'd' }; // unterminated within size
    EXPECT_EQ(cc::strlcat_impl(full, "zz", 4), 6u); // size + strlen(src), dst untouched
    EXPECT_EQ(full[3], 'd');
}
