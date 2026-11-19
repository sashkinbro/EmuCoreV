#include <emucorev/paf/resource_document.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

using namespace emucorev::paf;
namespace {
std::string identifier(std::uint32_t hash) {
    std::ostringstream text;
    text << std::hex << std::setfill('0') << std::setw(8) << hash;
    return text.str();
}
}
// Offline only. Export separate mono channels and the original event bindings;
// ready playback assets are prepared on the host, never extracted at UI startup.
int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("Usage: paf_export_sounds INPUT.rco NEW_DIRECTORY");
        std::ifstream input(argv[1], std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open sound resource");
        ResourceDocument document({std::istreambuf_iterator<char>(input), {}});
        const std::filesystem::path output(argv[2]);
        if (!std::filesystem::create_directory(output)) throw std::runtime_error("Sound output must be new");
        std::unordered_set<std::uint32_t> ids;
        bool first = true;
        std::cout << "{\"sounds\":[";
        for (const auto& element : document.elements()) {
            if (element.name != "sounddata") continue;
            std::uint32_t hash = 0, rate = 0;
            std::string type;
            for (const auto& a : element.attributes) {
                if (a.name == "id") hash = document.reference_value(a).hash;
                if (a.name == "rate" && a.type == AttributeType::integer) rate = a.first;
                if (a.name == "type") type = document.string_value(a);
            }
            if (!hash || !rate || type != "sound/vag" || !ids.insert(hash).second)
                throw std::runtime_error("Unsupported sound identity, rate, format or duplicate ID");
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"id\":\"" << identifier(hash) << "\",\"rate\":" << rate << ",\"channels\":{";
            bool first_channel = true;
            for (const auto& a : element.attributes) {
                if (a.type != AttributeType::file) continue;
                if (a.name != "src" && a.name != "left" && a.name != "right")
                    throw std::runtime_error("Unknown sound channel");
                const auto bytes = document.decoded_file(element, a, 64 * 1024 * 1024);
                const auto filename = identifier(hash) + "-" + a.name + ".vag";
                std::ofstream file(output / filename, std::ios::binary);
                file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                file.flush();
                if (!file) throw std::runtime_error("Cannot write sound channel");
                if (!first_channel) std::cout << ',';
                first_channel = false;
                std::cout << '"' << a.name << "\":\"" << filename << '"';
            }
            if (first_channel) throw std::runtime_error("Sound has no channels");
            std::cout << "}}";
        }
        std::cout << "],\"bindings\":[";
        first = true;
        for (const auto& element : document.elements()) {
            if (!element.name.starts_with("sound_")) continue;
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"widget\":\"" << element.name << "\",\"attributes\":{";
            bool first_attribute = true;
            for (const auto& a : element.attributes) {
                const auto reference = document.reference_value(a);
                if (!first_attribute) std::cout << ',';
                first_attribute = false;
                std::cout << '"' << a.name << "\":\"" << identifier(reference.hash) << '"';
            }
            std::cout << "}}";
        }
        std::cout << "]}\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
