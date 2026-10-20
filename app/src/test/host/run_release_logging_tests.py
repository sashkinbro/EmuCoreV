#!/usr/bin/env python3
"""Compile the production logging macros in both build modes.

The spdlog seam models the logger contract and counts evaluated
arguments. Release must avoid logging arguments, conditionals and once state;
Debug must retain them. Error-return behavior is independent of logging.
"""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
out = root / 'app/build/release-logging-probe'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'app/src/main/cpp/vita3k/vita3k/util/include/util/log.h').read_text()
prefix = source[source.index('#define SPDLOG_ACTIVE_LEVEL'):source.index('namespace logging')]
# Keep the build guards before the level definition, if present.
guard = source.rfind('#ifdef NDEBUG', 0, source.index('#define SPDLOG_ACTIVE_LEVEL'))
if guard >= 0:
    prefix = source[guard:source.index('namespace logging')]
prefix = '\n'.join(line for line in prefix.splitlines() if not line.startswith('#include'))
seam = r'''
#define SPDLOG_LEVEL_TRACE 0
#define SPDLOG_LEVEL_OFF 6
#define SPDLOG_FUNCTION __func__
namespace spdlog {
namespace level {enum level_enum {trace,debug,info,warn,err,critical,off};}
struct source_loc {const char* file;int line;const char* function;};
struct Logger {
 bool enabled=true;int calls=0;
 bool should_log(level::level_enum) const {return enabled;}
 template<class... T> void log(source_loc,level::level_enum,const char*,T...){++calls;}
};
Logger logger;
Logger* default_logger_raw(){return &logger;}
}
'''
ret_error = source[source.index('#define RET_ERROR'):source.index('/*', source.index('#define RET_ERROR'))]
cpp = '#include <atomic>\n#include <cstdio>\nint effects=0;\nint log_hex(int value){++effects;return value;}\n' + seam + prefix + ret_error + r'''
int main(){
 // Force the logger enabled even in release: zero effects must come from
 // production macro compilation, not the seam suppressing runtime logging.
 spdlog::logger.enabled=true;
 LOG_TRACE("trace",++effects); LOG_DEBUG("debug",++effects);
 LOG_INFO("info",++effects); LOG_WARN("warn",++effects);
 LOG_ERROR("error",++effects); LOG_CRITICAL("critical",++effects);
 LOG_INFO_IF(++effects,"condition",++effects);
 for(int i=0;i<2;i++) LOG_WARN_ONCE("once",++effects);
#ifdef NDEBUG
 const int expected=0;
#else
 const int expected=9;
#endif
 const char* export_name="test_export";
 const int error = RET_ERROR(-42);
 const int expected_final=expected+(expected?1:0);
 const int expected_calls=expected?9:0;
 if(spdlog::logger.calls!=expected_calls)return 2;
 spdlog::logger.enabled=false;
 LOG_ERROR("muted",++effects);
 LOG_INFO_IF(++effects,"muted condition",++effects);
 LOG_WARN_ONCE("muted once",++effects);
 if(spdlog::logger.calls!=expected_calls)return 3;
 std::printf("effects=%d expected=%d error=%d\n",effects,expected_final,error);
 return effects==expected_final&&error==-42?0:1;
}
'''
(out / 'probe.cpp').write_text(cpp)
if os.name == 'nt' and Path('C:/msys64/ucrt64/bin').is_dir():
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = os.environ.get('CXX') or shutil.which('g++') or shutil.which('clang++')
assert compiler, 'A C++20 compiler is required'
failed = False
for mode in ('release', 'debug'):
    exe = out / (mode + '.exe')
    subprocess.run([compiler, '-std=c++20', *(['-DNDEBUG'] if mode=='release' else []),
                    str(out / 'probe.cpp'), '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    print(mode + ': ' + result.stdout + result.stderr)
    (out / (mode + '.log')).write_text(result.stdout + result.stderr)
    failed |= bool(result.returncode)
logger = (root / 'app/src/main/cpp/vita3k/vita3k/util/src/logging.cpp').read_text()
release = logger[logger.index('#ifdef NDEBUG') + len('#ifdef NDEBUG'):logger.index('#else', logger.index('#ifdef NDEBUG'))]
logger_cpp = r'''
#include <functional>
#include <string>
#include <cstdio>
enum ExitCode {Success};
struct Root {};
namespace fs {struct path {};}
namespace spdlog {
namespace level {enum level_enum {trace,debug,info,warn,err,critical,off};}
level::level_enum current=level::trace;
void set_level(level::level_enum level){current=level;}
}
''' + release + r'''
int main(){
 Root root; int callbacks=0;
 if(logging::init(root,true)!=Success||spdlog::current!=spdlog::level::off)return 1;
 logging::set_log_callback([&](std::string,int){++callbacks;});
 logging::set_level(spdlog::level::trace);
 if(logging::add_sink(fs::path{})!=Success)return 2;
 logging::flush();
 if(spdlog::current!=spdlog::level::off||callbacks)return 3;
 std::puts("Release logger cannot create sinks or reenable output");
}
'''
(out / 'logger.cpp').write_text(logger_cpp)
exe = out / 'logger.exe'
subprocess.run([compiler,'-std=c++20',str(out/'logger.cpp'),'-o',str(exe)],check=True)
result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
print(result.stdout+result.stderr)
failed |= bool(result.returncode)
raise SystemExit(failed)
