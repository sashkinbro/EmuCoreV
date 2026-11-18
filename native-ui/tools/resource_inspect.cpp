#include <emucorev/paf/resource_document.h>

#include <bit>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace emucorev::paf;

namespace {
void quoted(const std::string &text) {
    std::cout << '"';
    for (const unsigned char c : text) {
        if (c == '"' || c == '\\') std::cout << '\\' << char(c);
        else if (c < 32) {
            std::cout << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << unsigned(c) << std::dec;
        } else std::cout << char(c);
    }
    std::cout << '"';
}

void real(float value) {
    // raw[] always retains the exact IEEE bits, including NaN payloads.
    if (std::isfinite(value)) std::cout << std::setprecision(9) << value;
    else quoted(std::isnan(value) ? "NaN" : value > 0 ? "+Infinity" : "-Infinity");
}

void value(const ResourceDocument &document, const Attribute &a) {
    switch (a.type) {
    case AttributeType::none: std::cout << "null"; break;
    case AttributeType::integer: std::cout << std::bit_cast<std::int32_t>(a.first); break;
    case AttributeType::real: real(std::bit_cast<float>(a.first)); break;
    case AttributeType::string: quoted(document.string_value(a)); break;
    case AttributeType::wide_string: {
        std::cout << '"';
        for (const auto c : document.wide_string_value(a))
            std::cout << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
        std::cout << '"';
        break;
    }
    case AttributeType::hash: std::cout << document.hash_value(a); break;
    case AttributeType::integers: {
        std::cout << '['; bool first = true;
        for (const auto v : document.integer_values(a)) { if (!first) std::cout << ','; first = false; std::cout << v; }
        std::cout << ']'; break;
    }
    case AttributeType::reals: {
        std::cout << '['; bool first = true;
        for (const auto v : document.real_values(a)) { if (!first) std::cout << ','; first = false; real(v); }
        std::cout << ']'; break;
    }
    case AttributeType::file:
        std::cout << "{\"offset\":" << a.first << ",\"bytes\":" << document.file_value(a).size() << '}'; break;
    default: {
        const auto ref = document.reference_value(a);
        std::cout << "{\"element\":" << ref.element << ",\"name\":"; quoted(ref.name);
        std::cout << ",\"hash\":" << ref.hash << '}'; break;
    }
    }
}
}

int main(int argc, char **argv) {
    if (argc != 2 && !(argc == 4 && std::string(argv[2]) == "--extract")) {
        std::cerr << "Usage: paf_resource_inspect resource.rco [--extract NEW_DIRECTORY]\n"; return 2;
    }
    try {
        std::ifstream input(argv[1], std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open resource");
        ResourceDocument document({std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()});
        if (argc == 4) {
            const std::filesystem::path directory(argv[3]);
            if (!std::filesystem::create_directory(directory))
                throw std::runtime_error("Extraction requires a new directory");
            for (const auto &element : document.elements()) {
                for (std::size_t i = 0; i < element.attributes.size(); ++i) {
                    const auto &a = element.attributes[i];
                    if (a.type != AttributeType::file) continue;
                    const auto bytes = document.decoded_file(element, a, 64 * 1024 * 1024);
                    // Filenames use validated offsets/ordinals, never resource-provided paths.
                    const auto path = directory / (std::to_string(element.offset) + "_" + std::to_string(i) + ".bin");
                    std::ofstream output(path, std::ios::binary);
                    output.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
                    if (!output) throw std::runtime_error("Cannot write extracted resource");
                }
            }
        }
        std::cout << "{\"magic\":"; quoted(document.magic());
        std::cout << ",\"version\":" << document.version() << ",\"elements\":[\n";
        bool first_element = true;
        for (const auto &element : document.elements()) {
            if (!first_element) std::cout << ",\n";
            first_element = false;
            std::cout << "{\"offset\":" << element.offset << ",\"name\":"; quoted(element.name);
            std::cout << ",\"parent\":" << element.parent << ",\"previous\":" << element.previous
                << ",\"next\":" << element.next << ",\"firstChild\":" << element.first_child
                << ",\"lastChild\":" << element.last_child << ",\"attributes\":[";
            bool first_attribute = true;
            for (const auto &a : element.attributes) {
                if (!first_attribute) std::cout << ',';
                first_attribute = false;
                std::cout << "{\"name\":"; quoted(a.name);
                std::cout << ",\"type\":" << unsigned(a.type) << ",\"raw\":[" << a.first << ',' << a.second << "],\"value\":";
                value(document, a); std::cout << '}';
            }
            std::cout << "]}";
        }
        std::cout << "\n]}\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
