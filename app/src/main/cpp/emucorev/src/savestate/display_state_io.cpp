#include <emucorev/savestate/display_state_io.h>
#include <emucorev/savestate/memory_image.h>
#include <emucorev/savestate/state_io.h>
#include <cmath>
#include <set>

namespace emucorev::savestate {
bool parse_display_state(const std::vector<uint8_t> &data, DisplaySnapshot &snapshot, std::string &error) {
    error.clear();
    BufferReader reader(data.data(), data.size());
    DisplaySnapshot staged;
    staged.viewport_drawable_w = reader.i32();
    staged.viewport_drawable_h = reader.i32();
    staged.viewport_x = reader.value<float>();
    staged.viewport_y = reader.value<float>();
    staged.viewport_w = reader.value<float>();
    staged.viewport_h = reader.value<float>();
    staged.sce_frame = reader.value<DisplayFrameInfo>();
    staged.next_rendered_frame = reader.value<DisplayFrameInfo>();
    staged.vblank_count = reader.u64();
    staged.last_setframe_vblank_count = reader.u64();
    staged.fps_hack = reader.boolean();
    const uint32_t count = reader.u32();
    if (!reader.ok() || count > 65536 || count > reader.remaining() / 4) {
        error = "invalid display callback count";
        return false;
    }
    std::set<SceUID> identities;
    staged.callbacks.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const auto uid = reader.i32();
        if (uid <= 0 || !identities.insert(uid).second) {
            error = "invalid display callback UID";
            return false;
        }
        staged.callbacks.push_back(uid);
    }
    if (!reader.ok() || reader.remaining()) {
        error = "invalid display section";
        return false;
    }
    snapshot = std::move(staged);
    return true;
}

bool validate_display_state(const DisplaySnapshot &s, const MemoryImage &memory, std::string &error) {
    const auto frame_valid = [&](const DisplayFrameInfo &frame) {
        if (frame.pixelformat != SCE_DISPLAY_PIXELFORMAT_A8B8G8R8 || frame.image_size.x < 0 || frame.image_size.y < 0)
            return false;
        if (!frame.base)
            return true;
        const uint32_t width = static_cast<uint32_t>(frame.image_size.x);
        const uint32_t height = static_cast<uint32_t>(frame.image_size.y);
        if (!width || !height || width > 65535 || height > 65535 || frame.pitch < width)
            return false;
        const uint64_t bytes = (uint64_t{frame.pitch} * (height - 1) + width) * 4;
        return memory.contains(frame.base.address(), bytes);
    };
    if (s.viewport_drawable_w < 0 || s.viewport_drawable_h < 0
        || !std::isfinite(s.viewport_x) || !std::isfinite(s.viewport_y)
        || !std::isfinite(s.viewport_w) || !std::isfinite(s.viewport_h)
        || s.viewport_w < 0 || s.viewport_h < 0 || !frame_valid(s.sce_frame) || !frame_valid(s.next_rendered_frame)) {
        error = "invalid display framebuffer or dimensions";
        return false;
    }
    return true;
}
} // namespace emucorev::savestate
