// Native firmware library runtime, isolated from normal game sessions.
#include <jni.h>
#include <android_state.h>
#include <app/session_controller.h>
#include <app/functions.h>
#include <config/state.h>
#include <display/state.h>
#include <cpu/functions.h>
#include <emuenv/state.h>
#include <io/state.h>
#include <regmgr/functions.h>
#include <packages/sfo.h>
#include <native_library_runtime.h>
#include <util/log.h>
#include <kernel/state.h>
#include <mem/functions.h>
#include <renderer/state.h>
#include <SDL3/SDL_system.h>
#include <atomic>
#include <cstring>
#include <string_view>
#include <array>
#include <deque>
#if ECV_ORACLE
#include <ecv_oracle.h>
#endif
#include <mutex>

namespace {
fs::path shell_boot_sandbox;
std::string shell_gpu_probe;
int shell_refresh_hz = 60;
std::atomic_bool shell_catalog_requested{false};
std::atomic_bool shell_pacing_requested{true};
std::mutex shell_events_mutex;
std::deque<std::array<uint32_t, 16>> shell_events;
std::string launched_title;
bool host_game_running = false;
bool host_returned = false;
bool host_app_activated = false;

// Called with shell_events_mutex held. Either Android return or native
// activation may happen first; emit one exit only after both are observed.
void finish_returned_host_game() {
    if (!host_returned || !host_app_activated || !host_game_running) return;
    host_returned = false;
    host_app_activated = false;
    std::array<uint32_t, 16> event{};
    event[0] = 4;
    event[5] = 1; // app ID, independently of the process ID
    event[6] = 0xffffffffu; // No replacement application after exit.
    std::memcpy(reinterpret_cast<char *>(event.data()) + 0x20, launched_title.c_str(),
        std::min<size_t>(launched_title.size(), 31));
    shell_events.push_back(event);
}
std::string read_string(JNIEnv *env, jstring input) {
    const char *value = env->GetStringUTFChars(input, nullptr);
    if (!value) return {};
    std::string result(value);
    env->ReleaseStringUTFChars(input, value);
    return result;
}
}
int native_library_app_id(const char *name, int *process_id) {
    const std::lock_guard lock(shell_events_mutex);
    if (!name || !process_id) return static_cast<int>(0x80802016u);
    if (!host_game_running || launched_title != name) return static_cast<int>(0x80802012u);
    *process_id = 1;
    return 0;
}
int native_library_app_name(int process_id, char *name) {
    const std::lock_guard lock(shell_events_mutex);
    if (!name) return static_cast<int>(0x80802016u);
    if (process_id != 1 || launched_title.empty()) return static_cast<int>(0x80802012u);
    std::memset(name, 0, 32);
    std::memcpy(name, launched_title.data(), std::min<size_t>(launched_title.size(), 31));
    return 0;
}
int native_library_app_status(int app_id, void *output) {
    if (!output) return static_cast<int>(0x80020005u);
    const std::lock_guard lock(shell_events_mutex);
    if (app_id != 1 || launched_title.empty()) return static_cast<int>(0x80802012u);
    // Complete SceAppMgrAppStatus ABI (VitaSDK, 0x80 bytes, no size field).
    std::array<uint32_t, 32> status{};
    status[0] = 0xffffffffu;
    std::memcpy(reinterpret_cast<char *>(status.data()) + 0xc, launched_title.c_str(),
        std::min<size_t>(launched_title.size(), 31));
    status[12] = 1; // app_id
    status[13] = 1; // process_id in this host's application namespace
    status[14] = host_game_running;
    status[15] = host_game_running;
    status[17] = 0xffffffffu; // no parent application
    reinterpret_cast<uint8_t *>(status.data())[0x71] = launched_title == "NPXS10008" ? 2 : 1;
    std::memcpy(output, status.data(), sizeof(status));
    return 0;
}
int native_library_shell_event_count() {
    const std::lock_guard lock(shell_events_mutex);
    return static_cast<int>(shell_events.size());
}
int native_library_app_path(int app_id, char *path) {
    const std::lock_guard lock(shell_events_mutex);
    if (!path) return static_cast<int>(0x80802016u);
    if (app_id != 1 || launched_title.empty()) return static_cast<int>(0x80802012u);
    const auto value = std::string(launched_title == "NPXS10008" ? "vs0:app/" : "ux0:app/") + launched_title;
    std::memcpy(path, value.c_str(), value.size() + 1);
    return 0;
}
int receive_native_library_shell_event(void *output) {
    if (!output) return static_cast<int>(0x80020005u);
    const std::lock_guard lock(shell_events_mutex);
    if (shell_events.empty()) return static_cast<int>(0x80802012u);
    std::memcpy(output, shell_events.front().data(), sizeof(shell_events.front()));
    if (shell_events.front()[0] == 4) host_game_running = false;
    shell_events.pop_front();
    return 0;
}
extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_core_vita_Emulator_notifyNativeLibraryGameStopped(JNIEnv *, jobject) {
    const std::lock_guard lock(shell_events_mutex);
    host_returned = true;
    finish_returned_host_game();
}
int activate_native_library_app(int app_id) {
    const std::lock_guard lock(shell_events_mutex);
    if (app_id != 1 || !host_game_running) return static_cast<int>(0x80802012u);
    host_app_activated = true;
    finish_returned_host_game();
    return 0;
}
void request_native_library_catalog_import() { shell_catalog_requested.store(true); }
bool configure_native_library(const std::string &path, const std::string &probe) {
    const auto *session = get_app_session_controller();
    if (!session || session->has_active_session()) return false;
#ifdef NDEBUG
    if (!probe.empty()) return false;
#endif
    shell_boot_sandbox = fs::path(path);
    shell_gpu_probe = probe;
    shell_pacing_requested.store(true);
    {
        const std::lock_guard lock(shell_events_mutex);
        shell_events.clear();
        launched_title.clear();
        host_game_running = host_returned = host_app_activated = false;
    }
    return shell_boot_sandbox.is_absolute();
}

void poll_native_library_catalog(EmuEnvState &emuenv, ThreadState &thread) {
    const auto *session = get_app_session_controller();
    if (!session || !session->is_running()) return;
    if (emuenv.io.app_path != "__VITA_NATIVE_LIBRARY__")
        return;
    if (shell_pacing_requested.load()) {
        Address init_tick = 0;
        {
            const std::lock_guard lock(emuenv.kernel.export_nids_mutex);
            const auto entry = emuenv.kernel.export_nids.find(0x25B066A8); // paf::Timer::InitTickUnit(float milliseconds)
            if (entry != emuenv.kernel.export_nids.end()) init_tick = entry->second;
        }
        if (init_tick && thread.cpu) {
            const float previous = read_float_reg(*thread.cpu, 0);
            write_float_reg(*thread.cpu, 0, 1000.0f / shell_refresh_hz);
            thread.run_callback(init_tick, {});
            write_float_reg(*thread.cpu, 0, previous);
            shell_pacing_requested.store(false);
        }
    }
#if ECV_ORACLE
    if (!shell_pacing_requested.load())
        ecv::oracle::on_native_library_poll(emuenv, thread);
#endif
    if (!shell_catalog_requested.load()) return;
    auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return;
    const auto cls = env->GetObjectClass(activity);
    const auto ready = cls ? env->GetMethodID(cls, "onNativeLibraryCatalogReady", "()Z") : nullptr;
    const bool catalog_ready = ready && !env->ExceptionCheck()
        && env->CallBooleanMethod(activity, ready) == JNI_TRUE && !env->ExceptionCheck();
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (cls) env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    if (!catalog_ready) return;
    Address callback = 0;
    {
        const std::lock_guard lock(emuenv.kernel.export_nids_mutex);
        const auto entry = emuenv.kernel.export_nids.find(0xC875C57A);
        if (entry != emuenv.kernel.export_nids.end())
            callback = entry->second;
    }
    if (!callback) {
        LOG_ERROR("Native LiveArea catalog parser is unavailable");
        return;
    }
    shell_catalog_requested.store(false);
    // C875C57A: native SceLsdb LiveArea import, (PAF string titleId,
    // contents directory). It reads metadata from its own DB and parses XML
    // through native PAF, including frames and firmware fallback resources.
    const Address scratch = alloc(emuenv.mem, 1024, "shell-catalog-import");
    if (!scratch)
        return;
    for (const auto &entry : fs::directory_iterator(shell_boot_sandbox / "ux0/app")) {
        if (!fs::is_directory(entry.path()))
            continue;
        const std::string id = entry.path().filename().string();
        if (id.empty() || id.size() > 64 || id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos)
            continue;
        const auto path = fmt::format("ux0:app/{}/sce_sys/livearea/contents", id);
        auto *words = Ptr<uint32_t>(scratch).get(emuenv.mem);
        words[0] = scratch + 16;
        words[1] = static_cast<uint32_t>(id.size());
        words[2] = 0; // Reserved word in the 12-byte PAF string ABI.
        std::memcpy(Ptr<char>(scratch + 16).get(emuenv.mem), id.c_str(), id.size() + 1);
        std::memcpy(Ptr<char>(scratch + 128).get(emuenv.mem), path.c_str(), path.size() + 1);
        const auto result = thread.run_callback(callback, {scratch, scratch + 128});
        LOG_DEBUG("Native LiveArea import {} returned 0x{:08X}", id, result);
    }
    free(emuenv.mem, scratch);
}

int dispatch_native_library_game_launch(EmuEnvState &emuenv, const char *path) {
    constexpr int not_found = static_cast<int>(0x80802012u);
    if (!path || !emuenv.renderer) return not_found;
    const std::string_view requested(path);
    const std::string_view prefix = requested == "vs0:app/NPXS10008/eboot.bin" ? "vs0:app/" : "ux0:app/";
    constexpr std::string_view suffix = "/eboot.bin";
    if (!requested.starts_with(prefix) || !requested.ends_with(suffix) || requested.size() <= prefix.size() + suffix.size())
        return not_found;
    const auto id = requested.substr(prefix.size(), requested.size() - prefix.size() - suffix.size());
    if (id.size() > 64 || id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") != std::string_view::npos)
        return not_found;
    auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return not_found;
    const auto cls = env->GetObjectClass(activity);
    const auto method = cls ? env->GetMethodID(cls, "onNativeLibraryLaunchRequested", "(Ljava/lang/String;)Z") : nullptr;
    bool accepted = false;
    if (method && !env->ExceptionCheck()) {
        const auto title = env->NewStringUTF(std::string(id).c_str());
        const bool previous = emuenv.renderer->android_menu_paused.exchange(true);
        accepted = env->CallBooleanMethod(activity, method, title) == JNI_TRUE && !env->ExceptionCheck();
        if (!accepted) emuenv.renderer->android_menu_paused.store(previous);
        env->DeleteLocalRef(title);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (cls) env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    // Keep the guest resident; Android pauses it while the separate game
    // process runs through the ordinary launch path.
    if (accepted) {
        const std::lock_guard lock(shell_events_mutex);
        launched_title = id;
        host_game_running = true;
        host_returned = false;
        host_app_activated = false;
        std::array<uint32_t, 16> event{};
        event[0] = 2;
        event[2] = 1;
        shell_events.push_back(event);
    }
    return accepted ? 1 : not_found;
}

bool prepare_native_library(EmuEnvState &emuenv) {
    // Give VSH its own writable filesystem. Firmware is linked read-only by
    // convention, while shell database/registry initialization stays isolated
    // from game data and the normal user's Vita filesystem.
    const auto firmware_root = emuenv.vita_fs_path;
    if (shell_boot_sandbox.empty())
        return false;
    const auto sandbox = shell_boot_sandbox;
    shell_catalog_requested.store(true);
    fs::create_directories(sandbox);
    for (const auto *device : { "vs0", "os0", "sa0" }) {
        const auto source = firmware_root / device;
        const auto target = sandbox / device;
        if (fs::is_directory(target) && !fs::is_symlink(target) && fs::is_empty(target))
            fs::remove(target); // Only remove the empty mount placeholder created by IO initialization.
        if (!fs::exists(target)) {
            boost::system::error_code error;
            fs::create_directory_symlink(source, target, error);
            if (error)
                return false;
        }
    }
    emuenv.config_path = sandbox.parent_path() / "config";
    emuenv.cache_path = sandbox.parent_path() / "cache";
    fs::create_directories(emuenv.config_path);
    fs::create_directories(emuenv.cache_path);
    fs::create_directories(sandbox / "vd0/registry");
    if (!app::switch_emulator_path(emuenv, sandbox))
        return false;
    regmgr::init_regmgr(emuenv.regmgr, sandbox);
    // This host supplies the user's configuration. The private shell registry
    // represents an initialized console and must not launch initialsetup.self.
    // SceShell itself writes 1 to this key after its initialization checks.
    regmgr::set_int_value(emuenv.regmgr, "/CONFIG/SYSTEM/", "initialize", 1);
    app::set_current_config(emuenv, "");
    // The library has its own presentation profile; never persist these values
    // into the normal game's configuration.
    emuenv.cfg.current_config.resolution_multiplier = 2.0f;
    // MSAA 4x for every scene of the shell, clamped by the renderer to the device's supported sample counts (4 -> 2 -> 1).
    emuenv.cfg.current_config.msaa_samples_override = 4;
    emuenv.cfg.current_config.high_accuracy = true;
    emuenv.cfg.current_config.fps_hack = true;
    emuenv.cfg.current_config.v_sync = false;
    emuenv.cfg.current_config.export_textures = false;
    emuenv.cfg.current_config.import_textures = false;
    emuenv.cfg.current_config.log_active_shaders = false;
    emuenv.cfg.current_config.log_uniforms = false;
    emuenv.cfg.current_config.color_surface_debug = false;
    emuenv.cfg.current_config.validation_layer = false;
    // PAF's shared-frame composition spans two display ticks. Keep its
    // animation tick in wall time while presenting at the host's 60/120 Hz.
    emuenv.display.refresh_hz = shell_refresh_hz * 2;
    emuenv.cfg.archive_log = false;
#ifndef NDEBUG
    emuenv.cfg.log_level = spdlog::level::warn;
    emuenv.kernel.debugger.watch_import_calls = false;
    logging::set_level(spdlog::level::warn);
#endif
#ifndef NDEBUG
    // Opt-in A/B probes affect only this private debug session, never user config.
    if (shell_gpu_probe == "no-mapping")
        emuenv.cfg.current_config.memory_mapping = "disabled";
    else if (shell_gpu_probe == "no-async-pipelines")
        emuenv.cfg.current_config.async_pipeline_compilation = false;
    else if (shell_gpu_probe == "accurate-sync")
        emuenv.cfg.current_config.high_accuracy = true;
    else if (shell_gpu_probe == "no-fetch") {
        emuenv.cfg.current_config.high_accuracy = true;
        emuenv.cfg.disable_programmable_blending = true;
    }
    else if (shell_gpu_probe == "dump-textures") {
        emuenv.cfg.current_config.high_accuracy = true;
        emuenv.cfg.current_config.export_textures = true;
        emuenv.cfg.current_config.export_as_png = true;
    }
#endif
    emuenv.cfg.current_config.modules_mode = ModulesMode::AUTO_MANUAL;
    emuenv.cfg.current_config.lle_modules.emplace_back("libpaf");
    emuenv.cfg.current_config.lle_modules.emplace_back("libnet");
    for (const auto *name : { "sqlite", "bXCe", "ini_file_processor", "common_gui_dialog", "dbutil", "activity_db" })
        emuenv.cfg.current_config.lle_modules.emplace_back(name);
#if ECV_ORACLE
    ecv::oracle::on_native_library_prepare(emuenv);
#endif
    emuenv.io.app_path = "__VITA_NATIVE_LIBRARY__";
    emuenv.kernel.is_system_process = true;
    emuenv.io.title_id = "SceShell";
    emuenv.io.content_id.clear();
    emuenv.io.addcont.clear();
    emuenv.io.savedata.clear();
    emuenv.current_app_title = "SceShell";
    emuenv.app_info = {};
    app::reset_perf_metrics(emuenv);
    return fs::is_regular_file(sandbox / "vs0/vsh/shell/shell.self");
}

bool prepare_native_trophies(EmuEnvState &emuenv) {
#ifndef NDEBUG
    logging::set_level(spdlog::level::warn);
#endif
    app::set_current_config(emuenv, "");
    auto &config = emuenv.cfg.current_config;
    config.modules_mode = ModulesMode::AUTO_MANUAL;
    for (const auto *module : {"libpaf", "libnet", "sqlite", "bXCe", "ini_file_processor", "common_gui_dialog", "dbutil", "activity_db", "np_utility", "np_common", "np_activity", "np_trophy"})
        config.lle_modules.emplace_back(module);
    config.resolution_multiplier = 2.0f;
    config.high_accuracy = true;
    config.export_textures = config.import_textures = false;
    emuenv.cfg.archive_log = false;
    emuenv.io.app_path = "__VITA_NATIVE_TROPHIES__";
    emuenv.io.title_id = "NPXS10008";
    emuenv.io.content_id.clear();
    emuenv.io.addcont.clear();
    emuenv.io.savedata.clear();
    emuenv.kernel.is_system_process = true;
    emuenv.current_app_title = "Trophies";
    fs::create_directories(emuenv.vita_fs_path / "ur0/user" / emuenv.io.user_id / "trophy/db");
    {
        const std::lock_guard lock(shell_events_mutex);
        launched_title = "NPXS10008";
        host_game_running = true;
    }
    emuenv.app_info = {};
    app::reset_perf_metrics(emuenv);
    return fs::is_regular_file(emuenv.vita_fs_path / "vs0/app/NPXS10008/eboot.bin");
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_core_vita_Emulator_prepareNativeLibraryRuntime(JNIEnv *env, jobject, jstring sandbox, jint refresh_hz) {
    shell_refresh_hz = refresh_hz >= 120 ? 120 : (refresh_hz >= 90 ? 90 : 60);
    return configure_native_library(read_string(env, sandbox)) ? JNI_TRUE : JNI_FALSE;
}

