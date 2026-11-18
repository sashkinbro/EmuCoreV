#include <emucorev/paf/animation_timer.h>
#include <array>
#include <bit>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace emucorev::paf;
namespace {
std::uint32_t word(std::istream &input) {
    unsigned char b[4]; input.read(reinterpret_cast<char *>(b), 4);
    if (!input) throw std::runtime_error("Truncated timer corpus");
    return std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8) | (std::uint32_t(b[2]) << 16) | (std::uint32_t(b[3]) << 24);
}
float real(std::istream &input) { return std::bit_cast<float>(word(input)); }
void compare(std::istream &input, TimerSnapshot snapshot, std::uint32_t trace, std::uint32_t frame) {
    const std::uint32_t actual[] = {std::bit_cast<std::uint32_t>(snapshot.total), std::bit_cast<std::uint32_t>(snapshot.inverse_total),
        std::bit_cast<std::uint32_t>(snapshot.current), snapshot.flags, std::bit_cast<std::uint32_t>(snapshot.mapped)};
    for (unsigned field = 0; field < 5; ++field) {
        const auto expected = word(input);
        if (actual[field] != expected) {
            std::cerr << "trace=" << trace << " frame=" << frame << " field=" << field << " expected=" << std::hex << expected << " actual=" << actual[field] << '\n';
            throw std::runtime_error("Original timer snapshot differs");
        }
    }
}
}
int main(int argc, char **argv) {
    if (argc != 2) { std::cerr << "Usage: paf_timer_test ORIGINAL_TIMER_CORPUS\n"; return 2; }
    try {
        AnimationTimer guarded(TimerTimebase::from_milliseconds(16.666666f), 100,
            AnimationCurve::linear, TimerRepeat::none, 0);
        for (const auto flag : {0x40000u, 0x80000u, 0x10000000u, 0x5000u, 0x200u, 0x100000u}) {
            auto snapshot = guarded.snapshot(); snapshot.flags |= flag;
            bool rejected = false;
            try { guarded.restore(snapshot); } catch (const std::runtime_error &) { rejected = true; }
            if (!rejected) throw std::runtime_error("Unsupported timer snapshot accepted");
        }
        std::ifstream input(argv[1], std::ios::binary);
        char magic[8]; input.read(magic, 8);
        if (!input || std::string(magic, 8) != "PAFTIM01") throw std::runtime_error("Invalid timer corpus");
        const auto count = word(input);
        if (!count || count > 100000) throw std::runtime_error("Invalid timer trace count");
        std::uint64_t total_frames = 0;
        for (std::uint32_t trace = 0; trace < count; ++trace) {
            const auto unit = real(input), duration = real(input);
            const auto curve = word(input), repeat = word(input), repeats = word(input), frames = word(input);
            if (curve >= 5 || frames > 100000) throw std::runtime_error("Invalid timer trace");
            AnimationTimer timer(TimerTimebase::from_milliseconds(unit), duration, static_cast<AnimationCurve>(curve),
                static_cast<TimerRepeat>(repeat), std::bit_cast<std::int8_t>(std::uint8_t(repeats)));
            compare(input, timer.snapshot(), trace, ~0u);
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                const auto interval = real(input);
                const auto reset = word(input), state = word(input);
                if (reset) timer.reset();
                if (state != 255) {
                    auto snapshot = timer.snapshot();
                    snapshot.flags = (snapshot.flags & ~0xf000u) | (state << 12);
                    timer.restore(snapshot);
                }
                const auto result = std::bit_cast<std::uint32_t>(timer.tick(interval));
                const auto expected = word(input);
                if (result != expected) {
                    std::cerr << "trace=" << trace << " frame=" << frame << " return expected=" << std::hex << expected << " actual=" << result << '\n';
                    throw std::runtime_error("Original timer return differs");
                }
                compare(input, timer.snapshot(), trace, frame);
            }
            total_frames += frames;
        }
        if (input.peek() != std::char_traits<char>::eof()) throw std::runtime_error("Trailing timer corpus bytes");
        std::cout << "PASS " << count << " timer traces, " << total_frames << " frame returns and state snapshots match bit for bit\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
