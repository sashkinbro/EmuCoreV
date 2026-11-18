#include <emucorev/paf/animation_curve.h>

#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace emucorev::paf;
namespace {
void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
std::uint32_t read_word(std::istream &input) {
    unsigned char bytes[4];
    input.read(reinterpret_cast<char *>(bytes), 4);
    if (!input) throw std::runtime_error("Truncated curve corpus");
    return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8)
        | (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
}
}
int main(int argc, char **argv) {
    try {
        for (unsigned i = 0; i < 5; ++i) {
            const auto curve = static_cast<AnimationCurve>(i);
            check(evaluate_curve(curve, -1) == 0, "Lower clamp");
            check(std::bit_cast<std::uint32_t>(evaluate_curve(curve, -0.0f)) == 0, "Signed zero clamp");
            check(evaluate_curve(curve, 2) == 1, "Upper clamp");
        }
        check(evaluate_curve(AnimationCurve::slower, 0.25f) == 0.5f, "Slower");
        check(evaluate_curve(AnimationCurve::faster, 0.5f) == 0.25f, "Faster");
        check(evaluate_curve(AnimationCurve::quadratic_out, 0.5f) == 0.75f, "Quadratic out");
        if (argc == 1) { std::cout << "PASS basic curve checks; no original-code corpus supplied\n"; return 0; }
        check(argc == 2, "Usage: paf_curve_test [original-code-corpus]");
        std::ifstream input(argv[1], std::ios::binary);
        char header[8]; input.read(header, 8);
        check(input && std::string(header, 8) == "PAFCURV1", "Invalid curve corpus");
        const auto count = read_word(input);
        check(count > 0 && count <= 10000000, "Invalid curve corpus count");
        std::uint32_t coverage[5]{};
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto curve = read_word(input), bits = read_word(input), expected = read_word(input);
            check(curve < 5, "Unknown corpus curve");
            const auto actual = std::bit_cast<std::uint32_t>(evaluate_curve(static_cast<AnimationCurve>(curve), std::bit_cast<float>(bits)));
            if (actual != expected) {
                std::cerr << "curve=" << curve << " input=" << std::hex << bits << " expected=" << expected << " actual=" << actual << '\n';
                throw std::runtime_error("Original-code curve result differs");
            }
            ++coverage[curve];
        }
        for (const auto covered : coverage) check(covered > 0, "Missing curve coverage");
        check(input.peek() == std::char_traits<char>::eof(), "Trailing curve corpus bytes");
        std::cout << "PASS " << count << " bit-exact original-code comparisons across five curves\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
