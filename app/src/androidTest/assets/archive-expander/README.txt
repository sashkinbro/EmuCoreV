Archive expander instrumentation fixtures

These tiny archives contain only synthetic test bytes; they contain no Vita
game, PKG license, save data, or user archive content.

stored.7z is a generated uncompressed 7z archive containing the text payload
"Hello archive backend". lzma2.7z is a generated LZMA2-compressed 7z archive
with the same payload.

rar5-compressed.rar is the compressed RAR5 fixture
libarchive/test/test_read_format_rar5_compressed.rar from libarchive v3.8.11:
https://github.com/libarchive/libarchive/blob/v3.8.11/libarchive/test/test_read_format_rar5_compressed.rar

rar4-singlefile.rar is derived from libarchive/test/test_read_format_rar.rar
from libarchive v3.8.11. It retains the authentic RAR4 header and the first
stored regular-file entry and omits the symlink and directory entries:
https://github.com/libarchive/libarchive/blob/v3.8.11/libarchive/test/test_read_format_rar.rar

The two RAR fixtures are distributed under the libarchive test suite's BSD
2-Clause terms. Copyright (c) 2003-2007 Tim Kientzle; Copyright (c) 2011
Andres Mejia; Copyright (c) 2011-2012 Michihiro NAKAJIMA; Copyright (c) 2026
Eduard Zateev. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
