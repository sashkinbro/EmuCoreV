"""Build and run the host firmware-install tests (see README.md).

    python run_fwinstall_host_tests.py --core CORE_PSP2UPDAT.PUP --fonts PSP2UPDAT.PUP [--reference TREE] [--measure]

Windows defaults assume the llvm-mingw toolchain of the other host oracles and a static OpenSSL 3.x
(`-DOSSL_DIR` / --ossl). The firmware packages are local-only files; nothing here ships them.
"""
import argparse
import os
import subprocess
import sys
from pathlib import Path

here = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument("--core", required=True, help="3.74 base PUP (CORE_PSP2UPDAT.PUP / PSVUPDAT.PUP)")
parser.add_argument("--fonts", required=True, help="3.74 fonts PUP (PSP2UPDAT.PUP)")
parser.add_argument("--reference", help="tree installed by the pre-change vendored install_pup (optional)")
parser.add_argument("--build", default=str(Path.home() / "emucorev-lab" / "fwbuild"))
parser.add_argument("--work", default=str(Path.home() / "emucorev-lab" / "fwwork"))
parser.add_argument("--ossl", default="C:/Users/SBRO/emucorev-lab/tools/ossl/openssl-3.3.2")
parser.add_argument("--mingw", default="C:/Users/SBRO/tools/llvm-mingw/bin")
parser.add_argument("--cmake-bin", default="C:/Users/SBRO/AppData/Local/Android/Sdk/cmake/3.30.5/bin")
parser.add_argument("--measure", action="store_true", help="also print peak RSS / peak disk of old vs new installer")
args = parser.parse_args()

env = dict(os.environ)
exe = ".exe" if os.name == "nt" else ""
cmake = str(Path(args.cmake_bin) / "cmake.exe") if os.name == "nt" else "cmake"
configure = ["-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", f"-DOSSL_DIR={args.ossl}"]
if os.name == "nt":
    env["PATH"] = args.mingw + os.pathsep + args.cmake_bin + os.pathsep + env["PATH"]
    configure += ["-DCMAKE_C_COMPILER=x86_64-w64-mingw32-clang", "-DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-clang++"]

subprocess.run([cmake, "-S", str(here), "-B", args.build, *configure], check=True, env=env)
subprocess.run([cmake, "--build", args.build, "-j", "8"], check=True, env=env)

cmd = [str(Path(args.build) / f"fw_tests{exe}"), "--core", args.core, "--fonts", args.fonts, "--work", args.work]
if args.reference:
    cmd += ["--reference", args.reference]
rc = subprocess.run(cmd, env=env).returncode

if args.measure:
    measure = Path(args.work) / "measure"
    for tool, label in (("fw_ref", "old (vendored install_pup)"), ("fw_tests", "new (staged installer)")):
        for pup in (args.core, args.fonts):
            root = measure / f"{tool}-{Path(pup).name}"
            subprocess.run([cmake, "-E", "remove_directory", str(root)], env=env)
            tool_args = [pup, str(root)] if tool == "fw_ref" else ["--install", pup, str(root)]
            out = subprocess.run([str(Path(args.build) / f"{tool}{exe}"), *tool_args], env=env, capture_output=True, text=True).stdout
            keep = [line for line in out.splitlines() if line.startswith(("version", "status", "peak"))]
            print(f"{label} {Path(pup).name}: " + ", ".join(keep))
sys.exit(rc)
