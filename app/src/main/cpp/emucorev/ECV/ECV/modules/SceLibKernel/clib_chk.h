// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#pragma once

// Destination-size checks of the sceClib*Chk (fortify) exports, free of emulator state.
// The guest passes the compiler-known object size as the trailing size argument (glibc
// __*_chk / bionic convention: PROVISIONAL, no SDK header for these exports ships in this
// tree). Each fits_* predicate returns true when the operation FITS; false means real
// firmware would abort. Strings are never scanned past what the destination could hold, so a
// missing terminator cannot cause an over-read.

#include <cstddef>

namespace clib_chk {

// Bounded strlen: stops at `max` even without a terminator.
inline size_t strnlen_bounded(const char *s, size_t max) {
    size_t n = 0;
    while (n < max && s[n] != '\0')
        ++n;
    return n;
}

// memcpy / memmove / memset / strncpy: len bytes are written.
constexpr bool fits_bytes(size_t dst_size, size_t len) { return len <= dst_size; }

// strcpy: strlen(src) + 1 bytes are written.
inline bool fits_strcpy(const char *src, size_t dst_size) {
    return strnlen_bounded(src, dst_size) < dst_size;
}

// strcat: strlen(dst) + strlen(src) + 1 bytes must fit.
inline bool fits_strcat(const char *dst, const char *src, size_t dst_size) {
    const size_t dlen = strnlen_bounded(dst, dst_size);
    if (dlen >= dst_size)
        return false;
    const size_t remaining = dst_size - dlen;
    return strnlen_bounded(src, remaining) < remaining;
}

// strncat: strlen(dst) + min(strlen(src), n) + 1 bytes must fit.
inline bool fits_strncat(const char *dst, const char *src, size_t n, size_t dst_size) {
    const size_t dlen = strnlen_bounded(dst, dst_size);
    if (dlen >= dst_size)
        return false;
    const size_t remaining = dst_size - dlen;
    const size_t slen = strnlen_bounded(src, n < remaining ? n : remaining);
    return slen < remaining;
}

// strlcpy / strlcat / snprintf / vsnprintf: the caller-supplied buffer size must not exceed
// the real object size (bionic __strlcpy_chk / glibc __snprintf_chk rule).
constexpr bool fits_size(size_t dst_size, size_t claimed_size) { return claimed_size <= dst_size; }

// BSD strlcpy: copies at most size-1 bytes, always terminates (size > 0), returns strlen(src).
inline size_t strlcpy_impl(char *dst, const char *src, size_t size) {
    size_t slen = 0;
    while (src[slen] != '\0')
        ++slen;
    if (size) {
        const size_t n = slen < size - 1 ? slen : size - 1;
        for (size_t i = 0; i < n; ++i)
            dst[i] = src[i];
        dst[n] = '\0';
    }
    return slen;
}

// BSD strlcat: size is the total size of dst; returns min(size, strlen(dst)) + strlen(src).
inline size_t strlcat_impl(char *dst, const char *src, size_t size) {
    const size_t dlen = strnlen_bounded(dst, size);
    size_t slen = 0;
    while (src[slen] != '\0')
        ++slen;
    if (dlen == size)
        return size + slen;
    const size_t n = slen < size - dlen - 1 ? slen : size - dlen - 1;
    for (size_t i = 0; i < n; ++i)
        dst[dlen + i] = src[i];
    dst[dlen + n] = '\0';
    return dlen + slen;
}

} // namespace clib_chk
