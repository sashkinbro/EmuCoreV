"""Exercise production process-clock pause accounting with a deterministic clock."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
out = root / 'app/build/process-clock-pause-tests'
out.mkdir(parents=True, exist_ok=True)
if os.name == 'nt':
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
source = r'''
#include <kernel/process_clock_pause.h>
#include <cassert>
#include <cstdio>
int main() {
 ProcessClockPause clock; uint64_t start=100;
 auto elapsed=[&](uint64_t now){return now-clock.effective_start(start,now);};
 assert(elapsed(150)==50);
 clock.set_paused(true,150,start);
 assert(elapsed(1000)==50);
 clock.set_paused(true,900,start); // repeated session updates must not reset pause
 assert(elapsed(2000)==50);
 const auto saved_start=clock.effective_start(start,2000);
 assert(2000-saved_start==50); // archive preserves pre-menu elapsed time
 clock.set_paused(false,3000,start);
 assert(elapsed(3017)==67);
 clock.set_paused(false,4000,start);
 assert(elapsed(4017)==1067); // redundant resume cannot subtract twice
 clock.set_paused(true,5000,start);
 // Restore a snapshot with elapsed=50 while still in the menu.
 clock.rebase(9000,8950,start);
 assert(elapsed(11000)==50);
 clock.set_paused(false,12000,start);
 assert(elapsed(12017)==67); // no double subtraction of pre-restore menu time
 clock.set_paused(true,13000,start);
 assert(elapsed(14000)==1050);
 clock.set_paused(false,15000,start);
 assert(elapsed(15017)==1067);
 ProcessClockPause cold;uint64_t cold_start=0;
 cold.rebase(20000,19950,cold_start);
 assert(20017-cold.effective_start(cold_start,20017)==67);
 std::puts("PASS process clock: live, pause, repeated updates, archive, warm/cold rebase, repeated pauses");
}
'''
(out / 'probe.cpp').write_text(source)
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler
exe = out / ('probe.exe' if os.name == 'nt' else 'probe')
subprocess.run([compiler, '-std=c++20', '-pthread', '-I'+str(root/'app/src/main/cpp/emucorev/ECV/ECV/kernel/include'), str(out/'probe.cpp'), '-o', str(exe)],check=True)
subprocess.run([str(exe)],check=True)
