#pragma once
// Persistable part of the shell session: what the host stores so a recreated shell (after the
// process was killed while a game ran) comes back unlocked on the same Home focus and open
// LiveAreas instead of on the lock screen. Pure data, no Android or GL dependencies.

#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace shell_session {

inline constexpr int kMaxTabs = 6;        // livearea_sheet.h kLiveAreaMaxOpenTabs
inline constexpr std::size_t kMaxTitle = 64;

struct State {
    bool unlocked = false;
    int focus_page = 0, focus_slot = 0;
    int active = -1;                      // -1 Home, otherwise index into tabs
    std::vector<std::string> tabs;        // open LiveArea title ids, newest first

    bool operator==(const State&) const = default;
};

// "v1" header, then "key value..." lines. Unknown keys are ignored (forward compatible).
inline std::string serialize(const State& state) {
    std::string text = "v1\n";
    text += state.unlocked ? "unlocked 1\n" : "unlocked 0\n";
    text += "focus " + std::to_string(state.focus_page) + ' ' + std::to_string(state.focus_slot) + '\n';
    text += "active " + std::to_string(state.active) + '\n';
    for (const auto& tab : state.tabs) text += "tab " + tab + '\n';
    return text;
}

namespace detail {
inline bool read_int(std::string_view text, int lo, int hi, int& out) {
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < lo || value > hi) return false;
    out = value;
    return true;
}
}

inline std::optional<State> parse(std::string_view text) {
    State state;
    bool header = false;
    while (!text.empty()) {
        const auto end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!header) {
            if (line != "v1") return std::nullopt;
            header = true;
            continue;
        }
        const auto space = line.find(' ');
        if (space == std::string_view::npos) continue;
        const auto key = line.substr(0, space);
        const auto value = line.substr(space + 1);
        if (key == "unlocked") {
            state.unlocked = value == "1";
        } else if (key == "focus") {
            const auto split = value.find(' ');
            if (split == std::string_view::npos
                || !detail::read_int(value.substr(0, split), 0, 4095, state.focus_page)
                || !detail::read_int(value.substr(split + 1), 0, 4095, state.focus_slot)) return std::nullopt;
        } else if (key == "active") {
            if (!detail::read_int(value, -1, kMaxTabs - 1, state.active)) return std::nullopt;
        } else if (key == "tab") {
            if (value.empty() || value.size() > kMaxTitle || value.find(' ') != std::string_view::npos
                || static_cast<int>(state.tabs.size()) >= kMaxTabs) return std::nullopt;
            state.tabs.emplace_back(value);
        }
    }
    if (!header) return std::nullopt;
    if (state.active >= static_cast<int>(state.tabs.size())) return std::nullopt;
    return state;
}

} // namespace shell_session
