#pragma once
#include <renderer/commands.h>
#include <gxm/types.h>
#include <renderer/gxm_types.h>
#include <array>
#include <display/state.h>
#include <string>
#include <vector>

namespace renderer {
struct RenderTarget;
struct Context;
// Only defined bytes are retained. Host pointers, completion pointers and
// allocator flags are reconstructed; next is a one-based graph node ID.
struct TransferImageSnapshot {
    uint32_t format = 0;
    Address address = 0;
    uint32_t x = 0, y = 0, width = 0, height = 0;
    int32_t stride = 0;
};
struct DisplayFrameSnapshot {
    Address base = 0;
    uint32_t pitch = 0;
    uint32_t pixelformat = 0;
    int32_t width = 0, height = 0;
};
struct CommandGuestRange {
    Address address = 0;
    uint64_t size = 0;
};
struct CommandSnapshot {
    CommandOpcode opcode{};
    bool deferred_allocation = false;
    uint32_t next = 0;
    uint64_t completion_id = 0;
    std::array<uint8_t, MAX_COMMAND_DATA_SIZE> data{};
    Address render_target_address = 0;
    bool has_color_surface = false, has_depth_surface = false;
    SceGxmColorSurface color_surface{};
    SceGxmDepthStencilSurface depth_surface{};

    uint32_t transfer_color_key_value = 0;
    uint32_t transfer_color_key_mask = 0;
    uint32_t transfer_color_key_mode = 0;
    uint32_t transfer_src_type = 0;
    uint32_t transfer_dst_type = 0;
    uint32_t transfer_fill_color = 0;
    TransferImageSnapshot transfer_src;
    TransferImageSnapshot transfer_dst;

    bool has_display_frame = false;
    DisplayFrameSnapshot display_frame;
    Address new_frame_context_address = 0;
    std::string screen_filter;
};
using SnapshotTargetAddress = std::function<Address(const RenderTarget *)>;
using SnapshotTargetPointer = std::function<RenderTarget *(Address)>;
using SnapshotContextAddress = std::function<Address(const Context *)>;
using SnapshotContextPointer = std::function<Context *(Address)>;
bool validate_command_snapshot(const CommandSnapshot &, std::string &);
bool capture_command_snapshot(const Command &, const SnapshotTargetAddress &, CommandSnapshot &, std::string &);
bool capture_command_snapshot(const Command &, const SnapshotTargetAddress &, const SnapshotContextAddress &,
    const DisplayState *, CommandSnapshot &, std::string &);
// On failure no payload ownership is transferred to output.
bool restore_command_snapshot(const CommandSnapshot &, const SnapshotTargetPointer &, Command &, std::string &);
bool restore_command_snapshot(const CommandSnapshot &, const SnapshotTargetPointer &, const SnapshotContextPointer &,
    DisplayState *, Command &, std::string &);
void destroy_command_payload(Command &);
bool command_snapshot_guest_ranges(const CommandSnapshot &, std::vector<CommandGuestRange> &, std::string &);
}
