#include <emucorev/paf/page_turn.h>
#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: paf_page_turn_test ORIGINAL_CORPUS");
        std::ifstream input(argv[1], std::ios::binary);
        char magic[8]; input.read(magic, 8);
        if (!input || std::string(magic, 8) != "PAFPAG02") throw std::runtime_error("Invalid page-turn corpus");
        auto word = [&]() {
            std::array<std::uint8_t, 4> bytes;
            input.read(reinterpret_cast<char*>(bytes.data()), 4);
            if (!input) throw std::runtime_error("Truncated page-turn corpus");
            return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8)
                | (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
        };
        const auto count = word();
        if (!count || count > 100000) throw std::runtime_error("Invalid page-turn case count");
        using namespace emucorev::paf;
        for (unsigned i = 0; i < count; ++i) {
            std::array<float, 16> values;
            for (auto& value : values) value = std::bit_cast<float>(word());
            const bool outward = word() != 0;
            auto point = [&](unsigned n) { return PagePoint{values[n], values[n + 1]}; };
            const auto size = point(0), position = point(4), initial = point(6), offset = point(8);
            const auto layout = page_turn_layout(size, values[2], values[3]);
            const auto control = page_turn_draw_control(size, position, offset, {values[10], values[11], values[12]});
            const auto drag = page_turn_drag_target(position, initial, offset);
            auto bits = [](float f) { return std::bit_cast<std::uint32_t>(f); };
            const std::array<std::uint32_t, 15> actual{bits(layout.rest.x), bits(layout.rest.y),
                bits(layout.hit_size.x), bits(layout.hit_size.y), bits(layout.corner_exclusion),
                bits(control.position.x), bits(control.position.y), bits(control.offset.x), bits(control.offset.y),
                control.back_color, bits(drag.x), bits(drag.y), page_turn_hit_test(size, layout, position),
                static_cast<std::uint32_t>(page_turn_release(point(13), values[15], outward, false)),
                static_cast<std::uint32_t>(page_turn_release(point(13), values[15], outward, true))};
            for (unsigned lane = 0; lane < actual.size(); ++lane) {
                const auto expected = word();
                if (actual[lane] != expected) {
                    std::cerr << "case=" << i << " lane=" << lane << std::hex
                        << " expected=" << expected << " actual=" << actual[lane] << '\n';
                    throw std::runtime_error("Original PageTurn result differs");
                }
            }
            std::array<float, 4> shape_args;
            for (auto& value : shape_args) value = std::bit_cast<float>(word());
            const auto shape = page_turn_fold_shape({shape_args[0], shape_args[1]}, {shape_args[2], shape_args[3]});
            std::array<std::uint32_t, 51> shape_actual{};
            unsigned next = 0;
            auto append_point = [&](PagePoint p) {
                shape_actual[next++] = bits(p.x); shape_actual[next++] = bits(p.y);
            };
            auto append_projection = [&](PageFoldProjection p) {
                append_point(p.on_fold); append_point(p.mirrored); shape_actual[next++] = bits(p.distance);
            };
            append_point(shape.center); append_point(shape.normal);
            for (auto corner : shape.corners) append_point(corner);
            for (auto corner : shape.projected) append_projection(corner);
            append_point(shape.far_corner); append_projection(shape.maximum); append_projection(shape.minimum);
            append_point(shape.line_start); append_point(shape.line_end); append_point(shape.aspect);
            shape_actual[next++] = shape.outside;
            if (next != shape_actual.size()) throw std::runtime_error("Invalid native fold layout");
            for (unsigned lane = 0; lane < next; ++lane) {
                const auto expected = word();
                if (shape_actual[lane] != expected) {
                    std::cerr << "case=" << i << " shape lane=" << lane << std::hex
                        << " expected=" << expected << " actual=" << shape_actual[lane] << '\n';
                    throw std::runtime_error("Original fold shape differs");
                }
            }
        }
        if (input.peek() != std::char_traits<char>::eof()) throw std::runtime_error("Trailing page-turn corpus data");
        std::cout << "PASS " << count << " bit-exact original PageTurn controls and fold-shape records\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
