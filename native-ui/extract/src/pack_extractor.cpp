// Builds the native shell's resource pack from the user's installed firmware.
// The conversions mirror the offline prepare_*.py tools; the golden manifests
// recorded for firmware 3.74 are verified by native-ui/tests/verify_extracted_pack.py.

#include <emucorev/extract/pack_extractor.h>

#include <emucorev/paf/gim_image.h>
#include <emucorev/paf/resource_document.h>

#include "internal.h"

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace emucorev::extract {
namespace fs = std::filesystem;
namespace {

using Bytes = std::vector<std::uint8_t>;

// Shell plugin resources under vs0/vsh/shell plus the common PAF resource.
constexpr const char* kShellResources[] = {"applauncher_plugin", "auth_reset_plugin", "builtin_service_info",
    "coldboot_plugin", "download_plugin", "idu_update_plugin", "ime_plugin", "impose_net_plugin", "impose_plugin",
    "indicator_plugin", "livearea_plugin", "location_plugin", "musiccore_plugin", "power_manage_plugin",
    "telephony_plugin", "theme_plugin", "topmenu_plugin"};
constexpr const char* kCommonResourceName = "paf-common-resource";
constexpr const char* kCommonResourcePath = "data/external/common/common_resource.rco";
constexpr const char* kSystemApps[] = {"NPXS10008", "NPXS10015"};
constexpr std::size_t kMaxFileBytes = 64u * 1024 * 1024;

std::string quote(const std::string& text) {
    std::string out = "\"";
    for (const unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c < 0x20) { char buffer[8]; std::snprintf(buffer, sizeof(buffer), "\\u%04x", c); out += buffer; }
        else out += char(c);
    }
    return out + "\"";
}

std::string hex8(std::uint32_t value) {
    std::ostringstream text;
    text << std::hex << std::setfill('0') << std::setw(8) << value;
    return text.str();
}

Bytes read_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open " + path.generic_string());
    const auto size = file.tellg();
    if (size < 0 || std::uintmax_t(size) > (200u << 20)) throw std::runtime_error("Unexpected size: " + path.generic_string());
    Bytes data(static_cast<std::size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), size);
    if (!file) throw std::runtime_error("Cannot read " + path.generic_string());
    return data;
}

class Extraction {
public:
    Extraction(const fs::path& root, const fs::path& out, const ProgressFn& progress)
        : vs0_(root / "vs0"), sa0_(root / "sa0"), out_(out), progress_(progress) {}

    Result run() {
        Result result;
        try {
            fs::create_directories(out_);
            report(0, "fonts");
            fonts();
            std::vector<std::pair<std::string, fs::path>> sources;
            for (const char* name : kShellResources) sources.emplace_back(name, vs0_ / "vsh/shell" / (std::string(name) + ".rco"));
            sources.emplace_back(kCommonResourceName, vs0_ / kCommonResourcePath);
            std::sort(sources.begin(), sources.end());
            std::string source_entries, image_entries, sound_source_entries, sound_entries;
            std::size_t image_total = 0, sound_total = 0, done = 0;
            for (const auto& [name, path] : sources) {
                report(20 + int(done * 60 / sources.size()), "resources");
                ++done;
                if (!fs::is_regular_file(path)) {
                    if (name == "topmenu_plugin" || name == kCommonResourceName)
                        throw std::runtime_error("Firmware resource missing: " + name + ".rco");
                    result_.warnings.push_back("Firmware resource missing: " + name + ".rco");
                    continue;
                }
                resource(name, path, source_entries, image_entries, sound_source_entries, sound_entries,
                    image_total, sound_total);
            }
            write_text("images.json", "{\n  \"version\": 1,\n  \"runtimeSource\": \"on-device firmware extraction\",\n  \"extractorRevision\": " +
                std::to_string(kExtractorRevision) + ",\n  \"sources\": [" + source_entries + "\n  ],\n  \"images\": [" +
                image_entries + "\n  ]\n}\n");
            write_text("audio/sounds.json", "{\n  \"version\": 1,\n  \"runtimeSource\": \"on-device firmware extraction (PCM16 WAV)\",\n  \"sources\": [" +
                sound_source_entries + "\n  ],\n  \"sounds\": [" + sound_entries + "\n  ]\n}\n");
            report(82, "effects");
            effects();
            lookups();
            report(95, "applications");
            system_apps();
            validate();
            report(100, "done");
            result_.ok = true;
        } catch (const std::exception& error) {
            result_.ok = false;
            result_.error = error.what();
        }
        result_.files = files_;
        return std::move(result_);
    }

private:
    fs::path vs0_, sa0_, out_;
    ProgressFn progress_;
    Result result_;
    std::size_t files_ = 0;

    void report(int percent, const char* stage) const { if (progress_) progress_(percent, stage); }

    void write_file(const std::string& relative, std::span<const std::uint8_t> data) {
        const fs::path target = out_ / relative;
        fs::create_directories(target.parent_path());
        std::ofstream file(target, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        file.flush();
        if (!file) throw std::runtime_error("Cannot write " + relative);
        ++files_;
    }
    void write_text(const std::string& relative, const std::string& text) {
        write_file(relative, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
    }

    // ---- fonts: PVF files are SFNT fonts (CFF OpenType or TrueType) -------------
    void fonts() {
        const fs::path directory = sa0_ / "data/font/pvf";
        std::vector<fs::path> list;
        std::error_code ignored;
        for (const auto& entry : fs::directory_iterator(directory, ignored))
            if (entry.is_regular_file() && entry.path().extension() == ".pvf") list.push_back(entry.path());
        if (list.empty()) throw std::runtime_error("Firmware fonts (sa0/data/font/pvf) are not installed");
        std::sort(list.begin(), list.end());
        std::string entries;
        for (const auto& path : list) {
            const Bytes data = read_file(path);
            const char* suffix = nullptr;
            if (data.size() >= 4 && !std::memcmp(data.data(), "OTTO", 4)) suffix = ".otf";
            else if (data.size() >= 4 && data[0] == 0 && data[1] == 1 && data[2] == 0 && data[3] == 0) suffix = ".ttf";
            if (!suffix) { result_.warnings.push_back("Not an SFNT font: " + path.filename().string()); continue; }
            const std::string relative = "fonts/" + path.stem().string() + suffix;
            write_file(relative, data);
            entries += std::string(entries.empty() ? "\n" : ",\n") + "  {\"path\": " + quote(relative) + ", \"sha256\": \"" +
                sha256_hex(data) + "\", \"bytes\": " + std::to_string(data.size()) + "}";
        }
        write_text("fonts.json", "[" + entries + "\n]\n");
    }

    // ---- one RCO: images and sounds ---------------------------------------------------
    void resource(const std::string& name, const fs::path& path, std::string& source_entries, std::string& image_entries,
        std::string& sound_source_entries, std::string& sound_entries, std::size_t& image_total, std::size_t& sound_total) {
        const Bytes rco = read_file(path);
        const std::string rco_sha = sha256_hex(rco);
        paf::ResourceDocument document{Bytes(rco)};
        std::set<std::uint32_t> image_ids, sound_ids;
        std::size_t images = 0, sounds = 0;
        std::string bindings;
        for (const auto& element : document.elements()) {
            if (element.name == "texture") {
                if (image_texture(name, document, element, image_ids, image_entries)) { ++images; ++image_total; }
            } else if (element.name == "sounddata") {
                sound(name, document, element, sound_ids, sound_entries);
                ++sounds; ++sound_total;
            } else if (element.name.starts_with("sound_")) {
                bindings += std::string(bindings.empty() ? "" : ",") + "{\"widget\": " + quote(element.name) + ", \"attributes\": {";
                bool first = true;
                for (const auto& attribute : element.attributes) {
                    bindings += std::string(first ? "" : ", ") + quote(attribute.name) + ": \"" +
                        hex8(document.reference_value(attribute).hash) + "\"";
                    first = false;
                }
                bindings += "}}";
            }
        }
        const std::string file = name + ".rco";
        source_entries += std::string(source_entries.empty() ? "\n" : ",\n") + "    {\"name\": " + quote(file) +
            ", \"sha256\": \"" + rco_sha + "\", \"images\": " + std::to_string(images) + "}";
        sound_source_entries += std::string(sound_source_entries.empty() ? "\n" : ",\n") + "    {\"name\": " + quote(file) +
            ", \"sha256\": \"" + rco_sha + "\", \"sounds\": " + std::to_string(sounds) + ", \"bindings\": [" + bindings + "]}";
    }

    bool image_texture(const std::string& group, const paf::ResourceDocument& document, const paf::Element& element,
        std::set<std::uint32_t>& ids, std::string& entries) {
        const paf::Attribute* source = nullptr;
        const paf::Attribute* id = nullptr;
        for (const auto& attribute : element.attributes) {
            if (attribute.name == "src" && attribute.type == paf::AttributeType::file) source = &attribute;
            if (attribute.name == "id") id = &attribute;
        }
        if (!source || !id) { result_.warnings.push_back(group + ": texture without image or id"); return false; }
        const std::uint32_t hash = document.reference_value(*id).hash;
        if (!ids.insert(hash).second) { result_.warnings.push_back(group + ": duplicate texture " + hex8(hash)); return false; }
        const Bytes bytes = document.decoded_file(element, *source, kMaxFileBytes);
        static const std::uint8_t png_magic[8] = {137, 80, 78, 71, 13, 10, 26, 10};
        const bool is_png = bytes.size() >= 8 && std::equal(png_magic, png_magic + 8, bytes.begin());
        const bool is_dds = bytes.size() >= 128 && !std::memcmp(bytes.data(), "DDS ", 4);
        std::uint32_t width = 0, height = 0;
        Bytes rgba;
        Bytes png_out;
        try {
            if (is_png) {
                DecodedPng decoded;
                if (decode_png_to_rgba(bytes, decoded)) {
                    width = decoded.width; height = decoded.height; rgba = std::move(decoded.rgba);
                } else {
                    // Unusual PNG variant: keep the original bytes, hash left empty.
                    png_out = bytes;
                    png_size(bytes, width, height);
                }
            } else if (is_dds) {
                auto mips = decode_dds(bytes, false);
                if (mips.front().alpha8) throw std::runtime_error("alpha-only DDS texture");
                width = mips.front().width; height = mips.front().height; rgba = std::move(mips.front().pixels);
            } else {
                auto images = paf::decode_gim(bytes, kMaxFileBytes);
                if (images.size() != 1) throw std::runtime_error("unexpected multiple-image texture");
                width = images.front().width; height = images.front().height; rgba = std::move(images.front().pixels);
            }
            if (png_out.empty()) {
                if (rgba.size() != std::size_t(width) * height * 4) throw std::runtime_error("incomplete image");
                png_out = encode_png(width, height, PngKind::rgba8, rgba);
            }
        } catch (const std::exception& error) {
            result_.warnings.push_back(group + "/" + hex8(hash) + ": " + error.what());
            return false;
        }
        const std::string relative = "images/" + group + "/" + hex8(hash) + ".png";
        write_file(relative, png_out);
        entries += std::string(entries.empty() ? "\n" : ",\n") + "    {\"path\": " + quote(relative) + ", \"resourceId\": \"" +
            hex8(hash) + "\", \"width\": " + std::to_string(width) + ", \"height\": " + std::to_string(height) +
            ", \"rgbaSha256\": \"" + (rgba.empty() ? std::string() : sha256_hex(rgba)) + "\", \"pngSha256\": \"" +
            sha256_hex(png_out) + "\"}";
        return true;
    }

    void sound(const std::string& group, const paf::ResourceDocument& document, const paf::Element& element,
        std::set<std::uint32_t>& ids, std::string& entries) {
        std::uint32_t hash = 0, rate = 0;
        std::string type;
        for (const auto& attribute : element.attributes) {
            if (attribute.name == "id") hash = document.reference_value(attribute).hash;
            if (attribute.name == "rate" && attribute.type == paf::AttributeType::integer) rate = attribute.first;
            if (attribute.name == "type") type = document.string_value(attribute);
        }
        if (!hash || !rate || type != "sound/vag" || !ids.insert(hash).second)
            throw std::runtime_error(group + ": unsupported sound identity, rate, format or duplicate ID");
        std::map<std::string, Bytes> channels;
        for (const auto& attribute : element.attributes) {
            if (attribute.type != paf::AttributeType::file) continue;
            if (attribute.name != "src" && attribute.name != "left" && attribute.name != "right")
                throw std::runtime_error(group + ": unknown sound channel");
            channels[attribute.name] = document.decoded_file(element, attribute, kMaxFileBytes);
        }
        std::vector<std::string> order;
        if (channels.size() == 1 && channels.count("src")) order = {"src"};
        else if (channels.size() == 2 && channels.count("left") && channels.count("right")) order = {"left", "right"};
        else throw std::runtime_error(group + ": unsupported sound channel arrangement");
        std::vector<std::vector<std::int16_t>> decoded;
        std::string original;
        for (const auto& channel : order) {
            VagInfo info;
            decoded.push_back(decode_hevag(channels[channel], info));
            if (info.sample_rate != rate) throw std::runtime_error(group + ": sound rate differs from RCO");
            if (decoded.back().size() != decoded.front().size()) throw std::runtime_error(group + ": channel lengths differ");
            Bytes mono(decoded.back().size() * 2);
            for (std::size_t i = 0; i < decoded.back().size(); ++i) {
                mono[i * 2] = std::uint8_t(decoded.back()[i]);
                mono[i * 2 + 1] = std::uint8_t(std::uint16_t(decoded.back()[i]) >> 8);
            }
            original += std::string(original.empty() ? "" : ",") + "{\"channel\": " + quote(channel) + ", \"sha256\": \"" +
                sha256_hex(channels[channel]) + "\", \"pcmSha256\": \"" + sha256_hex(mono) +
                "\", \"metadata\": {\"sampleRate\": " + std::to_string(rate) + ", \"channels\": 1, \"numberOfSamples\": " +
                std::to_string(info.samples) + ", \"encoding\": \"Sony HE-VAG 4-bit ADPCM\"}}";
        }
        std::vector<std::int16_t> pcm;
        if (decoded.size() == 1) pcm = decoded[0];
        else {
            pcm.resize(decoded[0].size() * 2);
            for (std::size_t i = 0; i < decoded[0].size(); ++i) { pcm[i * 2] = decoded[0][i]; pcm[i * 2 + 1] = decoded[1][i]; }
        }
        const Bytes wav = make_wav(rate, std::uint32_t(order.size()), pcm);
        const Bytes pcm_bytes(wav.begin() + 44, wav.end());
        const std::string relative = group + "/" + hex8(hash) + ".wav";
        write_file("audio/" + relative, wav);
        entries += std::string(entries.empty() ? "\n" : ",\n") + "    {\"path\": " + quote(relative) + ", \"resourceId\": \"" +
            hex8(hash) + "\", \"rate\": " + std::to_string(rate) + ", \"channels\": " + std::to_string(order.size()) +
            ", \"samples\": " + std::to_string(decoded[0].size()) + ", \"pcmSha256\": \"" + sha256_hex(pcm_bytes) +
            "\", \"wavSha256\": \"" + sha256_hex(wav) + "\", \"originalChannels\": [" + original + "]}";
    }

    // ---- effect textures and the lock wallpaper ---------------------------------------
    void effects() {
        const fs::path directory = vs0_ / "data/internal/effects";
        std::vector<fs::path> list;
        std::error_code ignored;
        for (fs::recursive_directory_iterator it(directory, ignored), end; !ignored && it != end; it.increment(ignored))
            if (it->is_regular_file() && it->path().extension() == ".dds") list.push_back(it->path());
        std::sort(list.begin(), list.end());
        std::string entries;
        for (const auto& path : list) {
            const Bytes data = read_file(path);
            std::vector<DdsMip> mips;
            try { mips = decode_dds(data, true); }
            catch (const std::exception& error) { result_.warnings.push_back(path.filename().string() + ": " + error.what()); continue; }
            const fs::path relative = fs::relative(path, directory);
            fs::path stem = relative;
            stem.replace_extension();
            std::string mip_entries;
            for (std::size_t level = 0; level < mips.size(); ++level) {
                const auto& mip = mips[level];
                const std::string file = "effects/" + stem.generic_string() + "/mip-" + std::to_string(level) + ".png";
                const Bytes png = encode_png(mip.width, mip.height, mip.alpha8 ? PngKind::gray8 : PngKind::rgba8, mip.pixels);
                write_file(file, png);
                mip_entries += std::string(mip_entries.empty() ? "" : ",") + "\n        {\"level\": " + std::to_string(level) +
                    ", \"path\": " + quote(file) + ", \"width\": " + std::to_string(mip.width) + ", \"height\": " +
                    std::to_string(mip.height) + ", \"mode\": \"" + (mip.alpha8 ? "L" : "RGBA") + "\", \"pixelSha256\": \"" +
                    sha256_hex(mip.pixels) + "\", \"pngSha256\": \"" + sha256_hex(png) + "\"}";
            }
            entries += std::string(entries.empty() ? "\n" : ",\n") + "    {\"source\": " + quote(relative.generic_string()) +
                ", \"sourceSha256\": \"" + sha256_hex(data) + "\", \"encoding\": \"" + (mips[0].alpha8 ? "alpha8" : "bgra8") +
                "\", \"mips\": [" + mip_entries + "\n      ]}";
        }
        if (entries.empty()) throw std::runtime_error("Firmware effect textures are not installed");
        const Bytes lock = read_file(vs0_ / "data/internal/keylock/keylock.png");
        std::uint32_t width = 0, height = 0;
        if (!png_size(lock, width, height)) throw std::runtime_error("Lock wallpaper is not a PNG");
        write_file("effects/keylock.png", lock);
        write_text("effects/effects.json", "{\n  \"version\": 1,\n  \"runtimeSource\": \"on-device firmware extraction (effect mip PNGs)\",\n  \"textures\": [" +
            entries + "\n  ],\n  \"lockWallpaper\": {\"path\": \"effects/keylock.png\", \"sha256\": \"" + sha256_hex(lock) +
            "\", \"width\": " + std::to_string(width) + ", \"height\": " + std::to_string(height) + "}\n}\n");
    }

    void lookups() {
        const Bytes exp = exp_minus_lookup(), corner = corner_distance_lookup();
        const Bytes exp_png = encode_png(128, 8, PngKind::gray8, exp);
        const Bytes corner_png = encode_png(64, 64, PngKind::gray8, corner);
        write_file("effects/lookups/exp-minus.png", exp_png);
        write_file("effects/lookups/corner-distance.png", corner_png);
        write_text("effects/lookups/lookups.json", "{\n  \"version\": 1,\n  \"runtimeSource\": \"analytic generators (bit-exact against the original output)\",\n  \"images\": [\n"
            "    {\"path\": \"effects/lookups/exp-minus.png\", \"width\": 128, \"height\": 8, \"encoding\": \"scalar8\", \"pixelSha256\": \"" +
            sha256_hex(exp) + "\", \"pngSha256\": \"" + sha256_hex(exp_png) + "\", \"exponentialRange\": 5},\n"
            "    {\"path\": \"effects/lookups/corner-distance.png\", \"width\": 64, \"height\": 64, \"encoding\": \"scalar8\", \"pixelSha256\": \"" +
            sha256_hex(corner) + "\", \"pngSha256\": \"" + sha256_hex(corner_png) + "\"}\n  ]\n}\n");
    }

    // ---- built-in system applications (icon + LiveArea) ---------------------------------
    void system_apps() {
        for (const char* id : kSystemApps) {
            const fs::path root = vs0_ / "app" / id / "sce_sys";
            if (!fs::is_regular_file(root / "icon0.png")) {
                result_.warnings.push_back(std::string("System application missing: ") + id);
                continue;
            }
            write_file(std::string("system-apps/") + id + "/icon0.png", read_file(root / "icon0.png"));
            const fs::path contents = root / "livearea/contents";
            std::error_code ignored;
            for (fs::recursive_directory_iterator it(contents, ignored), end; !ignored && it != end; it.increment(ignored))
                if (it->is_regular_file())
                    write_file(std::string("system-apps/") + id + "/livearea/contents/" +
                        fs::relative(it->path(), contents).generic_string(), read_file(it->path()));
        }
    }

    void validate() const {
        static const char* required[] = {"fonts/ltn0.otf", "fonts/ltn4.otf", "images.json", "fonts.json",
            "audio/sounds.json", "audio/paf-common-resource/48d64cf1.wav", "audio/paf-common-resource/5c756da0.wav",
            "audio/paf-common-resource/672a5e85.wav", "audio/paf-common-resource/857299df.wav",
            "audio/paf-common-resource/e0d9f918.wav", "effects/keylock.png", "effects/effects.json",
            "effects/lookups/exp-minus.png", "effects/lookups/corner-distance.png",
            "effects/page_turn/textures/page_corner/mip-0.png", "images/topmenu_plugin/754463d1.png",
            "images/livearea_plugin/ca6a6db1.png", "images/livearea_plugin/1e6a33de.png",
            "images/indicator_plugin/ca76be07.png", "images/indicator_plugin/56e72f16.png",
            "system-apps/NPXS10008/icon0.png", "system-apps/NPXS10015/icon0.png"};
        for (const char* file : required)
            if (!fs::is_regular_file(out_ / file)) throw std::runtime_error(std::string("Extracted pack is incomplete: ") + file);
    }
};

}  // namespace

bool firmware_present(const fs::path& vita_root) {
    std::error_code ignored;
    return fs::is_regular_file(vita_root / "vs0/vsh/shell/topmenu_plugin.rco", ignored) &&
        fs::is_regular_file(vita_root / "vs0" / kCommonResourcePath, ignored) &&
        fs::is_regular_file(vita_root / "sa0/data/font/pvf/ltn0.pvf", ignored) &&
        fs::is_regular_file(vita_root / "sa0/data/font/pvf/ltn4.pvf", ignored) &&
        fs::is_regular_file(vita_root / "vs0/data/internal/keylock/keylock.png", ignored);
}

Result extract_pack(const fs::path& vita_root, const fs::path& out_dir, const ProgressFn& progress) {
    return Extraction(vita_root, out_dir, progress).run();
}

}  // namespace emucorev::extract
