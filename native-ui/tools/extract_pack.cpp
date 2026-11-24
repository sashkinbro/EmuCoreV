#include <emucorev/extract/pack_extractor.h>

#include <iostream>

// Host/dev driver of the on-device extractor: extract_pack VITA_ROOT NEW_DIRECTORY.
// The application runs the same code through JNI after firmware installation.
int main(int argc, char** argv) {
    if (argc != 3) { std::cerr << "Usage: paf_extract_pack VITA_ROOT NEW_DIRECTORY\n"; return 2; }
    const auto result = emucorev::extract::extract_pack(argv[1], argv[2],
        [](int percent, const char* stage) { std::cerr << percent << "% " << stage << '\n'; });
    for (const auto& warning : result.warnings) std::cerr << "warning: " << warning << '\n';
    if (!result.ok) { std::cerr << "error: " << result.error << '\n'; return 1; }
    std::cout << "files " << result.files << '\n';
    return 0;
}
