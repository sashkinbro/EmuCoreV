#!/usr/bin/env python3
"""Compile the real Game DB helper and pugixml, then exercise profile layering."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
out = root / 'app/build/game-db-native-probe'
out.mkdir(parents=True, exist_ok=True)
if os.name == 'nt' and Path('C:/msys64/ucrt64/bin').is_dir():
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
cpp = r'''
#include <config/game_database.h>
#include <cstdio>
#include <iterator>
struct Settings {
 std::string backend_renderer="OpenGL", memory_mapping="page-table", driver="working-driver";
 bool high_accuracy=false; int audio_volume=37;
};
int main(int argc,char **argv){
 int failed=0;auto check=[&](bool ok,const char *name){
  std::printf("%s %s\n",ok?"PASS":"FAIL",name);failed+=!ok;};
 pugi::xml_document db;db.load_string(R"(<game-db version="1"><profile><title-id>EXACT</title-id><gpu high-accuracy="true"/></profile></game-db>)");
 Settings global,effective=global;
 config::game_database::apply_gpu_settings(effective,config::game_database::recommendation(db,"EXACT"));
 check(effective.high_accuracy&&!global.high_accuracy&&effective.backend_renderer=="OpenGL"
  &&effective.memory_mapping=="page-table"&&effective.driver==global.driver&&effective.audio_volume==37,
  "accuracy-only profile changes only accuracy");
 pugi::xml_document user;user.load_string(R"(<gpu high-accuracy="false"/>)");
 effective.high_accuracy=true;
 config::game_database::apply_gpu_settings(effective,user.child("gpu"));
 check(!effective.high_accuracy,"explicit user false overrides recommendation");
 for(const auto &attrs: {"", "unknown=\"true\"", "high-accuracy=\"TRUE\"", "high-accuracy=\"\"",
   "high-accuracy=\"enabled\"", "backend-renderer=\"Vulkan\" high-accuracy=\"true\"",
   "backend-renderer=\"Vulkan\" memory-mapping=\"native-buffer\" use-angle=\"false\" high-accuracy=\"invalid\""}){
  pugi::xml_document bad;
  bad.load_string((std::string("<game-db version=\"1\"><profile><title-id>EXACT</title-id><gpu ")+attrs+"/></profile></game-db>").c_str());
  check(!config::game_database::recommendation(bad,"EXACT"),"empty, incomplete or malformed profile ignored");
 }
 pugi::xml_document bundled;check(argc==2&&bundled.load_file(argv[1]),"bundled database parses");
 Settings recommended=global;
 config::game_database::apply_gpu_settings(recommended,config::game_database::recommendation(bundled," pcse00383 ","Renamed game"));
 check(recommended.high_accuracy&&recommended.backend_renderer==global.backend_renderer
  &&recommended.memory_mapping==global.memory_mapping,"verified Borderlands ID recommends only accuracy");
 check(!config::game_database::recommendation(bundled,"UNVERIFIED","Borderlands 2")
  &&!config::game_database::recommendation(bundled,"","Borderlands 2"),"no regional or global guesses");
 const char *ids[]={"PCSB00051","PCSB00052","PCSG00039","PCSB00082","PCSB00083","PCSB00084","PCSB00085","PCSB00086","PCSE00055","PCSE00059","PCSE00060","PCSG90012","PCSB00170","PCSB00171","PCSB00174","PCSE00093","PCSE00096","PCSG00107","PCSB00339","PCSB00340","PCSE00263","PCSG00201","PCSB00603","PCSB00604","PCSB00605","PCSB00606","PCSB00607","PCSE00481","PCSE00482","PCSE00483","PCSG00404","UNKNOWN"};
 for(auto mapping:{"disabled","double-buffer","external-host","page-table","native-buffer"}){
  for(auto id:ids) for(auto title:{"EA SPORTS FC mod","FIFA Football","FIFA 13","FIFA 14","FIFA 15","FIFA 26 mod"}){
   Settings cfg=global;cfg.memory_mapping=mapping;
   config::game_database::apply_gpu_settings(cfg,config::game_database::recommendation(bundled,id,title));
   check(cfg.backend_renderer==global.backend_renderer&&cfg.memory_mapping==mapping&&!cfg.high_accuracy
    &&cfg.driver==global.driver&&cfg.audio_volume==global.audio_volume,"FIFA regions and mods inherit all global settings");
  }
 }
 return failed?1:0;
}
'''
(out / 'probe.cpp').write_text(cpp)
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler, 'A C++20 compiler is required'
pugi = root / 'app/src/main/cpp/vita3k/external/pugixml/src'
subprocess.run([compiler, '-std=c++20', '-O0',
                '-I' + str(root / 'app/src/main/cpp/vita3k/vita3k/config/include'),
                '-I' + str(pugi), str(out / 'probe.cpp'), str(pugi / 'pugixml.cpp'),
                '-o', str(out / 'probe.exe')], check=True)
result = subprocess.run([str(out / 'probe.exe'),
                         str(root / 'app/src/main/assets/compatibility/game-db.xml')],
                        capture_output=True, text=True)
print(result.stdout + result.stderr)
(out / ('red.log' if result.returncode else 'green.log')).write_text(result.stdout + result.stderr)
raise SystemExit(result.returncode)
