"""Run production synchronization/wait code with shared core API test cases, without a device/JIT."""
from pathlib import Path
import os, subprocess, tempfile
here=Path(__file__).resolve().parent
core=here.parents[1]/"main/cpp/vita3k/vita3k"
vendor=core.parent
gtest=vendor/"external/googletest/googletest"
cxx="C:/msys64/ucrt64/bin/g++.exe"
env=dict(os.environ);env["PATH"]=str(Path(cxx).parent)+os.pathsep+env.get("PATH","")
def production_method(path, marker):
    source=path.read_text(encoding="utf-8")
    assert source.count(marker)==1
    start=source.index(marker);brace=source.index("{",start);depth=0
    for end in range(brace,len(source)):
        depth+=(source[end]=="{")-(source[end]=="}")
        if depth==0:
            return source[start:end+1]
    raise AssertionError(marker)
with tempfile.TemporaryDirectory(prefix="emucorev-lifecycle-") as temporary:
    exe=Path(temporary)/"lifecycle.exe"
    # Pull the small dependencies from production rather than stubbing typed
    # restored-object lookup or changing the tested primitive implementation.
    helpers=Path(temporary)/"production_helpers.cpp"
    memory_source=core/"mem/src/mem.cpp"
    memory_size=next(line for line in memory_source.read_text(encoding="utf-8").splitlines() if line.startswith("constexpr size_t TOTAL_MEM_SIZE ="))
    helpers.write_text("#include <kernel/thread/thread_state.h>\n#include <mem/state.h>\n#include <bit>\n"+
        memory_size+"\n"+
        production_method(core/"kernel/src/thread_continuation.cpp","std::shared_ptr<void> ThreadState::restoring_wait_object(")+"\n"+
        production_method(memory_source,"Address host_to_guest("),encoding="utf-8")
    includes=[here/"kernel_wait_support",*[core/p/"include" for p in ("kernel","cpu","mem","util")],vendor/"external/fmt/include",gtest/"include",gtest]
    sources=[here/"kernel_lifecycle_host.cpp",helpers,*[core/"kernel/src"/p for p in ("thread_wait.cpp","callback.cpp","sync_primitives.cpp","uid_class.cpp")],gtest/"src/gtest-all.cc",gtest/"src/gtest_main.cc"]
    subprocess.run([cxx,"-std=c++23","-pthread","-DFMT_HEADER_ONLY",*["-I"+str(p) for p in includes],*[str(p) for p in sources],"-o",str(exe)],check=True,env=env,timeout=120)
    subprocess.run([str(exe)],check=True,env=env,timeout=30)
