#pragma once
#include <modules/sysmem_state.h>
#include <vector>
namespace emucorev::savestate {
class MemoryImage;
struct SysmemSnapshot {
    SceUID next_uid = 1;
    uint32_t allocated_user = 0, allocated_cdram = 0, allocated_phycont = 0;
    std::map<SceUID, KernelMemBlock> blocks, vm_blocks;
};
bool parse_sysmem_state(const std::vector<uint8_t> &, SysmemSnapshot &, std::string &);
bool validate_sysmem_state(const SysmemSnapshot &, const MemoryImage &, std::string &);
}
