#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace config::game_compatibility {

// FIFA Football/Soccer, 13, 14, 15 and demos: regional IDs reported in
// https://github.com/Vita3K/compatibility/issues?q=FIFA
// Keep in sync with FifaCompatibilityPolicy.kt (checked by a unit test).
inline constexpr std::array fifa_title_ids = {
    "PCSB00051", "PCSB00052", "PCSG00039",
    "PCSB00082", "PCSB00083", "PCSB00084", "PCSB00085", "PCSB00086",
    "PCSE00055", "PCSE00059", "PCSE00060", "PCSG90012",
    "PCSB00170", "PCSB00171", "PCSB00174", "PCSE00093", "PCSE00096", "PCSG00107",
    "PCSB00339", "PCSB00340", "PCSE00263", "PCSG00201",
    "PCSB00603", "PCSB00604", "PCSB00605", "PCSB00606", "PCSB00607",
    "PCSE00481", "PCSE00482", "PCSE00483", "PCSG00404"
};

inline bool is_fifa(std::string_view title_id, std::string_view title = {}) {
    auto upper = [](std::string_view value) {
        std::string result(value);
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return result;
    };
    const auto id = upper(title_id);
    if (std::find(fifa_title_ids.begin(), fifa_title_ids.end(), id) != fifa_title_ids.end())
        return true;

    // Cover unlisted regional IDs and FIFA mods, without matching an unrelated
    // word containing "fifa". Digits may immediately follow (e.g. FIFA15).
    const auto name = upper(title);
    auto is_letter = [](char c) { return c >= 'A' && c <= 'Z'; };
    for (size_t pos = name.find("FIFA"); pos != std::string::npos; pos = name.find("FIFA", pos + 4)) {
        if ((pos == 0 || !is_letter(name[pos - 1]))
            && (pos + 4 == name.size() || !is_letter(name[pos + 4])))
            return true;
    }
    return false;
}

} // namespace config::game_compatibility
