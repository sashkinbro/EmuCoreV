// EmuCoreV adapter layer (Layer 2).
//
// JNI bridge for the Vita3K cheat module (FinalCheat/VitaCheat *.psv databases).
// The Kotlin UI lists, toggles, imports and persists cheats through it while
// the emulator core keeps owning the actual code interpreter. The functions are
// title aware so the cheat manager can work on a game that is not running.

#include "interface.h"

#include <android_state.h>
#include <app/session_controller.h>
#include <cheat/cheat.h>
#include <cheat/functions.h>
#include <emuenv/state.h>
#include <io/state.h>
#include <kernel/state.h>
#include <util/fs.h>
#include <util/log.h>

#include <jni.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>

namespace {

std::string json_escape(const std::string &value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char c : value) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buffer[7];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(c));
                out += buffer;
            } else {
                out += c;
            }
        }
    }
    return out;
}

std::string bool_literal(bool value) {
    return value ? "true" : "false";
}

std::string cheats_to_json(const cheat::CheatFile &file, bool master_enabled) {
    std::string json = "{";
    json += "\"titleId\":\"" + json_escape(file.title_id) + "\",";
    json += "\"header\":\"" + json_escape(file.header) + "\",";
    json += "\"path\":\"" + json_escape(file.path.empty() ? std::string() : fs_utils::path_to_utf8(file.path)) + "\",";
    json += "\"masterEnabled\":" + bool_literal(master_enabled) + ",";
    json += "\"cheats\":[";

    for (size_t i = 0; i < file.cheats.size(); ++i) {
        const cheat::Cheat &cheat = file.cheats[i];
        if (i != 0)
            json += ',';

        std::string codes;
        for (const cheat::CodeLine &line : cheat.lines)
            codes += fmt::format("${:04X} {:08X} {:08X}\n", line.control, line.first, line.second);

        json += "{";
        json += "\"name\":\"" + json_escape(cheat.name) + "\",";
        json += "\"enabled\":" + bool_literal(cheat.enabled) + ",";
        json += "\"enabledOnBoot\":" + bool_literal(cheat.enabled_on_boot) + ",";
        json += "\"broken\":" + bool_literal(cheat.broken) + ",";
        json += "\"codes\":\"" + json_escape(codes) + "\"";
        json += "}";
    }

    json += "]}";
    return json;
}

cheat::JitInvalidate make_invalidate(EmuEnvState &emuenv) {
    return [&emuenv](uint32_t address, size_t size) {
        emuenv.kernel.invalidate_jit_cache(address, size);
    };
}

EmuEnvState *running_emuenv() {
    auto *controller = get_app_session_controller();
    auto *emuenv = get_emuenv();
    if (!controller || !emuenv || !controller->is_running())
        return nullptr;

    return emuenv;
}

bool is_live_title(EmuEnvState *emuenv, const std::string &title_id) {
    return (emuenv != nullptr) && !title_id.empty() && (emuenv->io.title_id == title_id);
}

fs::path cheats_directory(const EmuEnvState &emuenv) {
    if (!emuenv.cheat_path.empty())
        return emuenv.cheat_path;

    return emuenv.shared_path / "cheats" / "";
}

cheat::CheatFile load_cheat_file(EmuEnvState &emuenv, const std::string &title_id) {
    cheat::CheatFile file = cheat::parse_cheat_file(cheat::find_cheat_file(cheats_directory(emuenv), title_id), title_id);
    // `_V1` means the cheat is on; the parser only records that in enabled_on_boot.
    for (cheat::Cheat &cheat : file.cheats)
        cheat.enabled = cheat.enabled_on_boot;

    return file;
}

jstring to_jstring(JNIEnv *env, const std::string &value) {
    return env->NewStringUTF(value.c_str());
}

std::string lowercase_extension(const std::string &filename) {
    const size_t dot = filename.find_last_of('.');
    if (dot == std::string::npos)
        return {};

    std::string extension = filename.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_sbro_emucorev_core_CheatBridge_getCheats(JNIEnv *env, jobject /*thiz*/, jstring title_id) {
    auto *emuenv = get_emuenv();
    if (!emuenv || !title_id)
        return to_jstring(env, "{}");

    const std::string title = jstring_to_string(env, title_id);
    if (is_live_title(emuenv, title))
        return to_jstring(env, cheats_to_json(cheat::snapshot(emuenv->cheat), emuenv->cfg.enable_cheats));

    return to_jstring(env, cheats_to_json(load_cheat_file(*emuenv, title), emuenv->cfg.enable_cheats));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_core_CheatBridge_setCheatEnabled(
    JNIEnv *env,
    jobject /*thiz*/,
    jstring title_id,
    jint index,
    jboolean enabled) {
    auto *emuenv = get_emuenv();
    if (!emuenv || !title_id || index < 0)
        return JNI_FALSE;

    const std::string title = jstring_to_string(env, title_id);
    if (is_live_title(emuenv, title)) {
        cheat::set_cheat_enabled(emuenv->cheat, static_cast<size_t>(index), enabled == JNI_TRUE, emuenv->mem, make_invalidate(*emuenv));
        return JNI_TRUE;
    }

    cheat::CheatFile file = load_cheat_file(*emuenv, title);
    if (static_cast<size_t>(index) >= file.cheats.size())
        return JNI_FALSE;

    file.cheats[static_cast<size_t>(index)].enabled = enabled == JNI_TRUE;
    return cheat::save_cheat_file(file) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_core_CheatBridge_setAllCheatsEnabled(
    JNIEnv *env,
    jobject /*thiz*/,
    jstring title_id,
    jboolean enabled) {
    auto *emuenv = get_emuenv();
    if (!emuenv || !title_id)
        return JNI_FALSE;

    const std::string title = jstring_to_string(env, title_id);
    if (is_live_title(emuenv, title)) {
        cheat::set_all_cheats_enabled(emuenv->cheat, enabled == JNI_TRUE, emuenv->mem, make_invalidate(*emuenv));
        return JNI_TRUE;
    }

    cheat::CheatFile file = load_cheat_file(*emuenv, title);
    if (file.cheats.empty())
        return JNI_FALSE;

    for (cheat::Cheat &cheat : file.cheats)
        cheat.enabled = enabled == JNI_TRUE;

    return cheat::save_cheat_file(file) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_core_CheatBridge_setCheatsEnabled(JNIEnv * /*env*/, jobject /*thiz*/, jboolean enabled) {
    auto *emuenv = get_emuenv();
    if (!emuenv)
        return JNI_FALSE;

    emuenv->cfg.enable_cheats = enabled == JNI_TRUE;
    cheat::set_enabled(emuenv->cheat, enabled == JNI_TRUE, emuenv->mem, make_invalidate(*emuenv));
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_core_CheatBridge_saveCheats(JNIEnv *env, jobject /*thiz*/, jstring title_id) {
    auto *emuenv = get_emuenv();
    if (!emuenv || !title_id)
        return JNI_FALSE;

    const std::string title = jstring_to_string(env, title_id);
    if (is_live_title(emuenv, title))
        return cheat::save(emuenv->cheat) ? JNI_TRUE : JNI_FALSE;

    cheat::CheatFile file = load_cheat_file(*emuenv, title);
    return cheat::save_cheat_file(file) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_sbro_emucorev_core_CheatBridge_reloadCheats(JNIEnv *env, jobject /*thiz*/, jstring title_id) {
    auto *emuenv = get_emuenv();
    if (!emuenv || !title_id)
        return to_jstring(env, "{}");

    const std::string title = jstring_to_string(env, title_id);
    if (is_live_title(emuenv, title))
        cheat::reload(emuenv->cheat, emuenv->cheat_path, title, emuenv->mem, make_invalidate(*emuenv));

    return to_jstring(env, cheats_to_json(is_live_title(emuenv, title) ? cheat::snapshot(emuenv->cheat) : load_cheat_file(*emuenv, title), emuenv->cfg.enable_cheats));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_sbro_emucorev_core_CheatBridge_getCheatFilePath(JNIEnv *env, jobject /*thiz*/, jstring title_id) {
    auto *emuenv = get_emuenv();
    if (!emuenv || !title_id)
        return to_jstring(env, "");

    const std::string title = jstring_to_string(env, title_id);
    if (is_live_title(emuenv, title)) {
        const cheat::CheatFile file = cheat::snapshot(emuenv->cheat);
        return to_jstring(env, file.path.empty() ? std::string() : fs_utils::path_to_utf8(file.path));
    }

    const fs::path path = cheat::find_cheat_file(cheats_directory(*emuenv), title);
    return to_jstring(env, path.empty() ? std::string() : fs_utils::path_to_utf8(path));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_sbro_emucorev_core_CheatBridge_importCheatFile(
    JNIEnv *env,
    jobject /*thiz*/,
    jstring title_id,
    jstring source_path,
    jstring display_name) {
    auto *emuenv = get_emuenv();
    if (!emuenv || !title_id || !source_path)
        return to_jstring(env, "{}");

    const std::string title = jstring_to_string(env, title_id);
    const fs::path source = fs_utils::utf8_to_path(jstring_to_string(env, source_path));
    if (!fs::is_regular_file(source)) {
        LOG_ERROR("Cheat import source {} is not a file", source);
        return to_jstring(env, "{}");
    }

    const std::string name = display_name
        ? jstring_to_string(env, display_name)
        : fs_utils::path_to_utf8(source.filename());

    std::string extension = lowercase_extension(name);
    const fs::path cheats_dir = cheats_directory(*emuenv);
    fs::path target;
    if (extension == ".db") {
        target = cheats_dir / "cheat.db";
    } else {
        if (extension != ".psv" && extension != ".txt")
            extension = ".psv";
        target = cheats_dir / (title + extension);
    }

    try {
        fs::create_directories(cheats_dir);
        // boost::copy_file uses copy_file_range, which FUSE-based emulated
        // storage rejects with EPERM; plain streams always work.
        std::ifstream input(source.string(), std::ios::binary);
        std::ofstream output(target.string(), std::ios::binary | std::ios::trunc);
        if (!input.is_open() || !output.is_open())
            throw std::runtime_error("failed to open the import streams");

        output << input.rdbuf();
        if (!output.good())
            throw std::runtime_error("failed to write the cheat file");
    } catch (const std::exception &e) {
        LOG_ERROR("Failed to import cheat file {}: {}", source, e.what());
        return to_jstring(env, "{}");
    }

    LOG_INFO("Imported cheat file {} as {}", source, target);
    if (is_live_title(emuenv, title))
        cheat::reload(emuenv->cheat, cheats_dir, title, emuenv->mem, make_invalidate(*emuenv));

    return to_jstring(env, cheats_to_json(is_live_title(emuenv, title) ? cheat::snapshot(emuenv->cheat) : load_cheat_file(*emuenv, title), emuenv->cfg.enable_cheats));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_core_CheatBridge_deleteCheats(JNIEnv *env, jobject /*thiz*/, jstring title_id) {
    auto *emuenv = get_emuenv();
    if (!emuenv || !title_id)
        return JNI_FALSE;

    const std::string title = jstring_to_string(env, title_id);
    const fs::path path = cheat::find_cheat_file(cheats_directory(*emuenv), title);
    if (path.empty())
        return JNI_FALSE;

    // A combined database holds the cheats of many games, only files named
    // after this title can be removed safely.
    const std::string filename = fs_utils::path_to_utf8(path.filename());
    if (filename.size() < title.size() || !std::equal(title.begin(), title.end(), filename.begin(), [](char a, char b) {
            return std::toupper(static_cast<unsigned char>(a)) == std::toupper(static_cast<unsigned char>(b));
        }))
        return JNI_FALSE;

    try {
        fs::remove(path);
    } catch (const std::exception &e) {
        LOG_ERROR("Failed to delete cheat file {}: {}", path, e.what());
        return JNI_FALSE;
    }

    LOG_INFO("Deleted cheat file {}", path);
    if (is_live_title(emuenv, title))
        cheat::unload(emuenv->cheat);

    return JNI_TRUE;
}
