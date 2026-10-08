#pragma once

#include <ngs/state.h>
#include <string>

namespace emucorev::savestate {
class MemoryImage;
struct NgsSnapshot {
    Address definitions = 0;
    std::vector<ngs::SystemInitInfo> systems;
    std::vector<ngs::RackInitInfo> racks;
};

// Structural decoding is independent of the live guest and host audio objects.
// The output is published only after the entire section has been decoded.
bool parse_ngs_state(const std::vector<uint8_t> &data, NgsSnapshot &snapshot, std::string &error);
// Validate the complete graph against staged bytes before changing live state.
bool validate_ngs_state(const NgsSnapshot &snapshot, const MemoryImage &memory, std::string &error);
} // namespace emucorev::savestate
