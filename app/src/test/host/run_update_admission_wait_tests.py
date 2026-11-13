#!/usr/bin/env python3
"""Exercise production NGS update-admission wait-lease accounting."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
out = root / "app/build/update-admission-wait-probe"
out.mkdir(parents=True, exist_ok=True)
if os.name == "nt":
    os.environ["PATH"] = "C:/msys64/ucrt64/bin;" + os.environ.get("PATH", "")

source = r'''
#include <ngs/scheduler.h>
#include <ngs/state.h>
#include <ngs/update_admission.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>

int main() {
    ngs::UpdateAdmission admission;
    const auto rack = ngs::UpdateAdmission::WaitKind::RackRelease;
    const auto system = ngs::UpdateAdmission::WaitKind::SystemRelease;
    int failed = 0;
    auto check = [&](bool value, const char *name) {
        std::printf("%s %s\n", value ? "PASS" : "FAIL", name);
        failed += !value;
    };

    check(admission.active_wait_count(rack) == 0 && admission.active_wait_count(system) == 0,
        "counters start clear");
    {
        auto wait = admission.track_wait(rack);
        check(admission.active_wait_count(rack) == 1, "lease increments its wait kind");
    }
    check(admission.active_wait_count(rack) == 0, "scope exit decrements wait count");

    {
        auto original = admission.track_wait(rack);
        auto moved = std::move(original);
        check(admission.active_wait_count(rack) == 1, "move construction transfers one counted lease");
    }
    check(admission.active_wait_count(rack) == 0, "moved lease decrements exactly once");

    {
        auto destination = admission.track_wait(rack);
        auto source = admission.track_wait(system);
        destination = std::move(source);
        check(admission.active_wait_count(rack) == 0 && admission.active_wait_count(system) == 1,
            "move assignment releases old kind and transfers new kind");
    }
    check(admission.active_wait_count(rack) == 0 && admission.active_wait_count(system) == 0,
        "move-assigned lease decrements exactly once");

    try {
        auto wait = admission.track_wait(system);
        throw std::runtime_error("unwind");
    } catch (const std::runtime_error &) {
        check(admission.active_wait_count(system) == 0, "exception unwind releases wait lease");
    }

    check(admission.active_wait_count(rack) == 0 && admission.active_wait_count(system) == 0,
        "all wait counters return to zero");

    ngs::VoiceScheduler scheduler;
    std::lock_guard<std::recursive_mutex> scheduler_lock(scheduler.mutex);
    check(scheduler.rack_release_action(42, false) == ngs::RackReleaseAction::Immediate,
        "normal release remains immediate outside update");
    scheduler.update_thread_id = 42;
    scheduler.is_updating = true;
    check(scheduler.is_update_thread(42), "active updater recognizes same-thread callback re-entry");
    check(!scheduler.is_update_thread(43), "active updater does not classify another thread as re-entry");
    check(scheduler.rack_release_action(42, false) == ngs::RackReleaseAction::RejectReentrant,
        "same-thread synchronous release is rejected instead of queued");
    check(scheduler.rack_release_action(42, true) == ngs::RackReleaseAction::DeferCallback,
        "callback-based asynchronous release keeps its existing deferred path");
    check(scheduler.rack_release_action(43, false) == ngs::RackReleaseAction::Wait,
        "other-thread synchronous release retains its wait path");
    scheduler.is_updating = false;
    check(!scheduler.is_update_thread(42), "inactive scheduler ignores stale thread id");

    ngs::State registry;
    ngs::RackInitInfo info;
    info.address = 0x1234;
    info.info.size = 0x400;
    check(!registry.rack_address_in_use(info.address), "unregistered guest buffer may be initialized");
    check(!registry.overlapping_rack_buffer(info.address, info.info.size).has_value(), "unregistered guest buffer has no recorded overlap");
    registry.rack_infos.push_back(info);
    check(registry.rack_address_in_use(info.address), "live rack address is protected against reinitialization");
    const auto overlap = registry.overlapping_rack_buffer(info.address, info.info.size);
    check(overlap.has_value() && overlap->address == info.address && overlap->size == info.info.size,
        "live rack buffer range is available for overlap diagnostics");
    check(ngs::rack_buffer_ranges_overlap(0x1000, 0x1000, 0x1800, 0x1000),
        "overlap probe detects intersecting rack buffers");
    check(!ngs::rack_buffer_ranges_overlap(0x1000, 0x800, 0x1800, 0x1000),
        "overlap probe accepts adjacent rack buffers");

    std::vector<Ptr<ngs::Voice>> voices(2);
    ngs::RackVectorIntegrity integrity;
    auto *rack_object = reinterpret_cast<ngs::Rack *>(static_cast<uintptr_t>(0x1000));
    integrity.capture(rack_object, 0x1000, 0x1000, 0x2000, voices, 0x80, 0x40, 0x400, 0x500);
    check(integrity.matches(rack_object, voices), "integrity snapshot accepts unchanged vector object");
    check(integrity.rack_object_reservation == 0x80 && integrity.voice_vector_offset == 0x40
            && integrity.allocated_bytes == 0x400 && integrity.allocation_high_water == 0x500,
        "integrity metadata preserves rack allocator offsets");
    std::array<std::byte, sizeof(voices)> saved_vector{};
    std::memcpy(saved_vector.data(), &voices, sizeof(voices));
    const uintptr_t bad_begin = 1;
    std::memcpy(&voices, &bad_begin, sizeof(bad_begin));
    check(!integrity.matches(rack_object, voices), "integrity snapshot detects corrupted vector object without dereferencing it");
    std::memcpy(&voices, saved_vector.data(), sizeof(voices));
    return failed ? 1 : 0;
}
'''

probe = out / "probe.cpp"
probe.write_text(source)

sngs_source = (root / "app/src/main/cpp/vita3k/vita3k/modules/SceNgsUser/SceNgs.cpp").read_text()
rack_release_start = sngs_source.index("EXPORT(SceInt32, sceNgsRackRelease")
reject_reentrant = sngs_source.index("return RET_ERROR(SCE_NGS_ERROR_INVALID_STATE);", rack_release_start)
queue_release = sngs_source.index("operations_pending.push(op)", rack_release_start)
assert reject_reentrant < queue_release, "reentrant sync release must return before any deferred raw pointer is queued"

rack_init_start = sngs_source.index("EXPORT(SceUInt32, sceNgsRackInit")
address_guard = sngs_source.index("overlapping_rack_buffer(info->data.address(), info->size)", rack_init_start)
overlap_log = sngs_source.index("LOG_CRITICAL(\"NGS rack buffer ranges overlap", address_guard)
overlap_status = sngs_source.index("return RET_ERROR(SCE_NGS_ERROR_INVALID_STATE);", overlap_log)
init_call = sngs_source.index("ngs::init_rack(", rack_init_start)
assert address_guard < overlap_log < overlap_status < init_call, "rack init must diagnose and reject a live overlapping guest buffer before placement-new path"

ngs_source = (root / "app/src/main/cpp/vita3k/vita3k/ngs/src/ngs.cpp").read_text()
init_rack_start = ngs_source.index("bool init_rack(")
overlap_guard = ngs_source.index("rack_buffer_ranges_overlap(rack_address, init_info->size", init_rack_start)
overlap_reject = ngs_source.index("return false;", overlap_guard)
placement_new = ngs_source.index("new (rack) Rack", init_rack_start)
assert overlap_guard < overlap_reject < placement_new, "all overlapping guest buffers must be rejected before placement new"

capture_start = ngs_source.index("bool capture_state(")
vector_guard = ngs_source.index("check_rack_vector(rack", capture_start)
first_vector_access = ngs_source.index("rack->voices.size()", capture_start)
assert vector_guard < first_vector_access, "capture must validate the Rack vector object before size/index access"

def function_body(text, marker):
    start = text.index(marker)
    opening = text.index("{", start)
    depth = 0
    for end in range(opening, len(text)):
        depth += (text[end] == "{") - (text[end] == "}")
        if depth == 0:
            return text[opening:end + 1]
    raise AssertionError(f"unterminated function: {marker}")

def depth_at(body, pos):
    depth = 0
    for char in body[:pos]:
        depth += (char == "{") - (char == "}")
    return depth

rack_release_body = function_body(ngs_source, "void release_rack(")
rack_lock = rack_release_body.index("rack_infos_lock(ngs.rack_infos_mutex)")
rack_erase = rack_release_body.index("std::erase_if(ngs.rack_infos")
rack_destroy = rack_release_body.index("rack->~Rack()")
assert rack_lock < rack_erase < rack_destroy
assert depth_at(rack_release_body, rack_lock) == depth_at(rack_release_body, rack_destroy), \
    "rack-address reservation lock must remain held through Rack destruction"

system_release_body = function_body(ngs_source, "void release_system(")
system_scheduler_lock = system_release_body.index("scheduler_lock(system->voice_scheduler.mutex)")
system_registry_lock = system_release_body.index("rack_infos_lock(ngs.rack_infos_mutex)")
system_destroy = system_release_body.index("system->~System()")
assert system_scheduler_lock < system_registry_lock < system_destroy
assert depth_at(system_release_body, system_registry_lock) == depth_at(system_release_body, system_destroy), \
    "system teardown must retain rack-address reservations through destruction"

compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
if not compiler and os.name == "nt" and Path("C:/msys64/ucrt64/bin/g++.exe").is_file():
    compiler = "C:/msys64/ucrt64/bin/g++.exe"
assert compiler, "A C++20 compiler is required"

vita3k = root / "app/src/main/cpp/vita3k/vita3k"
include_dirs = [path for path in vita3k.rglob("include") if path.is_dir()]
subprocess.run([
    compiler,
    "-std=c++20",
    "-pthread",
    *[f"-I{path.relative_to(root).as_posix()}" for path in include_dirs],
    probe.relative_to(root).as_posix(),
    "-o",
    (out / "probe.exe").relative_to(root).as_posix(),
], check=True, cwd=root)
raise SystemExit(subprocess.run([(out / "probe.exe").as_posix()], cwd=root).returncode)
