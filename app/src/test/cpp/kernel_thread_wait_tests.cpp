// Exercises production ThreadState waiting, Callback and WaitQueue code on host.
// Guest CPU execution and the unrelated kernel registry are test boundaries.
#include <kernel/state.h>
#include <kernel/thread/thread_state.h>
#include <cassert>
#include <future>
#include <iostream>
#include <functional>
#include <chrono>
using namespace std::chrono_literals;
namespace {
std::function<uint32_t(ThreadState &,Address,const std::vector<uint32_t>&)> guest;
struct StubCPU {
    CPUContext context;
    uint32_t tpidruro=0x1234;
    unsigned preparations=0;
    unsigned restorations=0;
};
StubCPU &registers(CPUState &cpu) {return *reinterpret_cast<StubCPU*>(&cpu);}
thread_local CPUState *token_cpu=nullptr;
std::atomic<unsigned> releases=0;
void await_waiting(const ThreadStatePtr &thread) {
    std::unique_lock lock(thread->mutex);
    assert(thread->status_cond.wait_for(lock,2s,[&] {return thread->status==ThreadStatus::waiting;}));
}
void await_suspended(const ThreadStatePtr &thread) {
    std::unique_lock lock(thread->mutex);
    assert(thread->status_cond.wait_for(lock,2s,[&] {return thread->status==ThreadStatus::suspended;}));
}
template<class T> T ready(std::future<T> &future) {
    assert(future.wait_for(2s)==std::future_status::ready); return future.get();
}
}
// Only construction and JIT execution are substituted. Production callback
// preparation/restoration operates on a host register bank through CPU functions.
ThreadState::ThreadState(SceUID uid,KernelState &k,MemState &m):id(uid),kernel(k),mem(m) {
    call_level=1;
    auto *stub=new StubCPU;
    stub->context.set_pc(0x800); stub->context.set_sp(0x1000);
    cpu=CPUStatePtr(reinterpret_cast<CPUState*>(stub),[](CPUState *p){delete reinterpret_cast<StubCPU*>(p);});
}
ThreadState::~ThreadState()=default;
void ThreadState::run_loop() {
    const auto &ctx=registers(*cpu).context;
    returned_value=guest(*this,ctx.get_pc(),{ctx.cpu_registers[0],ctx.cpu_registers[1],ctx.cpu_registers[2],ctx.cpu_registers[3]});
}
uint32_t read_sp(CPUState &cpu) {return registers(cpu).context.get_sp();}
uint32_t read_tpidruro(CPUState &cpu) {return registers(cpu).tpidruro;}
void write_reg(CPUState &cpu,size_t i,uint32_t value) {registers(cpu).context.cpu_registers[i]=value;}
void write_sp(CPUState &cpu,uint32_t value) {registers(cpu).context.set_sp(value);}
void write_pc(CPUState &cpu,uint32_t value) {++registers(cpu).preparations;registers(cpu).context.set_pc(value);}
void write_lr(CPUState &cpu,uint32_t value) {registers(cpu).context.set_lr(value);}
void write_tpidruro(CPUState &cpu,uint32_t value) {registers(cpu).tpidruro=value;}
CPUContext save_context(CPUState &cpu) {return registers(cpu).context;}
void load_context(CPUState &cpu,const CPUContext &context) {++registers(cpu).restorations;registers(cpu).context=context;}
// These regressions use host variables as output slots and never restore RAM.
// Their logical guest addresses are outside this test boundary.
Address host_to_guest(const MemState &, const void *) { return 0; }
void stop(CPUState &) {}
void guest_sched_release_for_block() {token_cpu=nullptr; ++releases;}
CPUState *guest_sched_token_cpu() {return token_cpu;}

int main() {
    KernelState kernel; MemState mem;
    auto owner=std::make_shared<ThreadState>(1,kernel,mem); owner->priority=100;
    auto cb=std::make_shared<Callback>(10,owner,"callback",Ptr<SceKernelCallbackFunction>(4),Ptr<void>{});
    kernel.callbacks.emplace(10,cb); owner->add_callback(cb);
    int executions=0;
    guest=[&](ThreadState &,Address,const std::vector<uint32_t>&args) {
        ++executions; assert(args[1]==1); assert(args[2]==42); return 0;
    };
    auto late=std::async(std::launch::async,[&] {return owner->delay_until(std::chrono::steady_clock::now()+80ms,true);});
    await_waiting(owner); cb->direct_notify(42);
    assert(*ready(late)==SCE_KERNEL_OK && executions==1);

    // Taking the old notification before guest execution leaves a self-notify pending.
    guest=[&](ThreadState &,Address,const std::vector<uint32_t>&args) {
        ++executions; assert(args[2]==7); cb->direct_notify(9);
        // A CB wait inside a running callback must not dispatch recursively.
        assert(*owner->delay_until(std::chrono::steady_clock::now()+2ms,true)==SCE_KERNEL_OK);
        return 0;
    };
    cb->direct_notify(7); assert(owner->process_callbacks()==1);
    assert(cb->get_num_notifications()==1);
    guest=[&](ThreadState &,Address,const std::vector<uint32_t>&args) {assert(args[2]==9); ++executions; return 0;};
    assert(owner->process_callbacks()==1 && cb->get_num_notifications()==0);

    // A non-CB wait keeps callbacks pending until explicitly processed.
    auto plain=std::async(std::launch::async,[&] {return owner->delay_until(std::chrono::steady_clock::now()+30ms,false);});
    await_waiting(owner); cb->direct_notify(9); assert(*ready(plain)==SCE_KERNEL_OK);
    assert(cb->get_num_notifications()==1); assert(owner->process_callbacks()==1);

    // A callback's nonzero return deletes it, including retained external references.
    guest=[](ThreadState &,Address,const std::vector<uint32_t>&) {return 1;};
    cb->direct_notify(2); assert(owner->process_callbacks()==1);
    assert(kernel.callbacks.empty()); cb->direct_notify(3); assert(cb->get_num_notifications()==0);

    // A callback exiting the owner unwinds an indefinite signal wait without a signal.
    auto exiting=std::make_shared<ThreadState>(2,kernel,mem); exiting->priority=100;
    auto exit_cb=std::make_shared<Callback>(11,exiting,"exit",Ptr<SceKernelCallbackFunction>(8),Ptr<void>{});
    exiting->add_callback(exit_cb); kernel.callbacks.emplace(11,exit_cb);
    guest=[](ThreadState &thread,Address,const std::vector<uint32_t>&) {thread.exit(77);return 0;};
    auto ended=std::async(std::launch::async,[&] {return exiting->wait_for_signal(true);});
    await_waiting(exiting); exit_cb->direct_notify(0); assert(!ready(ended));

    // Thread end wakes a blocked waiter with the recorded exit status.
    auto target=std::make_shared<ThreadState>(3,kernel,mem);target->priority=100;
    auto waiter=std::make_shared<ThreadState>(4,kernel,mem);waiter->priority=100;
    target->status=ThreadStatus::running; SceInt32 exit_status=-1;
    auto joined=std::async(std::launch::async,[&] {return target->wait_for_thread_end(waiter,&exit_status,false);});
    await_waiting(waiter); target->exit(123);
    {std::lock_guard lock(target->mutex);target->update_status(ThreadStatus::dormant);}
    assert(*ready(joined)==SCE_KERNEL_OK && exit_status==123);

    // A callback may satisfy a primitive while its waiter has dropped the object lock.
    auto queued=std::make_shared<ThreadState>(6,kernel,mem); queued->priority=100;
    auto queue_cb=std::make_shared<Callback>(12,queued,"queue",Ptr<SceKernelCallbackFunction>(12),Ptr<void>{});
    queued->add_callback(queue_cb); kernel.callbacks.emplace(12,queue_cb);
    std::mutex primitive; WaitQueue<int> queue; bool available=false; int consumed=0;
    guest=[&](ThreadState &,Address,const std::vector<uint32_t>&) {
        std::lock_guard lock(primitive); available=true; return 0;
    };
    auto queue_wait=std::async(std::launch::async,[&] {
        std::unique_lock lock(primitive);
        return queue.wait_until_ready(lock,queued,{},0,Deadline::max(),true,[&](auto &) {
            if(!available) return false; ++consumed; return true;
        });
    });
    await_waiting(queued); queue_cb->direct_notify(0);
    assert(*ready(queue_wait)==SCE_KERNEL_OK && consumed==1 && queue.empty());

    // Exiting from that callback must remove the waiter before its readiness check.
    auto abandoned=std::make_shared<ThreadState>(7,kernel,mem); abandoned->priority=100;
    auto abandon_cb=std::make_shared<Callback>(13,abandoned,"abandon",Ptr<SceKernelCallbackFunction>(16),Ptr<void>{});
    abandoned->add_callback(abandon_cb); kernel.callbacks.emplace(13,abandon_cb);
    consumed=0; available=true;
    guest=[](ThreadState &thread,Address,const std::vector<uint32_t>&) {thread.exit(1);return 0;};
    auto abandoned_wait=std::async(std::launch::async,[&] {
        std::unique_lock lock(primitive);
        return queue.wait_until_ready(lock,abandoned,{},0,Deadline::max(),true,[&](auto &) {++consumed;return true;});
    });
    await_waiting(abandoned); abandon_cb->direct_notify(0);
    assert(!ready(abandoned_wait) && consumed==0 && queue.empty());

    // A freeze accepted while WAITING must block later callback context preparation.
    auto frozen=std::make_shared<ThreadState>(8,kernel,mem); frozen->priority=100;
    auto frozen_cb=std::make_shared<Callback>(14,frozen,"freeze",Ptr<SceKernelCallbackFunction>(20),Ptr<void>{});
    frozen->add_callback(frozen_cb); kernel.callbacks.emplace(14,frozen_cb);
    std::atomic<int> context_mutations=0;
    guest=[&](ThreadState &thread,Address,const std::vector<uint32_t>&) {
        ++context_mutations; thread.send_signal(); return 0;
    };
    auto frozen_wait=std::async(std::launch::async,[&] {return frozen->wait_for_signal(true);});
    await_waiting(frozen);
    frozen->request_world_stop();
    assert(frozen->wait_world_stopped(std::chrono::steady_clock::now()+1s));
    frozen_cb->direct_notify(0); await_suspended(frozen);
    assert(frozen_wait.wait_for(30ms)==std::future_status::timeout);
    assert(context_mutations==0 && frozen_cb->get_num_notifications()==1);
    assert(registers(*frozen->cpu).preparations==0 && registers(*frozen->cpu).context.get_pc()==0x800);
    frozen->resume_from_world();
    assert(*ready(frozen_wait)==SCE_KERNEL_OK && context_mutations==1);

    // VM-only, both resume orders for overlapping freezes, and deletion while frozen.
    for(int scenario=0;scenario<4;++scenario) {
        auto paused=std::make_shared<ThreadState>(20+scenario,kernel,mem); paused->priority=100;
        auto paused_cb=std::make_shared<Callback>(30+scenario,paused,"paused",Ptr<SceKernelCallbackFunction>(24),Ptr<void>{});
        paused->add_callback(paused_cb); kernel.callbacks.emplace(30+scenario,paused_cb);
        context_mutations=0;
        auto paused_wait=std::async(std::launch::async,[&] {return paused->wait_for_signal(true);});
        await_waiting(paused); paused->suspend_and_wait();
        if(scenario==1 || scenario==2) paused->request_world_stop();
        paused_cb->direct_notify(0); await_suspended(paused);
        assert(paused_wait.wait_for(20ms)==std::future_status::timeout);
        assert(context_mutations==0 && paused_cb->get_num_notifications()==1);
        assert(registers(*paused->cpu).preparations==0 && registers(*paused->cpu).context.get_pc()==0x800);
        if(scenario==3) {
            paused->exit_delete(false);
            assert(!ready(paused_wait) && context_mutations==0);
            assert(paused_cb->get_num_notifications()==1);
            continue;
        }
        if(scenario==1) {
            paused->resume_if_suspended();
            assert(paused_wait.wait_for(20ms)==std::future_status::timeout && context_mutations==0);
            paused->resume_from_world();
        } else if(scenario==2) {
            paused->resume_from_world();
            assert(paused_wait.wait_for(20ms)==std::future_status::timeout && context_mutations==0);
            paused->resume_if_suspended();
        } else {
            paused->resume_if_suspended();
        }
        assert(*ready(paused_wait)==SCE_KERNEL_OK && context_mutations==1);
    }

    // Freeze after the wait has returned, before a direct callback context rewrite.
    auto direct=std::make_shared<ThreadState>(40,kernel,mem); direct->priority=100;
    direct->request_world_stop(); context_mutations=0;
    auto direct_call=std::async(std::launch::async,[&] {return direct->run_callback(28,{});});
    await_suspended(direct);
    assert(direct_call.wait_for(20ms)==std::future_status::timeout && context_mutations==0);
    direct->resume_from_world(); assert(ready(direct_call)==0 && context_mutations==1);

    // World and VM resume must leave an independent debugger suspension in force.
    auto debugged=std::make_shared<ThreadState>(41,kernel,mem); debugged->priority=100;
    debugged->status=ThreadStatus::running; debugged->suspend(); context_mutations=0;
    auto debug_call=std::async(std::launch::async,[&] {return debugged->run_callback(32,{});});
    await_suspended(debugged);
    debugged->request_world_stop(); debugged->suspend_and_wait();
    debugged->resume_from_world(); debugged->resume_if_suspended();
    assert(debug_call.wait_for(20ms)==std::future_status::timeout && context_mutations==0);
    debugged->resume(); assert(ready(debug_call)==0 && context_mutations==1);

    // Freeze when the callback returns, before restoring its saved guest context.
    // Delete bypasses the gate while leaving context untouched during the freeze.
    for(int scenario=0;scenario<3;++scenario) {
        auto restoring=std::make_shared<ThreadState>(50+scenario,kernel,mem); restoring->priority=100;
        guest=[&](ThreadState &thread,Address,const std::vector<uint32_t>&) {
            if(scenario==1) thread.suspend_and_wait(); else thread.request_world_stop();
            return 9;
        };
        auto restore_call=std::async(std::launch::async,[&] {return restoring->run_callback(0x44,{});});
        await_suspended(restoring);
        assert(restore_call.wait_for(20ms)==std::future_status::timeout);
        auto &bank=registers(*restoring->cpu);
        assert(bank.preparations==1 && bank.restorations==0 && bank.context.get_pc()==0x44);
        if(scenario==2) {
            restoring->exit_delete(false);
            assert(ready(restore_call)==9 && bank.restorations==0 && bank.context.get_pc()==0x44);
        } else {
            if(scenario==1) restoring->resume_if_suspended(); else restoring->resume_from_world();
            assert(ready(restore_call)==9 && bank.restorations==1 && bank.context.get_pc()==0x800);
        }
    }

    // The actual transition to waiting must release a token belonging to this CPU.
    auto gated=std::make_shared<ThreadState>(5,kernel,mem);gated->priority=100;
    gated->cpu=CPUStatePtr(reinterpret_cast<CPUState*>(1),[](CPUState*){});
    token_cpu=gated->cpu.get(); const auto before=releases.load();
    assert(*gated->delay_until(std::chrono::steady_clock::now()+2ms,false)==SCE_KERNEL_OK);
    assert(token_cpu==nullptr && releases>before);
    assert(kernel.thread_wake_counter>0);
    std::cout<<"Thread wait late CB, self-notify, non-CB isolation, callback deletion/exit, exit-status, token-release and world/VM/debugger freeze tests passed\n";
}
