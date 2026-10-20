#!/usr/bin/env python3
"""Exercise production native archive admission with a ZIP-directory reader seam.

The real path normalization, marker detection and content enumeration bodies are
compiled unchanged. Only miniz's directory access is replaced by fixture entries.
No emulator, firmware, device or archive installation is required.
"""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
out = root / 'app/build/vitamin-native-probe'
out.mkdir(parents=True, exist_ok=True)
if os.name == 'nt' and Path('C:/msys64/ucrt64/bin').is_dir():
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
source = (root / 'app/src/main/cpp/vita3k/vita3k/interface.cpp').read_text()


def block(marker):
    assert source.count(marker) == 1, marker
    start = source.index(marker)
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end + 1]
    raise AssertionError(marker)


cpp = r'''
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>
namespace fs = std::filesystem;
namespace fs_utils {auto utf8_to_path(const std::string &s){return fs::path(s);}}
#define LOG_ERROR(...) ((void)0)
#define LOG_WARN(...) ((void)0)
#define LOG_CRITICAL(...) ((void)0)
using mz_uint = unsigned;
struct Entry {std::string name; bool directory = false; std::string data = "runtime"; bool readable = true;};
struct mz_zip_archive {std::vector<Entry> entries;};
struct mz_zip_archive_file_stat {const char *m_filename; uint64_t m_uncomp_size; uint32_t m_crc32;};
using ZipPtr = std::unique_ptr<mz_zip_archive>;
auto mz_zip_reader_get_num_files(mz_zip_archive *z){return unsigned(z->entries.size());}
bool mz_zip_reader_file_stat(mz_zip_archive *z,unsigned i,mz_zip_archive_file_stat *s){
 if(i>=z->entries.size())return false;s->m_filename=z->entries[i].name.c_str();s->m_uncomp_size=z->entries[i].data.size();s->m_crc32=42;return true;}
bool mz_zip_reader_is_file_a_directory(mz_zip_archive *z,unsigned i){return z->entries[i].directory;}
bool mz_zip_reader_extract_to_mem(mz_zip_archive *z,unsigned i,void* out,size_t size,unsigned){
 auto &e=z->entries[i];if(!e.readable||size!=e.data.size())return false;memcpy(out,e.data.data(),size);return true;}
'''
for marker in ('static std::string normalize_archive_path', 'struct ArchiveContentPath',
               'static bool is_safe_archive_relative_path',
               'static std::optional<std::string> archive_content_root',
               'static std::string archive_path_identity',
               'static bool is_vitamin_marker_path'):
    cpp += block(marker) + (';' if marker.startswith('struct ') else '') + '\n'
if 'static bool is_identical_vitamin_duplicate' in source:
    cpp += block('static bool is_identical_vitamin_duplicate') + '\n'
cpp += block('static std::vector<ArchiveContentPath> get_archive_contents_path') + '\n'
cpp += r'''
int main(){int failed=0;auto check=[&](bool ok,const char *name){
 std::printf("%s %s\n",ok?"PASS":"FAIL",name);failed+=!ok;};
for(const auto &marker: std::vector<std::string>{"sce_module/steroid.suprx",
 "PCSB00183/./SCE_MODULE\\.\\STEROID.SUPRX"}){
 ZipPtr zip(new mz_zip_archive{{{"sce_sys/param.sfo"},{marker}}});
 bool valid=true;auto contents=get_archive_contents_path(zip,valid);
 check(valid&&contents.size()==1,"Vitamin runtime does not prevent content admission");
}
{
 ZipPtr zip(new mz_zip_archive{{{"sce_sys/param.sfo"},{"sce_module/steroid.suprx.bak"}}});
 bool valid=false;auto contents=get_archive_contents_path(zip,valid);
 check(valid&&contents.size()==1,"backup filename remains an ordinary asset");
}
{
 ZipPtr zip(new mz_zip_archive{{{"supported/sce_sys/param.sfo"},
 {"vitamin/sce_sys/param.sfo"},{"vitamin/sce_module/steroid.suprx"}}});
 bool valid=true;auto contents=get_archive_contents_path(zip,valid);
 check(valid&&contents.size()==2,"mixed archive admits both titles without discarding a runtime");
}
for(int variant=0;variant<6;++variant){
 Entry first{"sce_module/steroid.suprx"}, second=first;
 if(variant==1)second.data="changed"; // same length and test CRC: compare actual bytes too
 if(variant==2)second.readable=false;
 if(variant==3)second.name="SCE_MODULE/STEROID.SUPRX";
 if(variant==4){first.name=second.name="sce_sys/param.sfo";}
 if(variant==5){first.name=second.name="ordinary.bin";}
 ZipPtr zip(new mz_zip_archive{{{"sce_sys/param.sfo"},first,second}});
 bool valid=true;auto contents=get_archive_contents_path(zip,valid);
 check(variant==0 ? (valid&&contents.size()==1) : (!valid&&contents.empty()),
   variant==0?"identical repeated Vitamin runtime is admitted":"conflicting, unreadable, aliased or ordinary duplicates remain rejected");
}
return failed?1:0;}
'''
(out / 'probe.cpp').write_text(cpp)
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler, 'A C++20 compiler is required'
subprocess.run([compiler, '-std=c++20', '-O0', str(out / 'probe.cpp'),
                '-o', str(out / 'probe.exe')], check=True)
result = subprocess.run([str(out / 'probe.exe')], capture_output=True, text=True)
print(result.stdout + result.stderr)
(out / ('red.log' if result.returncode else 'green.log')).write_text(result.stdout + result.stderr)
raise SystemExit(result.returncode)
