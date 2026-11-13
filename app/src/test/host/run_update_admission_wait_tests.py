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
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <future>
#include <stdexcept>
#include <thread>
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
        "same-thread synchronous release is rejected outside a finished callback");
    check(scheduler.rack_release_action(42, true) == ngs::RackReleaseAction::DeferCallback,
        "callback-based asynchronous release keeps its existing deferred path");
    scheduler.callback_kind = ngs::UpdateCallbackKind::VoiceFinished;
    check(scheduler.rack_release_action(42, false) == ngs::RackReleaseAction::ReleaseFinishedCallback,
        "finished callback can release synchronously with an update abort");
    scheduler.callback_kind = ngs::UpdateCallbackKind::ModuleProcess;
    check(scheduler.rack_release_action(42, false) == ngs::RackReleaseAction::ReleaseModuleCallback,
        "module-processing callback can release synchronously with process-stack abort");
    auto *pending_rack = reinterpret_cast<ngs::Rack *>(static_cast<uintptr_t>(0x2000));
    ngs::OperationPending pending_release{};
    pending_release.type = ngs::PendingType::ReleaseRack;
    pending_release.release_data.rack = pending_rack;
    scheduler.operations_pending.push(pending_release);
    check(scheduler.has_pending_rack_release(pending_rack)
            && scheduler.rack_release_action(42, false, scheduler.has_pending_rack_release(pending_rack))
                == ngs::RackReleaseAction::RejectAlreadyPending,
        "duplicate synchronous release is rejected while its callback release is pending");
    scheduler.operations_pending.pop();
    check(!scheduler.has_pending_rack_release(pending_rack), "pending release record clears after pop");
    auto *another_pending_rack = reinterpret_cast<ngs::Rack *>(static_cast<uintptr_t>(0x3000));
    pending_release.release_data.rack = another_pending_rack;
    scheduler.operations_pending.push(pending_release);
    scheduler.callback_kind = ngs::UpdateCallbackKind::VoiceFinished;
    check(!scheduler.has_pending_rack_release(pending_rack)
            && scheduler.rack_release_action(42, false, scheduler.has_pending_rack_release(pending_rack))
                == ngs::RackReleaseAction::ReleaseFinishedCallback,
        "a pending release for another Rack does not block this Rack's finished callback release");
    scheduler.operations_pending.pop();
    scheduler.abort_update_after_callback = true;
    check(scheduler.consume_update_abort_after_callback() && !scheduler.consume_update_abort_after_callback(),
        "finished callback abort flag is consumed once");
    scheduler.callback_kind = ngs::UpdateCallbackKind::None;
    check(scheduler.rack_release_action(43, false) == ngs::RackReleaseAction::Wait,
        "other-thread synchronous release retains its wait path");
    scheduler.is_updating = false;
    check(!scheduler.is_update_thread(42), "inactive scheduler ignores stale thread id");
    {
        ngs::VoiceScheduler::CallbackContextScope module_context(scheduler, ngs::UpdateCallbackKind::ModuleProcess);
        check(scheduler.callback_kind == ngs::UpdateCallbackKind::ModuleProcess, "callback context identifies module processing");
        {
            ngs::VoiceScheduler::CallbackContextScope finished_context(scheduler, ngs::UpdateCallbackKind::VoiceFinished);
            check(std::string(ngs::update_callback_kind_name(scheduler.callback_kind)) == "voice-finished",
                "nested callback context identifies finished callback");
        }
        check(scheduler.callback_kind == ngs::UpdateCallbackKind::ModuleProcess, "nested callback context restores prior kind");
    }
    check(scheduler.callback_kind == ngs::UpdateCallbackKind::None, "callback context returns to none on scope exit");
    try {
        ngs::VoiceScheduler::CallbackContextScope module_context(scheduler, ngs::UpdateCallbackKind::ModuleProcess);
        throw std::runtime_error("callback unwind");
    } catch (const std::runtime_error &) {
        check(scheduler.callback_kind == ngs::UpdateCallbackKind::None,
            "callback context restores after exception unwind");
    }

    ngs::VoiceScheduler wait_scheduler;
    wait_scheduler.is_updating = true;
    std::promise<void> entered_wait;
    auto waiter = std::async(std::launch::async, [&] {
        std::unique_lock<std::recursive_mutex> lock(wait_scheduler.mutex);
        entered_wait.set_value();
        wait_scheduler.wait_for_update_to_finish(lock);
    });
    entered_wait.get_future().wait();
    {
        std::lock_guard<std::recursive_mutex> lock(wait_scheduler.mutex);
        wait_scheduler.condvar.notify_all(); // A spurious wake must not release the Rack.
    }
    check(waiter.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout,
        "Rack release waiter remains blocked after a spurious update notification");
    {
        std::lock_guard<std::recursive_mutex> lock(wait_scheduler.mutex);
        wait_scheduler.is_updating = false;
    }
    wait_scheduler.condvar.notify_all();
    check(waiter.wait_for(std::chrono::seconds(1)) == std::future_status::ready,
        "Rack release waiter proceeds after updater completion");
    waiter.get();

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
    registry.rack_infos[0].instance_id = 17;
    const auto waited_instance = registry.rack_instance_id(info.address);
    check(waited_instance == 17, "release waiter can snapshot the live Rack instance id");
    registry.rack_infos.erase(registry.rack_infos.begin());
    registry.rack_infos.push_back(info);
    registry.rack_infos[0].instance_id = 18;
    check(registry.rack_instance_id(info.address) != waited_instance,
        "release waiter distinguishes a placement-new Rack reusing the same guest address");
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

scheduler_source = (root / "app/src/main/cpp/vita3k/vita3k/ngs/src/scheduler.cpp").read_text()
assert "UpdateCallbackKind::ModuleProcess" in scheduler_source
assert "UpdateCallbackKind::VoiceFinished" in scheduler_source
assert "UpdateCallbackKind::RackReleaseCompletion" in scheduler_source

def function_body(text, marker):
    start = text.index(marker)
    opening = text.index("{", start)
    depth = 0
    for end in range(opening, len(text)):
        depth += (text[end] == "{") - (text[end] == "}")
        if depth == 0:
            return text[opening:end + 1]
    raise AssertionError(f"unterminated function: {marker}")

update_body = function_body(scheduler_source, "void VoiceScheduler::update(")
finish_callback = update_body.index("voice->invoke_callback(")
reacquire_scheduler = update_body.index("scheduler_lock.lock();", finish_callback)
abort_check = update_body.index("if (consume_update_abort_after_callback())", reacquire_scheduler)
detach_voice_lock = update_body.index("voice_lock.release();", abort_check)
abort_break = update_body.index("break;", detach_voice_lock)
reacquire_voice = update_body.index("voice_lock.lock();", abort_break)
master_route = update_body.index("const bool can_route_to_master", reacquire_voice)
assert finish_callback < reacquire_scheduler < abort_check < detach_voice_lock < abort_break < reacquire_voice < master_route, \
    "finished-callback release must abort and detach the destroyed voice lock before touching queue-copy, master, or routing pointers"
assert update_body.index("std::vector<ngs::Voice *> queue_copy = queue;") < finish_callback
assert update_body.index("ngs::Voice *implicit_master = nullptr;") < finish_callback
assert update_body.index("std::vector<ngs::Voice *> implicit_sources;") < finish_callback
process_abort_check = update_body.index("if (abort_update_after_callback)")
scope_reacquire = update_body.index("scheduler_lock.lock();", process_abort_check)
inner_abort_break = update_body.index("if (abort_update_after_callback)\n                    break;", scope_reacquire)
module_finished_check = update_body.index("if (module_finished)", inner_abort_break)
post_loop_abort = update_body.index("if (abort_update_after_callback)", module_finished_check)
detach_voice_lock = update_body.index("voice_lock.release();", post_loop_abort)
abort_break = update_body.index("break;", detach_voice_lock)
finished_voice_handling = update_body.index("if (finished)", abort_break)
assert process_abort_check < scope_reacquire < inner_abort_break < module_finished_check < post_loop_abort < detach_voice_lock < abort_break < finished_voice_handling, \
    "module callback release must return from the module loop, reacquire only the scheduler, and detach the unlocked voice lock before finished/routing logic"

finished_release_branch = sngs_source.index("} else if (release_action == ngs::RackReleaseAction::ReleaseFinishedCallback")
finished_release_end = sngs_source.index("} else if (release_action == ngs::RackReleaseAction::RejectReentrant)", finished_release_branch)
finished_release_body = sngs_source[finished_release_branch:finished_release_end]
assert "RackReleaseAction::ReleaseModuleCallback" in finished_release_body
assert finished_release_body.index("ngs::release_rack(") < finished_release_body.index("scheduler.request_update_abort_after_callback();"), \
    "module/finished callback must synchronously release guest storage before returning success and requesting update abort"

pending_drain = update_body[update_body.index("while (!operations_pending.empty())"):]
pending_front = pending_drain.index("operations_pending.front()")
pending_pop = pending_drain.index("operations_pending.pop()")
pending_release_rack = pending_drain.index("release_rack(")
completion_callback = pending_drain.index("run_callback(op.release_data.callback")
assert pending_front < pending_pop < pending_release_rack < completion_callback
completion_callback_end = pending_drain.index(";", completion_callback) + 1
assert "op." not in pending_drain[completion_callback_end:], \
    "pending-release completion must not retain or dereference its Rack operation after guest callback re-entry"
assert "UpdateCallbackKind::RackReleaseCompletion" in (root / "app/src/main/cpp/vita3k/vita3k/ngs/include/ngs/scheduler.h").read_text()

for relative_path in (
    "app/src/main/cpp/vita3k/vita3k/ngs/src/modules/player.cpp",
    "app/src/main/cpp/vita3k/vita3k/ngs/src/modules/atrac9.cpp",
):
    module_source = (root / relative_path).read_text()
    cursor = 0
    while True:
        callback_pos = module_source.find("data.invoke_callback(", cursor)
        if callback_pos < 0:
            break
        resume_lock = module_source.find("scheduler_lock.lock();", callback_pos)
        if resume_lock < 0:
            resume_lock = len(module_source)
        assert "return false;" in module_source[callback_pos:resume_lock], \
            f"{relative_path}: module callback abort must return before reacquiring locks or dereferencing destroyed ModuleData"
        cursor = callback_pos + len("data.invoke_callback(")

module_data_body = function_body(ngs_source, "bool ModuleData::invoke_callback(")
module_callback_call = module_data_body.index("parent->invoke_callback(")
module_callback_end = module_data_body.index("reason2, reason_ptr);", module_callback_call) + len("reason2, reason_ptr);")
assert "parent->" not in module_data_body[module_callback_end:], \
    "ModuleData callback wrapper must not touch the destroyed parent after guest callback return"
voice_callback_body = function_body(ngs_source, "bool Voice::invoke_callback(")
guest_callback_call = voice_callback_body.index("thread->run_callback(")
voice_callback_tail = voice_callback_body[guest_callback_call:]
assert voice_callback_tail.index("stack_free(") < voice_callback_tail.index("return scheduler->abort_update_after_callback;")

wait_release_branch = sngs_source.index("} else if (release_action == ngs::RackReleaseAction::Wait)", finished_release_end)
wait_release_end = sngs_source.index("} else if (release_action == ngs::RackReleaseAction::DeferCallback)", wait_release_branch)
wait_release_body = sngs_source[wait_release_branch:wait_release_end]
assert wait_release_body.index("scheduler.wait_for_update_to_finish(lock);") < wait_release_body.index("rack_instance_id(rack_address) == rack_instance_id")
assert "!is_updating" in (root / "app/src/main/cpp/vita3k/vita3k/ngs/include/ngs/scheduler.h").read_text()
assert "rack->system" not in wait_release_body and "rack->" not in wait_release_body.split("scheduler.wait_for_update_to_finish(lock);", 1)[1], \
    "a release waiter must not dereference a Rack after waking because its buffer may have been reused"

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
