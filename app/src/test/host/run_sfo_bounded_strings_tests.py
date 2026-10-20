#!/usr/bin/env python3
"""Run the production SFO parser against bounded legacy string metadata."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
base = root/'app/src/main/cpp/vita3k/vita3k/packages'
header = (base/'include/packages/sfo.h').read_text()
types = header[header.index('enum SfoDataFormat'):header.index('namespace sfo {')]
source = (base/'src/sfo.cpp').read_text()
body = source[source.index('bool load(SfoFile &'):source.index('\n} // namespace sfo')]
cpp = r'''
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#define LOG_ERROR(...) ((void)0)
''' + types + body + r'''
std::vector<uint8_t> fixture(const std::string& value, bool terminator) {
    SfoHeader h{0x46535000,0x101,36,44,1};
    SfoIndexTableEntry e{0,UTF8_NULL,uint32_t(value.size()+terminator),64,0};
    std::vector<uint8_t> bytes(108);
    memcpy(bytes.data(),&h,sizeof(h)); memcpy(bytes.data()+20,&e,sizeof(e));
    memcpy(bytes.data()+36,"TITLE",6); memcpy(bytes.data()+44,value.data(),value.size());
    return bytes;
}
int main() {
    int failures=0;
    auto check=[&](bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name); failures+=!ok;};
    for (bool terminated : {true,false}) {
        auto bytes=fixture("Synthetic title \xE2\x84\xA2",terminated); SfoFile file{};
        check(load(file,bytes) && file.entries[0].data.second=="Synthetic title \xE2\x84\xA2",
            terminated?"normal terminated UTF-8 metadata":"legacy declared string length excludes its terminator");
    }
    {
        auto bytes=fixture("Exact bounded text",false); bytes.resize(44+18); SfoFile file{};
        check(load(file,bytes) && file.entries[0].data.second=="Exact bounded text", "bounded text never needs to read beyond the file");
    }
    for(int bad=0;bad<7;++bad){
        auto bytes=fixture("Valid",true);
        SfoIndexTableEntry entry; memcpy(&entry,bytes.data()+20,sizeof(entry));
        if(bad==0) entry.data_len=1000;
        if(bad==1) entry.data_max_len=1;
        if(bad==2) entry.data_offset=0xffffffff;
        if(bad==3) entry.key_offset=1000;
        if(bad==4) entry.data_fmt=0x9999;
        if(bad==5) entry.data_len=0;
        if(bad==6) {entry.data_fmt=UINT32_T;entry.data_len=2;}
        memcpy(bytes.data()+20,&entry,sizeof(entry));
        SfoFile file{}; file.entries.push_back({{}, {"keep","unchanged"}});
        check(!load(file,bytes) && file.entries[0].data.second=="unchanged", "invalid offsets, lengths and formats fail before changing parsed metadata");
    }
    return failures?1:0;
}
'''
out=root/'app/build/sfo-bounded-string-probe';out.mkdir(parents=True,exist_ok=True)
(out/'probe.cpp').write_text(cpp)
if os.name=='nt':os.environ['PATH']='C:/msys64/ucrt64/bin;'+os.environ['PATH']
compiler=os.environ.get('CXX') or shutil.which('g++');assert compiler
subprocess.run([compiler,'-std=c++20',str(out/'probe.cpp'),'-o',str(out/'probe.exe')],check=True)
result=subprocess.run([str(out/'probe.exe')],capture_output=True,text=True)
print(result.stdout+result.stderr,end='')
(out/('red.log' if result.returncode else 'green.log')).write_text(result.stdout+result.stderr)
raise SystemExit(result.returncode)
