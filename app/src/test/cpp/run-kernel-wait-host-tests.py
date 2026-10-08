"""Run wait regressions on host without an Android device or the guest JIT.

Run with --mutation-check to verify the assertions reject missing late callback
wakeups, exit-status delivery and guest scheduler token release.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cxx", default=shutil.which("g++") or "C:/msys64/ucrt64/bin/g++.exe")
parser.add_argument("--mutation-check", action="store_true")
args = parser.parse_args()
here = Path(__file__).resolve().parent
core = here.parents[1] / "main/cpp/vita3k/vita3k"
vendor = core.parent
includes = [core / name / "include" for name in ("kernel", "cpu", "mem", "util")]
includes.append(vendor / "external/fmt/include")
env = dict(os.environ)
env["PATH"] = str(Path(args.cxx).resolve().parent) + os.pathsep + env.get("PATH", "")

def build_run(output, sources, *, support=False, expect_failure=False):
    command = [args.cxx, "-std=c++23", "-pthread"]
    if support:
        command += ["-I" + str(here / "kernel_wait_support")]
    command += ["-I" + str(p) for p in includes]
    command += [str(p) for p in sources] + ["-o", str(output)]
    subprocess.run(command, env=env, check=True, timeout=90)
    result = subprocess.run([str(output)], env=env, capture_output=True, text=True, timeout=15)
    if expect_failure:
        if result.returncode == 0:
            raise AssertionError("Regression survived mutation: " + str(output))
        print("Rejected mutation:", output.stem)
    else:
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        print(result.stdout.strip())

with tempfile.TemporaryDirectory(prefix="emucorev-kernel-waits-") as temporary:
    output = Path(temporary)
    wait = core / "kernel/src/thread_wait.cpp"
    callback = core / "kernel/src/callback.cpp"
    frame = core / "kernel/src/thread_callback.cpp"
    tests = here / "kernel_thread_wait_tests.cpp"
    build_run(output / "queue.exe", [here / "kernel_wait_queue_tests.cpp"])
    build_run(output / "thread.exe", [tests, wait, callback, frame], support=True)
    if args.mutation_check:
        mutants = [
            ("late-callback", callback, "        thread->notify_callbacks();", "        (void)thread;"),
            ("notification-retention", callback, "    this->reset();\n    return notification;", "    return notification;"),
            ("exit-status", wait, "*waiter.entry.exit_status = static_cast<SceInt32>(returned_value);", "*waiter.entry.exit_status = -99;"),
            ("scheduler-token", wait, "if (status == ThreadStatus::waiting && cpu && cpu.get() == guest_sched_token_cpu())\n        guest_sched_release_for_block();", "if (status == ThreadStatus::waiting)\n        (void)0;"),
            ("freeze-entry", wait, "if ((world_stop_requested || vm_suspended || debugger_suspended) && !exiting())", "if (false)"),
            ("freeze-restoration", frame, "if (!wait_for_guest_resume(thread_lock))\n        return returned_value;", "if (exiting())\n        return returned_value;"),
        ]
        for name, original, before, after in mutants:
            source = original.read_text(encoding="utf8")
            if before not in source:
                raise AssertionError("Mutation target absent: " + name)
            mutant = output / (name + ".cpp")
            mutant.write_text(source.replace(before, after, 1), encoding="utf8")
            sources = [tests, mutant if original == wait else wait, mutant if original == callback else callback, mutant if original == frame else frame]
            build_run(output / (name + ".exe"), sources, support=True, expect_failure=True)
