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
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>
namespace fs = std::filesystem;
namespace fs_utils {auto utf8_to_path(const std::string &s){return fs::path(s);}}
#define LOG_ERROR(...) ((void)0)
#define LOG_WARN(...) ((void)0)
#define LOG_CRITICAL(...) ((void)0)
using mz_uint = unsigned;
struct Entry {std::string name; bool directory = false;};
struct mz_zip_archive {std::vector<Entry> entries;};
struct mz_zip_archive_file_stat {const char *m_filename;};
using ZipPtr = std::unique_ptr<mz_zip_archive>;
auto mz_zip_reader_get_num_files(mz_zip_archive *z){return unsigned(z->entries.size());}
bool mz_zip_reader_file_stat(mz_zip_archive *z,unsigned i,mz_zip_archive_file_stat *s){
 if(i>=z->entries.size())return false;s->m_filename=z->entries[i].name.c_str();return true;}
bool mz_zip_reader_is_file_a_directory(mz_zip_archive *z,unsigned i){return z->entries[i].directory;}
'''
for marker in ('static std::string normalize_archive_path', 'struct ArchiveContentPath',
               'static bool is_safe_archive_relative_path',
               'static std::optional<std::string> archive_content_root',
               'static std::string archive_path_identity',
               'static bool is_vitamin_marker_path',
               'static std::vector<ArchiveContentPath> get_archive_contents_path'):
    cpp += block(marker) + (';' if marker.startswith('struct ') else '') + '\n'
cpp += r'''
int main(){int failed=0;auto check=[&](bool ok,const char *name){
 std::printf("%s %s\n",ok?"PASS":"FAIL",name);failed+=!ok;};
for(const auto &marker: std::vector<std::string>{"sce_module/steroid.suprx",
 "PCSB00183/./SCE_MODULE\\.\\STEROID.SUPRX"}){
 ZipPtr zip(new mz_zip_archive{{{"sce_sys/param.sfo"},{marker}}});
 bool valid=true;auto contents=get_archive_contents_path(zip,valid);
 check(!valid&&contents.empty(),"Vitamin runtime rejects entire archive before installation");
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
 check(!valid&&contents.empty(),"mixed archive cannot partially install before rejection");
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
