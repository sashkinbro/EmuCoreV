#!/usr/bin/env python3
"""Exercise production trophy copy + TRP streaming with deterministic canaries.

Only Context's unrelated fields/guest ABI are adapted. The real TRPFile type,
get_entry_data/search_file and Context copy method are compiled verbatim.
"""
from pathlib import Path
import argparse
import os
import shutil
import struct
import subprocess
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument('--asset-vpk', type=Path)
parser.add_argument('--context-source', type=Path)
args = parser.parse_args()

root = next(p for p in Path(__file__).resolve().parents if (p/'app/src/main/cpp/vita3k/vita3k').is_dir())
base = root/'app/src/main/cpp/vita3k/vita3k'
out = root/'tools/system-services/trophy-stream-host'
out.mkdir(parents=True, exist_ok=True)
context = (args.context_source or base/'np/src/trophy/context.cpp').read_text(encoding='utf-8')
copy = context[context.index('int Context::copy_file_data_from_trophy_file'):context.index('bool Context::init_info_from_trp')]
trp = (base/'np/src/trophy/trp_parser.cpp').read_text(encoding='utf-8')
trp = trp[trp.index('bool TRPFile::get_entry_data'):]
cpp = r'''
#include <np/trophy/trp_parser.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using SceSize=uint32_t;
namespace np::trophy {struct Context {TRPFile trophy_file;int copy_file_data_from_trophy_file(const char*,void*,SceSize*);};
'''+copy+trp+r'''
int main(int argc,char**argv){int failed=0,checks=0;auto check=[&](bool ok,const std::string&name){++checks;printf("%s %s\n",ok?"PASS":"FAIL",name.c_str());failed+=!ok;};
 auto run=[&](const std::vector<uint8_t>&payload,const std::string&label){const SceSize length=payload.size();
  for(SceSize capacity:{SceSize(0),SceSize(17),SceSize(32768),SceSize(65535),SceSize(65536),SceSize(65537),SceSize(100000),length,SceSize(length+17)}){
   np::trophy::Context context;context.trophy_file.entries.push_back({"ICON0.PNG",0,length});
   size_t cursor=0,reads=0;context.trophy_file.seek_func=[&](int at){cursor=at;return at==0;};
   context.trophy_file.read_func=[&](void*dst,uint32_t count){++reads;if(count>payload.size()-cursor)return false;memcpy(dst,payload.data()+cursor,count);cursor+=count;return true;};
   // Oversize backing makes the pre-fix overflow deterministic without corrupting the host allocator.
   const size_t prefix=32;std::vector<uint8_t> bytes(prefix+std::max<SceSize>(length,capacity)+32,0xCD);
   SceSize size=capacity;int ret=context.copy_file_data_from_trophy_file("ICON0.PNG",bytes.data()+prefix,&size);
   const size_t expected=std::min(length,capacity);bool prefix_ok=std::equal(bytes.begin()+prefix,bytes.begin()+prefix+expected,payload.begin());
   bool guards=std::all_of(bytes.begin(),bytes.begin()+prefix,[](uint8_t x){return x==0xCD;})&&std::all_of(bytes.begin()+prefix+expected,bytes.end(),[](uint8_t x){return x==0xCD;});
   check(ret==0&&size==expected&&prefix_ok&&guards,label+" entry="+std::to_string(length)+" capacity="+std::to_string(capacity)+" exact prefix and canaries");
  }
  np::trophy::Context context;context.trophy_file.entries.push_back({"ICON0.PNG",0,length});bool accessed=false;
  context.trophy_file.seek_func=[&](int){accessed=true;return false;};context.trophy_file.read_func=[&](void*,uint32_t){accessed=true;return false;};
  SceSize size=5;check(context.copy_file_data_from_trophy_file("ICON0.PNG",nullptr,&size)==0&&size==length&&!accessed,"size query entry="+std::to_string(length)+" needs no stream IO");
 };
 for(SceSize length:{SceSize(147157),SceSize(99696),SceSize(169869),SceSize(65536),SceSize(0),SceSize(15)}){
  std::vector<uint8_t> payload(length);for(size_t i=0;i<payload.size();++i)payload[i]=uint8_t((i*13+7)%251);run(payload,"synthetic");
 }
 if(argc==2){std::ifstream file(argv[1],std::ios::binary);std::vector<uint8_t>payload((std::istreambuf_iterator<char>(file)),{});check(!payload.empty(),"actual local PNG fixture readable");if(!payload.empty())run(payload,"actual local PNG");}
 {np::trophy::Context context;bool accessed=false;context.trophy_file.seek_func=[&](int){accessed=true;return false;};
  std::vector<uint8_t>buffer(64,0xCD);SceSize size=17;int result=context.copy_file_data_from_trophy_file("MISSING.PNG",buffer.data(),&size);
  check(!accessed&&size==17&&std::all_of(buffer.begin(),buffer.end(),[](uint8_t b){return b==0xCD;}),"missing entry preserves output/size without stream access");
  printf("OBSERVATION missing entry existing return=%d size=%u\n",result,size);}
 for(size_t fail_at:{size_t(0),size_t(65536)}){
  np::trophy::Context context;context.trophy_file.entries.push_back({"ICON0.PNG",0,147157});size_t cursor=0;context.trophy_file.seek_func=[&](int){cursor=0;return true;};
  context.trophy_file.read_func=[&](void*dst,uint32_t n){if(cursor>=fail_at)return false;memset(dst,0x3C,n);cursor+=n;return true;};
  std::vector<uint8_t>buffer(147157+32,0xCD);SceSize size=100000;int result=context.copy_file_data_from_trophy_file("ICON0.PNG",buffer.data(),&size);
  check(size==100000&&std::all_of(buffer.begin()+std::min(cursor,size_t(100000)),buffer.end(),[](uint8_t b){return b==0xCD;}),"read error at "+std::to_string(fail_at)+" preserves capacity/canaries and existing inout size");
  printf("OBSERVATION read error existing return=%d size=%u delivered=%zu\n",result,size,std::min(cursor,size_t(100000)));}
 {np::trophy::Context context;context.trophy_file.entries.push_back({"ICON0.PNG",0,147157});size_t reads=0;
  context.trophy_file.seek_func=[](int){return true;};context.trophy_file.read_func=[&](void*dst,uint32_t n){++reads;memset(dst,0,n);return true;};
  std::vector<uint8_t>buffer(147157,0xCD);SceSize size=17;context.copy_file_data_from_trophy_file("ICON0.PNG",buffer.data(),&size);
  check(reads==1,"partial copy stops stream after the chunk filling caller capacity");}
 printf("%d/%d checks passed\n",checks-failed,checks);return failed?1:0;
}
'''
(out/'probe.cpp').write_text(cpp,encoding='utf-8')
compiler=os.environ.get('CXX') or shutil.which('g++') or 'C:/msys64/ucrt64/bin/g++.exe'
env=os.environ.copy();env['PATH']=str(Path(compiler).parent)+os.pathsep+env['PATH']
subprocess.run([compiler,'-std=c++20','-O0','-I'+str(base/'np/include'),str(out/'probe.cpp'),'-o',str(out/'probe.exe')],check=True,env=env)
command=[str(out/'probe.exe')]
if args.asset_vpk:
    with zipfile.ZipFile(args.asset_vpk) as archive:
        entry=next(e for e in archive.infolist() if e.filename.upper().endswith('TROPHY.TRP'))
        blob=archive.read(entry)
    assert blob[:4]==bytes.fromhex('DCA24D00'), 'uncompressed raw TRP required'
    count,offset=struct.unpack_from('>II',blob,0x10)
    assert count<=4096
    icons=[]
    for i in range(count):
        at=offset+i*64
        assert at+48<=len(blob)
        name=blob[at:at+32].split(b'\0')[0].decode('ascii')
        begin,length=struct.unpack_from('>QQ',blob,at+32)
        assert begin+length<=len(blob)
        if name.endswith('.PNG'):icons.append((length,name,begin))
    length,name,begin=max(icons)
    fixture=out/'local-icon-fixture.bin'
    fixture.write_bytes(blob[begin:begin+length])
    print(f'Using local {name}: {length} bytes from {args.asset_vpk.name}')
    command.append(str(fixture))
result=subprocess.run(command,capture_output=True,text=True,env=env,timeout=20)
print(result.stdout,end='')
(out/'result.log').write_text(result.stdout+result.stderr,encoding='utf-8')
raise SystemExit(result.returncode)
