"""Run real swresample continuation tests with the matching pinned MinGW prebuilt.

Pass the extracted ffmpeg-mingw-x64 release02f4f26 library directory as argv[1].
This runner does not install dependencies or change emulator build directories.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

here = Path(__file__).resolve().parent
core = here.parents[1] / "main/cpp/vita3k/vita3k"
vendor = core.parent
gtest = vendor / "external/googletest/googletest"
libraries = Path(sys.argv[1])
cxx = os.environ.get("CXX", "C:/msys64/ucrt64/bin/g++.exe")
env = dict(os.environ)
env["PATH"] = str(Path(cxx).parent) + os.pathsep + env.get("PATH", "")
includes = [core / p / "include" for p in ("ngs", "codec", "mem", "util")]
includes += [vendor / "external" / p for p in ("boost", "fmt/include", "spdlog/include", "ffmpeg/include")]
includes += [gtest / "include", gtest, vendor / "external/LibAtrac9/C/src"]
with tempfile.TemporaryDirectory(prefix="emucorev-ngs-resampler-") as temporary:
    executable = Path(temporary) / "ngs-resampler.exe"
    c_objects = []
    for source in (vendor / "external/LibAtrac9/C/src").glob("*.c"):
        obj = Path(temporary) / (source.stem + ".o")
        subprocess.run([str(Path(cxx).with_name("gcc.exe")), "-std=c11", "-c", str(source), "-o", str(obj)], check=True, env=env, timeout=30)
        c_objects.append(obj)
    sources = [here / "ngs_resampler_host.cpp", core / "ngs/src/rate_resampler.cpp",
               core / "codec/src/pcm.cpp", core / "codec/src/decoder.cpp", core / "codec/src/atrac9.cpp",
               gtest / "src/gtest-all.cc", gtest / "src/gtest_main.cc"]
    subprocess.run([cxx, "-std=c++23", "-pthread", "-DFMT_HEADER_ONLY", "-DSPDLOG_FMT_EXTERNAL",
                    *["-I" + str(p) for p in includes], *map(str, sources), *map(str, c_objects),
                    str(libraries / "libavcodec.a"), str(libraries / "libswresample.a"), str(libraries / "libavutil.a"),
                    "-lbcrypt", "-lws2_32", "-lole32", "-lstrmiids", "-loleaut32", "-luser32", "-o", str(executable)],
                   check=True, env=env, timeout=120)
    subprocess.run([str(executable)], check=True, env=env, timeout=30)
