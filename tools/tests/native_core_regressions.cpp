#include <config/game_database.h>
#include <renderer/vulkan/native_buffer_memory.h>
#include <packages/sfo.h>
#include <ime/functions.h>

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

int main(int argc, char **argv) {
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

        using namespace config::game_database;
        pugi::xml_document database;
        check(argc == 2 && database.load_file(argv[1]), "Load the actual shared Game DB asset");
        unsigned id_count = 0;
        for (auto id : database.child("game-db").child("profile").children("title-id")) {
            ++id_count;
            check(bool(recommendation(database, id.text().as_string(), "EA SPORTS FC mod")), "All regional IDs match renamed mods");
        }
        check(id_count == 31, "All 31 regional and demo IDs retained");
        check(bool(recommendation(database, " pcse00483 ")), "Case-insensitive trimmed IDs");
        check(bool(recommendation(database, "UNKNOWN", "EA SPORTS FIFA Football Demo")), "Family-name fallback");
        check(bool(recommendation(database, "UNKNOWN", "fifa15")), "Compact lowercase names");
        check(!recommendation(database, "OTHER", "Killzone"), "Unrelated games unchanged");
        check(!recommendation(database, "OTHER", "NotFIFA"), "Unrelated word prefix");
        check(!recommendation(database, "OTHER", "FIFAworld"), "Unrelated word suffix");
        check(!recommendation(database, "", "FIFA 15"), "Global settings unchanged");
        struct GpuConfig { std::string backend_renderer = "OpenGL", memory_mapping = "double-buffer"; };
        GpuConfig effective;
        apply_gpu_settings(effective, recommendation(database, "PCSE00481"));
        check(effective.backend_renderer == "Vulkan" && effective.memory_mapping == "native-buffer", "Apply DB default");
        for (const char *mapping : { "disabled", "double-buffer", "external-host", "page-table", "native-buffer" }) {
            pugi::xml_document user;
            auto gpu = user.append_child("gpu");
            gpu.append_attribute("memory-mapping") = mapping;
            gpu.append_attribute("backend-renderer") = "OpenGL";
            apply_gpu_settings(effective, recommendation(database, "PCSE00481"));
            apply_gpu_settings(effective, gpu);
            check(effective.memory_mapping == mapping && effective.backend_renderer == "OpenGL", "User config wins for EVERY buffer option");
        }
        pugi::xml_document partial;
        auto gpu = partial.append_child("gpu");
        gpu.append_attribute("backend-renderer") = "OpenGL";
        apply_gpu_settings(effective, recommendation(database, "PCSE00481"));
        apply_gpu_settings(effective, gpu);
        check(effective.backend_renderer == "OpenGL" && effective.memory_mapping == "native-buffer", "Sparse user XML inherits missing keys");
        database.child("game-db").attribute("version") = "99";
        check(!recommendation(database, "PCSE00481"), "Unknown DB version ignored");

        using renderer::vulkan::native_buffer_memory_type;
        using renderer::vulkan::native_buffer_supported;
        VkPhysicalDeviceMemoryProperties memory{};
        memory.memoryTypeCount = 3;
        memory.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        memory.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        memory.memoryTypes[2].propertyFlags = memory.memoryTypes[1].propertyFlags | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        check(native_buffer_memory_type(memory, 7) == 2, "Prefer coherent cached import when available");
        check(native_buffer_memory_type(memory, 3) == 1, "Coherent noncached import must not be rejected");
        check(native_buffer_memory_type(memory, 1) == -1, "Reject noncoherent imports");
        check(native_buffer_memory_type(memory, 0) == -1, "Reject empty import mask");
        check(native_buffer_memory_type(memory, 8) == -1, "Ignore bits outside memoryTypeCount");
        memory.memoryTypes[2].propertyFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        check(native_buffer_memory_type(memory, 7) == 1, "Cached alone is insufficient without coherence");
        check(native_buffer_supported(true, true, false), "AHB import supported independently of Vulkan host-cache flags");
        check(native_buffer_supported(true, false, true), "FD import capability retained");
        check(!native_buffer_supported(false, true, true), "Memory mapping features required");
        check(!native_buffer_supported(true, false, false), "Import extension required");
        Ime ime{};
        ime.param.maxTextLength = 6;
        ime_commit_text(ime, u"Ab");
        check(ime.str == u"Ab" && ime.edit_text.caretIndex == 2, "IME appends text and moves caret");
        ime_cursor_left(ime);
        ime_commit_text(ime, u"X");
        check(ime.str == u"AXb", "IME inserts at caret");
        ime_backspace(ime);
        check(ime.str == u"Ab", "IME removes the preceding character");
        ime_cursor_right(ime);
        ime_commit_text(ime, u"123456789");
        check(ime.str == u"Ab1234", "IME enforces game length limit");
        ime.param.maxTextLength = 1;
        ime_commit_text(ime, u"Z");
        check(ime.str == u"Ab1234", "Reduced game limit must not underflow remaining length");
        ime_set_preedit(ime, u"Z");
        check(ime.str == u"Ab1234", "Preedit also respects a reduced length limit");
        ime.deinit();
        ime.param.maxTextLength = 8;
        ime_commit_text(ime, u"A\U0001F600B");
        ime_cursor_left(ime);
        check(ime.edit_text.caretIndex == 3, "IME cursor moves before B");
        ime_cursor_left(ime);
        check(ime.edit_text.caretIndex == 1, "IME left skips a complete surrogate pair");
        ime_cursor_right(ime);
        check(ime.edit_text.caretIndex == 3, "IME right skips a complete surrogate pair");
        ime_backspace(ime);
        check(ime.str == u"AB", "IME backspace cannot leave half a surrogate pair");
        ime.edit_text.caretIndex = 999;
        ime_commit_text(ime, u"C");
        check(ime.str == u"ABC", "Out-of-range caret is safely clamped");
        ime.deinit();
        ime.param.maxTextLength = 1;
        ime_commit_text(ime, u"\U0001F600");
        check(ime.str.empty(), "Length truncation cannot split a surrogate pair");
        ime_set_preedit(ime, u"\U0001F600");
        check(ime.str.empty(), "Preedit truncation cannot split a surrogate pair");
        ime.param.maxTextLength = 8;
        ime_set_preedit(ime, u"ab");
        ime_commit_text(ime, u"\u0416");
        check(ime.str == u"\u0416" && ime.edit_text.preeditLength == 0, "Committing replaces preedit with Unicode text");
        ime_commit_text(ime, u"\n");
        check(ime.str == u"\u0416\n", "IME helper accepts multiline text");
        ime.deinit();
        ime_backspace(ime);
        ime_cursor_left(ime);
        ime_cursor_right(ime);
        check(ime.str.empty() && ime.edit_text.caretIndex == 0, "Empty IME edits are safe");
        std::cout << checks << " native regression checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Native regression failed: " << error.what() << '\n';
        return 1;
    }
}
