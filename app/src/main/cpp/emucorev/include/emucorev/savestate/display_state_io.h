#pragma once
#include <display/state.h>
#include <string>

namespace emucorev::savestate {
class MemoryImage;
struct DisplaySnapshot {
    int32_t viewport_drawable_w = 0;
    int32_t viewport_drawable_h = 0;
    float viewport_x = 0;
    float viewport_y = 0;
    float viewport_w = 0;
    float viewport_h = 0;
    DisplayFrameInfo sce_frame;
    DisplayFrameInfo next_rendered_frame;
    uint64_t vblank_count = 0;
    uint64_t last_setframe_vblank_count = 0;
    bool fps_hack = false;
    std::vector<SceUID> callbacks;
};
bool parse_display_state(const std::vector<uint8_t> &data, DisplaySnapshot &snapshot, std::string &error);
bool validate_display_state(const DisplaySnapshot &snapshot, const MemoryImage &memory, std::string &error);
} // namespace emucorev::savestate
