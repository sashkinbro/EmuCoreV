"""Exercise production NGS logical serializers against actual module state types."""
from pathlib import Path
import os
import subprocess
import tempfile

here = Path(__file__).resolve().parent
core = here.parents[1] / "main/cpp/vita3k/vita3k"
vendor = core.parent
gtest = vendor / "external/googletest/googletest"
cxx = os.environ.get("CXX", "C:/msys64/ucrt64/bin/g++.exe")
env = dict(os.environ)
env["PATH"] = str(Path(cxx).parent) + os.pathsep + env.get("PATH", "")
includes = [core / p / "include" for p in ("ngs", "codec", "mem", "util")]
includes += [vendor / "external/fmt/include", gtest / "include", gtest]
with tempfile.TemporaryDirectory(prefix="emucorev-ngs-logical-") as temporary:
    executable = Path(temporary) / "ngs-logical.exe"
    sources = [here / "ngs_logical_host.cpp", gtest / "src/gtest-all.cc", gtest / "src/gtest_main.cc"]
    subprocess.run([cxx, "-std=c++23", "-pthread", "-DFMT_HEADER_ONLY",
                    *["-I" + str(p) for p in includes], *map(str, sources), "-o", str(executable)],
                   check=True, env=env, timeout=120)
    subprocess.run([str(executable)], check=True, env=env, timeout=30)
