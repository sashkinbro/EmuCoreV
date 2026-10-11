#!/usr/bin/env python3
"""Actual NoNpDrm/theme admission bodies with deterministic PFS fault injection.

The production mount/decrypt/keystone sequence and install helper bodies are
extracted unchanged; only crypto/PFS I/O have seams. Real filesystem canaries
verify partial decrypt output cannot replace the original source.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[4]
out = root / "app/build/theme-decryption-probe"
out.mkdir(parents=True, exist_ok=True)
relative = "app/src/main/cpp/vita3k/vita3k/packages/src/pkg.cpp"
source = (subprocess.check_output(["git", "show", "HEAD:" + relative], cwd=root, text=True)
          if "--baseline" in sys.argv else (root / relative).read_text())
pfs = (root / "app/src/main/cpp/vita3k/external/psvpfstools/psvpfsparser/PsvPfsParserConfig.cpp").read_text()


def body(text, marker):
    assert text.count(marker) == 1, marker
    begin = text.index(marker)
    opening = text.index("{", begin)
    depth = 0
    for end in range(opening, len(text)):
        depth += (text[end] == "{") - (text[end] == "}")
        if depth == 0:
            return text[begin:end + 1]
    raise AssertionError(marker)


cpp = r'''
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
namespace fs {using namespace std::filesystem;using std::ifstream;using std::ofstream;}
namespace psvpfs {using path=fs::path;}
namespace fs_utils {fs::path path_concat(const fs::path&p,const std::string&s){return p.string()+s;}}
using PfsProgressCallback=std::function<void(uint64_t,uint64_t,const std::string&)>;
enum class F00DEncryptorTypes {native};
enum class CryptoOperationsTypes {openssl};
struct ICryptoOperations {};
struct IF00DKeyEncryptor {void print_cache(std::ostream&) {}};
struct PsvPfsParserConfig {std::string zRIF,title_id_src,title_id_dst,f00d_arg;F00DEncryptorTypes f00d_enc_type;};
struct CryptoOperationsFactory {static auto create(CryptoOperationsTypes){return std::make_shared<ICryptoOperations>();}};
auto create_F00D_encryptor(const PsvPfsParserConfig&,std::shared_ptr<ICryptoOperations>){return std::make_shared<IF00DKeyEncryptor>();}
int extract_klicensee(const PsvPfsParserConfig&,std::shared_ptr<ICryptoOperations>,unsigned char*){return 0;}
int mount_result=0,decrypt_result=0,keystone_result=0,keystone_calls=0,license_calls=0;
struct PfsFilesystem {
 PfsFilesystem(std::shared_ptr<ICryptoOperations>,std::shared_ptr<IF00DKeyEncryptor>,std::ostream&,const unsigned char*,const psvpfs::path&){}
 int mount(){return mount_result;}
 int decrypt_files(const psvpfs::path&dst,PfsProgressCallback progress=nullptr){
   fs::create_directories(dst);fs::ofstream(dst/"decoded.bin")<<"decrypted";
   if(progress)progress(10,10,"decoded.bin");return decrypt_result;
 }
};
int get_keystone(std::shared_ptr<ICryptoOperations>,const psvpfs::path&){++keystone_calls;return keystone_result;}
struct EmuEnvState {fs::path vita_fs_path;struct {std::string app_category="ac";} app_info;};
std::string rif2zrif(fs::ifstream&){return "valid";}
void copy_license(EmuEnvState&,const fs::path&){++license_calls;}
'''
cpp += body(pfs, "int execute(std::shared_ptr<ICryptoOperations>") + "\n"
cpp += r'''
int execute(std::string&zrif,fs::path&src,fs::path&dst,F00DEncryptorTypes,std::string&,PfsProgressCallback progress=nullptr){
 unsigned char key[16]{};return execute(std::make_shared<ICryptoOperations>(),std::make_shared<IF00DKeyEncryptor>(),key,src,dst,progress);
}
'''
cpp += body(source, "static bool decrypt_theme_pfs(") + "\n"
cpp += body(source, "bool decrypt_install_nonpdrm(") + "\n"
cpp += r'''
int failures=0,checks=0;
void expect(bool value,const char*name){++checks;if(!value){++failures;std::cerr<<"FAIL "<<name<<"\n";}}
std::string read(const fs::path&p){fs::ifstream in(p);return {std::istreambuf_iterator<char>(in),{}};}
int main(int argc,char**argv){
 auto base=fs::path(argv[1])/"vita_theme_named_parent";fs::remove_all(base);fs::create_directories(base);
 fs::ofstream(base/"work.bin")<<"license";
 auto run=[&](const char*folder,int mount,int decrypt,int keystone,bool accepted,const char*name){
  EmuEnvState env;env.vita_fs_path=base;env.app_info.app_category="ac";
  auto path=base/"ux0"/folder/"CONTENT.stage-test";fs::remove_all(path);fs::remove_all(path.string()+"_dec");
  fs::create_directories(path);fs::ofstream(path/"source.bin")<<"original";
  mount_result=mount;decrypt_result=decrypt;keystone_result=keystone;keystone_calls=license_calls=0;int progress=0;
  bool result=false;try{result=decrypt_install_nonpdrm(env,base/"work.bin",path,[&](float p){if(p==1.f)++progress;});}catch(const fs::filesystem_error&){}
  expect(result==accepted,name);
  if(!accepted){expect(read(path/"source.bin")=="original","failed decrypt preserves original source");expect(license_calls==0,"failed decrypt creates no license");}
  else {expect(read(path/"decoded.bin")=="decrypted","success publishes complete decrypted content");expect(!fs::exists(path/"source.bin"),"success replaces source once");expect(progress==1,"progress forwarded once");}
  if(std::string(folder)=="theme")expect(keystone_calls==0,"theme skips only keystone step");
 };
 run("theme",-1,0,0,false,"theme mount failure rejected");
 run("theme",0,-1,0,false,"theme partial decrypt failure rejected");
 run("theme",0,0,-1,true,"complete keystone-less theme accepted");
 run("app",0,-1,0,false,"app in theme-named ancestor rejects partial decrypt");
 run("app",0,0,-1,false,"ordinary app still requires keystone");
 run("app",0,0,0,true,"complete ordinary app accepted");
 fs::remove_all(base);std::cout<<checks<<" checks; "<<failures<<" failures\n";return failures?1:0;
}
'''
probe = out / "probe.cpp"
probe.write_text(cpp, encoding="utf-8")
compiler = shutil.which("g++") or "C:/msys64/ucrt64/bin/g++.exe"
env = os.environ.copy()
env["PATH"] = str(Path(compiler).parent) + os.pathsep + env["PATH"]
exe = out / "probe.exe"
subprocess.run([compiler, "-std=c++23", "-O0", str(probe), "-o", str(exe)], check=True, env=env)
subprocess.run([str(exe), str(out)], check=True, env=env)
