#pragma once

#include <pugixml.hpp>
#include <string>
#include <string_view>

namespace config::game_database {

inline std::string normalized(std::string_view value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return {};
    value = value.substr(start, value.find_last_not_of(" \t\r\n") - start + 1);
    std::string result(value);
    for (auto &c : result)
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    return result;
}

inline bool contains_word(std::string_view title, std::string_view word) {
    if (word.empty()) return false;
    const auto letter = [](char c) { return c >= 'A' && c <= 'Z'; };
    for (size_t pos = title.find(word); pos != std::string_view::npos; pos = title.find(word, pos + 1)) {
        const size_t end = pos + word.size();
        if ((pos == 0 || !letter(title[pos - 1])) && (end == title.size() || !letter(title[end]))) return true;
    }
    return false;
}

inline bool valid_gpu(pugi::xml_node gpu) {
    const std::string_view renderer = gpu.attribute("backend-renderer").value();
    const std::string_view mapping = gpu.attribute("memory-mapping").value();
    const std::string_view angle = gpu.attribute("use-angle").value();
    return (renderer == "Vulkan" || renderer == "OpenGL") && (angle == "true" || angle == "false")
        && (mapping == "disabled" || mapping == "double-buffer" || mapping == "external-host"
            || mapping == "page-table" || mapping == "native-buffer");
}

inline pugi::xml_node recommendation(const pugi::xml_document &db, std::string_view title_id, std::string_view title = {}) {
    const auto id = normalized(title_id);
    const auto root = db.child("game-db");
    if (id.empty() || std::string_view(root.attribute("version").value()) != "1") return {};
    for (auto profile : root.children("profile")) {
        if (!valid_gpu(profile.child("gpu"))) continue;
        for (auto candidate : profile.children("title-id"))
            if (normalized(candidate.text().as_string()) == id) return profile.child("gpu");
    }
    const auto name = normalized(title);
    for (auto profile : root.children("profile"))
        if (valid_gpu(profile.child("gpu")) && contains_word(name, normalized(profile.attribute("title-word").value())))
            return profile.child("gpu");
    return {};
}

// Reused for user XML AFTER recommendations. Missing attributes inherit;
// explicit values (including disabled) always win.
template <typename Config>
inline void apply_gpu_settings(Config &out, pugi::xml_node gpu) {
    if (auto renderer = gpu.attribute("backend-renderer")) out.backend_renderer = renderer.value();
    if (auto mapping = gpu.attribute("memory-mapping")) out.memory_mapping = mapping.value();
}

} // namespace config::game_database
