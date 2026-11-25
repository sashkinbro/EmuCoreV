#include <emucorev/paf/bubble_reference.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>

// Usage: paf_bubble_reference_test [RECORDED_BUBBLE_DIRECTORY]
// Without a directory only the structure is checked. With the (git-ignored) local
// capture directory the indices and uniform blocks must match exactly and the
// vertex stream to within 5e-6 per component.
namespace {
std::vector<char> slurp(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
int fail(const char* message) { std::cerr << "FAIL: " << message << '\n'; return 1; }
}  // namespace

int main(int argc, char** argv) {
    const auto mesh = emucorev::paf::bubble_reference();
    if (mesh.vertices.size() != 608 || mesh.indices.size() != 1220) return fail("mesh size");
    for (const auto index : mesh.indices) if (index >= 608) return fail("index range");
    if (emucorev::paf::bubble_environment_map().size() != 64 * 64 * 4 ||
        emucorev::paf::bubble_placeholder_icon().size() != 128 * 128 * 4) return fail("placeholder sizes");
    if (argc < 2) { std::cout << "structure ok\n"; return 0; }
    const std::string dir = argv[1];
    const auto stream = slurp(dir + "/stream-0.bin"), indices = slurp(dir + "/indices.bin"),
        vertex = slurp(dir + "/vertex-ubo-0.bin"), fragment = slurp(dir + "/fragment-ubo-0.bin");
    if (stream.size() != 608 * 32 || indices.size() != 1220 * 2 || vertex.size() != 96 * 4 || fragment.size() != 40)
        return fail("recorded file sizes");
    if (std::memcmp(indices.data(), mesh.indices.data(), indices.size()) != 0) return fail("indices differ");
    if (std::memcmp(vertex.data(), mesh.vertex_uniforms.data(), vertex.size()) != 0) return fail("vertex uniforms differ");
    if (std::memcmp(fragment.data(), mesh.fragment_uniforms.data(), fragment.size()) != 0) return fail("fragment uniforms differ");
    double worst = 0;
    for (std::size_t v = 0; v < 608; ++v) {
        float recorded[8];
        std::memcpy(recorded, stream.data() + v * 32, 32);
        const float* mine = reinterpret_cast<const float*>(&mesh.vertices[v]);
        for (int c = 0; c < 8; ++c) worst = std::max(worst, double(std::fabs(recorded[c] - mine[c])));
    }
    std::cout << "max vertex deviation " << worst << '\n';
    return worst <= 5e-6 ? 0 : fail("vertex deviation too large");
}
