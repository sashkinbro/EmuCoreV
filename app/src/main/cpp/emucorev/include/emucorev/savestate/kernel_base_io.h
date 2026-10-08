#pragma once

#include <emucorev/savestate/state_io.h>
#include <kernel/state.h>
#include <array>
#include <optional>

namespace emucorev::savestate {
class MemoryImage;
struct SavedVarBinding {
    Address entries = 0;
    uint32_t size = 0;
    uint32_t module_nid = 0;
};
struct KernelBaseSnapshot {
    SceUID next_uid = 0;
    SceUID main_thread_id = 0;
    bool accurate_scheduling = false;
    Address tls_address = 0;
    uint32_t tls_psize = 0;
    uint32_t tls_msize = 0;
    Address thread_event_start = 0;
    uint32_t thread_event_start_arg = 0;
    Address thread_event_end = 0;
    uint32_t thread_event_end_arg = 0;
    uint64_t start_tick = 0;
    uint64_t base_tick = 0;
    Address process_param = 0;
    Address client_vtable = 0;
    Address shellsvc_client = 0;
    Address libc_dso_handle_main = 0;
    Address halt_instruction_pc = 0;
    std::array<Address, KernelState::EXCEPTION_HANDLER_MAX> exception_handlers{};
    CodecEngineBlocks codec_blocks;
    std::map<SceUID, std::optional<KernelModule>> loaded_modules;
    LoadedSysmodules loaded_sysmodules;
    LoadedInternalSysmodules loaded_internal_sysmodules;
    ExportNids export_nids;
    LibExportNids export_nids_by_lib;
    ExportNidOwners export_nid_owners;
    FuncBindingInfos func_binding_infos;
    std::multimap<uint32_t, SavedVarBinding> var_binding_infos;
    decltype(KernelState::nid_libraries) nid_libraries;
    ModuleUidByNid module_uid_by_nid;
    std::vector<KernelState::SavedCallback> callbacks;
};

bool parse_kernel_base(const std::vector<uint8_t> &data, KernelBaseSnapshot &snapshot, std::string &error);
bool validate_kernel_base(const KernelBaseSnapshot &snapshot, const MemoryImage &memory, std::string &error);
bool validate_kernel_modules(const KernelBaseSnapshot &snapshot, const MemoryImage &memory, std::string &error);
} // namespace emucorev::savestate
