#!/usr/bin/env python3
"""Compile the real GetOutputPatch export with real NGS guest object types."""
from pathlib import Path
import os, shutil, subprocess, sys

root=Path(__file__).resolve().parents[4]
core=root/'app/src/main/cpp/vita3k/vita3k';ext=core.parent/'external'
out=root/'app/build/ngs-output-patch-probe';out.mkdir(parents=True,exist_ok=True)
if os.name=='nt':os.environ['PATH']='C:/msys64/ucrt64/bin;'+os.environ['PATH']

def block(path,marker):
    s=path.read_text();assert s.count(marker)==1,marker
    start=s.index(marker);brace=s.index('{',start);depth=0
    for end in range(brace,len(s)):
        depth+=(s[end]=='{')-(s[end]=='}')
        if not depth:return s[start:end+1]
    raise AssertionError(marker)

ngs=core/'ngs/src/ngs.cpp';api=core/'modules/SceNgsUser/SceNgs.cpp'
baseline='--baseline' in sys.argv
if baseline:
    api=out/'baseline-api.cpp'
    api.write_bytes(subprocess.check_output(['git','show','HEAD:app/src/main/cpp/vita3k/vita3k/modules/SceNgsUser/SceNgs.cpp'],cwd=root))
getter=block(api,'EXPORT(SceInt32, sceNgsVoiceGetOutputPatch,').replace('EXPORT(SceInt32, sceNgsVoiceGetOutputPatch, ngs::Voice *voice, const SceInt32 output_index, const SceInt32 output_subindex, Ptr<ngs::Patch> *patch)', 'SceInt32 get_output_patch(TestEnv &emuenv, ngs::Voice *voice, const SceInt32 output_index, const SceInt32 output_subindex, Ptr<ngs::Patch> *patch)')
errors=block(api,'enum SceNgsErrorCode : uint32_t')+';'
matrix_type=block(api,'struct SceNgsVolumeMatrix')+';'
setter=block(api,'EXPORT(SceInt32, sceNgsVoicePatchSetVolumesMatrix,').replace('EXPORT(SceInt32, sceNgsVoicePatchSetVolumesMatrix, ngs::Patch *patch, const SceNgsVolumeMatrix *matrix)','SceInt32 set_matrix(TestEnv &emuenv, ngs::Patch *patch, const SceNgsVolumeMatrix *matrix)')
cpp=r'''
#include <ngs/system.h>
#include <cstdio>
#include <util/log.h>
#define TRACY_FUNC(...)
#undef RET_ERROR
#define RET_ERROR(code) static_cast<SceInt32>(code)
struct TestEnv{struct{struct{bool ngs_enable=true;}current_config;}cfg;MemState mem;};
// Only needed by the old null-patch workaround, which the normal routing fix removes.
static thread_local ngs::Voice *last_missing_output_patch_voice=nullptr;
''' + errors + matrix_type + '\nnamespace ngs {\n' + '\n'.join(block(ngs,m) for m in ('Rack::Rack(', 'Ptr<Patch> Voice::patch(', 'bool Voice::remove_patch(')) + '\n}\n' + getter + setter + r'''
int main(){
 TestEnv env;env.mem.memory=Memory(new uint8_t[16384]{},[](uint8_t*p){delete[]p;});
 ngs::VoiceDefinition def{};def.output_count=2;ngs::Rack rack{nullptr,Ptr<void>(0x2000),1024};rack.vdef=&def;rack.patches_per_output=2;
 ngs::Voice voice{};voice.rack=&rack;voice.voice_mutex=std::make_unique<std::mutex>();for(auto&port:voice.patches)port.resize(2);
 Ptr<ngs::Patch> handle,result(0x1000);int checks=0,failures=0;
 auto check=[&](bool ok,const char*name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}};
 constexpr auto unavailable=static_cast<SceInt32>(0x804A000Eu),invalid=static_cast<SceInt32>(0x804A0002u);
 check(get_output_patch(env,&voice,0,0,&result)==unavailable,"empty slot reports PATCH_NOT_AVAIL so guest creates its graph");check(!result,"empty slot clears returned handle");
 // Mirror the guest's real control flow: create routing only after lookup fails.
 if(get_output_patch(env,&voice,0,0,&result)!=0)
     handle=voice.patch(env.mem,0,0,0,Ptr<ngs::Voice>(0x100),Ptr<ngs::Voice>(0x600));
 check(bool(handle),"lookup error lets guest create a real route");
 if(!handle){std::printf("%d checks, %d failures\n",checks,failures);return 1;}
 auto*p=handle.get(env.mem);
 check(get_output_patch(env,&voice,0,0,&result)==0&&result==handle,"live patch returns exact handle");
 std::memset(p->volume_matrix,0,sizeof(p->volume_matrix));check(get_output_patch(env,&voice,0,0,&result)==0&&result==handle,"zero-gain active route is still available");
 check(voice.remove_patch(env.mem,handle),"remove routing succeeds");check(get_output_patch(env,&voice,0,0,&result)==unavailable&&!result,"removed route reports unavailable instead of success-null");
 auto recreated=voice.patch(env.mem,0,0,0,Ptr<ngs::Voice>(0x100),Ptr<ngs::Voice>(0x600));
 check(recreated==handle,"routing recreation reuses released slot");check(get_output_patch(env,&voice,0,0,&result)==0&&result==handle,"recreated slot returns reused handle");
 SceNgsVolumeMatrix gain{{{0.4f,0.1f},{0.2f,0.3f}}};
 check(set_matrix(env,p,&gain)==0&&p->volume_matrix[0][0]==0.4f&&p->volume_matrix[1][0]==0.2f,"live patch stores caller gain matrix");
 check(set_matrix(env,nullptr,&gain)==invalid,"null matrix route is rejected instead of accepting pseudo gain");
 voice.remove_patch(env.mem,handle);check(set_matrix(env,p,&gain)==invalid,"removed matrix route is rejected");
 check(get_output_patch(env,&voice,1,1,&result)==unavailable&&!result,"last valid empty output slot reports unavailable");
 check(get_output_patch(env,&voice,-1,0,&result)==invalid,"negative port is invalid");check(get_output_patch(env,&voice,0,-1,&result)==invalid,"negative subindex is invalid");
 check(get_output_patch(env,&voice,2,0,&result)==invalid,"out-of-range port is invalid");check(get_output_patch(env,&voice,0,2,&result)==invalid,"out-of-range subindex is invalid");
 check(get_output_patch(env,nullptr,0,0,&result)==invalid,"null voice is invalid");check(get_output_patch(env,&voice,0,0,nullptr)==invalid,"null output pointer is invalid");
 env.cfg.current_config.ngs_enable=false;check(get_output_patch(env,&voice,0,0,nullptr)==0,"disabled NGS retains its compatibility result");
 std::printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}
'''
(out/'probe.cpp').write_text(cpp)
includes=[core/(p+'/include') for p in ('ngs','mem','util','codec')]+[ext/'fmt/include',ext/'spdlog/include',ext/'boost']
compiler=os.environ.get('CXX')or shutil.which('g++');assert compiler
subprocess.run([compiler,'-std=c++23','-DFMT_HEADER_ONLY','-DSPDLOG_FMT_EXTERNAL','-DBOOST_ALL_NO_LIB',*['-I'+str(p) for p in includes],str(out/'probe.cpp'),'-o',str(out/'probe.exe')],check=True)
run=subprocess.run([str(out/'probe.exe')],capture_output=True,text=True,timeout=10);print(run.stdout+run.stderr)
(out/('baseline.log'if baseline else'green.log')).write_text(run.stdout+run.stderr)
raise SystemExit(run.returncode)
