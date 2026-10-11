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
 auto fifa=bundled.child("game-db").find_child_by_attribute("profile","id","fifa-native-buffer");
 check(std::distance(fifa.children("title-id").begin(),fifa.children("title-id").end())==31,"FIFA retains all 31 regional IDs");
 for(auto id:fifa.children("title-id")){
  Settings cfg=global;config::game_database::apply_gpu_settings(cfg,config::game_database::recommendation(bundled,id.text().as_string()));
  check(cfg.backend_renderer=="Vulkan"&&cfg.memory_mapping=="native-buffer"&&!cfg.high_accuracy,
   "FIFA keeps existing recommendations and inherits accuracy");
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
