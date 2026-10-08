#pragma once
// Only the external kernel registry boundary is replaced. ThreadState, Callback,
// WaitQueue and all waiting/callback servicing methods are production code.
#include <kernel/callback.h>
#include <atomic>
#include <map>
struct KernelState {
    Address halt_instruction_pc=0x100;
    std::atomic<uint64_t> thread_wake_counter{0};
    std::map<SceUID, CallbackPtr> callbacks;
    bool delete_callback(SceUID id) {
        auto it=callbacks.find(id);
        if(it==callbacks.end()) return false;
        it->second->mark_deleted(); callbacks.erase(it); return true;
    }
};
