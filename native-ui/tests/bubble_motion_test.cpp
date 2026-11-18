#include <emucorev/paf/bubble_motion.h>
#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: paf_bubble_motion_test ORIGINAL_CORPUS");
        std::ifstream input(argv[1], std::ios::binary);
        char magic[8];
        input.read(magic, 8);
        if (!input || std::string(magic, 8) != "PAFWOB01") throw std::runtime_error("Invalid motion corpus");
        auto word = [&]() {
            std::array<std::uint8_t, 4> data;
            input.read(reinterpret_cast<char*>(data.data()), 4);
            if (!input) throw std::runtime_error("Truncated motion corpus");
            return std::uint32_t(data[0]) | (std::uint32_t(data[1]) << 8)
                | (std::uint32_t(data[2]) << 16) | (std::uint32_t(data[3]) << 24);
        };
        const auto count = word();
        if (!count || count > 10000) throw std::runtime_error("Invalid motion case count");
        for (unsigned i = 0; i < count; ++i) {
            std::array<float, 6> args;
            for (auto& argument : args) argument = std::bit_cast<float>(word());
            const auto phase = emucorev::paf::advance_bubble_phase(args[3], args[4], args[5]);
            const auto basis = emucorev::paf::bubble_wobble_basis(args[0], args[1], args[2]);
            std::array<float, 13> actual;
            actual[0] = phase;
            for (unsigned column = 0; column < 3; ++column)
                for (unsigned lane = 0; lane < 4; ++lane) actual[1 + column * 4 + lane] = basis[column][lane];
            for (unsigned lane = 0; lane < 13; ++lane) {
                const auto expected = word(), bits = std::bit_cast<std::uint32_t>(actual[lane]);
                if (bits != expected) {
                    std::cerr << "case=" << i << " lane=" << lane << std::hex
                        << " expected=" << expected << " actual=" << bits << '\n';
                    throw std::runtime_error("Original wobble result differs");
                }
            }
        }
        if (input.peek() != std::char_traits<char>::eof()) throw std::runtime_error("Trailing corpus data");
        std::cout << "PASS " << count << " bit-exact original phase/basis cases\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
