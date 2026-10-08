#include <emucorev/savestate/ngs_state_io.h>
#include <emucorev/savestate/state_io.h>
#include <utility>

namespace emucorev::savestate {
static bool read_blob(BufferReader &reader, std::vector<uint8_t> &blob) {
    const uint32_t size = reader.u32();
    if (!reader.ok() || size > (64u << 20) || size > reader.remaining()) {
        blob.clear();
        return false;
    }
    blob.resize(size);
    if (size && !reader.bytes(blob.data(), size)) {
        blob.clear();
        return false;
    }
    return true;
}

bool parse_ngs_state(const std::vector<uint8_t> &data, NgsSnapshot &snapshot, std::string &error) {
    error.clear();
    BufferReader reader(data.data(), data.size());

    const Address definitions = reader.u32();
    const uint32_t system_count = reader.u32();
    if (!reader.ok() || system_count > 4096 || system_count > reader.remaining() / 36) {
        error = "invalid NGS system table";
        return false;
    }
    std::vector<ngs::SystemInitInfo> systems(system_count);
    for (ngs::SystemInitInfo &info : systems) {
        info.address = reader.u32();
        info.memspace = Ptr<void>(reader.u32());
        info.memspace_size = reader.u32();
        info.params = reader.value<SceNgsSystemInitParams>();
        const uint32_t queued_count = reader.u32();
        if (!reader.ok() || queued_count > 65536 || queued_count > reader.remaining() / 8) {
            error = "invalid NGS queue table";
            return false;
        }
        info.queued_voices.resize(queued_count);
        for (auto &entry : info.queued_voices) {
            entry.first = reader.u32();
            entry.second = reader.u32();
        }
    }

    const uint32_t rack_count = reader.u32();
    if (!reader.ok() || rack_count > 65536 || rack_count > reader.remaining() / 52) {
        error = "invalid NGS rack table";
        return false;
    }
    std::vector<ngs::RackInitInfo> racks(rack_count);
    for (ngs::RackInitInfo &info : racks) {
        info.address = reader.u32();
        info.system_address = reader.u32();
        info.system = Ptr<ngs::System>(reader.u32());
        info.info = reader.value<SceNgsBufferInfo>();
        info.description = reader.value<SceNgsRackDescription>();
        const uint32_t block_count = reader.u32();
        if (!reader.ok() || block_count > 65536 || block_count > reader.remaining() / 9) {
            error = "invalid NGS mempool table";
            return false;
        }
        info.blocks.resize(block_count);
        for (MemspaceBlockAllocator::Block &block : info.blocks) {
            block.size = reader.u32();
            block.offset = reader.u32();
            block.free = reader.boolean();
        }
        const uint32_t voice_count = reader.u32();
        if (!reader.ok() || voice_count > 4096 || voice_count > reader.remaining() / 47) {
            error = "invalid NGS voice table";
            return false;
        }
        info.voices.resize(voice_count);
        for (ngs::VoiceInfo &voice : info.voices) {
            voice.address = reader.u32();
            voice.state = reader.u32();
            voice.is_pending = reader.boolean();
            voice.is_paused = reader.boolean();
            voice.is_keyed_off = reader.boolean();
            voice.frame_count = reader.u32();
            if (!reader.bytes(voice.implicit_volume_matrix, sizeof(voice.implicit_volume_matrix))) {
                error = "invalid NGS voice data";
                return false;
            }
            voice.finished_callback = reader.u32();
            voice.finished_callback_user_data = reader.u32();
            const uint32_t port_count = reader.u32();
            if (!reader.ok() || port_count > 64 || port_count > reader.remaining() / 4) {
                error = "invalid NGS patch table";
                return false;
            }
            voice.patches.resize(port_count);
            for (std::vector<Address> &port : voice.patches) {
                const uint32_t patch_count = reader.u32();
                if (!reader.ok() || patch_count > 4096 || patch_count > reader.remaining() / 4) {
                    error = "invalid NGS patch table";
                    return false;
                }
                port.resize(patch_count);
                for (Address &patch : port)
                    patch = reader.u32();
            }
            const uint32_t module_count = reader.u32();
            if (!reader.ok() || module_count > 1024 || module_count > reader.remaining() / 26) {
                error = "invalid NGS module table";
                return false;
            }
            voice.modules.resize(module_count);
            for (ngs::ModuleDataInfo &module : voice.modules) {
                module.is_bypassed = reader.boolean();
                module.flags = reader.u8();
                module.callback = reader.u32();
                module.user_data = reader.u32();
                if (!read_blob(reader, module.guest_state_data) ||
                    !read_blob(reader, module.parameters) ||
                    !read_blob(reader, module.last_info) ||
                    !read_blob(reader, module.logical_state)) {
                    error = "invalid NGS module data";
                    return false;
                }
            }
        }
    }

    if (!reader.ok() || reader.remaining() != 0) {
        error = "invalid NGS state";
        return false;
    }

    snapshot = NgsSnapshot{definitions, std::move(systems), std::move(racks)};
    return true;
}
} // namespace emucorev::savestate
