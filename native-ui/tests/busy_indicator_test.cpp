#include <emucorev/paf/busy_indicator.h>
#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: paf_busy_test ORIGINAL_CORPUS");
        std::ifstream input(argv[1], std::ios::binary);
        char magic[8]; input.read(magic, 8);
        if (!input || std::string(magic, 8) != "PAFBUS01") throw std::runtime_error("Invalid busy corpus");
        auto word = [&]() {
            std::array<std::uint8_t, 4> bytes;
            input.read(reinterpret_cast<char*>(bytes.data()), 4);
            if (!input) throw std::runtime_error("Truncated busy corpus");
            return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8)
                | (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
        };
        auto real = [&]() { return std::bit_cast<float>(word()); };
        const auto traces = word();
        if (!traces || traces > 10000) throw std::runtime_error("Invalid busy trace count");
        unsigned total = 0;
        for (unsigned trace = 0; trace < traces; ++trace) {
            const float width = real(), ball = real();
            emucorev::paf::BusyIndicator indicator(width, ball);
            const auto steps = word();
            if (!steps || steps > 100000) throw std::runtime_error("Invalid busy step count");
            for (unsigned step = 0; step < steps; ++step) {
                const auto action = word();
                const float delta = real();
                if (action == 0) indicator.tick(delta);
                else if (action == 1) indicator.start();
                else if (action == 2) indicator.stop();
                else throw std::runtime_error("Invalid busy action");
                const auto& s = indicator.snapshot();
                auto compare = [&](std::uint32_t actual, const char* field) {
                    const auto expected = word();
                    if (expected != actual) {
                        std::cerr << "trace=" << trace << " step=" << step << " field=" << field
                            << std::hex << " expected=" << expected << " actual=" << actual << '\n';
                        throw std::runtime_error("Original busy result differs");
                    }
                };
                compare(static_cast<std::uint32_t>(s.state), "state");
                compare(std::bit_cast<std::uint32_t>(s.phase), "phase");
                compare(std::bit_cast<std::uint32_t>(s.elapsed), "elapsed");
                compare(std::bit_cast<std::uint32_t>(s.last_emission), "last_emission");
                for (unsigned index = 0; index < s.dots.size(); ++index) {
                    const auto& dot = s.dots[index];
                    try {
                        compare(std::bit_cast<std::uint32_t>(dot.x), "x");
                        compare(std::bit_cast<std::uint32_t>(dot.y), "y");
                        compare(std::bit_cast<std::uint32_t>(dot.size), "size");
                        compare(dot.alpha, "alpha");
                    } catch (...) { std::cerr << "dot=" << index << '\n'; throw; }
                }
                ++total;
            }
        }
        if (input.peek() != std::char_traits<char>::eof()) throw std::runtime_error("Trailing busy corpus data");
        std::cout << "PASS " << traces << " original busy traces, " << total << " bit-exact operations\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
