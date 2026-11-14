#pragma once
#include <gxm/types.h>
#include <mem/ptr.h>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct KernelState;
struct ThreadState;
namespace renderer {
struct State;
struct Context;
struct CommandSnapshot;
enum class FinishPhase : uint32_t { Fence, Drain };
struct FinishSnapshot {
    uint64_t id = 0;
    SceUID owner = 0; // Zero means the deleted caller abandoned its queued fence.
    Address context_address = 0;
    FinishPhase phase = FinishPhase::Fence;
    bool fence_done = false;
    bool drain_done = false;
    int32_t result = 0;
};
struct FinishOperation {
    std::mutex mutex;
    FinishSnapshot saved;
    std::weak_ptr<ThreadState> waiter;
    bool drain_submitted = false;
    bool aborted = false;
};
// Caller/renderer are parked at the snapshot boundary. Output publishes atomically.
bool capture_finish_operations(State &, std::vector<FinishSnapshot> &, std::string &);
bool restore_finish_operations(State &, const std::vector<FinishSnapshot> &, std::string &);
bool validate_finish_operations(const std::vector<FinishSnapshot> &, const std::vector<CommandSnapshot> &, std::string &);
void register_finish_wait_handlers(KernelState &, State &);
SceInt32 finish_guest(State &, ThreadState &, Context *, Address context_address);
void complete_finish_command(State &, uint64_t id, int result);
void abort_finish_operations(State &);
}
