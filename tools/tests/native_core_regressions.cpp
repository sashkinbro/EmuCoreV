#include <config/game_compatibility.h>
#include <packages/sfo.h>

#include <cstring>
#include <iostream>
#include <stdexcept>

static int checks = 0;
static void check(bool passed, const char *message) {
    ++checks;
    if (!passed)
        throw std::runtime_error(message);
}

static std::vector<uint8_t> valid_sfo() {
    const std::vector<std::pair<std::string, std::string>> values = {
        { "TITLE_ID", "PCSE00481" }, { "TITLE", "FIFA 15" }, { "APP_VER", "01.00" }
    };
    SfoHeader header{ 0x46535000, 0x101, uint32_t(sizeof(SfoHeader) + values.size() * sizeof(SfoIndexTableEntry)), 0, uint32_t(values.size()) };
    std::vector<SfoIndexTableEntry> indices;
    std::vector<uint8_t> keys, data;
    for (const auto &[key, value] : values) {
        indices.push_back({ uint16_t(keys.size()), SfoDataFormat::UTF8_NULL, uint32_t(value.size() + 1), uint32_t(value.size() + 1), uint32_t(data.size()) });
        keys.insert(keys.end(), key.begin(), key.end());
        keys.push_back(0);
        data.insert(data.end(), value.begin(), value.end());
        data.push_back(0);
    }
    header.data_table_start = header.key_table_start + uint32_t(keys.size());
    std::vector<uint8_t> bytes(header.data_table_start + data.size());
    memcpy(bytes.data(), &header, sizeof(header));
    memcpy(bytes.data() + sizeof(header), indices.data(), indices.size() * sizeof(SfoIndexTableEntry));
    memcpy(bytes.data() + header.key_table_start, keys.data(), keys.size());
    memcpy(bytes.data() + header.data_table_start, data.data(), data.size());
    return bytes;
}

int main() {
    try {
        const auto valid = valid_sfo();
        SfoFile file{};
        sfo::SfoAppInfo info;
        check(sfo::load(file, valid), "Valid PSF magic must be accepted");
        check(sfo::get_param_info(info, valid, 1), "Valid SFO must explicitly return success");
        check(info.app_title == "FIFA 15" && info.app_title_id == "PCSE00481", "SFO title metadata");
        check(info.app_version == "1.00", "SFO version normalization");
        check(!sfo::load(file, {}), "Empty SFO must fail");
        check(!sfo::load(file, std::vector<uint8_t>(19)), "Truncated header must fail");
        auto corrupt = valid;
        uint32_t wrong_magic = 0x46535121;
        memcpy(corrupt.data(), &wrong_magic, sizeof(wrong_magic));
        check(!sfo::load(file, corrupt), "Do not accept the broken upstream SFO magic");
        check(!sfo::get_param_info(info, corrupt, 1), "Invalid SFO must propagate failure");
        for (const size_t header_offset : { size_t(8), size_t(12), size_t(16) }) {
            corrupt = valid;
            uint32_t huge = 0xFFFFFFFF;
            memcpy(corrupt.data() + header_offset, &huge, sizeof(huge));
            check(!sfo::load(file, corrupt), "Out-of-range tables/count must fail before allocation");
        }
        corrupt = valid;
        uint16_t bad_key = 0xFFFF;
        memcpy(corrupt.data() + sizeof(SfoHeader), &bad_key, sizeof(bad_key));
        check(!sfo::load(file, corrupt), "Out-of-range key must fail");
        corrupt = valid;
        uint32_t bad_data = 0xFFFFFFFF;
        memcpy(corrupt.data() + sizeof(SfoHeader) + 12, &bad_data, sizeof(bad_data));
        check(!sfo::load(file, corrupt), "Out-of-range data must fail");
        corrupt = valid;
        uint32_t empty_string = 0;
        memcpy(corrupt.data() + sizeof(SfoHeader) + 4, &empty_string, sizeof(empty_string));
        check(!sfo::load(file, corrupt), "Zero-length null string must fail safely");
        corrupt = valid;
        corrupt.back() = 'X';
        check(!sfo::load(file, corrupt), "Unterminated value must fail");

        using config::game_compatibility::is_fifa;
        for (const auto id : config::game_compatibility::fifa_title_ids)
            check(is_fifa(id, "EA SPORTS FC mod"), "Every known FIFA ID must match renamed mods");
        check(is_fifa("pcse00483"), "Case-insensitive IDs");
        check(is_fifa("UNKNOWN", "EA SPORTS FIFA Football Demo"), "Name fallback for other regions");
        check(is_fifa("UNKNOWN", "fifa15"), "Compact lowercase names");
        check(!is_fifa("PCSA00107", "Killzone"), "Other games must not be overridden");
        check(!is_fifa("", "NotFIFA"), "Do not match an unrelated word");
        check(!is_fifa("", "FIFAworld"), "Do not match an unrelated suffix");
        check(!is_fifa(""), "Global settings must not be overridden");
        std::cout << checks << " native regression checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Native regression failed: " << error.what() << '\n';
        return 1;
    }
}
