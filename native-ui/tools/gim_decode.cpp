#include <emucorev/paf/gim_image.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

int main(int argc, char **argv) {
    if (argc != 3) { std::cerr << "Usage: paf_gim_decode INPUT.gim NEW_DIRECTORY\n"; return 2; }
    try {
        std::ifstream input(argv[1], std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open GIM");
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        const auto images = emucorev::paf::decode_gim(bytes, 64 * 1024 * 1024);
        const std::filesystem::path directory(argv[2]);
        if (!std::filesystem::create_directory(directory)) throw std::runtime_error("GIM output requires a new directory");
        std::cout << "[";
        for (std::size_t i = 0; i < images.size(); ++i) {
            const auto &image = images[i];
            const auto filename = std::to_string(i) + ".rgba";
            std::ofstream output(directory / filename, std::ios::binary);
            output.write(reinterpret_cast<const char *>(image.pixels.data()), std::streamsize(image.pixels.size()));
            if (!output) throw std::runtime_error("Cannot write decoded GIM");
            if (i) std::cout << ',';
            std::cout << "{\"width\":" << image.width << ",\"height\":" << image.height
                << ",\"file\":\"" << filename << "\",\"bytes\":" << image.pixels.size() << '}';
        }
        std::cout << "]\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
