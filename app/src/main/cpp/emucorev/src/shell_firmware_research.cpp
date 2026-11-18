// Opt-in firmware research for instrumentation. Never linked into release builds.
#ifndef NDEBUG
#include <jni.h>

#include <android_state.h>
#include <app/session_controller.h>
#include <app/functions.h>
#include <config/state.h>
#include <emuenv/state.h>
#include <io/state.h>
#include <packages/sfo.h>
#include <regmgr/functions.h>
#include <modules/SceSysmem/SceSysmemForDriver.h>
#include <modules/SceKernelThreadMgr/SceThreadmgr.h>
#include <modules/SceSblUpdateMgr/SceSblSsUpdateMgr.h>
#include <packages/functions.h>
#include <native_library_runtime.h>
#include <util/log.h>
#include <cpu/functions.h>
#include <cpu/state.h>
#include <kernel/load_self.h>
#include <kernel/state.h>
#include <mem/functions.h>
#include <mem/state.h>
#include <nids/functions.h>
#include <packages/sce_types.h>

#include <chrono>
#include <cstring>
#include <sstream>
#include <string_view>
#include <vector>
#include <atomic>
#include <renderer/state.h>
#include <SDL3/SDL_system.h>

namespace {

std::vector<uint8_t> read_bytes(JNIEnv *env, jbyteArray input) {
    std::vector<uint8_t> bytes(env->GetArrayLength(input));
    env->GetByteArrayRegion(input, 0, bytes.size(), reinterpret_cast<jbyte *>(bytes.data()));
    return bytes;
}

std::string read_string(JNIEnv *env, jstring input) {
    const char *text = env->GetStringUTFChars(input, nullptr);
    const std::string result(text);
    env->ReleaseStringUTFChars(input, text);
    return result;
}
}

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_ShellFirmwareResearchTest_requestCatalogImport(JNIEnv *, jobject) {
    request_native_library_catalog_import();
}

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_ShellFirmwareResearchTest_installShellProbes(JNIEnv *env, jobject, jintArray addresses) {
    auto *emuenv = get_emuenv();
    if (!emuenv || emuenv->io.app_path != "__VITA_NATIVE_LIBRARY__") return;
    const auto count = env->GetArrayLength(addresses);
    if (count < 1 || count > 256) return;
    std::vector<jint> values(count);
    env->GetIntArrayRegion(addresses, 0, count, values.data());
    for (const auto value : values) {
        const auto address = static_cast<Address>(value) & ~1u;
        if (address >= 0x81000000 && address < 0x81542130 && Ptr<uint32_t>(address).valid(emuenv->mem))
            emuenv->kernel.debugger.add_probe(emuenv->mem, address | 1u);
    }
}


extern "C" JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_ShellFirmwareResearchTest_prepareBoot(JNIEnv *env, jobject, jstring sandbox, jstring gpu_probe) {
    const auto probe = read_string(env, gpu_probe);
    if (!probe.empty() && probe != "no-mapping" && probe != "no-async-pipelines" && probe != "accurate-sync" && probe != "dump-textures" && probe != "no-fetch") return JNI_FALSE;
    return configure_native_library(read_string(env, sandbox), probe) ? JNI_TRUE : JNI_FALSE;
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

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_ShellFirmwareResearchTest_verifyKernelCopy(JNIEnv *, jobject) {
    if (const auto *session = get_app_session_controller(); session && session->has_active_session())
        return JNI_FALSE;
    bool valid = false;
    {
        EmuEnvState emuenv;
        if (!init(emuenv.mem, false))
            return JNI_FALSE;
        const auto source = Ptr<uint8_t>(alloc(emuenv.mem, 4096, "copy-test-source"));
        const auto target = Ptr<uint8_t>(alloc(emuenv.mem, 4096, "copy-test-target"));
        std::memset(source.get(emuenv.mem), 0x35, 4096);
        std::memset(target.get(emuenv.mem), 0xA7, 4096);
        const auto copy = [&](Ptr<void> dst, Ptr<const void> src, SceSize size) {
            return export_ksceKernelMemcpyKernelToUser(emuenv, 0, "ksceKernelMemcpyKernelToUser", dst, src, size);
        };
        valid = copy(target + 3, source + 1, 37) == 0
            && target.get(emuenv.mem)[2] == 0xA7
            && target.get(emuenv.mem)[40] == 0xA7
            && std::memcmp((target + 3).get(emuenv.mem), (source + 1).get(emuenv.mem), 37) == 0
            && copy({}, {}, 0) == 0
            && copy({}, source, 1) == SCE_KERNEL_ERROR_ILLEGAL_ADDR
            && copy(target, {}, 1) == SCE_KERNEL_ERROR_ILLEGAL_ADDR
            && copy(Ptr<void>(0xFFFFFFF0), source, 32) == SCE_KERNEL_ERROR_ILLEGAL_ADDR
            && copy(target, Ptr<const void>(0xFFFFFFF0), 32) == SCE_KERNEL_ERROR_ILLEGAL_ADDR
            && copy(Ptr<void>(0xFFFFF000), source, 1) == SCE_KERNEL_ERROR_ILLEGAL_ADDR;
        deinit_mem(emuenv.mem);
    }
    return valid ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_ShellFirmwareResearchTest_verifySystemThreads(JNIEnv *, jobject) {
    if (const auto *session = get_app_session_controller(); session && session->has_active_session())
        return JNI_FALSE;
    EmuEnvState emuenv;
    if (!init(emuenv.mem, false))
        return JNI_FALSE;
    emuenv.kernel.init(emuenv.mem, [](CPUState &, uint32_t, SceUID) {}, false);
    SceKernelCreateThread_opt options{
        .size = sizeof(SceKernelCreateThread_opt),
        .stack_size = 64 * 1024,
        .cpu_affinity_mask = 0xF0000,
    };
    int priority = 160;
    const auto create = [&]() {
        return export_sceKernelCreateThreadForUser(emuenv, 0, "sceKernelCreateThreadForUser", "system-affinity-probe",
            SceKernelThreadEntry(emuenv.kernel.halt_instruction_pc), priority, &options);
    };
    bool valid = create() == SCE_KERNEL_ERROR_INVALID_CPU_AFFINITY;
    emuenv.kernel.is_system_process = true;
    const auto id = create();
    valid = valid && id > 0 && emuenv.kernel.get_thread(id)->affinity_mask == 0xF0000;
    priority = 229; // Native SceGameGcPromoteJobQueue's absolute system priority.
    const auto background = create();
    valid = valid && background > 0 && emuenv.kernel.get_thread(background)->priority == 229;
    priority = SCE_KERNEL_DEFAULT_PRIORITY;
    const auto system_default = create();
    valid = valid && system_default > 0 && emuenv.kernel.get_thread(system_default)->priority == 96;
    options.cpu_affinity_mask = 0x100000;
    valid = valid && create() == SCE_KERNEL_ERROR_INVALID_CPU_AFFINITY;
    emuenv.kernel.is_system_process = false;
    options.cpu_affinity_mask = 0;
    const auto game_default = create();
    valid = valid && game_default > 0 && emuenv.kernel.get_thread(game_default)->priority == 160;
    priority = 229;
    valid = valid && create() == SCE_KERNEL_ERROR_ILLEGAL_PRIORITY;
    emuenv.kernel.deinit(emuenv.mem);
    valid = valid && !emuenv.kernel.is_system_process
        && emuenv.kernel.process_cpu_affinity_mask() == SCE_KERNEL_CPU_MASK_USER_ALL;
    deinit_mem(emuenv.mem);
    return valid ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_ShellFirmwareResearchTest_verifyPackageInfo(JNIEnv *env, jobject, jstring root) {
    if (const auto *session = get_app_session_controller(); session && session->has_active_session())
        return JNI_FALSE;
    EmuEnvState emuenv;
    emuenv.vita_fs_path = read_string(env, root);
    if (!init(emuenv.mem, false))
        return JNI_FALSE;
    const auto info = Ptr<SceSblUsSpkgInfo>(alloc(emuenv.mem, 64, "package-info-probe"));
    std::memset(info.get(emuenv.mem), 0xA5, 64);
    const auto path = firmware::package_version_path(emuenv.vita_fs_path, 23);
    fs::create_directories(path.parent_path());
    { fs::ofstream output(path); output << "1234567\n"; }
    const auto query = [&](int type, Ptr<SceSblUsSpkgInfo> output) {
        return export_sceSblUsGetSpkgInfo(emuenv, 0, "sceSblUsGetSpkgInfo", type, output);
    };
    bool valid = query(23, info) == 0 && info.get(emuenv.mem)->size == 16
        && info.get(emuenv.mem)->version == 0x1234567 && info.get(emuenv.mem)->reserved == 0;
    for (const auto byte : info.get(emuenv.mem)->status)
        valid = valid && byte == 0;
    valid = valid && info.cast<uint8_t>().get(emuenv.mem)[16] == 0xA5;
    valid = valid && query(23, {}) == SCE_KERNEL_ERROR_ILLEGAL_ADDR
        && query(23, Ptr<SceSblUsSpkgInfo>(1)) == SCE_KERNEL_ERROR_ILLEGAL_ADDR
        && query(23, Ptr<SceSblUsSpkgInfo>(0xFFFFFFF8)) == SCE_KERNEL_ERROR_ILLEGAL_ADDR
        && query(0, info) == SCE_KERNEL_ERROR_INVALID_ARGUMENT;
    { fs::ofstream output(path); output << "not-a-version\n"; }
    valid = valid && query(23, info) < 0;
    { fs::ofstream output(path); output << "1234junk\n"; }
    valid = valid && query(23, info) < 0;
    fs::remove(path);
    valid = valid && query(23, info) < 0;
    deinit_mem(emuenv.mem);
    return valid ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_sbro_emucorev_ShellFirmwareResearchTest_installPackageFixture(JNIEnv *env, jobject, jstring pup, jstring root) {
    if (const auto *session = get_app_session_controller(); session && session->has_active_session())
        return env->NewStringUTF("");
    try {
        const fs::path target(read_string(env, root));
        fs::create_directories(target);
        const auto version = install_pup(target, fs::path(read_string(env, pup)));
        return env->NewStringUTF(version.c_str());
    } catch (const std::exception &error) {
        LOG_ERROR("Private firmware install probe failed: {}", error.what());
        return env->NewStringUTF("");
    }
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
