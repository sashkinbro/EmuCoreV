// EmuCoreV save-state engine.
//
// Save states capture guest RAM, guest CPU contexts, kernel objects, module
// tables, display and GXM runtime objects. Host-only state (JIT caches, Vulkan
// objects, std::thread handles) is rebuilt on load.

#include <emucorev/savestate/savestate.h>

#include <emucorev/savestate/archive.h>
#include <emucorev/savestate/state_io.h>

#include <cpu/functions.h>
#include <audio/state.h>
#include <ctrl/state.h>
#include <display/state.h>
#include <emuenv/state.h>
#include <gxm/functions.h>
#include <gxm/savestate.h>
#include <gxm/state.h>
#include <io/functions.h>
#include <io/state.h>
#include <kernel/callback.h>
#include <kernel/state.h>
#include <kernel/sync_primitives.h>
#include <kernel/thread/thread_state.h>
#include <mem/functions.h>
#include <mem/state.h>
#include <modules/sysmem_state.h>
#include <ngs/state.h>
#include <renderer/functions.h>
#include <renderer/state.h>
#include <util/log.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

namespace emucorev::savestate {

namespace {

constexpr uint32_t kMemoryFormatVersion = 1;
constexpr const char *kEngineRevision = "emucorev-savestate-1";
constexpr size_t kMemoryChunkSize = 1u << 20;

enum ChunkFlags : uint32_t {
    kChunkRaw = 0,
    kChunkDeflated = 1,
    kChunkZeros = 2,
};

struct MemorySpan {
    Address address = 0;
    uint32_t page_count = 0;
};

constexpr uint32_t kPageSize = 4096;
constexpr uint32_t kArenaPages = 1u << 20;
constexpr uint64_t kArenaSize = static_cast<uint64_t>(kArenaPages) * kPageSize;

uint64_t host_time_us() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch())
                                     .count());
}

uint32_t make_session_token(EmuEnvState &emuenv) {
    uint64_t mixed = reinterpret_cast<uintptr_t>(&emuenv);
    mixed ^= emuenv.kernel.start_tick * 0x9E3779B97F4A7C15ull;
    mixed ^= static_cast<uint64_t>(emuenv.kernel.base_tick.tick) * 0xC2B2AE3D27D4EB4Full;
    mixed ^= static_cast<uint64_t>(emuenv.main_thread_id) << 17;
    mixed ^= mixed >> 33;
    return static_cast<uint32_t>(mixed ^ (mixed >> 32));
}

void report(const ProgressCallback &progress, float value, const char *stage) {
    if (progress)
        progress(value, stage);
}

void copy_object_name(char *target, size_t target_size, const char *source) {
    if (target_size == 0)
        return;
    std::memset(target, 0, target_size);
    if (source)
        std::strncpy(target, source, target_size - 1);
}

WaitingThreadQueuePtr make_wait_queue(uint32_t attr) {
    if (attr & SCE_KERNEL_ATTR_TH_PRIO)
        return std::make_unique<PriorityThreadDataQueue<WaitingThreadData>>();
    return std::make_unique<FIFOThreadDataQueue<WaitingThreadData>>();
}

void write_string_field(BufferWriter &writer, const std::string &value) {
    writer.str(value);
}

std::string read_string_field(BufferReader &reader) {
    return reader.str();
}

template <typename Map, typename Fn>
void write_map(BufferWriter &writer, const Map &map, Fn &&write_entry) {
    writer.u32(static_cast<uint32_t>(map.size()));
    for (const auto &entry : map)
        write_entry(entry);
}

std::vector<MemorySpan> collect_memory_spans(const MemState &mem) {
    std::vector<MemorySpan> spans;
    const uint32_t total_pages = kArenaPages;
    for (uint32_t page = 1; page < total_pages;) {
        const AllocMemPage &entry = mem.alloc_table[page];
        if (entry.allocated) {
            MemorySpan span;
            span.address = page * kPageSize;
            span.page_count = entry.size;
            if (span.page_count == 0 || page + span.page_count > total_pages) {
                page++;
                continue;
            }
            spans.push_back(span);
            page += span.page_count;
        } else {
            page++;
        }
    }
    return spans;
}

void clear_host_protections(MemState &mem) {
    const std::lock_guard<std::mutex> protect_lock(mem.protect_mutex);
    mem.protect_tree.clear();
}

void reset_memory(MemState &mem) {
    std::vector<std::pair<uint64_t, uint32_t>> external;
    {
        const std::lock_guard<std::mutex> lock(mem.external_mapping_mutex);
        external.reserve(mem.external_mapping.size());
        for (const auto &entry : mem.external_mapping)
            external.emplace_back(entry.first, entry.second.size);
    }
    for (const auto &[host, size] : external)
        remove_external_mapping(mem, reinterpret_cast<uint8_t *>(host), size);

    clear_host_protections(mem);

    const std::vector<MemorySpan> spans = collect_memory_spans(mem);
    for (const MemorySpan &span : spans)
        free(mem, span.address);
}

bool is_external_address(const MemState &mem, Address address, uint32_t size) {
    if (mem.external_mapping.empty())
        return false;
    const uint64_t start = address;
    const uint64_t end = start + size;
    for (const auto &entry : mem.external_mapping) {
        const uint64_t host = entry.first;
        const uint64_t map_start = static_cast<uint64_t>(entry.second.address);
        const uint64_t map_end = map_start + entry.second.size;
        if (start < map_end && end > map_start)
            return true;
    }
    return false;
}

void read_guest_chunk(const MemState &mem, Address address, uint8_t *out, size_t size) {
    if (!is_external_address(mem, address, static_cast<uint32_t>(size))) {
        std::memcpy(out, mem.memory.get() + address, size);
        return;
    }

    // Copy page by page, sourcing externally mapped pages from their host buffer.
    size_t done = 0;
    while (done < size) {
        const Address current = address + static_cast<Address>(done);
        const size_t page_remaining = kPageSize - (current & (kPageSize - 1));
        const size_t chunk = std::min(size - done, page_remaining);
        const uint8_t *source = nullptr;

        for (const auto &entry : mem.external_mapping) {
            const uint64_t map_start = static_cast<uint64_t>(entry.second.address);
            const uint64_t map_end = map_start + entry.second.size;
            if (current >= map_start && current < map_end) {
                source = reinterpret_cast<const uint8_t *>(entry.first) + (current - map_start);
                break;
            }
        }

        if (!source)
            source = mem.memory.get() + current;
        std::memcpy(out + done, source, chunk);
        done += chunk;
    }
}

void write_guest_chunk(MemState &mem, Address address, const uint8_t *data, size_t size) {
    if (!mem.use_page_table) {
        std::memcpy(mem.memory.get() + address, data, size);
        return;
    }
    size_t done = 0;
    while (done < size) {
        const Address current = address + static_cast<Address>(done);
        const size_t page_remaining = kPageSize - (current & (kPageSize - 1));
        const size_t chunk = std::min(size - done, page_remaining);
        mem.page_table[current / kPageSize] = mem.memory.get();
        std::memcpy(mem.memory.get() + current, data + done, chunk);
        done += chunk;
    }
}

bool write_meta(EmuEnvState &emuenv, BufferWriter &writer, const std::string &app_version) {
    writer.u32(kEngineVersion);
    writer.u32(make_session_token(emuenv));
    writer.u64(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count()));
    writer.u32(static_cast<uint32_t>(emuenv.backend_renderer));
    writer.u32(emuenv.kernel.cpu_opt ? 1u : 0u);
    write_string_field(writer, emuenv.io.title_id);
    write_string_field(writer, app_version);
    write_string_field(writer, kEngineRevision);
    return true;
}

bool read_meta(const std::vector<uint8_t> &data, Meta &meta) {
    BufferReader reader(data.data(), data.size());
    meta.engine_version = reader.u32();
    meta.format_version = kFormatVersion;
    meta.session_token = reader.u32();
    meta.timestamp_ms = reader.u64();
    meta.core_flags = reader.u32();
    (void)reader.u32();
    meta.title_id = read_string_field(reader);
    meta.app_version = read_string_field(reader);
    meta.engine_revision = read_string_field(reader);
    return reader.ok();
}

bool write_memory(EmuEnvState &emuenv, Writer &writer, const ProgressCallback &progress, std::string &error) {
    MemState &mem = emuenv.mem;
    if (!writer.begin_section(SectionId::Memory, false, error))
        return false;

    const std::vector<MemorySpan> spans = collect_memory_spans(mem);

    uint64_t total_bytes = 0;
    for (const MemorySpan &span : spans)
        total_bytes += static_cast<uint64_t>(span.page_count) * kPageSize;

    BufferWriter header;
    header.u32(kMemoryFormatVersion);
    header.u32(static_cast<uint32_t>(kMemoryChunkSize));
    header.u32(static_cast<uint32_t>(spans.size()));
    for (const MemorySpan &span : spans) {
        header.u32(span.address);
        header.u32(span.page_count);
    }
    if (!writer.append_section_data(header.data().data(), header.size(), error))
        return false;

    std::vector<uint8_t> chunk;
    chunk.resize(kMemoryChunkSize);
    std::vector<uint8_t> compressed;
    uint64_t processed = 0;

    for (const MemorySpan &span : spans) {
        const uint64_t span_bytes = static_cast<uint64_t>(span.page_count) * kPageSize;
        for (uint64_t offset = 0; offset < span_bytes; offset += kMemoryChunkSize) {
            const uint32_t raw_size = static_cast<uint32_t>(std::min<uint64_t>(kMemoryChunkSize, span_bytes - offset));
            const Address address = span.address + static_cast<Address>(offset);
            read_guest_chunk(mem, address, chunk.data(), raw_size);

            bool all_zeros = true;
            for (uint32_t i = 0; i < raw_size; i++) {
                if (chunk[i] != 0) {
                    all_zeros = false;
                    break;
                }
            }

            BufferWriter record;
            record.u64(address);
            record.u32(raw_size);

            if (all_zeros) {
                record.u32(kChunkZeros);
                record.u32(0);
                record.u32(0);
                if (!writer.append_section_data(record.data().data(), record.size(), error))
                    return false;
            } else {
                if (!compress_buffer(chunk.data(), raw_size, compressed, error))
                    return false;
                const bool use_compressed = compressed.size() < raw_size;
                record.u32(use_compressed ? kChunkDeflated : kChunkRaw);
                record.u32(static_cast<uint32_t>(use_compressed ? compressed.size() : raw_size));
                record.u32(crc32_buffer(chunk.data(), raw_size));
                if (!writer.append_section_data(record.data().data(), record.size(), error))
                    return false;
                const uint8_t *payload = use_compressed ? compressed.data() : chunk.data();
                const size_t payload_size = use_compressed ? compressed.size() : raw_size;
                if (!writer.append_section_data(payload, payload_size, error))
                    return false;
            }

            processed += raw_size;
            if (total_bytes > 0)
                report(progress, static_cast<float>(static_cast<double>(processed) / static_cast<double>(total_bytes)) * 0.75f, "memory");
        }
    }

    return writer.end_section(error);
}

bool read_memory(EmuEnvState &emuenv, Reader &reader, const ProgressCallback &progress, std::string &error) {
    MemState &mem = emuenv.mem;

    auto section = reader.open_section(SectionId::Memory, error);
    if (!section)
        return false;

    uint32_t format_version = 0;
    uint32_t chunk_size = 0;
    uint32_t span_count = 0;
    if (!section->read(&format_version, sizeof(format_version), error)
        || !section->read(&chunk_size, sizeof(chunk_size), error)
        || !section->read(&span_count, sizeof(span_count), error))
        return false;

    if (format_version != kMemoryFormatVersion || chunk_size == 0 || chunk_size > (4u << 20)) {
        error = "unsupported memory section";
        return false;
    }
    if (span_count > kArenaPages) {
        error = "invalid memory allocation map";
        return false;
    }

    std::vector<MemorySpan> spans(span_count);
    for (MemorySpan &span : spans) {
        if (!section->read(&span.address, sizeof(span.address), error)
            || !section->read(&span.page_count, sizeof(span.page_count), error))
            return false;
        if (span.page_count == 0 || (static_cast<uint64_t>(span.address) + static_cast<uint64_t>(span.page_count) * kPageSize) > kArenaSize) {
            error = "invalid memory allocation map";
            return false;
        }
    }

    reset_memory(mem);

    for (const MemorySpan &span : spans) {
        const uint32_t size = span.page_count * kPageSize;
        if (!try_alloc_at(mem, span.address, size, "savestate")) {
            error = "failed to restore guest memory map";
            return false;
        }
        if (span.address == 0x803F8000)
            LOG_CRITICAL("[savestate-error] trace: stack span restored valid={}", is_valid_addr(mem, 0x803F8000));
    }
    LOG_CRITICAL("[savestate-error] trace: spans restored valid={}", is_valid_addr(mem, 0x804F7658));
    {
        bool watch_a = false;
        bool watch_b = false;
        for (const MemorySpan &span : spans) {
            const uint64_t start = span.address;
            const uint64_t end = start + static_cast<uint64_t>(span.page_count) * kPageSize;
            if (start <= 0x86064000 && end > 0x86064000)
                watch_a = true;
            if (start <= 0x857a4000 && end > 0x857a4000)
                watch_b = true;
        }
        LOG_CRITICAL("[savestate-error] trace: watch 0x86064000 in save={} 0x857a4000 in save={} (spans={})", watch_a, watch_b, spans.size());
    }

    if (mem.use_page_table) {
        const uint32_t total_pages = kArenaPages;
        for (uint32_t page = 0; page < total_pages; page++)
            mem.page_table[page] = mem.memory.get();
    }

    std::vector<uint8_t> chunk;
    std::vector<uint8_t> compressed;
    uint64_t total_bytes = 0;
    for (const MemorySpan &span : spans)
        total_bytes += static_cast<uint64_t>(span.page_count) * kPageSize;
    uint64_t processed = 0;

    while (section->remaining()) {
        struct {
            uint64_t address;
            uint32_t raw_size;
            uint32_t flags;
            uint32_t stored_size;
            uint32_t crc;
        } record{};
        if (!section->read(&record, sizeof(record), error))
            return false;
        if (record.raw_size == 0 || record.raw_size > chunk_size) {
            error = "invalid memory chunk";
            return false;
        }
        if (record.stored_size > record.raw_size + (1u << 16)) {
            error = "invalid memory chunk";
            return false;
        }

        if (record.flags == kChunkZeros) {
            chunk.assign(record.raw_size, 0);
        } else if (record.flags == kChunkRaw) {
            if (record.stored_size != record.raw_size) {
                error = "invalid memory chunk";
                return false;
            }
            chunk.resize(record.raw_size);
            if (!section->read(chunk.data(), record.raw_size, error))
                return false;
        } else if (record.flags == kChunkDeflated) {
            compressed.resize(record.stored_size);
            if (record.stored_size && !section->read(compressed.data(), record.stored_size, error))
                return false;
            if (!decompress_buffer(compressed.data(), compressed.size(), record.raw_size, chunk, error))
                return false;
        } else {
            error = "unknown memory chunk encoding";
            return false;
        }

        if (record.crc && crc32_buffer(chunk.data(), chunk.size()) != record.crc) {
            error = "memory chunk checksum mismatch";
            return false;
        }

        if (record.address >= kArenaSize || record.address + record.raw_size > kArenaSize) {
            error = "memory chunk out of range";
            return false;
        }
        write_guest_chunk(mem, static_cast<Address>(record.address), chunk.data(), chunk.size());

        processed += record.raw_size;
        if (total_bytes > 0)
            report(progress, 0.75f + static_cast<float>(static_cast<double>(processed) / static_cast<double>(total_bytes)) * 0.15f, "memory");
    }

    return true;
}

bool write_threads(EmuEnvState &emuenv, Writer &writer, std::string &error) {
    BufferWriter buffer;
    KernelState &kernel = emuenv.kernel;

    std::vector<ThreadState::Snapshot> snapshots;
    snapshots.reserve(kernel.threads.size());
    for (const auto &[id, thread] : kernel.threads) {
        // The GXM display queue thread only exists as a context holder for the
        // host-driven display loop; it is recreated on load.
        if (thread && id != emuenv.gxm.display_queue_thread) {
            const ThreadState::Snapshot snapshot = thread->capture_snapshot();
            if (thread->cpu)
                LOG_CRITICAL("[savestate-error] save thread {} '{}' status={} pc={:X} wait_kind={} wait_uid={} vm_susp={} call_level={}", id, snapshot.name,
                    static_cast<int>(snapshot.status), read_pc(*thread->cpu), static_cast<int>(snapshot.wait_kind),
                    snapshot.wait_prim_uid, snapshot.vm_suspended, snapshot.call_level);
            snapshots.push_back(snapshot);
        }
    }
    LOG_CRITICAL("[savestate-error] save main={} display={}", emuenv.main_thread_id, emuenv.gxm.display_queue_thread);

    buffer.u32(static_cast<uint32_t>(snapshots.size()));
    for (const ThreadState::Snapshot &snapshot : snapshots) {
        buffer.i32(snapshot.id);
        buffer.str(snapshot.name);
        buffer.u32(snapshot.entry_point);
        buffer.u32(snapshot.stack_addr);
        buffer.i32(snapshot.stack_size);
        buffer.u32(snapshot.tls_addr);
        buffer.i32(snapshot.priority);
        buffer.i32(snapshot.affinity_mask);
        buffer.u64(snapshot.start_tick);
        buffer.u64(snapshot.last_vblank_waited);
        buffer.u32(static_cast<uint32_t>(snapshot.status));
        buffer.u32(snapshot.returned_value);
        buffer.value(snapshot.context);
        buffer.value(snapshot.init_context);
        buffer.u8(static_cast<uint8_t>(snapshot.wait_kind));
        buffer.i32(snapshot.wait_prim_uid);
        buffer.u32(snapshot.wait_extra);
        buffer.boolean(snapshot.signal_pending);
        buffer.boolean(snapshot.exit_requested);
        buffer.boolean(snapshot.delete_requested);
        buffer.boolean(snapshot.vm_suspended);
        buffer.boolean(snapshot.single_stepping);
        buffer.boolean(snapshot.run_start_callback);
        buffer.boolean(snapshot.run_end_callback);
        buffer.boolean(snapshot.is_processing_callbacks);
        buffer.i32(snapshot.call_level);
        buffer.list<SceUID>(snapshot.callback_uids, [&buffer](SceUID uid) { buffer.i32(uid); });
        buffer.list<SceUID>(snapshot.waiting_thread_uids, [&buffer](SceUID uid) { buffer.i32(uid); });
    }

    return writer.write_section(SectionId::Threads, buffer.data().data(), buffer.size(), true, error);
}

bool read_threads(EmuEnvState &emuenv, const std::vector<uint8_t> &data, std::vector<ThreadState::Snapshot> &deferred, std::string &error) {
    BufferReader reader(data.data(), data.size());
    KernelState &kernel = emuenv.kernel;

    const uint32_t count = reader.u32();
    if (!reader.ok() || count > MAX_CORE_COUNT + 64) {
        error = "invalid thread table";
        return false;
    }

    struct RestoredThread {
        ThreadState::Snapshot snapshot;
        std::vector<SceUID> callback_uids;
        std::vector<SceUID> waiting_thread_uids;
    };
    std::vector<RestoredThread> restored;
    restored.reserve(count);

    for (uint32_t i = 0; i < count && reader.ok(); i++) {
        RestoredThread item;
        ThreadState::Snapshot &snapshot = item.snapshot;
        snapshot.id = reader.i32();
        snapshot.name = reader.str();
        snapshot.entry_point = reader.u32();
        snapshot.stack_addr = reader.u32();
        snapshot.stack_size = reader.i32();
        snapshot.tls_addr = reader.u32();
        snapshot.priority = reader.i32();
        snapshot.affinity_mask = reader.i32();
        snapshot.start_tick = reader.u64();
        snapshot.last_vblank_waited = reader.u64();
        snapshot.status = static_cast<ThreadStatus>(reader.u32());
        snapshot.returned_value = reader.u32();
        snapshot.context = reader.value<CPUContext>();
        snapshot.init_context = reader.value<CPUContext>();
        snapshot.wait_kind = static_cast<ThreadState::Snapshot::WaitKind>(reader.u8());
        snapshot.wait_prim_uid = reader.i32();
        snapshot.wait_extra = reader.u32();
        snapshot.signal_pending = reader.boolean();
        snapshot.exit_requested = reader.boolean();
        snapshot.delete_requested = reader.boolean();
        snapshot.vm_suspended = reader.boolean();
        snapshot.single_stepping = reader.boolean();
        snapshot.run_start_callback = reader.boolean();
        snapshot.run_end_callback = reader.boolean();
        snapshot.is_processing_callbacks = reader.boolean();
        snapshot.call_level = reader.i32();
        item.callback_uids = reader.list<SceUID>([&reader]() { return reader.i32(); });
        item.waiting_thread_uids = reader.list<SceUID>([&reader]() { return reader.i32(); });

        if ((snapshot.status == ThreadStatus::wait) || (snapshot.status == ThreadStatus::run && snapshot.context.get_pc() == 0)) {
            // Drop the blocked HLE frame: resume the thread as if the wait returned.
            snapshot.status = ThreadStatus::run;
            snapshot.context.cpu_registers[0] = 0;
        } else if (snapshot.status == ThreadStatus::suspend && !snapshot.vm_suspended) {
            // Suspensions caused by the UI pause (menu/background) must not
            // survive a load; only explicit guest/debugger suspensions do.
            snapshot.status = ThreadStatus::run;
        }
        if (snapshot.delete_requested || snapshot.exit_requested)
            continue;

        restored.push_back(std::move(item));
    }

    if (!reader.ok()) {
        error = "invalid thread table";
        return false;
    }

    deferred.clear();
    for (RestoredThread &item : restored) {
        ThreadStatePtr thread = kernel.create_thread_from_snapshot(emuenv.mem, item.snapshot, true);
        if (!thread) {
            error = "failed to recreate thread";
            return false;
        }
        for (const SceUID uid : item.callback_uids) {
            const auto it = kernel.callbacks.find(uid);
            if (it != kernel.callbacks.end())
                thread->callbacks.push_back(it->second);
        }
        for (const SceUID uid : item.waiting_thread_uids) {
            const ThreadStatePtr waiter = kernel.get_thread(uid);
            if (waiter)
                thread->waiting_threads.push_back(waiter);
        }
        deferred.push_back(item.snapshot);
    }

    return true;
}

bool write_kernel_base(EmuEnvState &emuenv, BufferWriter &buffer) {
    KernelState &kernel = emuenv.kernel;

    buffer.i32(kernel.peek_next_uid());
    buffer.u32(emuenv.main_thread_id);
    buffer.boolean(kernel.accurate_thread_scheduling);
    buffer.u32(kernel.tls_address.address());
    buffer.u32(kernel.tls_psize);
    buffer.u32(kernel.tls_msize);
    buffer.u32(kernel.thread_event_start.address());
    buffer.u32(kernel.thread_event_start_arg);
    buffer.u32(kernel.thread_event_end.address());
    buffer.u32(kernel.thread_event_end_arg);
    buffer.u64(kernel.start_tick);
    buffer.u64(kernel.base_tick.tick);
    buffer.u32(kernel.process_param.address());
    buffer.u32(kernel.client_vtable.address());
    buffer.u32(kernel.shellsvc_client.address());
    buffer.u32(kernel.libc_dso_handle_main.address());
    buffer.u32(kernel.halt_instruction_pc);
    for (int i = 0; i < KernelState::EXCEPTION_HANDLER_MAX; i++)
        buffer.u32(kernel.exception_handlers[i].load());

    write_map(buffer, kernel.codec_blocks, [&buffer](const auto &entry) {
        buffer.i32(entry.first);
        buffer.u32(entry.second.size);
        buffer.i32(entry.second.vaddr);
    });

    write_map(buffer, kernel.loaded_modules, [&buffer](const auto &entry) {
        buffer.i32(entry.first);
        if (!entry.second) {
            buffer.boolean(false);
            return;
        }
        buffer.boolean(true);
        buffer.value(entry.second->info);
        buffer.u32(entry.second->info_segment_address.address());
        buffer.u32(entry.second->info_offset);
    });

    write_map(buffer, kernel.loaded_sysmodules, [&buffer](const auto &entry) {
        buffer.u32(static_cast<uint32_t>(entry.first));
        buffer.list<SceUID>(entry.second, [&buffer](SceUID uid) { buffer.i32(uid); });
    });

    buffer.list<SceSysmoduleInternalModuleId>(kernel.loaded_internal_sysmodules,
        [&buffer](SceSysmoduleInternalModuleId id) { buffer.u32(static_cast<uint32_t>(id)); });

    write_map(buffer, kernel.export_nids, [&buffer](const auto &entry) {
        buffer.u32(entry.first);
        buffer.u32(entry.second);
    });
    write_map(buffer, kernel.export_nids_by_lib, [&buffer](const auto &entry) {
        buffer.u64(entry.first);
        buffer.u32(entry.second);
    });
    write_map(buffer, kernel.export_nid_owners, [&buffer](const auto &entry) {
        buffer.u32(entry.first);
        buffer.u32(entry.second);
    });
    write_map(buffer, kernel.func_binding_infos, [&buffer](const auto &entry) {
        buffer.u32(entry.first);
        buffer.u32(entry.second.entry_address);
        buffer.u32(entry.second.library_nid);
    });
    write_map(buffer, kernel.var_binding_infos, [&buffer](const auto &entry) {
        buffer.u32(entry.first);
        buffer.u32(entry.second.entries ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(entry.second.entries)) : 0);
        buffer.u32(entry.second.size);
        buffer.u32(entry.second.module_nid);
    });
    write_map(buffer, kernel.nid_libraries, [&buffer](const auto &entry) {
        buffer.u32(entry.first);
        buffer.str(entry.second);
    });
    write_map(buffer, kernel.module_uid_by_nid, [&buffer](const auto &entry) {
        buffer.u32(entry.first);
        buffer.u32(entry.second);
    });

    buffer.boolean(emuenv.gxm.params.flags != 0);
    return true;
}

bool read_kernel_base(EmuEnvState &emuenv, BufferReader &reader, std::string &error) {
    KernelState &kernel = emuenv.kernel;

    const SceUID next_uid = reader.i32();
    const SceUID main_thread_id = reader.u32();
    const bool accurate_scheduling = reader.boolean();
    const Address tls_address = reader.u32();
    const uint32_t tls_psize = reader.u32();
    const uint32_t tls_msize = reader.u32();
    const Address thread_event_start = reader.u32();
    const uint32_t thread_event_start_arg = reader.u32();
    const Address thread_event_end = reader.u32();
    const uint32_t thread_event_end_arg = reader.u32();
    const uint64_t start_tick = reader.u64();
    const uint64_t base_tick = reader.u64();
    const Address process_param = reader.u32();
    const Address client_vtable = reader.u32();
    const Address shellsvc_client = reader.u32();
    const Address libc_dso_handle_main = reader.u32();
    const Address halt_instruction_pc = reader.u32();
    Address exception_handlers[KernelState::EXCEPTION_HANDLER_MAX]{};
    for (int i = 0; i < KernelState::EXCEPTION_HANDLER_MAX; i++)
        exception_handlers[i] = reader.u32();

    if (!reader.ok()) {
        error = "invalid kernel section";
        return false;
    }

    kernel.clear_paused_threads_state();
    kernel.reset_world_stop_state();

    kernel.codec_blocks.clear();
    const uint32_t codec_count = reader.u32();
    for (uint32_t i = 0; i < codec_count && reader.ok(); i++) {
        CodecEngineBlock block{};
        const SceUID uid = reader.i32();
        block.size = reader.u32();
        block.vaddr = reader.i32();
        kernel.codec_blocks.emplace(uid, block);
    }

    kernel.loaded_modules.clear();
    const uint32_t module_count = reader.u32();
    for (uint32_t i = 0; i < module_count && reader.ok(); i++) {
        const SceUID uid = reader.i32();
        if (!reader.boolean())
            continue;
        auto module = std::make_shared<KernelModule>();
        module->info = reader.value<SceKernelModuleInfo>();
        module->info_segment_address = Ptr<const uint8_t>(reader.u32());
        module->info_offset = reader.u32();
        kernel.loaded_modules.emplace(uid, module);
    }

    kernel.loaded_sysmodules.clear();
    const uint32_t sysmodule_count = reader.u32();
    for (uint32_t i = 0; i < sysmodule_count && reader.ok(); i++) {
        const auto id = static_cast<SceSysmoduleModuleId>(reader.u32());
        kernel.loaded_sysmodules.emplace(id, reader.list<SceUID>([&reader]() { return reader.i32(); }));
    }

    kernel.loaded_internal_sysmodules = reader.list<SceSysmoduleInternalModuleId>(
        [&reader]() { return static_cast<SceSysmoduleInternalModuleId>(reader.u32()); });

    kernel.export_nids.clear();
    const uint32_t export_count = reader.u32();
    for (uint32_t i = 0; i < export_count && reader.ok(); i++) {
        const uint32_t nid = reader.u32();
        kernel.export_nids.emplace(nid, reader.u32());
    }

    kernel.export_nids_by_lib.clear();
    const uint32_t lib_export_count = reader.u32();
    for (uint32_t i = 0; i < lib_export_count && reader.ok(); i++) {
        const uint64_t key = reader.u64();
        kernel.export_nids_by_lib.emplace(key, reader.u32());
    }

    kernel.export_nid_owners.clear();
    const uint32_t owner_count = reader.u32();
    for (uint32_t i = 0; i < owner_count && reader.ok(); i++) {
        const uint32_t nid = reader.u32();
        kernel.export_nid_owners.emplace(nid, reader.u32());
    }

    kernel.func_binding_infos.clear();
    const uint32_t func_count = reader.u32();
    for (uint32_t i = 0; i < func_count && reader.ok(); i++) {
        const uint32_t nid = reader.u32();
        FuncBindingInfo info{};
        info.entry_address = reader.u32();
        info.library_nid = reader.u32();
        kernel.func_binding_infos.emplace(nid, info);
    }

    kernel.var_binding_infos.clear();
    const uint32_t var_count = reader.u32();
    for (uint32_t i = 0; i < var_count && reader.ok(); i++) {
        const uint32_t nid = reader.u32();
        VarBindingInfo info{};
        const Address entries = reader.u32();
        info.entries = entries ? Ptr<void>(entries).get(emuenv.mem) : nullptr;
        info.size = reader.u32();
        info.module_nid = reader.u32();
        kernel.var_binding_infos.emplace(nid, info);
    }

    kernel.nid_libraries.clear();
    const uint32_t library_count = reader.u32();
    for (uint32_t i = 0; i < library_count && reader.ok(); i++) {
        const uint32_t nid = reader.u32();
        kernel.nid_libraries.emplace(nid, reader.str());
    }

    kernel.module_uid_by_nid.clear();
    const uint32_t uid_count = reader.u32();
    for (uint32_t i = 0; i < uid_count && reader.ok(); i++) {
        const uint32_t nid = reader.u32();
        kernel.module_uid_by_nid.emplace(nid, reader.u32());
    }

    (void)reader.boolean();

    if (!reader.ok()) {
        error = "invalid kernel section";
        return false;
    }

    kernel.thread_event_start = Ptr<const void>(thread_event_start);
    kernel.thread_event_start_arg = thread_event_start_arg;
    kernel.thread_event_end = Ptr<const void>(thread_event_end);
    kernel.thread_event_end_arg = thread_event_end_arg;
    kernel.tls_address = Ptr<const void>(tls_address);
    kernel.tls_psize = tls_psize;
    kernel.tls_msize = tls_msize;
    kernel.accurate_thread_scheduling = accurate_scheduling;
    kernel.start_tick = start_tick;
    kernel.base_tick = { base_tick };
    kernel.process_param = Ptr<SceProcessParam>(process_param);
    kernel.client_vtable = Ptr<void>(client_vtable);
    kernel.shellsvc_client = Ptr<Address>(shellsvc_client);
    kernel.libc_dso_handle_main = Ptr<void>(libc_dso_handle_main);
    kernel.halt_instruction_pc = halt_instruction_pc;
    for (int i = 0; i < KernelState::EXCEPTION_HANDLER_MAX; i++)
        kernel.exception_handlers[i].store(exception_handlers[i]);

    emuenv.main_thread_id = main_thread_id;
    kernel.set_next_uid(next_uid);

    return true;
}

bool write_callbacks(KernelState &kernel, BufferWriter &buffer) {
    write_map(buffer, kernel.callbacks, [&buffer](const auto &entry) {
        const CallbackPtr &callback = entry.second;
        buffer.i32(entry.first);
        if (!callback) {
            buffer.boolean(false);
            return;
        }
        buffer.boolean(true);
        const Callback::Snapshot snapshot = callback->capture_snapshot();
        buffer.i32(callback->get_owner_thread_id());
        buffer.str(callback->get_name());
        buffer.u32(callback->get_callback_function().address());
        buffer.u32(callback->get_user_common_ptr().address());
        buffer.u32(snapshot.num_notifications);
        buffer.i32(snapshot.notification_arg);
        buffer.i32(snapshot.notifier_id);
    });
    return true;
}

bool read_callbacks(KernelState &kernel, BufferReader &reader, std::string &error) {
    kernel.callbacks.clear();
    const uint32_t count = reader.u32();
    for (uint32_t i = 0; i < count && reader.ok(); i++) {
        const SceUID uid = reader.i32();
        if (!reader.boolean())
            continue;
        const SceUID owner = reader.i32();
        std::string name = reader.str();
        const Address function = reader.u32();
        const Address userdata = reader.u32();
        Callback::Snapshot snapshot;
        snapshot.num_notifications = reader.u32();
        snapshot.notification_arg = reader.i32();
        snapshot.notifier_id = reader.i32();
        auto callback = std::make_shared<Callback>(owner, name, Ptr<SceKernelCallbackFunction>(function), Ptr<void>(userdata));
        callback->apply_snapshot(snapshot);
        kernel.callbacks.emplace(uid, callback);
    }
    if (!reader.ok()) {
        error = "invalid callback table";
        return false;
    }
    return true;
}

bool write_kernel_sync(EmuEnvState &emuenv, Writer &writer, std::string &error) {
    KernelState &kernel = emuenv.kernel;
    BufferWriter buffer;
    buffer.u64(host_time_us());

    write_map(buffer, kernel.simple_events, [&buffer](const auto &entry) {
        buffer.i32(entry.first);
        buffer.i32(entry.second->uid);
        buffer.u32(entry.second->attr);
        buffer.append(entry.second->name, sizeof(entry.second->name));
        buffer.u32(entry.second->pattern);
        buffer.u64(entry.second->last_user_data);
        buffer.boolean(entry.second->auto_reset);
        buffer.boolean(entry.second->cb_wakeup_only);
    });

    write_map(buffer, kernel.timers, [&buffer](const auto &entry) {
        const Timer &timer = *entry.second;
        buffer.i32(entry.first);
        buffer.i32(timer.uid);
        buffer.u32(timer.attr);
        buffer.append(timer.name, sizeof(timer.name));
        buffer.boolean(timer.is_started);
        buffer.boolean(timer.is_repeat);
        buffer.boolean(timer.is_pulse);
        buffer.boolean(timer.event_set);
        buffer.u64(timer.time);
        buffer.u64(timer.next_event);
        buffer.u64(timer.event_interval);
    });

    write_map(buffer, kernel.semaphores, [&buffer](const auto &entry) {
        const Semaphore &semaphore = *entry.second;
        buffer.i32(entry.first);
        buffer.i32(semaphore.uid);
        buffer.u32(semaphore.attr);
        buffer.append(semaphore.name, sizeof(semaphore.name));
        buffer.i32(semaphore.max);
        buffer.i32(semaphore.val);
        buffer.i32(semaphore.init_val);
    });

    const auto write_mutexes = [&buffer](const MutexPtrs &mutexes) {
        write_map(buffer, mutexes, [&buffer](const auto &entry) {
            const Mutex &mutex = *entry.second;
            buffer.i32(entry.first);
            buffer.i32(mutex.uid);
            buffer.u32(mutex.attr);
            buffer.append(mutex.name, sizeof(mutex.name));
            buffer.i32(mutex.init_count);
            buffer.i32(mutex.lock_count);
            buffer.i32(mutex.owner ? mutex.owner->id : 0);
            buffer.u32(mutex.workarea.address());
            buffer.boolean(mutex.deleted.load());
        });
    };
    write_mutexes(kernel.mutexes);
    write_mutexes(kernel.lwmutexes);

    const auto write_condvars = [&buffer](const CondvarPtrs &condvars) {
        write_map(buffer, condvars, [&buffer](const auto &entry) {
            const Condvar &condvar = *entry.second;
            buffer.i32(entry.first);
            buffer.i32(condvar.uid);
            buffer.u32(condvar.attr);
            buffer.append(condvar.name, sizeof(condvar.name));
            buffer.i32(condvar.associated_mutex ? condvar.associated_mutex->uid : 0);
        });
    };
    write_condvars(kernel.condvars);
    write_condvars(kernel.lwcondvars);

    write_map(buffer, kernel.rwlocks, [&buffer](const auto &entry) {
        const RWLock &lock = *entry.second;
        buffer.i32(entry.first);
        buffer.i32(lock.uid);
        buffer.u32(lock.attr);
        buffer.append(lock.name, sizeof(lock.name));
        buffer.u32(static_cast<uint32_t>(lock.state));
        buffer.u32(static_cast<uint32_t>(lock.owners.size()));
        for (const auto &owner : lock.owners) {
            buffer.i32(owner.first ? owner.first->id : 0);
            buffer.i32(owner.second);
        }
    });

    write_map(buffer, kernel.eventflags, [&buffer](const auto &entry) {
        const EventFlag &eventflag = *entry.second;
        buffer.i32(entry.first);
        buffer.i32(eventflag.uid);
        buffer.u32(eventflag.attr);
        buffer.append(eventflag.name, sizeof(eventflag.name));
        buffer.i32(eventflag.flags);
    });

    write_map(buffer, kernel.msgpipes, [&buffer](const auto &entry) {
        const MsgPipe &pipe = *entry.second;
        buffer.i32(entry.first);
        buffer.i32(pipe.uid);
        buffer.u32(pipe.attr);
        buffer.append(pipe.name, sizeof(pipe.name));
        buffer.boolean(pipe.beingDeleted);
        buffer.u64(pipe.remainingThreads.load());
        const uint64_t capacity = pipe.data_buffer.Capacity();
        const uint64_t used = pipe.data_buffer.Used();
        buffer.u64(capacity);
        buffer.u64(used);
        if (used > 0) {
            std::vector<char> data(used);
            pipe.data_buffer.Peek(data.data(), data.size());
            buffer.append(data.data(), data.size());
        }
    });

    return writer.write_section(SectionId::KernelSync, buffer.data().data(), buffer.size(), true, error);
}

bool read_kernel_sync(EmuEnvState &emuenv, const std::vector<uint8_t> &data, std::string &error) {
    BufferReader reader(data.data(), data.size());
    KernelState &kernel = emuenv.kernel;
    const uint64_t saved_time = reader.u64();
    const uint64_t now = host_time_us();

    kernel.simple_events.clear();
    uint32_t count = reader.u32();
    for (uint32_t i = 0; i < count && reader.ok(); i++) {
        const SceUID uid = reader.i32();
        auto event = std::make_shared<SimpleEvent>();
        event->uid = reader.i32();
        event->attr = reader.u32();
        reader.bytes(event->name, sizeof(event->name));
        event->pattern = reader.u32();
        event->last_user_data = reader.u64();
        event->auto_reset = reader.boolean();
        event->cb_wakeup_only = reader.boolean();
        event->waiting_threads = make_wait_queue(event->attr);
        kernel.simple_events.emplace(uid, event);
    }

    kernel.timers.clear();
    count = reader.u32();
    for (uint32_t i = 0; i < count && reader.ok(); i++) {
        const SceUID uid = reader.i32();
        auto timer = std::make_shared<Timer>();
        timer->uid = reader.i32();
        timer->attr = reader.u32();
        reader.bytes(timer->name, sizeof(timer->name));
        timer->is_started = reader.boolean();
        timer->is_repeat = reader.boolean();
        timer->is_pulse = reader.boolean();
        timer->event_set = reader.boolean();
        const uint64_t time = reader.u64();
        const uint64_t next_event = reader.u64();
        timer->event_interval = reader.u64();
        if (timer->is_started && next_event != std::numeric_limits<uint64_t>::max()) {
            const uint64_t elapsed = now > saved_time ? now - saved_time : 0;
            timer->next_event = next_event + elapsed;
            if (timer->is_repeat && timer->event_interval > 0) {
                while (timer->next_event < now)
                    timer->next_event += timer->event_interval;
            }
        } else {
            timer->next_event = next_event;
        }
        timer->time = time;
        timer->waiting_threads = make_wait_queue(timer->attr);
        kernel.timers.emplace(uid, timer);
    }

    kernel.semaphores.clear();
    count = reader.u32();
    for (uint32_t i = 0; i < count && reader.ok(); i++) {
        const SceUID uid = reader.i32();
        auto semaphore = std::make_shared<Semaphore>();
        semaphore->uid = reader.i32();
        semaphore->attr = reader.u32();
        reader.bytes(semaphore->name, sizeof(semaphore->name));
        semaphore->max = reader.i32();
        semaphore->val = reader.i32();
        semaphore->init_val = reader.i32();
        semaphore->waiting_threads = make_wait_queue(semaphore->attr);
        kernel.semaphores.emplace(uid, semaphore);
    }

    const auto read_mutexes = [&kernel, &reader](MutexPtrs &mutexes, bool lightweight) {
        mutexes.clear();
        const uint32_t mutex_count = reader.u32();
        for (uint32_t i = 0; i < mutex_count && reader.ok(); i++) {
            const SceUID uid = reader.i32();
            auto mutex = std::make_shared<Mutex>();
            mutex->uid = reader.i32();
            mutex->attr = reader.u32();
            reader.bytes(mutex->name, sizeof(mutex->name));
            mutex->init_count = reader.i32();
            mutex->lock_count = reader.i32();
            const SceUID owner = reader.i32();
            mutex->owner = owner ? kernel.get_thread(owner) : nullptr;
            mutex->workarea = Ptr<SceKernelLwMutexWork>(reader.u32());
            mutex->deleted.store(reader.boolean());
            (void)lightweight;
            mutex->waiting_threads = make_wait_queue(mutex->attr);
            mutexes.emplace(uid, mutex);
        }
    };
    read_mutexes(kernel.mutexes, false);
    read_mutexes(kernel.lwmutexes, true);

    const auto read_condvars = [&kernel, &reader](CondvarPtrs &condvars, bool lightweight) {
        condvars.clear();
        const uint32_t condvar_count = reader.u32();
        for (uint32_t i = 0; i < condvar_count && reader.ok(); i++) {
            const SceUID uid = reader.i32();
            auto condvar = std::make_shared<Condvar>();
            condvar->uid = reader.i32();
            condvar->attr = reader.u32();
            reader.bytes(condvar->name, sizeof(condvar->name));
            const SceUID mutex_uid = reader.i32();
            const auto mutex = lightweight ? kernel.lwmutexes.find(mutex_uid) : kernel.mutexes.find(mutex_uid);
            condvar->associated_mutex = mutex != (lightweight ? kernel.lwmutexes.end() : kernel.mutexes.end()) ? mutex->second : nullptr;
            condvar->waiting_threads = make_wait_queue(condvar->attr);
            condvars.emplace(uid, condvar);
        }
    };
    read_condvars(kernel.condvars, false);
    read_condvars(kernel.lwcondvars, true);

    kernel.rwlocks.clear();
    count = reader.u32();
    for (uint32_t i = 0; i < count && reader.ok(); i++) {
        const SceUID uid = reader.i32();
        auto lock = std::make_shared<RWLock>();
        lock->uid = reader.i32();
        lock->attr = reader.u32();
        reader.bytes(lock->name, sizeof(lock->name));
        lock->state = static_cast<RWLockState>(reader.u32());
        const uint32_t owner_count = reader.u32();
        for (uint32_t owner = 0; owner < owner_count && reader.ok(); owner++) {
            const SceUID thread_id = reader.i32();
            const int32_t lock_count = reader.i32();
            const ThreadStatePtr thread = thread_id ? kernel.get_thread(thread_id) : nullptr;
            if (thread)
                lock->owners.emplace(thread, lock_count);
        }
        lock->waiting_threads = make_wait_queue(lock->attr);
        kernel.rwlocks.emplace(uid, lock);
    }

    kernel.eventflags.clear();
    count = reader.u32();
    for (uint32_t i = 0; i < count && reader.ok(); i++) {
        const SceUID uid = reader.i32();
        auto eventflag = std::make_shared<EventFlag>();
        eventflag->uid = reader.i32();
        eventflag->attr = reader.u32();
        reader.bytes(eventflag->name, sizeof(eventflag->name));
        eventflag->flags = reader.i32();
        eventflag->waiting_threads = make_wait_queue(eventflag->attr);
        kernel.eventflags.emplace(uid, eventflag);
    }

    kernel.msgpipes.clear();
    count = reader.u32();
    for (uint32_t i = 0; i < count && reader.ok(); i++) {
        const SceUID uid = reader.i32();
        const uint32_t base_uid = reader.i32();
        const uint32_t base_attr = reader.u32();
        char name[KERNELOBJECT_MAX_NAME_LENGTH + 1]{};
        reader.bytes(name, sizeof(name));
        const bool being_deleted = reader.boolean();
        const uint64_t remaining = reader.u64();
        const uint64_t capacity = reader.u64();
        const uint64_t used = reader.u64();
        if (capacity > (16u << 20)) {
            error = "invalid message pipe capacity";
            return false;
        }
        auto pipe = std::make_shared<MsgPipe>(static_cast<std::size_t>(capacity));
        pipe->uid = base_uid;
        pipe->attr = base_attr;
        copy_object_name(pipe->name, sizeof(pipe->name), name);
        pipe->beingDeleted = being_deleted;
        pipe->remainingThreads.store(remaining);
        pipe->senders = make_wait_queue(pipe->attr);
        pipe->receivers = make_wait_queue(pipe->attr);
        if (used > 0) {
            std::vector<char> data(used);
            if (!reader.bytes(data.data(), data.size())) {
                error = "invalid message pipe data";
                return false;
            }
            pipe->data_buffer.Insert(data.data(), data.size());
        }
        kernel.msgpipes.emplace(uid, pipe);
    }

    if (!reader.ok()) {
        error = "invalid kernel sync state";
        return false;
    }
    return true;
}

bool write_display(EmuEnvState &emuenv, Writer &writer, std::string &error) {
    DisplayState &display = emuenv.display;
    BufferWriter buffer;

    buffer.i32(display.viewport_drawable_w);
    buffer.i32(display.viewport_drawable_h);
    buffer.value(display.viewport_x);
    buffer.value(display.viewport_y);
    buffer.value(display.viewport_w);
    buffer.value(display.viewport_h);
    buffer.value(display.sce_frame);
    {
        const std::lock_guard<std::mutex> lock(display.display_info_mutex);
        buffer.value(display.next_rendered_frame);
    }
    buffer.u64(display.vblank_count.load());
    buffer.u64(display.last_setframe_vblank_count.load());
    buffer.boolean(display.fps_hack);
    buffer.u32(static_cast<uint32_t>(display.vblank_callbacks.size()));
    for (const auto &[uid, callback] : display.vblank_callbacks)
        buffer.i32(uid);

    return writer.write_section(SectionId::Display, buffer.data().data(), buffer.size(), true, error);
}

bool read_display(EmuEnvState &emuenv, const std::vector<uint8_t> &data, std::string &error) {
    DisplayState &display = emuenv.display;
    BufferReader reader(data.data(), data.size());

    display.viewport_drawable_w = reader.i32();
    display.viewport_drawable_h = reader.i32();
    display.viewport_x = reader.value<float>();
    display.viewport_y = reader.value<float>();
    display.viewport_w = reader.value<float>();
    display.viewport_h = reader.value<float>();
    display.sce_frame = reader.value<DisplayFrameInfo>();
    {
        const std::lock_guard<std::mutex> lock(display.display_info_mutex);
        display.next_rendered_frame = reader.value<DisplayFrameInfo>();
    }
    display.vblank_count.store(reader.u64());
    display.last_setframe_vblank_count.store(reader.u64());
    display.fps_hack = reader.boolean();

    const uint32_t callback_count = reader.u32();
    std::vector<SceUID> callback_uids;
    callback_uids.reserve(callback_count);
    for (uint32_t i = 0; i < callback_count && reader.ok(); i++)
        callback_uids.push_back(reader.i32());

    if (!reader.ok()) {
        error = "invalid display state";
        return false;
    }

    {
        const std::lock_guard<std::mutex> lock(display.mutex);
        display.vblank_wait_infos.clear();
        display.vblank_callbacks.clear();
        for (const SceUID uid : callback_uids) {
            const auto it = emuenv.kernel.callbacks.find(uid);
            if (it != emuenv.kernel.callbacks.end())
                display.vblank_callbacks.emplace(uid, it->second);
        }
    }

    display.predicted_frames.clear();
    display.predicted_frame_position = static_cast<uint32_t>(-1);
    display.predicted_cycles_seen = 0;
    display.predicting.store(false);
    display.current_sync_object.store(0);

    return true;
}

bool write_gxm(EmuEnvState &emuenv, Writer &writer, std::string &error) {
    BufferWriter buffer;
    GxmState &gxm = emuenv.gxm;

    buffer.value(gxm.params);
    buffer.u32(gxm.global_timestamp.load());
    buffer.u32(gxm.last_display_global);
    buffer.u32(gxm.notification_region.address());
    buffer.u32(gxm.last_immediate_context);

    const auto contexts = gxm::capture_contexts(emuenv);
    buffer.u32(static_cast<uint32_t>(contexts.size()));
    for (const gxm::ContextSnapshot &context : contexts) {
        buffer.u32(context.address);
        buffer.boolean(context.deferred);
        buffer.value(context.state);
        buffer.boolean(context.last_precomputed);
        buffer.u64(context.command_next_free_pos);
        buffer.u32(context.alloc_space.address());
        buffer.u32(context.alloc_space_end.address());
        buffer.u32(context.command_allocator_size);
    }

    const auto sync_objects = gxm::capture_sync_objects(emuenv);
    buffer.u32(static_cast<uint32_t>(sync_objects.size()));
    for (const gxm::SyncObjectSnapshot &sync : sync_objects) {
        buffer.u32(sync.address);
        buffer.u32(sync.timestamp_current);
        buffer.u32(sync.timestamp_ahead);
        buffer.u32(sync.last_display);
        buffer.u32(sync.last_operation_global);
    }

    const auto render_targets = gxm::capture_render_targets(emuenv);
    buffer.u32(static_cast<uint32_t>(render_targets.size()));
    for (const gxm::RenderTargetSnapshot &render_target : render_targets) {
        buffer.u32(render_target.address);
        buffer.u16(render_target.width);
        buffer.u16(render_target.height);
        buffer.u16(render_target.scenes_per_frame);
        buffer.i32(render_target.driver_mem_block);
        buffer.value(render_target.params);
    }

    const auto fragment_programs = gxm::capture_fragment_programs(emuenv);
    buffer.u32(static_cast<uint32_t>(fragment_programs.size()));
    for (const gxm::FragmentProgramSnapshot &program : fragment_programs) {
        buffer.u32(program.address);
        buffer.u32(program.program.address());
        buffer.boolean(program.has_blend);
        buffer.value(program.blend);
        buffer.boolean(program.is_mask_update);
        buffer.u32(program.reference_count);
    }

    const auto vertex_programs = gxm::capture_vertex_programs(emuenv);
    buffer.u32(static_cast<uint32_t>(vertex_programs.size()));
    for (const gxm::VertexProgramSnapshot &program : vertex_programs) {
        buffer.u32(program.address);
        buffer.u32(program.program.address());
        buffer.u64(program.key_hash);
        buffer.u32(program.reference_count);
        buffer.list<SceGxmVertexAttribute>(program.attributes, [&buffer](const SceGxmVertexAttribute &attribute) { buffer.value(attribute); });
        buffer.list<SceGxmVertexStream>(program.streams, [&buffer](const SceGxmVertexStream &stream) { buffer.value(stream); });
    }

    const auto shader_patchers = gxm::capture_shader_patchers(emuenv);
    buffer.u32(static_cast<uint32_t>(shader_patchers.size()));
    for (const gxm::ShaderPatcherSnapshot &patcher : shader_patchers) {
        buffer.u32(patcher.address);
        buffer.value(patcher.params);
    }

    const std::vector<DisplayCallback> display_entries = gxm.display_queue.snapshot_items();
    buffer.u32(static_cast<uint32_t>(display_entries.size()));
    for (const DisplayCallback &entry : display_entries)
        buffer.value(entry);

    write_map(buffer, gxm.memory_mapped_regions, [&buffer](const auto &entry) {
        buffer.u32(entry.first);
        buffer.u32(entry.second.offset);
        buffer.u32(entry.second.size);
        buffer.u32(entry.second.perm);
    });

    return writer.write_section(SectionId::Gxm, buffer.data().data(), buffer.size(), true, error);
}

bool read_gxm(EmuEnvState &emuenv, const std::vector<uint8_t> &data, std::string &error) {
    BufferReader reader(data.data(), data.size());
    GxmState &gxm = emuenv.gxm;

    const SceGxmInitializeParams params = reader.value<SceGxmInitializeParams>();
    const uint32_t global_timestamp = reader.u32();
    const uint32_t last_display_global = reader.u32();
    const Address notification_region = reader.u32();
    const Address last_immediate_context = reader.u32();

    const uint32_t context_count = reader.u32();
    if (!reader.ok() || context_count > 4096) {
        error = "invalid GXM context table";
        return false;
    }
    std::vector<gxm::ContextSnapshot> contexts(context_count);
    for (gxm::ContextSnapshot &context : contexts) {
        context.address = reader.u32();
        context.deferred = reader.boolean();
        context.state = reader.value<GxmContextState>();
        context.last_precomputed = reader.boolean();
        context.command_next_free_pos = reader.u64();
        context.alloc_space = Ptr<void>(reader.u32());
        context.alloc_space_end = Ptr<void>(reader.u32());
        context.command_allocator_size = reader.u32();
    }

    const uint32_t sync_count = reader.u32();
    if (!reader.ok() || sync_count > 65536) {
        error = "invalid GXM sync object table";
        return false;
    }
    std::vector<gxm::SyncObjectSnapshot> sync_objects(sync_count);
    for (gxm::SyncObjectSnapshot &sync : sync_objects) {
        sync.address = reader.u32();
        sync.timestamp_current = reader.u32();
        sync.timestamp_ahead = reader.u32();
        sync.last_display = reader.u32();
        sync.last_operation_global = reader.u32();
    }

    const uint32_t render_target_count = reader.u32();
    if (!reader.ok() || render_target_count > 4096) {
        error = "invalid GXM render target table";
        return false;
    }
    std::vector<gxm::RenderTargetSnapshot> render_targets(render_target_count);
    for (gxm::RenderTargetSnapshot &render_target : render_targets) {
        render_target.address = reader.u32();
        render_target.width = reader.u16();
        render_target.height = reader.u16();
        render_target.scenes_per_frame = reader.u16();
        render_target.driver_mem_block = reader.i32();
        render_target.params = reader.value<SceGxmRenderTargetParams>();
    }

    const uint32_t fragment_program_count = reader.u32();
    if (!reader.ok() || fragment_program_count > 65536) {
        error = "invalid GXM fragment program table";
        return false;
    }
    std::vector<gxm::FragmentProgramSnapshot> fragment_programs(fragment_program_count);
    for (gxm::FragmentProgramSnapshot &program : fragment_programs) {
        program.address = reader.u32();
        program.program = Ptr<const SceGxmProgram>(reader.u32());
        program.has_blend = reader.boolean();
        program.blend = reader.value<SceGxmBlendInfo>();
        program.is_mask_update = reader.boolean();
        program.reference_count = reader.u32();
    }

    const uint32_t vertex_program_count = reader.u32();
    if (!reader.ok() || vertex_program_count > 65536) {
        error = "invalid GXM vertex program table";
        return false;
    }
    std::vector<gxm::VertexProgramSnapshot> vertex_programs(vertex_program_count);
    for (gxm::VertexProgramSnapshot &program : vertex_programs) {
        program.address = reader.u32();
        program.program = Ptr<const SceGxmProgram>(reader.u32());
        program.key_hash = reader.u64();
        program.reference_count = reader.u32();
        program.attributes = reader.list<SceGxmVertexAttribute>([&reader]() { return reader.value<SceGxmVertexAttribute>(); });
        program.streams = reader.list<SceGxmVertexStream>([&reader]() { return reader.value<SceGxmVertexStream>(); });
    }

    const uint32_t shader_patcher_count = reader.u32();
    if (!reader.ok() || shader_patcher_count > 65536) {
        error = "invalid GXM shader patcher table";
        return false;
    }
    std::vector<gxm::ShaderPatcherSnapshot> shader_patchers(shader_patcher_count);
    for (gxm::ShaderPatcherSnapshot &patcher : shader_patchers) {
        patcher.address = reader.u32();
        patcher.params = reader.value<SceGxmShaderPatcherParams>();
    }

    const uint32_t display_entry_count = reader.u32();
    if (!reader.ok() || display_entry_count > 16) {
        error = "invalid GXM display queue";
        return false;
    }
    gxm.restored_display_queue.clear();
    gxm.restored_display_queue.reserve(display_entry_count);
    for (uint32_t i = 0; i < display_entry_count; i++)
        gxm.restored_display_queue.push_back(reader.value<DisplayCallback>());

    std::map<Address, MemoryMapInfo> regions;
    const uint32_t region_count = reader.u32();
    for (uint32_t i = 0; i < region_count && reader.ok(); i++) {
        const Address address = reader.u32();
        MemoryMapInfo info{};
        info.offset = reader.u32();
        info.size = reader.u32();
        info.perm = reader.u32();
        regions.emplace(address, info);
    }

    if (!reader.ok()) {
        error = "invalid GXM state";
        return false;
    }

    emuenv.renderer->gxp_ptr_map.clear();
    LOG_CRITICAL("[savestate-error] gxm: patchers");
    gxm::restore_shader_patchers(emuenv, shader_patchers);
    LOG_CRITICAL("[savestate-error] gxm: fragment programs");
    gxm::restore_fragment_programs(emuenv, fragment_programs);
    LOG_CRITICAL("[savestate-error] gxm: vertex programs");
    gxm::restore_vertex_programs(emuenv, vertex_programs);
    LOG_CRITICAL("[savestate-error] gxm: memory regions");
    gxm::restore_memory_regions(emuenv, regions);
    LOG_CRITICAL("[savestate-error] gxm: sync objects");
    gxm::restore_sync_objects(emuenv, sync_objects);
    LOG_CRITICAL("[savestate-error] gxm: contexts");
    gxm::restore_contexts(emuenv, contexts);
    LOG_CRITICAL("[savestate-error] gxm: render targets");
    gxm::restore_render_targets(emuenv, render_targets);
    LOG_CRITICAL("[savestate-error] gxm: done");

    gxm.params = params;
    gxm.global_timestamp.store(global_timestamp);
    gxm.last_display_global = last_display_global;
    gxm.notification_region = Ptr<uint32_t>(notification_region);
    gxm.last_immediate_context = last_immediate_context;

    return true;
}

bool write_sysmem(EmuEnvState &emuenv, BufferWriter &buffer) {
    auto *sysmem = emuenv.kernel.obj_store.get<SysmemState>();
    const std::lock_guard<std::mutex> lock(sysmem->mutex);
    buffer.i32(sysmem->next_uid);
    buffer.u32(sysmem->allocated_user);
    buffer.u32(sysmem->allocated_cdram);
    buffer.u32(sysmem->allocated_phycont);
    const auto write_blocks = [&buffer](const Blocks &blocks) {
        write_map(buffer, blocks, [&buffer](const auto &entry) {
            buffer.i32(entry.first);
            if (entry.second)
                buffer.value(*entry.second);
        });
    };
    write_blocks(sysmem->blocks);
    write_blocks(sysmem->vm_blocks);
    return true;
}

bool read_sysmem(EmuEnvState &emuenv, BufferReader &reader, std::string &error) {
    auto *sysmem = emuenv.kernel.obj_store.get<SysmemState>();
    const std::lock_guard<std::mutex> lock(sysmem->mutex);
    sysmem->next_uid = reader.i32();
    sysmem->allocated_user = reader.u32();
    sysmem->allocated_cdram = reader.u32();
    sysmem->allocated_phycont = reader.u32();
    const auto read_blocks = [&reader](Blocks &blocks) {
        blocks.clear();
        const uint32_t count = reader.u32();
        for (uint32_t i = 0; i < count && reader.ok(); i++) {
            const SceUID uid = reader.i32();
            auto block = std::make_shared<KernelMemBlock>();
            *block = reader.value<KernelMemBlock>();
            blocks.emplace(uid, block);
        }
    };
    read_blocks(sysmem->blocks);
    read_blocks(sysmem->vm_blocks);
    if (!reader.ok()) {
        error = "invalid sysmem state";
        return false;
    }
    return true;
}

bool write_obj_store(EmuEnvState &emuenv, Writer &writer, std::string &error) {
    BufferWriter buffer;
    write_sysmem(emuenv, buffer);
    return writer.write_section(SectionId::ObjStore, buffer.data().data(), buffer.size(), true, error);
}

bool read_obj_store(EmuEnvState &emuenv, const std::vector<uint8_t> &data, std::string &error) {
    BufferReader reader(data.data(), data.size());
    return read_sysmem(emuenv, reader, error);
}

static void write_blob(BufferWriter &buffer, const std::vector<uint8_t> &blob) {
    buffer.u32(static_cast<uint32_t>(blob.size()));
    if (!blob.empty())
        buffer.append(blob.data(), blob.size());
}

static bool read_blob(BufferReader &reader, std::vector<uint8_t> &blob) {
    const uint32_t size = reader.u32();
    if (!reader.ok() || size > (64u << 20)) {
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

bool write_ngs(EmuEnvState &emuenv, Writer &writer, std::string &error) {
    BufferWriter buffer;
    Ptr<ngs::VoiceDefinition> definitions;
    std::vector<ngs::SystemInitInfo> systems;
    std::vector<ngs::RackInitInfo> racks;
    ngs::capture_state(emuenv.ngs, emuenv.mem, definitions, systems, racks);

    buffer.u32(definitions.address());
    buffer.u32(static_cast<uint32_t>(systems.size()));
    for (const ngs::SystemInitInfo &info : systems) {
        buffer.u32(info.address);
        buffer.u32(info.memspace.address());
        buffer.u32(info.memspace_size);
        buffer.value(info.params);
        buffer.u32(static_cast<uint32_t>(info.queued_voices.size()));
        for (const auto &[rack_index, voice_index] : info.queued_voices) {
            buffer.u32(rack_index);
            buffer.u32(voice_index);
        }
    }

    buffer.u32(static_cast<uint32_t>(racks.size()));
    for (const ngs::RackInitInfo &info : racks) {
        buffer.u32(info.address);
        buffer.u32(info.system_address);
        buffer.u32(info.system.address());
        buffer.value(info.info);
        buffer.value(info.description);
        buffer.u32(static_cast<uint32_t>(info.blocks.size()));
        for (const MemspaceBlockAllocator::Block &block : info.blocks) {
            buffer.u32(block.size);
            buffer.u32(block.offset);
            buffer.boolean(block.free);
        }
        buffer.u32(static_cast<uint32_t>(info.voices.size()));
        for (const ngs::VoiceInfo &voice : info.voices) {
            buffer.u32(voice.address);
            buffer.u32(voice.state);
            buffer.boolean(voice.is_pending);
            buffer.boolean(voice.is_paused);
            buffer.boolean(voice.is_keyed_off);
            buffer.u32(voice.frame_count);
            buffer.append(voice.implicit_volume_matrix, sizeof(voice.implicit_volume_matrix));
            buffer.u32(voice.finished_callback);
            buffer.u32(voice.finished_callback_user_data);
            buffer.u32(static_cast<uint32_t>(voice.patches.size()));
            for (const std::vector<Address> &port : voice.patches) {
                buffer.u32(static_cast<uint32_t>(port.size()));
                for (const Address patch : port)
                    buffer.u32(patch);
            }
            buffer.u32(static_cast<uint32_t>(voice.modules.size()));
            for (const ngs::ModuleDataInfo &module : voice.modules) {
                buffer.boolean(module.is_bypassed);
                buffer.u8(module.flags);
                write_blob(buffer, module.guest_state_data);
                write_blob(buffer, module.parameters);
                write_blob(buffer, module.last_info);
                write_blob(buffer, module.logical_state);
            }
        }
    }

    return writer.write_section(SectionId::Ngs, buffer.data().data(), buffer.size(), true, error);
}

bool read_ngs(EmuEnvState &emuenv, const std::vector<uint8_t> &data, std::string &error) {
    BufferReader reader(data.data(), data.size());

    const Address definitions = reader.u32();
    const uint32_t system_count = reader.u32();
    if (!reader.ok() || system_count > 4096) {
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
        if (!reader.ok() || queued_count > 65536) {
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
    if (!reader.ok() || rack_count > 65536) {
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
        if (!reader.ok() || block_count > 65536) {
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
        if (!reader.ok() || voice_count > 4096) {
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
            if (!reader.ok() || port_count > 64) {
                error = "invalid NGS patch table";
                return false;
            }
            voice.patches.resize(port_count);
            for (std::vector<Address> &port : voice.patches) {
                const uint32_t patch_count = reader.u32();
                if (!reader.ok() || patch_count > 4096) {
                    error = "invalid NGS patch table";
                    return false;
                }
                port.resize(patch_count);
                for (Address &patch : port)
                    patch = reader.u32();
            }
            const uint32_t module_count = reader.u32();
            if (!reader.ok() || module_count > 1024) {
                error = "invalid NGS module table";
                return false;
            }
            voice.modules.resize(module_count);
            for (ngs::ModuleDataInfo &module : voice.modules) {
                module.is_bypassed = reader.boolean();
                module.flags = reader.u8();
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

    if (!reader.ok()) {
        error = "invalid NGS state";
        return false;
    }

    ngs::restore_state(emuenv.ngs, emuenv.mem, Ptr<ngs::VoiceDefinition>(definitions), systems, racks);
    return true;
}

bool write_audio(EmuEnvState &emuenv, Writer &writer, std::string &error) {
    BufferWriter buffer;
    uint32_t port_count = 0;
    {
        const std::lock_guard<std::mutex> lock(emuenv.audio.mutex);
        for (const auto &[id, port] : emuenv.audio.out_ports) {
            if (port && !port->stopping)
                port_count++;
        }
        buffer.u32(static_cast<uint32_t>(emuenv.audio.next_port_id));
        buffer.u32(port_count);
        for (const auto &[id, port] : emuenv.audio.out_ports) {
            if (!port || port->stopping)
                continue;
            buffer.i32(id);
            buffer.i32(port->left_channel_volume);
            buffer.i32(port->right_channel_volume);
            buffer.value(port->volume);
            buffer.i32(port->type);
            buffer.i32(port->len);
            buffer.i32(port->freq);
            buffer.i32(port->mode);
        }
    }

    return writer.write_section(SectionId::Audio, buffer.data().data(), buffer.size(), true, error);
}

bool read_audio(EmuEnvState &emuenv, const std::vector<uint8_t> &data, std::string &error) {
    BufferReader reader(data.data(), data.size());

    const uint32_t next_port_id = reader.u32();
    const uint32_t port_count = reader.u32();
    if (!reader.ok() || port_count > 256) {
        error = "invalid audio port table";
        return false;
    }

    struct PortInfo {
        int32_t id = 0;
        int32_t left_volume = 0;
        int32_t right_volume = 0;
        float volume = 1.0f;
        int32_t type = 0;
        int32_t len = 0;
        int32_t freq = 0;
        int32_t mode = 0;
    };

    std::vector<PortInfo> ports(port_count);
    for (PortInfo &port : ports) {
        port.id = reader.i32();
        port.left_volume = reader.i32();
        port.right_volume = reader.i32();
        port.volume = reader.value<float>();
        port.type = reader.i32();
        port.len = reader.i32();
        port.freq = reader.i32();
        port.mode = reader.i32();
    }
    if (!reader.ok()) {
        error = "invalid audio port table";
        return false;
    }

    // Audio ports are host objects: a fresh process must recreate the ones the
    // guest state still refers to, or the game's audio thread spins on
    // SCE_AUDIO_OUT_ERROR_INVALID_PORT.
    std::map<int, AudioOutPortPtr> reopened;
    for (const PortInfo &info : ports) {
        if (info.len <= 0 || info.freq <= 0) {
            error = "invalid audio port configuration";
            return false;
        }
        const int channels = (info.mode == 0) ? 1 : 2;
        AudioOutPortPtr port = emuenv.audio.open_port(channels, info.freq, info.len);
        if (!port) {
            error = "failed to reopen audio port";
            return false;
        }
        port->type = info.type;
        port->len = info.len;
        port->freq = info.freq;
        port->mode = info.mode;
        port->left_channel_volume = info.left_volume;
        port->right_channel_volume = info.right_volume;
        port->volume = info.volume;
        port->last_output = 0;
        reopened.emplace(info.id, port);
    }

    {
        const std::lock_guard<std::mutex> lock(emuenv.audio.mutex);
        emuenv.audio.out_ports = std::move(reopened);
        emuenv.audio.next_port_id = static_cast<int>(next_port_id);
    }

    // The host audio device may be paused if the session was paused when the
    // state was captured.
    if (emuenv.audio.adapter)
        emuenv.audio.switch_state(false);

    LOG_CRITICAL("[savestate-audio] restored {} audio ports (next_id={})", port_count, next_port_id);
    return true;
}

bool write_input(EmuEnvState &emuenv, Writer &writer, std::string &error) {
    BufferWriter buffer;
    buffer.i32(static_cast<int32_t>(emuenv.ctrl.input_mode));
    buffer.i32(static_cast<int32_t>(emuenv.ctrl.input_mode_ext));
    for (int i = 0; i < 5; i++)
        buffer.u64(emuenv.ctrl.last_vcount[i]);
    return writer.write_section(SectionId::Input, buffer.data().data(), buffer.size(), true, error);
}

bool read_input(EmuEnvState &emuenv, const std::vector<uint8_t> &data, std::string &error) {
    BufferReader reader(data.data(), data.size());
    const int32_t input_mode = reader.i32();
    const int32_t input_mode_ext = reader.i32();
    for (int i = 0; i < 5; i++)
        emuenv.ctrl.last_vcount[i] = reader.u64();
    if (!reader.ok()) {
        error = "invalid input state";
        return false;
    }

    emuenv.ctrl.input_mode = static_cast<SceCtrlPadInputMode>(input_mode);
    emuenv.ctrl.input_mode_ext = static_cast<SceCtrlPadInputMode>(input_mode_ext);
    LOG_CRITICAL("[savestate-input] restored input_mode={} input_mode_ext={}", input_mode, input_mode_ext);
    return true;
}

bool write_io(EmuEnvState &emuenv, Writer &writer, std::string &error) {
    BufferWriter buffer;
    IOState &io = emuenv.io;
    const std::lock_guard<std::mutex> lock(io.file_mutex);

    buffer.i32(io.next_fd);

    buffer.u32(static_cast<uint32_t>(io.tty_files.size()));
    for (const auto &[fd, type] : io.tty_files) {
        buffer.i32(fd);
        buffer.i32(static_cast<int32_t>(type));
    }

    buffer.u32(static_cast<uint32_t>(io.std_files.size()));
    for (const auto &[fd, stats] : io.std_files) {
        buffer.i32(fd);
        buffer.str(stats.get_translated_path());
        buffer.i32(stats.get_open_mode());
        buffer.i64(stats.tell());
    }

    buffer.u32(static_cast<uint32_t>(io.dir_entries.size()));
    for (const auto &[fd, stats] : io.dir_entries) {
        buffer.i32(fd);
        buffer.str(stats.get_translated_path());
    }

    return writer.write_section(SectionId::Io, buffer.data().data(), buffer.size(), true, error);
}

bool read_io(EmuEnvState &emuenv, const std::vector<uint8_t> &data, std::string &error) {
    BufferReader reader(data.data(), data.size());
    IOState &io = emuenv.io;

    const int32_t next_fd = reader.i32();

    const uint32_t tty_count = reader.u32();
    if (!reader.ok() || tty_count > 4096) {
        error = "invalid IO tty table";
        return false;
    }
    std::vector<std::pair<int32_t, int32_t>> ttys(tty_count);
    for (auto &entry : ttys) {
        entry.first = reader.i32();
        entry.second = reader.i32();
    }

    const uint32_t file_count = reader.u32();
    if (!reader.ok() || file_count > 4096) {
        error = "invalid IO file table";
        return false;
    }
    struct FileInfo {
        int32_t fd = 0;
        std::string path;
        int32_t open_mode = 0;
        int64_t position = 0;
    };
    std::vector<FileInfo> files(file_count);
    for (FileInfo &file : files) {
        file.fd = reader.i32();
        file.path = reader.str();
        file.open_mode = reader.i32();
        file.position = reader.i64();
    }

    const uint32_t dir_count = reader.u32();
    if (!reader.ok() || dir_count > 4096) {
        error = "invalid IO dir table";
        return false;
    }
    std::vector<std::pair<int32_t, std::string>> dirs(dir_count);
    for (auto &entry : dirs) {
        entry.first = reader.i32();
        entry.second = reader.str();
    }

    if (!reader.ok()) {
        error = "invalid IO state";
        return false;
    }

    // File descriptors are host objects: reopen them so the game's fds stay valid.
    uint32_t reopened_files = 0;
    uint32_t reopened_dirs = 0;
    {
        const std::lock_guard<std::mutex> lock(io.file_mutex);
        io.tty_files.clear();
        io.std_files.clear();
        io.dir_entries.clear();
        io.next_fd = 1;

        for (const auto &[fd, type] : ttys) {
            io.tty_files.emplace(fd, static_cast<TtyType>(type));
            io.next_fd = std::max(io.next_fd, fd + 1);
        }

        for (const FileInfo &file : files) {
            if (file.path.empty()) {
                io.next_fd = std::max(io.next_fd, file.fd + 1);
                continue;
            }
            io.next_fd = file.fd;
            const SceUID opened = open_file(io, file.path.c_str(), file.open_mode, emuenv.vita_fs_path, "savestate");
            if (opened < 0) {
                LOG_CRITICAL("[savestate-io] failed to reopen {} (mode 0x{:X})", file.path, file.open_mode);
                io.next_fd = std::max(io.next_fd, file.fd + 1);
                continue;
            }
            const auto it = io.std_files.find(opened);
            if (it != io.std_files.end()) {
                if (file.position > 0)
                    it->second.seek(file.position, SCE_SEEK_SET);
                reopened_files++;
            }
            io.next_fd = std::max(io.next_fd, opened + 1);
        }

        for (const auto &[fd, path] : dirs) {
            if (path.empty()) {
                io.next_fd = std::max(io.next_fd, fd + 1);
                continue;
            }
            io.next_fd = fd;
            const SceUID opened = open_dir(io, path.c_str(), emuenv.vita_fs_path, "savestate");
            if (opened < 0) {
                LOG_CRITICAL("[savestate-io] failed to reopen dir {}", path);
                io.next_fd = std::max(io.next_fd, fd + 1);
                continue;
            }
            reopened_dirs++;
            io.next_fd = std::max(io.next_fd, opened + 1);
        }

        io.next_fd = std::max(io.next_fd, next_fd);
    }

    LOG_CRITICAL("[savestate-io] restored files={}/{} dirs={}/{} tty={} next_fd={}", reopened_files, file_count, reopened_dirs, dir_count, ttys.size(), io.next_fd);
    return true;
}

bool write_kernel(EmuEnvState &emuenv, Writer &writer, std::string &error) {
    BufferWriter base;
    write_kernel_base(emuenv, base);
    write_callbacks(emuenv.kernel, base);
    return writer.write_section(SectionId::Kernel, base.data().data(), base.size(), true, error);
}

bool read_kernel(EmuEnvState &emuenv, const std::vector<uint8_t> &data, std::string &error) {
    BufferReader reader(data.data(), data.size());
    if (!read_kernel_base(emuenv, reader, error))
        return false;
    LOG_CRITICAL("[savestate-error] trace: kernel base done valid={}", is_valid_addr(emuenv.mem, 0x803F8000));
    if (!read_callbacks(emuenv.kernel, reader, error))
        return false;
    LOG_CRITICAL("[savestate-error] trace: kernel callbacks done valid={}", is_valid_addr(emuenv.mem, 0x803F8000));
    return true;
}

} // namespace

const char *status_name(Status status) {
    switch (status) {
    case Status::Ok: return "ok";
    case Status::NotRunning: return "not-running";
    case Status::InvalidFile: return "invalid-file";
    case Status::UnsupportedVersion: return "unsupported-version";
    case Status::TitleMismatch: return "title-mismatch";
    case Status::SessionMismatch: return "session-mismatch";
    case Status::NoSpace: return "no-space";
    case Status::IoError: return "io-error";
    case Status::InternalError: return "internal-error";
    }
    return "internal-error";
}

Result save_state(EmuEnvState &emuenv, const fs::path &path, const std::string &app_version, const ProgressCallback &progress) {
    Result result;

    if (!emuenv.renderer) {
        result.status = Status::NotRunning;
        result.error = "no active session";
        return result;
    }

    const int not_parked = emuenv.kernel.stop_world(0, std::chrono::seconds(10));
    if (not_parked != 0) {
        emuenv.kernel.resume_world();
        result.status = Status::InternalError;
        result.error = "guest threads did not stop";
        return result;
    }

    // Pixels written by the GPU must land in guest RAM before we snapshot it.
    renderer::stop_render_thread(*emuenv.renderer);
    emuenv.renderer->flush_surfaces(emuenv.mem);
    renderer::start_render_thread(*emuenv.renderer, emuenv.display, emuenv.gxm, emuenv.mem, emuenv.cfg);

    Writer writer;
    std::string error;
    if (!writer.open(path, error)) {
        emuenv.kernel.resume_world();
        result.status = Status::IoError;
        result.error = error;
        return result;
    }

    const auto fail = [&](Status status, const std::string &message) {
        writer.abort();
        emuenv.kernel.resume_world();
        result.status = status;
        result.error = message;
        return result;
    };

    report(progress, 0.02f, "meta");
    BufferWriter meta;
    write_meta(emuenv, meta, app_version);
    if (!writer.write_section(SectionId::Meta, meta.data().data(), meta.size(), true, error))
        return fail(Status::IoError, error);

    report(progress, 0.05f, "memory");
    if (!write_memory(emuenv, writer, progress, error))
        return fail(Status::IoError, error);

    report(progress, 0.92f, "kernel");
    if (!write_kernel(emuenv, writer, error))
        return fail(Status::IoError, error);

    if (!write_kernel_sync(emuenv, writer, error))
        return fail(Status::IoError, error);

    report(progress, 0.94f, "threads");
    if (!write_threads(emuenv, writer, error))
        return fail(Status::IoError, error);

    if (!write_display(emuenv, writer, error))
        return fail(Status::IoError, error);

    if (!write_obj_store(emuenv, writer, error))
        return fail(Status::IoError, error);

    report(progress, 0.97f, "gpu");
    if (!write_gxm(emuenv, writer, error))
        return fail(Status::IoError, error);

    if (!write_ngs(emuenv, writer, error))
        return fail(Status::IoError, error);

    if (!write_audio(emuenv, writer, error))
        return fail(Status::IoError, error);

    if (!write_input(emuenv, writer, error))
        return fail(Status::IoError, error);

    if (!write_io(emuenv, writer, error))
        return fail(Status::IoError, error);

    report(progress, 0.99f, "commit");
    result.bytes = writer.bytes_written();
    if (!writer.finalize(error))
        return fail(Status::IoError, error);

    emuenv.kernel.resume_world();
    report(progress, 1.0f, "done");

    result.meta.title_id = emuenv.io.title_id;
    result.meta.app_version = app_version;
    result.meta.timestamp_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                     std::chrono::system_clock::now().time_since_epoch())
                                                     .count());
    return result;
}

Result load_state(EmuEnvState &emuenv, const fs::path &path, bool allow_cross_session, const ProgressCallback &progress) {
    Result result;

    Reader reader;
    std::string error;
    if (!reader.open(path, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }

    std::vector<uint8_t> meta_data;
    if (!reader.read_section(SectionId::Meta, meta_data, error) || !read_meta(meta_data, result.meta)) {
        result.status = Status::InvalidFile;
        result.error = error.empty() ? "invalid meta section" : error;
        return result;
    }

    if (result.meta.engine_version != kEngineVersion) {
        result.status = Status::UnsupportedVersion;
        result.error = "save state was created by an incompatible build";
        return result;
    }

    if (!emuenv.io.title_id.empty() && !result.meta.title_id.empty() && emuenv.io.title_id != result.meta.title_id) {
        result.status = Status::TitleMismatch;
        result.error = "save state belongs to another title";
        return result;
    }

    // Host objects (GXM programs, shader patchers, NGS systems) cannot keep
    // their process-local pointers across sessions, so they are rebuilt from
    // the state. Loading a state from an earlier session is therefore allowed.
    const bool cross_session = !is_current_session(emuenv, result.meta);
    result.session_match = !cross_session;
    if (cross_session && !allow_cross_session)
        LOG_WARN("Loading a save state created in a different session; rebuilding host objects");

    if (!emuenv.renderer) {
        result.status = Status::NotRunning;
        result.error = "no active session";
        return result;
    }

    report(progress, 0.02f, "prepare");
    std::vector<uint8_t> kernel_data;
    std::vector<uint8_t> kernel_sync_data;
    std::vector<uint8_t> threads_data;
    std::vector<uint8_t> display_data;
    std::vector<uint8_t> gxm_data;
    std::vector<uint8_t> ngs_data;
    std::vector<uint8_t> objstore_data;
    std::vector<uint8_t> audio_data;
    std::vector<uint8_t> input_data;
    std::vector<uint8_t> io_data;

    const bool has_ngs_section = reader.has_section(SectionId::Ngs);
    const bool has_audio_section = reader.has_section(SectionId::Audio);
    const bool has_input_section = reader.has_section(SectionId::Input);
    const bool has_io_section = reader.has_section(SectionId::Io);
    if (!reader.read_section(SectionId::Kernel, kernel_data, error)
        || !reader.read_section(SectionId::KernelSync, kernel_sync_data, error)
        || !reader.read_section(SectionId::Threads, threads_data, error)
        || !reader.read_section(SectionId::Display, display_data, error)
        || !reader.read_section(SectionId::Gxm, gxm_data, error)
        || !reader.read_section(SectionId::ObjStore, objstore_data, error)
        || (has_ngs_section && !reader.read_section(SectionId::Ngs, ngs_data, error))
        || (has_audio_section && !reader.read_section(SectionId::Audio, audio_data, error))
        || (has_input_section && !reader.read_section(SectionId::Input, input_data, error))
        || (has_io_section && !reader.read_section(SectionId::Io, io_data, error))) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }

    LOG_CRITICAL("[savestate-error] load: sections read, stopping world (cross_session={})", cross_session);
    if (emuenv.kernel.stop_world(0, std::chrono::seconds(10)) != 0) {
        emuenv.kernel.resume_world();
        result.status = Status::InternalError;
        result.error = "guest threads did not stop";
        LOG_CRITICAL("[savestate-error] load: guest threads did not stop");
        return result;
    }
    LOG_CRITICAL("[savestate-error] load: world stopped");

    // Guest sync waits can leave the render thread blocked inside a leading
    // WaitSyncObject command; invalidate the sync objects so it can drain.
    gxm::invalidate_sync_objects(emuenv.gxm);
    LOG_CRITICAL("[savestate-error] load: sync objects invalidated");

    // Destroy the GPU-side runtime while the render thread can still process
    // the destroy commands, then park it so the GPU goes idle before RAM is
    // overwritten.
    gxm::destroy_runtime_objects(emuenv);
    LOG_CRITICAL("[savestate-error] load: gxm runtime destroyed");
    renderer::stop_render_thread(*emuenv.renderer);
    LOG_CRITICAL("[savestate-error] load: render thread stopped");

    emuenv.kernel.process_exit();
    emuenv.kernel.clear_paused_threads_state();
    emuenv.kernel.reset_world_stop_state();
    LOG_CRITICAL("[savestate-error] load: guest threads exited");

    // Drop the previous kernel objects while the old allocator is still in
    // place: destroying the last reference to a ThreadState frees its stack,
    // which must not happen after guest RAM has been rebuilt from the state.
    KernelState &kernel = emuenv.kernel;
    kernel.threads.clear();
    kernel.simple_events.clear();
    kernel.timers.clear();
    kernel.semaphores.clear();
    kernel.condvars.clear();
    kernel.lwcondvars.clear();
    kernel.mutexes.clear();
    kernel.lwmutexes.clear();
    kernel.rwlocks.clear();
    kernel.eventflags.clear();
    kernel.msgpipes.clear();
    kernel.callbacks.clear();
    LOG_CRITICAL("[savestate-error] load: old kernel objects released");

    // NGS host objects live in guest memory and cannot survive the RAM restore;
    // release them now (their memory is still valid) and rebuild after loading.
    ngs::deinit(emuenv.ngs, emuenv.mem);
    LOG_CRITICAL("[savestate-error] load: ngs released");

    report(progress, 0.10f, "memory");
    if (!read_memory(emuenv, reader, progress, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }
    LOG_CRITICAL("[savestate-error] load: memory restored");
    LOG_CRITICAL("[savestate-error] trace: after memory sp-valid={} stack-valid={}", is_valid_addr(emuenv.mem, 0x804F7658), is_valid_addr(emuenv.mem, 0x803F8000));

    // Cached GPU textures/surfaces were built from the pre-load guest RAM.
    emuenv.renderer->reset_caches();
    LOG_CRITICAL("[savestate-error] load: renderer caches reset");

    report(progress, 0.88f, "kernel");
    if (!read_kernel(emuenv, kernel_data, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }
    LOG_CRITICAL("[savestate-error] load: kernel restored");
    LOG_CRITICAL("[savestate-error] trace: after kernel sp-valid={} stack-valid={}", is_valid_addr(emuenv.mem, 0x804F7658), is_valid_addr(emuenv.mem, 0x803F8000));

    // Restart the renderer before guest threads are recreated: they may issue
    // GPU commands as soon as they start running.
    renderer::start_render_thread(*emuenv.renderer, emuenv.display, emuenv.gxm, emuenv.mem, emuenv.cfg);
    LOG_CRITICAL("[savestate-error] load: renderer restarted");

    report(progress, 0.95f, "runtime");
    if (!read_display(emuenv, display_data, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }
    LOG_CRITICAL("[savestate-error] load: display restored");
    if (!read_obj_store(emuenv, objstore_data, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }
    LOG_CRITICAL("[savestate-error] load: objstore restored");
    if (!read_gxm(emuenv, gxm_data, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }
    LOG_CRITICAL("[savestate-error] load: gxm restored");

    if (!ngs_data.empty() && !read_ngs(emuenv, ngs_data, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }
    LOG_CRITICAL("[savestate-error] load: ngs restored");
    LOG_CRITICAL("[savestate-error] trace: after ngs sp-valid={} stack-valid={}", is_valid_addr(emuenv.mem, 0x804F7658), is_valid_addr(emuenv.mem, 0x803F8000));

    // Audio ports are host objects the restored guest state still refers to.
    if (!audio_data.empty() && !read_audio(emuenv, audio_data, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }
    LOG_CRITICAL("[savestate-error] load: audio restored");

    if (!input_data.empty() && !read_input(emuenv, input_data, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }
    LOG_CRITICAL("[savestate-error] load: input restored");

    if (!io_data.empty() && !read_io(emuenv, io_data, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }
    LOG_CRITICAL("[savestate-error] load: io restored");

    // Create the guest threads parked, restore the kernel objects they depend
    // on, and only then let them run.
    report(progress, 0.92f, "threads");
    std::vector<ThreadState::Snapshot> deferred_threads;
    if (!read_threads(emuenv, threads_data, deferred_threads, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }
    LOG_CRITICAL("[savestate-error] load: threads recreated");
    LOG_CRITICAL("[savestate-error] trace: after threads sp-valid={} stack-valid={}", is_valid_addr(emuenv.mem, 0x804F7658), is_valid_addr(emuenv.mem, 0x803F8000));

    if (!read_kernel_sync(emuenv, kernel_sync_data, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }
    LOG_CRITICAL("[savestate-error] load: kernel sync restored");

    gxm::restart_display_queue(emuenv);
    LOG_CRITICAL("[savestate-error] load: display queue restarted");
    LOG_CRITICAL("[savestate-error] trace: after display queue sp-valid={} stack-valid={}", is_valid_addr(emuenv.mem, 0x804F7658), is_valid_addr(emuenv.mem, 0x803F8000));

    for (const ThreadState::Snapshot &snapshot : deferred_threads) {
        const ThreadStatePtr thread = kernel.get_thread(snapshot.id);
        if (thread)
            thread->apply_private_snapshot(snapshot);
    }
    LOG_CRITICAL("[savestate-error] load: threads started");

    for (const auto &[id, thread] : emuenv.kernel.threads) {
        if (!thread || !thread->cpu)
            continue;
        const uint32_t sp = read_sp(*thread->cpu);
        const uint32_t pc = read_pc(*thread->cpu);
        if ((sp && !is_valid_addr(emuenv.mem, sp)) || (pc && !is_valid_addr(emuenv.mem, pc)))
            LOG_CRITICAL("[savestate-error] load: thread {} has invalid sp=0x{:X} pc=0x{:X} status={}", id, sp, pc, static_cast<int>(thread->status));
    }

    std::thread([&emuenv]() {
        uint64_t last_flips = emuenv.display.setframe_call_count.load();
        int stalled_reports = 0;
        for (int i = 0; i < 200; i++) {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            const uint64_t flips = emuenv.display.setframe_call_count.load();
            const bool stalled = (flips == last_flips);
            const bool report = stalled || (i % 10) == 0;
            last_flips = flips;
            if (!report)
                continue;
            if (stalled && ++stalled_reports > 20)
                continue;
            std::string summary;
            for (const auto &[id, thread] : emuenv.kernel.threads) {
                if (!thread)
                    continue;
                const int status = static_cast<int>(thread->status);
                uint32_t pc = 0, nid = 0, r0 = 0, sp = 0, lr = 0;
                if (thread->cpu) {
                    pc = read_pc(*thread->cpu);
                    r0 = read_reg(*thread->cpu, 0);
                    sp = read_sp(*thread->cpu);
                    lr = read_lr(*thread->cpu);
                }
                nid = thread->last_import_nid;
                summary += fmt::format(" {}:s{}/pc{:X}/lr{:X}/sp{:X}/nid{:08X}/r0{:X}", id, status, pc, lr, sp, nid, r0);
                if (status == 3)
                    summary += fmt::format("/w={}#{}", thread->wait_prim_kind ? thread->wait_prim_kind : "?", thread->wait_prim_uid);
            }
            LOG_CRITICAL("[savestate-error] sweep{} main={} display_thread={} worker={} flips={} accepted={} queue={}", stalled ? " STALLED" : "", emuenv.main_thread_id,
                emuenv.gxm.display_queue_thread, emuenv.gxm.display_worker_state.load(), flips,
                emuenv.display.setframe_accept_count.load(), emuenv.gxm.display_queue.size());
            LOG_CRITICAL("[savestate-error] thread sweep:{}", summary);
        }
    }).detach();

    report(progress, 1.0f, "done");
    return result;
}

Result inspect_state(const fs::path &path) {
    Result result;
    Reader reader;
    std::string error;
    if (!reader.open(path, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }

    std::vector<uint8_t> meta_data;
    if (!reader.read_section(SectionId::Meta, meta_data, error) || !read_meta(meta_data, result.meta)) {
        result.status = Status::InvalidFile;
        result.error = error.empty() ? "invalid meta section" : error;
        return result;
    }
    return result;
}

Result delete_state(const fs::path &path) {
    Result result;
    boost::system::error_code ec;
    fs::remove(path, ec);
    if (ec) {
        result.status = Status::IoError;
        result.error = ec.message();
    }
    return result;
}

bool is_current_session(EmuEnvState &emuenv, const Meta &meta) {
    return meta.session_token == make_session_token(emuenv);
}

} // namespace emucorev::savestate
