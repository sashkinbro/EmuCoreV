#include <renderer/command_snapshot.h>
#include <renderer/functions.h>
#include <renderer/types.h>
#include <gxm/functions.h>

#include <algorithm>
#include <cstring>
#include <memory>

namespace renderer {
namespace {
template<class T> T read(const uint8_t *data, size_t offset) {
    T result{};
    std::memcpy(&result, data + offset, sizeof(T));
    return result;
}

template<class T> void write(uint8_t *data, size_t offset, const T &value) {
    std::memcpy(data + offset, &value, sizeof(T));
}

bool boolean_byte(const uint8_t *data, size_t offset) {
    return data[offset] <= 1;
}

bool is_generic_opcode(CommandOpcode opcode) {
    switch (opcode) {
    case CommandOpcode::TransferCopy:
    case CommandOpcode::TransferDownscale:
    case CommandOpcode::TransferFill:
    case CommandOpcode::NewFrame:
    case CommandOpcode::SetScreenFilter:
        return true;
    default:
        return false;
    }
}

bool has_empty_generic_fields(const CommandSnapshot &s) {
    const TransferImageSnapshot empty_image{};
    const DisplayFrameSnapshot empty_frame{};
    return s.transfer_color_key_value == 0 && s.transfer_color_key_mask == 0
        && s.transfer_color_key_mode == 0 && s.transfer_src_type == 0 && s.transfer_dst_type == 0
        && s.transfer_fill_color == 0 && std::memcmp(&s.transfer_src, &empty_image, sizeof(empty_image)) == 0
        && std::memcmp(&s.transfer_dst, &empty_image, sizeof(empty_image)) == 0
        && !s.has_display_frame && std::memcmp(&s.display_frame, &empty_frame, sizeof(empty_frame)) == 0
        && s.new_frame_context_address == 0 && s.screen_filter.empty();
}

bool valid_transfer_format(uint32_t format) {
    switch (static_cast<SceGxmTransferFormat>(format)) {
    case SCE_GXM_TRANSFER_FORMAT_U8_R:
    case SCE_GXM_TRANSFER_FORMAT_U4U4U4U4_ABGR:
    case SCE_GXM_TRANSFER_FORMAT_U1U5U5U5_ABGR:
    case SCE_GXM_TRANSFER_FORMAT_U5U6U5_BGR:
    case SCE_GXM_TRANSFER_FORMAT_U8U8_GR:
    case SCE_GXM_TRANSFER_FORMAT_U8U8U8_BGR:
    case SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR:
    case SCE_GXM_TRANSFER_FORMAT_VYUY422:
    case SCE_GXM_TRANSFER_FORMAT_YVYU422:
    case SCE_GXM_TRANSFER_FORMAT_UYVY422:
    case SCE_GXM_TRANSFER_FORMAT_YUYV422:
    case SCE_GXM_TRANSFER_FORMAT_U2U10U10U10_ABGR:
    case SCE_GXM_TRANSFER_FORMAT_RAW16:
    case SCE_GXM_TRANSFER_FORMAT_RAW32:
    case SCE_GXM_TRANSFER_FORMAT_RAW64:
    case SCE_GXM_TRANSFER_FORMAT_RAW128:
        return true;
    default:
        return false;
    }
}

bool valid_transfer_type(uint32_t type) {
    return type == SCE_GXM_TRANSFER_LINEAR || type == SCE_GXM_TRANSFER_TILED
        || type == SCE_GXM_TRANSFER_SWIZZLED;
}

bool valid_transfer_image(const TransferImageSnapshot &image, std::string &error) {
    const uint32_t bits = gxm::get_bits_per_pixel(static_cast<SceGxmTransferFormat>(image.format));
    const uint64_t right = uint64_t(image.x) + image.width;
    const uint64_t bottom = uint64_t(image.y) + image.height;
    if (!valid_transfer_format(image.format) || !image.address || !image.width || !image.height
        || image.width > 32768 || image.height > 32768 || image.stride <= 0 || !bits || bits % 8
        || right > UINT32_MAX || bottom > UINT32_MAX) {
        error = "invalid transfer image dimensions or format";
        return false;
    }
    return true;
}

TransferImageSnapshot capture_image(const SceGxmTransferImage &image) {
    return { static_cast<uint32_t>(image.format), image.address.address(), image.x, image.y,
        image.width, image.height, image.stride };
}

SceGxmTransferImage restore_image(const TransferImageSnapshot &image) {
    SceGxmTransferImage result{};
    result.format = static_cast<SceGxmTransferFormat>(image.format);
    result.address = Ptr<void>(image.address);
    result.x = image.x;
    result.y = image.y;
    result.width = image.width;
    result.height = image.height;
    result.stride = image.stride;
    return result;
}

bool schema(const CommandSnapshot &s, size_t &size, std::string &error) {
    const auto boolean = [&](size_t offset) { return boolean_byte(s.data.data(), offset); };
    bool valid = true;
    switch (s.opcode) {
    case CommandOpcode::Nop: size = sizeof(int32_t); break;
    case CommandOpcode::Draw: size = 20; break;
    case CommandOpcode::SyncSurfaceData: size = 2 * sizeof(SceGxmNotification); break;
    case CommandOpcode::MidSceneFlush:
    case CommandOpcode::SignalNotification: size = sizeof(SceGxmNotification); break;
    case CommandOpcode::SignalSyncObject:
    case CommandOpcode::WaitSyncObject: size = 8; break;
    case CommandOpcode::SetContext: size = 0; break;
    case CommandOpcode::SetState: {
        const auto state = read<GXMState>(s.data.data(), 0);
        constexpr size_t h = sizeof(GXMState);
        switch (state) {
        case GXMState::RegionClip: size = h + 20; break;
        case GXMState::Program: size = h + 5; valid = boolean(h + 4); break;
        case GXMState::Viewport:
            valid = boolean(h); size = h + 1 + (s.data[h] == 0 ? 24 : 0); break;
        case GXMState::DepthBias: size = h + 9; valid = boolean(h); break;
        case GXMState::DepthFunc:
        case GXMState::DepthWriteEnable:
        case GXMState::PolygonMode:
        case GXMState::PointLineWidth:
        case GXMState::FragmentProgramEnable: size = h + 5; valid = boolean(h); break;
        case GXMState::StencilFunc: size = h + 19; valid = boolean(h); break;
        case GXMState::Texture:
            size = h + 4 + sizeof(SceGxmTexture);
            valid = read<uint32_t>(s.data.data(), h) < SCE_GXM_MAX_TEXTURE_UNITS * 2; break;
        case GXMState::StencilRef: size = h + 2; valid = boolean(h); break;
        case GXMState::VertexStream:
            size = h + 4 + 2 * sizeof(size_t);
            valid = read<size_t>(s.data.data(), h + 4) < SCE_GXM_MAX_VERTEX_STREAMS; break;
        case GXMState::TwoSided:
        case GXMState::CullMode: size = h + 4; break;
        case GXMState::UniformBuffer:
            size = h + 13; valid = boolean(h + 4)
                && read<int32_t>(s.data.data(), h + 5) >= 0
                && read<int32_t>(s.data.data(), h + 5) < SCE_GXM_REAL_MAX_UNIFORM_BUFFER; break;
        case GXMState::VisibilityBuffer: size = h + 8; break;
        case GXMState::VisibilityIndex: size = h + 6; valid = boolean(h + 4) && boolean(h + 5); break;
        default: valid = false; size = 0; break;
        }
        break;
    }
    default:
        if (is_generic_opcode(s.opcode)) {
            size = 0;
            break;
        }
        error = "unsupported pending renderer command opcode";
        return false;
    }
    if (!valid || size > s.data.size()) {
        error = "invalid pending renderer command payload";
        return false;
    }
    return true;
}

bool validate_generic_fields(const CommandSnapshot &s, std::string &error) {
    const auto bad = [&] { error = "invalid generic renderer command snapshot"; return false; };
    switch (s.opcode) {
    case CommandOpcode::TransferCopy:
        return valid_transfer_image(s.transfer_src, error) && valid_transfer_image(s.transfer_dst, error)
            && s.transfer_src.format == s.transfer_dst.format
            && s.transfer_src.width == s.transfer_dst.width && s.transfer_src.height == s.transfer_dst.height
            && s.transfer_color_key_mode <= SCE_GXM_TRANSFER_COLORKEY_REJECT
            && valid_transfer_type(s.transfer_src_type) && valid_transfer_type(s.transfer_dst_type)
            && !(s.transfer_src_type == SCE_GXM_TRANSFER_TILED && s.transfer_dst_type == SCE_GXM_TRANSFER_SWIZZLED)
            && !(s.transfer_src_type == SCE_GXM_TRANSFER_SWIZZLED && s.transfer_dst_type == SCE_GXM_TRANSFER_TILED)
            && !(s.transfer_src_type == SCE_GXM_TRANSFER_SWIZZLED && s.transfer_dst_type == SCE_GXM_TRANSFER_SWIZZLED)
            && s.transfer_fill_color == 0 && !s.has_display_frame && s.new_frame_context_address == 0
            && s.screen_filter.empty();
    case CommandOpcode::TransferDownscale:
        return valid_transfer_image(s.transfer_src, error) && valid_transfer_image(s.transfer_dst, error)
            && s.transfer_src.format == s.transfer_dst.format
            && s.transfer_dst.width == s.transfer_src.width / 2
            && s.transfer_dst.height == s.transfer_src.height / 2
            && s.transfer_color_key_value == 0 && s.transfer_color_key_mask == 0 && s.transfer_color_key_mode == 0
            && s.transfer_src_type == 0 && s.transfer_dst_type == 0 && s.transfer_fill_color == 0
            && !s.has_display_frame && s.new_frame_context_address == 0 && s.screen_filter.empty();
    case CommandOpcode::TransferFill: {
        const TransferImageSnapshot empty{};
        return valid_transfer_image(s.transfer_dst, error)
            && s.transfer_color_key_value == 0 && s.transfer_color_key_mask == 0 && s.transfer_color_key_mode == 0
            && s.transfer_src_type == 0 && s.transfer_dst_type == 0
            && std::memcmp(&s.transfer_src, &empty, sizeof(empty)) == 0
            && !s.has_display_frame && s.new_frame_context_address == 0 && s.screen_filter.empty();
    }
    case CommandOpcode::NewFrame: {
        const TransferImageSnapshot empty_image{};
        const DisplayFrameSnapshot empty_frame{};
        if (s.transfer_color_key_value || s.transfer_color_key_mask || s.transfer_color_key_mode
            || s.transfer_src_type || s.transfer_dst_type || s.transfer_fill_color
            || std::memcmp(&s.transfer_src, &empty_image, sizeof(empty_image))
            || std::memcmp(&s.transfer_dst, &empty_image, sizeof(empty_image)) || !s.screen_filter.empty())
            return bad();
        if (!s.has_display_frame && std::memcmp(&s.display_frame, &empty_frame, sizeof(s.display_frame)))
            return bad();
        if (s.has_display_frame && (s.display_frame.width < 0 || s.display_frame.height < 0
            || s.display_frame.pixelformat != SCE_DISPLAY_PIXELFORMAT_A8B8G8R8
            || ((s.display_frame.width || s.display_frame.height) && (!s.display_frame.width
                || !s.display_frame.height || !s.display_frame.base
                || s.display_frame.pitch < static_cast<uint32_t>(s.display_frame.width)))))
            return bad();
        return true;
    }
    case CommandOpcode::SetScreenFilter:
        {
        const TransferImageSnapshot empty{};
        return s.screen_filter.size() <= 4096 && s.transfer_color_key_value == 0
            && s.transfer_color_key_mask == 0 && s.transfer_color_key_mode == 0
            && s.transfer_src_type == 0 && s.transfer_dst_type == 0 && s.transfer_fill_color == 0
            && std::memcmp(&s.transfer_src, &empty, sizeof(s.transfer_src)) == 0
            && std::memcmp(&s.transfer_dst, &empty, sizeof(s.transfer_dst)) == 0
            && !s.has_display_frame && s.new_frame_context_address == 0;
        }
    default:
        return has_empty_generic_fields(s);
    }
}

bool add_image_range(const TransferImageSnapshot &image, uint32_t type,
    std::vector<CommandGuestRange> &ranges, std::string &error) {
    if (!valid_transfer_image(image, error) || !valid_transfer_type(type)) {
        if (error.empty()) error = "invalid transfer image layout";
        return false;
    }
    const uint64_t bytes_per_pixel = gxm::get_bits_per_pixel(static_cast<SceGxmTransferFormat>(image.format)) / 8;
    const uint64_t right = uint64_t(image.x) + image.width;
    const uint64_t bottom = uint64_t(image.y) + image.height;
    uint64_t size = 0;
    if (type == SCE_GXM_TRANSFER_LINEAR) {
        const uint64_t row_end = right * bytes_per_pixel;
        if (row_end > static_cast<uint32_t>(image.stride)) {
            error = "linear transfer exceeds its row stride";
            return false;
        }
        size = (bottom - 1) * static_cast<uint32_t>(image.stride) + row_end;
    } else if (type == SCE_GXM_TRANSFER_TILED) {
        const uint64_t pixel_stride = static_cast<uint32_t>(image.stride) / bytes_per_pixel;
        if (static_cast<uint64_t>(image.stride) % bytes_per_pixel || pixel_stride % 32) {
            error = "invalid tiled transfer stride";
            return false;
        }
        const uint64_t tile_columns = pixel_stride / 32;
        if (!tile_columns || right > tile_columns * 32) {
            error = "tiled transfer exceeds its row stride";
            return false;
        }
        const uint64_t tile_index = ((bottom - 1) / 32) * tile_columns + ((right - 1) / 32);
        size = (tile_index + 1) * 1024 * bytes_per_pixel;
    } else {
        const auto power_of_two = [](uint32_t v) { return v && (v & (v - 1)) == 0; };
        if (image.x || image.y || !power_of_two(image.width) || !power_of_two(image.height)) {
            error = "unsupported swizzled transfer bounds";
            return false;
        }
        size = uint64_t(image.width) * image.height * bytes_per_pixel;
    }
    if (!size || uint64_t(image.address) + size > uint64_t(UINT32_MAX) + 1) {
        error = "transfer guest range overflows address space";
        return false;
    }
    ranges.push_back({ image.address, size });
    return true;
}
}

bool validate_command_snapshot(const CommandSnapshot &s, std::string &error) {
    if ((s.opcode == CommandOpcode::Nop) != (s.completion_id != 0)
        || (s.completion_id && (s.deferred_allocation || read<int32_t>(s.data.data(), 0) != 1))) {
        error = "invalid durable renderer completion association";
        return false;
    }
    size_t size = 0;
    if (!schema(s, size, error)) return false;
    if (std::any_of(s.data.begin() + size, s.data.end(), [](uint8_t b) { return b != 0; })) {
        error = "noncanonical pending renderer command payload";
        return false;
    }
    if (s.opcode != CommandOpcode::SetContext && (s.render_target_address || s.has_color_surface || s.has_depth_surface)) {
        error = "unexpected pending GXM surface payload";
        return false;
    }
    if (s.opcode == CommandOpcode::SetContext && (!s.render_target_address || s.deferred_allocation)) {
        error = "invalid pending GXM render target association";
        return false;
    }
    if (is_generic_opcode(s.opcode)) {
        if (s.deferred_allocation) { error = "generic command has deferred allocation"; return false; }
        return validate_generic_fields(s, error);
    }
    if (!has_empty_generic_fields(s)) {
        error = "unexpected generic command payload";
        return false;
    }
    return true;
}

bool capture_command_snapshot(const Command &cmd, const SnapshotTargetAddress &target_address,
    const SnapshotContextAddress &context_address, const DisplayState *expected_display,
    CommandSnapshot &out, std::string &error) {
    if (!is_generic_opcode(cmd.opcode))
        return capture_command_snapshot(cmd, target_address, out, error);
    if (cmd.status || cmd.completion_id || (cmd.flags & ~(Command::FLAG_FROM_HOST | Command::FLAG_NO_FREE))) {
        error = "pending generic command has completion state or invalid allocation";
        return false;
    }
    if (cmd.flags & Command::FLAG_NO_FREE) {
        error = "generic command cannot use deferred allocation";
        return false;
    }
    CommandHelper helper(const_cast<Command *>(&cmd));
    CommandSnapshot staged;
    staged.opcode = cmd.opcode;
    switch (cmd.opcode) {
    case CommandOpcode::TransferCopy: {
        staged.transfer_color_key_value = helper.pop<uint32_t>();
        staged.transfer_color_key_mask = helper.pop<uint32_t>();
        staged.transfer_color_key_mode = helper.pop<SceGxmTransferColorKeyMode>();
        const auto *images = helper.pop<const SceGxmTransferImage *>();
        staged.transfer_src_type = helper.pop<SceGxmTransferType>();
        staged.transfer_dst_type = helper.pop<SceGxmTransferType>();
        if (!images) { error = "missing transfer copy images"; return false; }
        staged.transfer_src = capture_image(images[0]);
        staged.transfer_dst = capture_image(images[1]);
        break;
    }
    case CommandOpcode::TransferDownscale: {
        const auto *src = helper.pop<SceGxmTransferImage *>();
        const auto *dst = helper.pop<SceGxmTransferImage *>();
        if (!src || !dst) { error = "missing transfer downscale images"; return false; }
        staged.transfer_src = capture_image(*src);
        staged.transfer_dst = capture_image(*dst);
        break;
    }
    case CommandOpcode::TransferFill: {
        staged.transfer_fill_color = helper.pop<uint32_t>();
        const auto *dst = helper.pop<const SceGxmTransferImage *>();
        if (!dst) { error = "missing transfer fill image"; return false; }
        staged.transfer_dst = capture_image(*dst);
        break;
    }
    case CommandOpcode::NewFrame: {
        const auto *frame = helper.pop<const DisplayFrameInfo *>();
        const auto *display = helper.pop<DisplayState *>();
        const auto *context = helper.pop<Context *>();
        if (!expected_display || display != expected_display) {
            error = "NewFrame references an unknown display state";
            return false;
        }
        if (frame) {
            staged.has_display_frame = true;
            staged.display_frame = { frame->base.address(), frame->pitch, frame->pixelformat,
                frame->image_size.x, frame->image_size.y };
        }
        if (context) {
            staged.new_frame_context_address = context_address ? context_address(context) : 0;
            if (!staged.new_frame_context_address) { error = "NewFrame references an unknown renderer context"; return false; }
        }
        break;
    }
    case CommandOpcode::SetScreenFilter: {
        const auto *filter = helper.pop<const std::string *>();
        if (!filter || filter->size() > 4096) { error = "invalid pending screen filter"; return false; }
        staged.screen_filter = *filter;
        break;
    }
    default:
        error = "unsupported generic renderer command opcode";
        return false;
    }
    if (!validate_command_snapshot(staged, error)) return false;
    out = std::move(staged);
    return true;
}

bool capture_command_snapshot(const Command &cmd, const SnapshotTargetAddress &target_address,
    CommandSnapshot &out, std::string &error) {
    if (is_generic_opcode(cmd.opcode)) {
        error = "generic command snapshot requires display and context resolvers";
        return false;
    }
    if (cmd.status || (cmd.flags & ~(Command::FLAG_FROM_HOST | Command::FLAG_NO_FREE))) {
        error = "pending GXM command has an external completion or invalid allocation";
        return false;
    }
    CommandSnapshot s;
    s.opcode = cmd.opcode;
    s.completion_id = cmd.completion_id;
    s.deferred_allocation = (cmd.flags & Command::FLAG_NO_FREE) != 0;
    if (cmd.opcode == CommandOpcode::SetContext) {
        const auto *target = read<RenderTarget *>(cmd.data, 0);
        const auto *color = read<SceGxmColorSurface *>(cmd.data, sizeof(void *));
        const auto *depth = read<SceGxmDepthStencilSurface *>(cmd.data, 2 * sizeof(void *));
        s.render_target_address = target_address ? target_address(target) : 0;
        s.has_color_surface = color != nullptr;
        s.has_depth_surface = depth != nullptr;
        if (color) s.color_surface = *color;
        if (depth) s.depth_surface = *depth;
    } else {
        std::memcpy(s.data.data(), cmd.data, s.data.size());
        size_t size = 0;
        if (!schema(s, size, error)) return false;
        std::fill(s.data.begin() + size, s.data.end(), 0);
    }
    if (!validate_command_snapshot(s, error)) return false;
    out = std::move(s);
    return true;
}

bool restore_command_snapshot(const CommandSnapshot &s, const SnapshotTargetPointer &target_pointer,
    const SnapshotContextPointer &context_pointer, DisplayState *display, Command &out, std::string &error) {
    if (!validate_command_snapshot(s, error)) return false;
    Command staged{};
    staged.opcode = s.opcode;
    staged.completion_id = s.completion_id;
    if (!is_generic_opcode(s.opcode)) {
        staged.flags = s.deferred_allocation ? Command::FLAG_NO_FREE : Command::FLAG_FROM_HOST;
        std::memcpy(staged.data, s.data.data(), s.data.size());
        if (s.opcode == CommandOpcode::SetContext) {
            auto *target = target_pointer ? target_pointer(s.render_target_address) : nullptr;
            if (!target) { error = "pending GXM render target is missing"; return false; }
            auto color = s.has_color_surface ? std::make_unique<SceGxmColorSurface>(s.color_surface) : nullptr;
            auto depth = s.has_depth_surface ? std::make_unique<SceGxmDepthStencilSurface>(s.depth_surface) : nullptr;
            auto *c = color.get(); auto *d = depth.get();
            std::memcpy(staged.data, &target, sizeof(target));
            std::memcpy(staged.data + sizeof(target), &c, sizeof(c));
            std::memcpy(staged.data + sizeof(target) + sizeof(c), &d, sizeof(d));
            color.release(); depth.release();
        }
        out = staged;
        return true;
    }
    staged.flags = Command::FLAG_FROM_HOST;

    CommandHelper helper(&staged);
    std::unique_ptr<SceGxmTransferImage[]> copy_images;
    std::unique_ptr<SceGxmTransferImage> src_image, dst_image;
    std::unique_ptr<DisplayFrameInfo> frame;
    std::unique_ptr<std::string> filter;
    switch (s.opcode) {
    case CommandOpcode::TransferCopy: {
        copy_images = std::make_unique<SceGxmTransferImage[]>(2);
        copy_images[0] = restore_image(s.transfer_src);
        copy_images[1] = restore_image(s.transfer_dst);
        uint32_t key_value = s.transfer_color_key_value;
        uint32_t key_mask = s.transfer_color_key_mask;
        auto key_mode = static_cast<SceGxmTransferColorKeyMode>(s.transfer_color_key_mode);
        const SceGxmTransferImage *images = copy_images.get();
        auto src_type = static_cast<SceGxmTransferType>(s.transfer_src_type);
        auto dst_type = static_cast<SceGxmTransferType>(s.transfer_dst_type);
        if (!helper.push(key_value) || !helper.push(key_mask)
            || !helper.push(key_mode) || !helper.push(images)
            || !helper.push(src_type) || !helper.push(dst_type)) {
            error = "transfer copy command exceeds ABI payload"; return false;
        }
        break;
    }
    case CommandOpcode::TransferDownscale:
        src_image = std::make_unique<SceGxmTransferImage>(restore_image(s.transfer_src));
        dst_image = std::make_unique<SceGxmTransferImage>(restore_image(s.transfer_dst));
        { auto *src = src_image.get(); auto *dst = dst_image.get();
        if (!helper.push(src) || !helper.push(dst)) {
            error = "transfer downscale command exceeds ABI payload"; return false;
        } }
        break;
    case CommandOpcode::TransferFill:
        dst_image = std::make_unique<SceGxmTransferImage>(restore_image(s.transfer_dst));
        { uint32_t fill_color = s.transfer_fill_color;
        const SceGxmTransferImage *dst = dst_image.get();
        if (!helper.push(fill_color) || !helper.push(dst)) {
            error = "transfer fill command exceeds ABI payload"; return false;
        } }
        break;
    case CommandOpcode::NewFrame: {
        if (!display) { error = "missing display state for NewFrame restore"; return false; }
        if (s.has_display_frame) {
            frame = std::make_unique<DisplayFrameInfo>();
            frame->base = Ptr<const void>(s.display_frame.base);
            frame->pitch = s.display_frame.pitch;
            frame->pixelformat = s.display_frame.pixelformat;
            frame->image_size = { s.display_frame.width, s.display_frame.height };
        }
        Context *context = nullptr;
        if (s.new_frame_context_address) {
            context = context_pointer ? context_pointer(s.new_frame_context_address) : nullptr;
            if (!context) { error = "NewFrame renderer context is missing"; return false; }
        }
        { auto *next_frame = frame.get();
        if (!helper.push(next_frame) || !helper.push(display) || !helper.push(context)) {
            error = "NewFrame command exceeds ABI payload"; return false;
        } }
        break;
    }
    case CommandOpcode::SetScreenFilter:
        filter = std::make_unique<std::string>(s.screen_filter);
        { auto *filter_ptr = filter.get();
        if (!helper.push(filter_ptr)) {
            error = "screen filter command exceeds ABI payload"; return false;
        } }
        break;
    default:
        error = "unsupported generic renderer command opcode";
        return false;
    }
    out = staged;
    copy_images.release(); src_image.release(); dst_image.release(); frame.release(); filter.release();
    return true;
}

bool restore_command_snapshot(const CommandSnapshot &s, const SnapshotTargetPointer &target_pointer,
    Command &out, std::string &error) {
    return restore_command_snapshot(s, target_pointer, {}, nullptr, out, error);
}

void destroy_command_payload(Command &cmd) {
    CommandHelper helper(&cmd);
    bool released = false;
    switch (cmd.opcode) {
    case CommandOpcode::SetContext:
        (void)helper.pop<RenderTarget *>();
        delete helper.pop<SceGxmColorSurface *>();
        delete helper.pop<SceGxmDepthStencilSurface *>();
        released = true;
        break;
    case CommandOpcode::TransferCopy:
        (void)helper.pop<uint32_t>(); (void)helper.pop<uint32_t>(); (void)helper.pop<SceGxmTransferColorKeyMode>();
        delete[] helper.pop<const SceGxmTransferImage *>();
        released = true;
        break;
    case CommandOpcode::TransferDownscale: {
        auto *src = helper.pop<SceGxmTransferImage *>();
        auto *dst = helper.pop<SceGxmTransferImage *>();
        delete src;
        if (dst != src) delete dst;
        released = true;
        break;
    }
    case CommandOpcode::TransferFill:
        (void)helper.pop<uint32_t>(); delete helper.pop<const SceGxmTransferImage *>();
        released = true;
        break;
    case CommandOpcode::NewFrame:
        delete helper.pop<DisplayFrameInfo *>();
        released = true;
        break;
    case CommandOpcode::SetScreenFilter:
        delete helper.pop<std::string *>();
        released = true;
        break;
    default:
        break;
    }
    if (released)
        std::memset(cmd.data, 0, sizeof(cmd.data));
}

bool command_snapshot_guest_ranges(const CommandSnapshot &s, std::vector<CommandGuestRange> &ranges,
    std::string &error) {
    if (!validate_command_snapshot(s, error)) return false;
    std::vector<CommandGuestRange> staged;
    switch (s.opcode) {
    case CommandOpcode::TransferCopy:
        if (!add_image_range(s.transfer_src, s.transfer_src_type, staged, error)
            || !add_image_range(s.transfer_dst, s.transfer_dst_type, staged, error)) return false;
        break;
    case CommandOpcode::TransferDownscale:
        if (!add_image_range(s.transfer_src, SCE_GXM_TRANSFER_LINEAR, staged, error)
            || !add_image_range(s.transfer_dst, SCE_GXM_TRANSFER_LINEAR, staged, error)) return false;
        break;
    case CommandOpcode::TransferFill:
        if (!add_image_range(s.transfer_dst, SCE_GXM_TRANSFER_LINEAR, staged, error)) return false;
        break;
    case CommandOpcode::NewFrame:
        if (s.has_display_frame && s.display_frame.base && s.display_frame.width > 0 && s.display_frame.height > 0) {
            const uint64_t size = uint64_t(s.display_frame.pitch) * s.display_frame.height * 4;
            if (!size || uint64_t(s.display_frame.base) + size > uint64_t(UINT32_MAX) + 1) {
                error = "display frame guest range overflows address space";
                return false;
            }
            staged.push_back({ s.display_frame.base, size });
        }
        break;
    default:
        break;
    }
    ranges = std::move(staged);
    return true;
}
}
