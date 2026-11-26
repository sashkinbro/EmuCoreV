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
#include <vector>
struct Settings {
 std::string backend_renderer="OpenGL", memory_mapping="page-table", driver="working-driver";
 bool accurate_thread_scheduling=false, eager_surface_sync=false, depth_clamp=true; int audio_volume=37;
 int modules_mode=0; std::vector<std::string> lle_modules;
};
int main(int argc,char **argv){
 int failed=0;auto check=[&](bool ok,const char *name){
  std::printf("%s %s\n",ok?"PASS":"FAIL",name);failed+=!ok;};
 Settings global;
 for(const auto &attrs: {"", "unknown=\"true\"", "high-accuracy=\"true\"", "high-accuracy=\"false\"",
   "high-accuracy=\"invalid\"", "backend-renderer=\"Vulkan\" high-accuracy=\"true\""}){
  pugi::xml_document bad;
  bad.load_string((std::string("<game-db version=\"1\"><profile><title-id>EXACT</title-id><gpu ")+attrs+"/></profile></game-db>").c_str());
  check(!config::game_database::recommendation(bad,"EXACT"),"empty, incomplete, malformed or removed-key profile ignored");
 }
 pugi::xml_document bundled;check(argc==2&&bundled.load_file(argv[1]),"bundled database parses");
 Settings crazy=global;crazy.lle_modules={"libngs"};
 config::game_database::apply_module_settings(crazy,config::game_database::module_recommendation(bundled," pcse00291 "));
 config::game_database::apply_module_settings(crazy,config::game_database::module_recommendation(bundled,"PCSE00291"));
 check(crazy.modules_mode==1 && crazy.lle_modules==std::vector<std::string>({"libngs","libnet","libnetctl"})
  &&crazy.memory_mapping==global.memory_mapping,
  "Crazy firmware modules preserve other choices and do not duplicate");
 check(!config::game_database::module_recommendation(bundled,"PCSE00089")
  &&!config::game_database::module_recommendation(bundled,"PCSB00374")
  &&!config::game_database::module_recommendation(bundled,""),"module policy requires verified exact ID");
 for(const auto &xml:{"<modules mode='auto-manual'/>","<modules mode='manual'><module>libnet</module></modules>",
  "<modules mode='auto-manual'><module>../libnet</module></modules>","<modules mode='auto-manual'><module>libnet.suprx</module></modules>",
  "<modules mode='auto-manual'><module/></modules>","<modules mode='auto-manual'><unknown>libnet</unknown></modules>"}){
  pugi::xml_document bad;bad.load_string(xml);Settings unchanged=global;
  config::game_database::apply_module_settings(unchanged,bad.child("modules"));
  check(unchanged.modules_mode==global.modules_mode&&unchanged.lle_modules.empty(),"invalid module policies do not change config");
 }
 Settings pvz=global;
 Settings stealth=global;
 config::game_database::apply_gpu_settings(stealth,config::game_database::recommendation(bundled,"PCSE00280"));
 check(!stealth.depth_clamp && stealth.memory_mapping==global.memory_mapping,
  "Stealth disables depth clamp without changing unrelated GPU settings");
 Settings unrelated=global;
 config::game_database::apply_gpu_settings(unrelated,config::game_database::recommendation(bundled,"PCSE00089"));
 check(unrelated.depth_clamp,"NFS and unprofiled games retain depth clamp");
 pugi::xml_document depth_user;depth_user.load_string(R"(<gpu depth-clamp="true"/>)");
 config::game_database::apply_gpu_settings(stealth,depth_user.child("gpu"));
 check(stealth.depth_clamp,"explicit depth-clamp true overrides recommendation");
 pugi::xml_document invalid_depth;invalid_depth.load_string(R"(<gpu depth-clamp="invalid"/>)");
 check(!config::game_database::valid_gpu(invalid_depth.child("gpu")),"reject invalid depth-clamp policy");
 config::game_database::apply_gpu_settings(pvz,config::game_database::recommendation(bundled,"PCSE00064"));
 check(pvz.accurate_thread_scheduling&&pvz.eager_surface_sync&&pvz.memory_mapping==global.memory_mapping,
  "PvZ startup recommendation enables scheduling and eager surface writeback");
 pugi::xml_document scheduling_user;scheduling_user.load_string(R"(<gpu accurate-thread-scheduling="false" eager-surface-sync="false"/>)");
 config::game_database::apply_gpu_settings(pvz,scheduling_user.child("gpu"));
 check(!pvz.accurate_thread_scheduling&&!pvz.eager_surface_sync,"explicit user scheduling false overrides recommendation");
 check(!config::game_database::recommendation(bundled," pcse00383 ","Renamed game")
  &&!config::game_database::recommendation(bundled,"PCSE00001")&&!config::game_database::recommendation(bundled,"PCSB00356"),
  "former accuracy-only profiles are gone");
 const char *ids[]={"PCSB00051","PCSB00052","PCSG00039","PCSB00082","PCSB00083","PCSB00084","PCSB00085","PCSB00086","PCSE00055","PCSE00059","PCSE00060","PCSG90012","PCSB00170","PCSB00171","PCSB00174","PCSE00093","PCSE00096","PCSG00107","PCSB00339","PCSB00340","PCSE00263","PCSG00201","PCSB00603","PCSB00604","PCSB00605","PCSB00606","PCSB00607","PCSE00481","PCSE00482","PCSE00483","PCSG00404","UNKNOWN"};
 for(auto mapping:{"disabled","double-buffer","external-host","page-table","native-buffer"}){
  for(auto id:ids) for(auto title:{"EA SPORTS FC mod","FIFA Football","FIFA 13","FIFA 14","FIFA 15","FIFA 26 mod"}){
   Settings cfg=global;cfg.memory_mapping=mapping;
   config::game_database::apply_gpu_settings(cfg,config::game_database::recommendation(bundled,id,title));
   check(cfg.backend_renderer==global.backend_renderer&&cfg.memory_mapping==mapping
    &&cfg.driver==global.driver&&cfg.audio_volume==global.audio_volume,"FIFA regions and mods inherit all global settings");
  }
 }
 return failed?1:0;
}
'''
(out / 'probe.cpp').write_text(cpp)
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler, 'A C++20 compiler is required'
pugi = root / 'app/src/main/cpp/emucorev/ECV/external/pugixml/src'
subprocess.run([compiler, '-std=c++20', '-O0',
                '-I' + str(root / 'app/src/main/cpp/emucorev/ECV/ECV/config/include'),
                '-I' + str(pugi), str(out / 'probe.cpp'), str(pugi / 'pugixml.cpp'),
                '-o', str(out / 'probe.exe')], check=True)
result = subprocess.run([str(out / 'probe.exe'),
                         str(root / 'app/src/main/assets/compatibility/game-db.xml')],
                        capture_output=True, text=True)
print(result.stdout + result.stderr)
(out / ('red.log' if result.returncode else 'green.log')).write_text(result.stdout + result.stderr)
raise SystemExit(result.returncode)
