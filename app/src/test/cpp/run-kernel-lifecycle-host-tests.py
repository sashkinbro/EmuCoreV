"""Run production synchronization/wait code with shared core API test cases, without a device/JIT."""
from pathlib import Path
import os, subprocess, tempfile
here=Path(__file__).resolve().parent
core=here.parents[1]/"main/cpp/vita3k/vita3k"
vendor=core.parent
gtest=vendor/"external/googletest/googletest"
cxx="C:/msys64/ucrt64/bin/g++.exe"
env=dict(os.environ);env["PATH"]=str(Path(cxx).parent)+os.pathsep+env.get("PATH","")
with tempfile.TemporaryDirectory(prefix="emucorev-lifecycle-") as temporary:
    exe=Path(temporary)/"lifecycle.exe"
    includes=[here/"kernel_wait_support",*[core/p/"include" for p in ("kernel","cpu","mem","util")],vendor/"external/fmt/include",gtest/"include",gtest]
    sources=[here/"kernel_lifecycle_host.cpp",*[core/"kernel/src"/p for p in ("thread_wait.cpp","callback.cpp","sync_primitives.cpp")],gtest/"src/gtest-all.cc",gtest/"src/gtest_main.cc"]
    subprocess.run([cxx,"-std=c++23","-pthread","-DFMT_HEADER_ONLY",*["-I"+str(p) for p in includes],*[str(p) for p in sources],"-o",str(exe)],check=True,env=env,timeout=120)
    subprocess.run([str(exe)],check=True,env=env,timeout=30)
