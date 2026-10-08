#include <emucorev/savestate/kernel_base_io.h>
#include <emucorev/savestate/memory_image.h>

#define SCE_ELF_DEFS_TARGET
#include <sce-elf-defs.h>
#undef SCE_ELF_DEFS_TARGET

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string_view>

namespace emucorev::savestate {
namespace {
static_assert(sizeof(sce_module_info_raw) == 0x5C);
static_assert(sizeof(sce_module_exports_raw) == 0x20);
static_assert(sizeof(sce_module_imports_short_raw) == 0x24);
static_assert(sizeof(sce_module_imports_raw) == 0x34);
constexpr uint32_t kMaxModuleNameBytes = 4096;
constexpr uint64_t kMaxModuleSymbols = 1u << 20;
constexpr uint32_t kMaxModuleDescriptors = 1u << 16;
constexpr uint32_t kShortImportSize = 0x24;
constexpr uint32_t kLongImportSize = 0x34;

bool synthetic_hle_module(const KernelModule &module, SceUID uid) {
    // load_module publishes a zeroed KernelModule for HLE system libraries,
    // with only size/modid/name/path populated. It has no ELF header to read.
    const auto &info = module.info;
    if (module.info_segment_address || module.info_offset || info.modid != uid
        || info.size != sizeof(info)
        || !std::memchr(info.module_name, 0, sizeof(info.module_name))
        || !std::memchr(info.path, 0, sizeof(info.path)) || !info.module_name[0])
        return false;
    const std::string_view name(info.module_name);
    const std::string stem = std::string("vs0:sys/external/") + std::string(name);
    const std::string_view path(info.path);
    if (name.find_first_of("/\\") != std::string_view::npos || name == "." || name == ".."
        || (path != stem + ".suprx" && path != stem + ".skprx"))
        return false;
    auto payload = info;
    payload.size = 0;
    payload.modid = 0;
    std::memset(payload.module_name, 0, sizeof(payload.module_name));
    std::memset(payload.path, 0, sizeof(payload.path));
    const auto *bytes = reinterpret_cast<const unsigned char *>(&payload);
    return std::all_of(bytes, bytes + sizeof(payload), [](unsigned char byte) { return byte == 0; });
}

bool read_name(const MemoryImage &memory, Address address, std::string &error) {
    if (!address)
        return true;
    std::array<char, 256> bytes{};
    for (uint32_t offset = 0; offset < kMaxModuleNameBytes;) {
        size_t count = std::min(bytes.size(), static_cast<size_t>(kMaxModuleNameBytes - offset));
        while (count && !memory.contains(uint64_t{address} + offset, count))
            count /= 2;
        if (!count || !memory.read_bytes(uint64_t{address} + offset, bytes.data(), count, error))
            return false;
        if (std::memchr(bytes.data(), 0, count))
            return true;
        offset += static_cast<uint32_t>(count);
    }
    error = "unterminated module library name";
    return false;
}

bool valid_var_relocations(const MemoryImage &memory, Address address, uint32_t size, std::string &error) {
    if (!size)
        return true;
    if (size % sizeof(uint32_t) || !memory.contains(address, size)) {
        error = "invalid module variable relocation range";
        return false;
    }
    uint32_t offset = 0;
    while (offset < size) {
        uint32_t first_word = 0;
        if (size - offset < sizeof(first_word)
            || !memory.read_bytes(uint64_t{address} + offset, &first_word, sizeof(first_word), error))
            return false;
        const uint32_t format = first_word & 0xF;
        const uint32_t entry_size = format == 1 ? 8 : format == 2 ? 12 : 0;
        if (!entry_size || entry_size > size - offset) {
            error = "truncated or unsupported module variable relocation";
            return false;
        }
        offset += entry_size;
    }
    return offset == size;
}

bool array_in_memory(const MemoryImage &memory, Address address, uint64_t count, uint64_t element_size) {
    if (!count)
        return true;
    if (!address || (element_size == sizeof(uint32_t) && address % alignof(uint32_t))
        || count > std::numeric_limits<uint64_t>::max() / element_size)
        return false;
    return memory.contains(address, count * element_size);
}

bool table_range(const MemoryImage &memory, uint64_t base, uint32_t segment_size,
    uint32_t begin, uint32_t end, std::string &error) {
    if (begin > end || begin % 4 || end % 4 || end > segment_size
        || uint64_t{base} + end > UINT32_MAX + uint64_t{1}) {
        error = "invalid module table offsets";
        return false;
    }
    if (begin == end)
        return true;
    if (!memory.contains(base + begin, end - begin)) {
        error = "module table is outside saved memory";
        return false;
    }
    return true;
}

bool validate_exports(const MemoryImage &memory, uint64_t base, uint32_t segment_size,
    const sce_module_info_raw &module, std::string &error) {
    if (!table_range(memory, base, segment_size, module.export_top, module.export_end, error))
        return false;
    uint32_t cursor = module.export_top;
    uint32_t descriptors = 0;
    while (cursor < module.export_end) {
        if (++descriptors > kMaxModuleDescriptors) {
            error = "too many module export descriptors";
            return false;
        }
        if (module.export_end - cursor < sizeof(sce_module_exports_raw)) {
            error = "truncated module export descriptor";
            return false;
        }
        sce_module_exports_raw descriptor{};
        if (!memory.read_bytes(base + cursor, &descriptor, sizeof(descriptor), error))
            return false;
        if (descriptor.size != sizeof(descriptor)) {
            error = "invalid module export descriptor size";
            return false;
        }
        const uint64_t count = uint64_t{descriptor.num_syms_funcs} + descriptor.num_syms_vars;
        if (count > kMaxModuleSymbols
            || !array_in_memory(memory, descriptor.nid_table, count, sizeof(uint32_t))
            || !array_in_memory(memory, descriptor.entry_table, count, sizeof(uint32_t))) {
            error = "module export arrays are outside saved memory";
            return false;
        }
        if (!read_name(memory, descriptor.library_name, error))
            return false;
        std::vector<uint32_t> entries(static_cast<size_t>(count));
        if (count && !memory.read_bytes(descriptor.entry_table, entries.data(), entries.size() * sizeof(uint32_t), error))
            return false;
        for (uint32_t entry : entries) {
            if (entry && !memory.contains(entry & ~Address{1}, 1)) {
                error = "module export entry is outside saved memory";
                return false;
            }
        }
        cursor += descriptor.size;
    }
    return cursor == module.export_end;
}

bool validate_imports(const MemoryImage &memory, uint64_t base, uint32_t segment_size,
    const sce_module_info_raw &module, std::string &error) {
    if (!table_range(memory, base, segment_size, module.import_top, module.import_end, error))
        return false;
    uint32_t cursor = module.import_top;
    uint32_t descriptors = 0;
    while (cursor < module.import_end) {
        if (++descriptors > kMaxModuleDescriptors) {
            error = "too many module import descriptors";
            return false;
        }
        if (module.import_end - cursor < sizeof(uint16_t)) {
            error = "truncated module import descriptor";
            return false;
        }
        uint16_t descriptor_size = 0;
        if (!memory.read_bytes(base + cursor, &descriptor_size, sizeof(descriptor_size), error))
            return false;
        if ((descriptor_size != kShortImportSize && descriptor_size != kLongImportSize)
            || descriptor_size > module.import_end - cursor) {
            error = "invalid module import descriptor size";
            return false;
        }
        uint32_t library_name = 0;
        uint32_t func_nid_table = 0;
        uint32_t func_entry_table = 0;
        uint32_t var_nid_table = 0;
        uint32_t var_entry_table = 0;
        uint16_t func_count = 0;
        uint16_t var_count = 0;
        uint16_t tls_count = 0;
        if (descriptor_size == kShortImportSize) {
            sce_module_imports_short_raw descriptor{};
            if (!memory.read_bytes(base + cursor, &descriptor, sizeof(descriptor), error))
                return false;
            library_name = descriptor.library_name;
            func_nid_table = descriptor.func_nid_table;
            func_entry_table = descriptor.func_entry_table;
            var_nid_table = descriptor.var_nid_table;
            var_entry_table = descriptor.var_entry_table;
            func_count = descriptor.num_syms_funcs;
            var_count = descriptor.num_syms_vars;
            tls_count = descriptor.num_syms_tls_vars;
        } else {
            sce_module_imports_raw descriptor{};
            if (!memory.read_bytes(base + cursor, &descriptor, sizeof(descriptor), error))
                return false;
            library_name = descriptor.library_name;
            func_nid_table = descriptor.func_nid_table;
            func_entry_table = descriptor.func_entry_table;
            var_nid_table = descriptor.var_nid_table;
            var_entry_table = descriptor.var_entry_table;
            func_count = descriptor.num_syms_funcs;
            var_count = descriptor.num_syms_vars;
            tls_count = descriptor.num_syms_tls_vars;
        }
        if (tls_count != 0
            || !array_in_memory(memory, func_nid_table, func_count, sizeof(uint32_t))
            || !array_in_memory(memory, func_entry_table, func_count, sizeof(uint32_t))
            || !array_in_memory(memory, var_nid_table, var_count, sizeof(uint32_t))
            || !array_in_memory(memory, var_entry_table, var_count, sizeof(uint32_t))
            || !read_name(memory, library_name, error)) {
            if (error.empty()) error = "invalid module import arrays";
            return false;
        }

        std::vector<uint32_t> func_entries(func_count);
        if (func_count && !memory.read_bytes(func_entry_table, func_entries.data(), func_entries.size() * sizeof(uint32_t), error))
            return false;
        for (uint32_t stub : func_entries) {
            if (!memory.contains(stub, 4 * sizeof(uint32_t))) {
                error = "module function import stub is outside saved memory";
                return false;
            }
        }

        std::vector<uint32_t> var_entries(var_count);
        if (var_count && !memory.read_bytes(var_entry_table, var_entries.data(), var_entries.size() * sizeof(uint32_t), error))
            return false;
        for (uint32_t entry : var_entries) {
            uint32_t packed_header = 0;
            if (entry % alignof(uint32_t) || !memory.read_bytes(entry, &packed_header, sizeof(packed_header), error)) {
                error = "module variable import header is outside saved memory";
                return false;
            }
            const uint32_t relocation_size = (packed_header >> 4) & 0x00FFFFFF;
            const uint32_t payload_size = relocation_size > sizeof(packed_header)
                ? relocation_size - sizeof(packed_header) : 0;
            if (!valid_var_relocations(memory, entry + sizeof(packed_header), payload_size, error))
                return false;
        }
        cursor += descriptor_size;
    }
    return cursor == module.import_end;
}
} // namespace

bool validate_kernel_modules(const KernelBaseSnapshot &snapshot, const MemoryImage &memory, std::string &error) {
    error.clear();
    for (const auto &[uid, module_ptr] : snapshot.loaded_modules) {
        if (!module_ptr)
            continue;
        const KernelModule &module = *module_ptr;
        if (synthetic_hle_module(module, uid))
            continue;
        const Address base = module.info_segment_address.address();
        const uint64_t offset = module.info_offset;
        const SceKernelSegmentInfo *info_segment = nullptr;
        for (const SceKernelSegmentInfo &segment : module.info.segments) {
            if (segment.memsz && segment.vaddr.address() == base) {
                info_segment = &segment;
                break;
            }
        }
        if (!base || base % alignof(sce_module_info_raw) || offset % alignof(sce_module_info_raw)
            || !info_segment || offset > info_segment->memsz
            || sizeof(sce_module_info_raw) > uint64_t{info_segment->memsz} - offset
            || !memory.contains(uint64_t{base} + offset, sizeof(sce_module_info_raw))) {
            error = "module raw header is outside its saved segment";
            return false;
        }
        sce_module_info_raw raw{};
        if (!memory.read_bytes(uint64_t{base} + offset, &raw, sizeof(raw), error))
            return false;
        if (!validate_exports(memory, base, info_segment->memsz, raw, error)
            || !validate_imports(memory, base, info_segment->memsz, raw, error))
            return false;
    }
    return true;
}
} // namespace emucorev::savestate
