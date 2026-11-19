#include <emucorev/paf/resource_document.h>
#include <emucorev/paf/gim_image.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

using namespace emucorev::paf;

// Offline development tool. The UI consumes the finished PNG pack, never RCO
// firmware containers or this exporter at menu startup.
int main(int argc, char** argv) {
    if (argc != 3) { std::cerr << "Usage: paf_export_images INPUT.rco NEW_DIRECTORY\n"; return 2; }
    try {
        std::ifstream input(argv[1], std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open resource input");
        ResourceDocument document({std::istreambuf_iterator<char>(input), {}});
        const std::filesystem::path directory(argv[2]);
        if (!std::filesystem::create_directory(directory)) throw std::runtime_error("Output directory must be new");
        std::unordered_set<std::uint32_t> identifiers;
        bool first = true;
        std::cout << '[';
        for (const auto& element : document.elements()) {
            if (element.name != "texture") continue;
            const Attribute* source = nullptr;
            const Attribute* id = nullptr;
            for (const auto& attribute : element.attributes) {
                if (attribute.name == "src" && attribute.type == AttributeType::file) source = &attribute;
                if (attribute.name == "id") id = &attribute;
            }
            if (!source || !id) throw std::runtime_error("Texture is missing its image or ID");
            const auto hash = document.reference_value(*id).hash;
            if (!identifiers.insert(hash).second) throw std::runtime_error("Duplicate texture ID");
            const auto bytes = document.decoded_file(element, *source, 64 * 1024 * 1024);
            constexpr std::uint8_t png_magic[]{137, 80, 78, 71, 13, 10, 26, 10};
            const bool png = bytes.size() >= 8 && std::equal(std::begin(png_magic), std::end(png_magic), bytes.begin());
            const bool dds = bytes.size() >= 128 && bytes[0] == 'D' && bytes[1] == 'D'
                && bytes[2] == 'S' && bytes[3] == ' ';
            const bool encoded = png || dds;
            std::vector<RgbaImage> images;
            try { if (!encoded) images = decode_gim(bytes, 64 * 1024 * 1024); }
            catch (const std::exception& error) {
                std::ostringstream detail;
                detail << error.what() << ": texture 0x" << std::hex << hash
                    << " at " << element.offset << " bytes ";
                for (std::size_t i = 0; i < std::min<std::size_t>(16, bytes.size()); ++i)
                    detail << std::setw(2) << std::setfill('0') << unsigned(bytes[i]);
                throw std::runtime_error(detail.str());
            }
            if (!encoded && images.size() != 1) throw std::runtime_error("Unexpected multiple-image texture");
            std::ostringstream name;
            name << std::hex << std::setfill('0') << std::setw(8) << hash;
            const auto filename = name.str() + (png ? ".png" : dds ? ".dds" : ".rgba");
            std::ofstream output(directory / filename, std::ios::binary);
            const auto& pixels = encoded ? bytes : images.front().pixels;
            output.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
            output.flush();
            if (!output) throw std::runtime_error("Cannot write exported pixels");
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"id\":\"" << name.str() << "\",\"width\":" << (encoded ? 0 : images.front().width)
                << ",\"height\":" << (encoded ? 0 : images.front().height)
                << ",\"format\":\"" << (png ? "png" : dds ? "dds" : "rgba") << "\",\"file\":\"" << filename << "\"}";
        }
        std::cout << "]\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
