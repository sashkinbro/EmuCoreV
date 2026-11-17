// Opt-in firmware research for instrumentation. Never linked into release builds.
#ifndef NDEBUG
#include <jni.h>

#include <android_state.h>
#include <app/session_controller.h>
#include <cpu/functions.h>
#include <cpu/state.h>
#include <kernel/load_self.h>
#include <kernel/state.h>
#include <mem/functions.h>
#include <mem/state.h>
#include <nids/functions.h>
#include <packages/sce_types.h>

#include <chrono>
#include <sstream>
#include <string_view>
#include <vector>

namespace {
std::vector<uint8_t> read_bytes(JNIEnv *env, jbyteArray input) {
    std::vector<uint8_t> bytes(env->GetArrayLength(input));
    env->GetByteArrayRegion(input, 0, bytes.size(), reinterpret_cast<jbyte *>(bytes.data()));
    return bytes;
}
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_sbro_emucorev_ShellFirmwareResearchTest_decode(JNIEnv *env, jobject, jbyteArray input) {
    const auto bytes = read_bytes(env, input);
    const uint8_t key[16]{};
    const auto decoded = decrypt_fself(bytes, key);
    auto result = env->NewByteArray(decoded.size());
    if (!decoded.empty())
        env->SetByteArrayRegion(result, 0, decoded.size(), reinterpret_cast<const jbyte *>(decoded.data()));
    return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_sbro_emucorev_ShellFirmwareResearchTest_link(JNIEnv *env, jobject, jobjectArray inputs, jboolean trace) {
    // A fresh address space: do not borrow an active emulator. Optional tracing
    // stops at the first SVC; neither mode proves shell boot compatibility.
    if (const auto *session = get_app_session_controller(); session && session->has_active_session())
        return env->NewStringUTF("ERROR: stop the active emulator session before firmware research");
    MemState mem;
    if (!init(mem, false))
        return env->NewStringUTF("ERROR: cannot initialize isolated guest memory");
    std::ostringstream report;
    const auto started = std::chrono::steady_clock::now();
    {
        KernelState kernel;
        for (jsize i = 0; i < env->GetArrayLength(inputs); ++i) {
            auto input = static_cast<jbyteArray>(env->GetObjectArrayElement(inputs, i));
            const auto bytes = read_bytes(env, input);
            env->DeleteLocalRef(input);
            const auto id = load_self(kernel, mem, bytes.data(), "firmware-research", {});
            if (id < 0) {
                report << "ERROR: module " << i << " failed to link: " << id << '\n';
                break;
            }
            const auto &info = kernel.loaded_modules.at(id)->info;
            report << info.module_name << " entry=0x" << std::hex
                   << info.start_entry.address() << std::dec << '\n';
        }
        size_t bound = 0;
        for (const auto &[nid, binding] : kernel.func_binding_infos)
            if (kernel.export_nids_by_lib.contains(lib_export_key(binding.library_nid, nid)))
                ++bound;
        report << "modules=" << kernel.loaded_modules.size()
               << " imports=" << kernel.func_binding_infos.size()
               << " native_bindings=" << bound << '\n';
        report << "link_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count() << '\n';
        if (trace == JNI_TRUE) {
            // Trace only as far as the first system call, without fabricating
            // its result or continuing with uninitialized system services.
            auto cpu = init_cpu(false, 0, 0, mem);
            const auto stack = alloc(mem, 256 * 1024, "shell-research-stack");
            for (const auto &[id, module] : kernel.loaded_modules) {
                const auto &info = module->info;
                if (!info.start_entry || (std::string_view(info.module_name) != "ScePaf"
                    && std::string_view(info.module_name) != "SceShell"))
                    continue;
                CPUContext context{};
                load_context(*cpu, context);
                write_sp(*cpu, stack + 256 * 1024);
                write_pc(*cpu, info.start_entry.address());
                cpu->svc_called = false;
                constexpr size_t instruction_budget = 10000;
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                size_t instructions = 0;
                while (instructions < instruction_budget && std::chrono::steady_clock::now() < deadline) {
                    ++instructions;
                    if (step(*cpu) < 0 || cpu->svc_called || read_pc(*cpu) == 0)
                        break;
                }
                report << "trace " << info.module_name << " instructions=" << instructions;
                if (cpu->svc_called) {
                    const auto nid = *Ptr<uint32_t>(read_pc(*cpu) + 4).get(mem);
                    report << " first_import=0x" << std::hex << nid << std::dec
                           << " " << import_name(nid);
                    for (size_t reg = 0; reg < 4; ++reg)
                        report << " r" << reg << "=0x" << std::hex << read_reg(*cpu, reg) << std::dec;
                } else {
                    report << " stopped_pc=0x" << std::hex << read_pc(*cpu) << std::dec;
                }
                report << '\n';
            }
        }
    }
    deinit_mem(mem);
    return env->NewStringUTF(report.str().c_str());
}
#endif
