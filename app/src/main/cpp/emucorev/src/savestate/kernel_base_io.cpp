#include <emucorev/savestate/kernel_base_io.h>
#include <emucorev/savestate/kernel_state_io.h>
#include <emucorev/savestate/memory_image.h>
#include <cstring>
#include <set>

namespace emucorev::savestate {
namespace {
bool count(BufferReader &reader, uint32_t &value, size_t minimum_bytes, std::string &error) {
    value = reader.u32();
    if (!reader.ok() || value > (1u << 20) || value > reader.remaining() / minimum_bytes) {
        error = "invalid kernel table count";
        return false;
    }
    return true;
}
template <typename Map, typename Read>
bool unique_map(BufferReader &reader, Map &output, size_t minimum_bytes, Read read, std::string &error) {
    uint32_t size;
    if (!count(reader, size, minimum_bytes, error))
        return false;
    for (uint32_t i = 0; i < size; ++i) {
        auto entry = read();
        if (!reader.ok() || !output.emplace(std::move(entry.first), std::move(entry.second)).second) {
            error = "invalid or duplicate kernel table entry";
            return false;
        }
    }
    return true;
}
} // namespace

bool parse_kernel_base(const std::vector<uint8_t> &data, KernelBaseSnapshot &snapshot, std::string &error) {
    error.clear();
    BufferReader reader(data.data(), data.size());
    KernelBaseSnapshot staged;
    staged.next_uid = reader.i32();
    staged.main_thread_id = reader.i32();
    staged.accurate_scheduling = reader.boolean();
    staged.tls_address = reader.u32();
    staged.tls_psize = reader.u32();
    staged.tls_msize = reader.u32();
    staged.thread_event_start = reader.u32();
    staged.thread_event_start_arg = reader.u32();
    staged.thread_event_end = reader.u32();
    staged.thread_event_end_arg = reader.u32();
    staged.start_tick = reader.u64();
    staged.base_tick = reader.u64();
    staged.process_param = reader.u32();
    staged.client_vtable = reader.u32();
    staged.shellsvc_client = reader.u32();
    staged.libc_dso_handle_main = reader.u32();
    staged.halt_instruction_pc = reader.u32();
    for (auto &handler : staged.exception_handlers)
        handler = reader.u32();

    if (!unique_map(reader, staged.codec_blocks, 12, [&] {
            const SceUID uid = reader.i32();
            CodecEngineBlock block{reader.u32(), reader.i32()};
            return std::pair{uid, block};
        }, error)) return false;
    if (!unique_map(reader, staged.loaded_modules, 5, [&] {
            const SceUID uid = reader.i32();
            std::optional<KernelModule> module;
            if (reader.boolean()) {
                module.emplace();
                module->info = reader.value<SceKernelModuleInfo>();
                module->info_segment_address = Ptr<const uint8_t>(reader.u32());
                module->info_offset = reader.u32();
            }
            return std::pair{uid, std::move(module)};
        }, error)) return false;
    if (!unique_map(reader, staged.loaded_sysmodules, 8, [&] {
            const auto id = static_cast<SceSysmoduleModuleId>(reader.u32());
            uint32_t size;
            std::vector<SceUID> modules;
            if (count(reader, size, 4, error)) {
                modules.reserve(size);
                for (uint32_t i = 0; i < size; ++i)
                    modules.push_back(reader.i32());
            } else {
                // Invalidate the shared reader so the outer map also fails.
                uint8_t ignored;
                reader.bytes(&ignored, reader.remaining() + 1);
            }
            return std::pair{id, std::move(modules)};
        }, error)) return false;
    uint32_t size;
    if (!count(reader, size, 4, error)) return false;
    std::set<uint32_t> internal_ids;
    for (uint32_t i = 0; i < size; ++i) {
        const uint32_t id = reader.u32();
        if (!internal_ids.insert(id).second) { error = "duplicate internal sysmodule"; return false; }
        staged.loaded_internal_sysmodules.push_back(static_cast<SceSysmoduleInternalModuleId>(id));
    }
    if (!unique_map(reader, staged.export_nids, 8, [&] {
            const auto nid = reader.u32(); const auto address = reader.u32(); return std::pair{nid, address};
        }, error)) return false;
    if (!unique_map(reader, staged.export_nids_by_lib, 12, [&] {
            const auto nid = reader.u64(); const auto address = reader.u32(); return std::pair{nid, address};
        }, error)) return false;
    if (!unique_map(reader, staged.export_nid_owners, 8, [&] {
            const auto nid = reader.u32(); const auto owner = reader.u32(); return std::pair{nid, owner};
        }, error)) return false;
    if (!count(reader, size, 12, error)) return false;
    for (uint32_t i = 0; i < size; ++i) {
        const auto nid = reader.u32();
        const FuncBindingInfo info{reader.u32(), reader.u32()};
        staged.func_binding_infos.emplace(nid, info);
    }
    if (!count(reader, size, 16, error)) return false;
    for (uint32_t i = 0; i < size; ++i) {
        const auto nid = reader.u32();
        const SavedVarBinding info{reader.u32(), reader.u32(), reader.u32()};
        staged.var_binding_infos.emplace(nid, info);
    }
    if (!unique_map(reader, staged.nid_libraries, 8, [&] {
            const auto nid = reader.u32(); auto name = reader.str(); return std::pair{nid, std::move(name)};
        }, error)) return false;
    if (!unique_map(reader, staged.module_uid_by_nid, 8, [&] {
            const auto nid = reader.u32(); const auto uid = reader.u32(); return std::pair{nid, uid};
        }, error)) return false;
    (void)reader.boolean();
    if (!parse_saved_callbacks(reader, staged.callbacks, error) || !reader.ok() || reader.remaining()) {
        if (error.empty()) error = "invalid kernel section";
        return false;
    }
    snapshot = std::move(staged);
    return true;
}

bool validate_kernel_base(const KernelBaseSnapshot &s, const MemoryImage &memory, std::string &error) {
    const auto fail = [&] { error = "invalid kernel guest reference or identity"; return false; };
    const auto optional = [&](Address address, uint64_t bytes) { return !address || memory.contains(address, bytes); };
    const auto code = [&](Address address) { return optional(address & ~Address{1}, 2); };
    if (s.next_uid <= 0 || s.main_thread_id < 0 || s.tls_psize > s.tls_msize
        || !optional(s.tls_address, s.tls_psize) || !code(s.thread_event_start) || !code(s.thread_event_end)
        || !optional(s.process_param, 4) || !optional(s.client_vtable, 4) || !optional(s.shellsvc_client, 4)
        || !optional(s.libc_dso_handle_main, 1) || !code(s.halt_instruction_pc)) return fail();
    for (auto handler : s.exception_handlers) if (!code(handler)) return fail();
    std::set<SceUID> identities;
    const auto identity = [&](SceUID uid) { return uid > 0 && uid < s.next_uid && identities.insert(uid).second; };
    for (const auto &[uid, block] : s.codec_blocks)
        if (!identity(uid) || !block.size || !memory.contains(static_cast<Address>(block.vaddr), block.size)) return fail();
    for (const auto &[uid, module] : s.loaded_modules) {
        if (!identity(uid)) return fail();
        if (!module) continue;
        const auto &info = module->info;
        if (info.modid != uid || !std::memchr(info.module_name, 0, sizeof(info.module_name))
            || !std::memchr(info.path, 0, sizeof(info.path)) || !code(info.start_entry.address())
            || !code(info.stop_entry.address()) || !code(info.exit_entry.address())
            || info.tlsInitSize > info.tlsAreaSize || !optional(info.tlsInit.address(), info.tlsInitSize)) return fail();
        for (const auto &segment : info.segments) {
            if (segment.filesz > segment.memsz || (segment.memsz && !memory.contains(segment.vaddr.address(), segment.memsz))) return fail();
        }
        if (!optional(module->info_segment_address.address(), uint64_t{module->info_offset} + 1)) return fail();
    }
    for (const auto &[id, modules] : s.loaded_sysmodules) {
        std::set<SceUID> unique;
        for (auto uid : modules)
            if (!s.loaded_modules.contains(uid) || !unique.insert(uid).second) return fail();
    }
    // Firmware export descriptors can contain a null variable value. The
    // loader retains that value; only nonnull entries refer to guest memory.
    for (const auto &[nid, address] : s.export_nids) if (address && !memory.contains(address & ~Address{1}, 1)) return fail();
    for (const auto &[nid, address] : s.export_nids_by_lib) if (address && !memory.contains(address & ~Address{1}, 1)) return fail();
    for (const auto &[nid, binding] : s.func_binding_infos) if (!memory.contains(binding.entry_address, 12)) return fail();
    for (const auto &[nid, binding] : s.var_binding_infos)
        if (binding.size && !memory.contains(binding.entries, binding.size)) return fail();
    for (const auto &[nid, name] : s.nid_libraries) if (name.size() > 4096 || name.find('\0') != std::string::npos) return fail();
    for (const auto &[nid, uid] : s.module_uid_by_nid)
        if (!s.loaded_modules.contains(static_cast<SceUID>(uid))) return fail();
    return true;
}
} // namespace emucorev::savestate
