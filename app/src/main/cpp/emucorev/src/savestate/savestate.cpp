// EmuCoreV save-state engine.
//
// Save states capture guest RAM, guest CPU contexts, kernel objects, module
// tables, display and GXM runtime objects. Host-only state (JIT caches, Vulkan
// objects, std::thread handles) is rebuilt on load.

#include <emucorev/savestate/savestate.h>

#include <emucorev/savestate/archive.h>
#include <emucorev/savestate/memory_image.h>
#include <emucorev/savestate/ngs_state_io.h>
#include <emucorev/savestate/state_io.h>
#include <emucorev/savestate/kernel_state_io.h>
#include <emucorev/savestate/kernel_base_io.h>
#include <emucorev/savestate/display_state_io.h>
#include <emucorev/savestate/gxm_state_io.h>
#include <emucorev/savestate/gxm_display_io.h>
#include <emucorev/savestate/gxm_finish_io.h>
#include <emucorev/savestate/sysmem_state_io.h>
#include <emucorev/savestate/gpu_state_io.h>
#include <emucorev/savestate/kernel_import_io.h>

#include <cpu/functions.h>
#include <audio/state.h>
#include <audio/continuation.h>
#include <ctrl/state.h>
#include <display/state.h>
#include <display/functions.h>
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
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <set>
#include <vector>
#include <type_traits>

namespace emucorev::savestate {

namespace {

constexpr uint32_t kMemoryFormatVersion = 1;
constexpr const char *kEngineRevision = "emucorev-savestate-15";
constexpr size_t kMemoryChunkSize = 1u << 20;

class ScopedWorldStop {
public:
    explicit ScopedWorldStop(KernelState &kernel) : kernel_(kernel) {}
    int stop() {
        engaged_ = true;
        return kernel_.stop_world(0, std::chrono::seconds(10));
    }
    ~ScopedWorldStop() {
        release();
    }
    void release() {
        if (engaged_) {
            kernel_.resume_world();
            engaged_ = false;
        }
    }
    ScopedWorldStop(const ScopedWorldStop &) = delete;
    ScopedWorldStop &operator=(const ScopedWorldStop &) = delete;
private:
    KernelState &kernel_;
    bool engaged_ = false;
};

class ScopedSnapshotGate {
public:
    explicit ScopedSnapshotGate(renderer::SnapshotGate &gate, bool worker_present = true) {
        acquired_ = !worker_present || gate.acquire(std::chrono::seconds(2));
        if (worker_present && acquired_)
            gate_ = &gate;
    }
    ~ScopedSnapshotGate() { release(); }
    explicit operator bool() const { return acquired_; }
    void release() {
        if (gate_) {
            gate_->release();
            gate_ = nullptr;
        }
    }
    ScopedSnapshotGate(const ScopedSnapshotGate &) = delete;
    ScopedSnapshotGate &operator=(const ScopedSnapshotGate &) = delete;
private:
    renderer::SnapshotGate *gate_ = nullptr;
    bool acquired_ = false;
};

class ScopedRestoreFailure {
public:
    ScopedRestoreFailure(EmuEnvState &env, Result &result) : env_(env), result_(result) {}
    ~ScopedRestoreFailure() {
        if (!result_.session_usable) {
            env_.kernel.process_exit();
            gxm::stop_display_queue_host(env_);
            renderer::stop_render_thread(*env_.renderer);
        }
    }
private:
    EmuEnvState &env_;
    Result &result_;
};

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

bool read_memory(EmuEnvState &emuenv, const MemoryImage &image, const ProgressCallback &progress, std::string &error) {
    MemState &mem = emuenv.mem;
    reset_memory(mem);
    uint64_t total_bytes = 0;
    for (const auto &span : image.spans()) {
        const uint64_t size = static_cast<uint64_t>(span.page_count) * kPageSize;
        if (!try_alloc_at(mem, span.address, static_cast<uint32_t>(size), "savestate")) {
            error = "failed to restore guest memory map";
            return false;
        }
        total_bytes += size;
    }
    if (mem.use_page_table) {
        for (uint32_t page = 0; page < kArenaPages; ++page)
            mem.page_table[page] = mem.memory.get();
    }
    uint64_t processed = 0;
    return image.visit_validated_chunks([&](uint64_t address, const uint8_t *data, size_t size, std::string &) {
        write_guest_chunk(mem, static_cast<Address>(address), data, size);
        processed += size;
        if (total_bytes)
            report(progress, 0.75f + static_cast<float>(static_cast<double>(processed) / total_bytes) * 0.15f, "memory");
        return true;
    }, error);
}


bool write_threads(EmuEnvState &emuenv, Writer &writer, std::string &error) {
    BufferWriter buffer;
    std::vector<ThreadState::Snapshot> snapshots;
    if (!capture_thread_snapshots(emuenv.kernel, 0, snapshots, error)) return false;
    write_thread_snapshots(buffer, snapshots);
    return writer.write_section(SectionId::Threads, buffer.data().data(), buffer.size(), true, error);
}

bool read_threads(EmuEnvState &emuenv, const std::vector<uint8_t> &data, std::vector<ThreadState::Snapshot> &deferred, bool session_paused, std::string &error) {
    if (!parse_thread_snapshots(data, deferred, error)) return false;
    KernelState &kernel = emuenv.kernel;
    kernel.snapshot_thread_owners.clear();
    for (const auto &snapshot : deferred) {
        if (!snapshot.registered) { kernel.create_retained_thread_from_snapshot(emuenv.mem, snapshot); continue; }
        if (!kernel.create_thread_from_snapshot(emuenv.mem, snapshot, true)) { error = "failed to recreate thread"; return false; }
    }
    kernel.restore_snapshot_callbacks();
    for (const auto &snapshot : deferred) {
        auto thread = snapshot.registered ? kernel.get_thread(snapshot.id) : kernel.snapshot_thread_owners.at(snapshot.id);
        thread->apply_private_snapshot(snapshot);
    }
    if (session_paused)
        kernel.pause_threads();
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
    write_map(buffer, kernel.var_binding_infos, [&buffer, &emuenv](const auto &entry) {
        buffer.u32(entry.first);
        buffer.u32(entry.second.entries ? host_to_guest(emuenv.mem, entry.second.entries) : 0);
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

bool read_kernel_base(EmuEnvState &emuenv, const KernelBaseSnapshot &snapshot, std::string &error) {
    KernelState &kernel = emuenv.kernel;
    kernel.codec_blocks = snapshot.codec_blocks;
    kernel.loaded_modules.clear();
    for (const auto &[uid, module] : snapshot.loaded_modules)
        kernel.loaded_modules.emplace(uid, module ? std::make_shared<KernelModule>(*module) : nullptr);
    kernel.loaded_sysmodules = snapshot.loaded_sysmodules;
    kernel.loaded_internal_sysmodules = snapshot.loaded_internal_sysmodules;
    kernel.export_nids = snapshot.export_nids;
    kernel.export_nids_by_lib = snapshot.export_nids_by_lib;
    kernel.export_nid_owners = snapshot.export_nid_owners;
    kernel.func_binding_infos = snapshot.func_binding_infos;
    kernel.var_binding_infos.clear();
    for (const auto &[nid, saved] : snapshot.var_binding_infos)
        kernel.var_binding_infos.emplace(nid, VarBindingInfo{Ptr<void>(saved.entries).get(emuenv.mem), saved.size, saved.module_nid});
    kernel.nid_libraries = snapshot.nid_libraries;
    kernel.module_uid_by_nid = snapshot.module_uid_by_nid;
    kernel.thread_event_start = Ptr<const void>(snapshot.thread_event_start);
    kernel.thread_event_start_arg = snapshot.thread_event_start_arg;
    kernel.thread_event_end = Ptr<const void>(snapshot.thread_event_end);
    kernel.thread_event_end_arg = snapshot.thread_event_end_arg;
    kernel.tls_address = Ptr<const void>(snapshot.tls_address);
    kernel.tls_psize = snapshot.tls_psize;
    kernel.tls_msize = snapshot.tls_msize;
    kernel.accurate_thread_scheduling = snapshot.accurate_scheduling;
    kernel.start_tick = snapshot.start_tick;
    kernel.base_tick = { snapshot.base_tick };
    kernel.process_param = Ptr<SceProcessParam>(snapshot.process_param);
    kernel.client_vtable = Ptr<void>(snapshot.client_vtable);
    kernel.shellsvc_client = Ptr<Address>(snapshot.shellsvc_client);
    kernel.libc_dso_handle_main = Ptr<void>(snapshot.libc_dso_handle_main);
    kernel.halt_instruction_pc = snapshot.halt_instruction_pc;
    for (int i = 0; i < KernelState::EXCEPTION_HANDLER_MAX; ++i)
        kernel.exception_handlers[i].store(snapshot.exception_handlers[i]);
    emuenv.main_thread_id = snapshot.main_thread_id;
    kernel.set_next_uid(snapshot.next_uid);
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
    std::vector<KernelState::SavedCallback> callbacks;
    if (!parse_saved_callbacks(reader, callbacks, error)) return false;
    kernel.callbacks.clear();
    kernel.snapshot_callbacks = std::move(callbacks);
    return true;
}

template <class Objects>
Objects retained_sync_objects(KernelState &kernel, const Objects &registered, bool lightweight) {
    Objects objects = registered;
    using Object = typename Objects::mapped_type::element_type;
    for (const auto &[_, thread] : kernel.threads) {
        for (const auto &retained : thread->saved_sync_objects()) {
            if (auto object = std::dynamic_pointer_cast<Object>(retained)) {
                if constexpr (std::is_same_v<Object, Mutex>) {
                    if ((object->workarea.address() != 0) != lightweight) continue;
                }
                if constexpr (std::is_same_v<Object, Condvar>) {
                    if (object->lightweight != lightweight) continue;
                }
                objects.emplace(object->uid, std::move(object));
            }
        }
    }
    if constexpr (std::is_same_v<Object, Mutex>) {
        const auto &conditions = lightweight ? kernel.lwcondvars : kernel.condvars;
        for (const auto &[_, condition] : conditions) {
            if (condition->associated_mutex) objects.emplace(condition->associated_mutex->uid, condition->associated_mutex);
        }
    }
    return objects;
}

void write_sync_header(BufferWriter &buffer, SceUID uid, bool registered, const SyncPrimitive &object) {
    buffer.i32(uid);
    buffer.boolean(registered);
    buffer.i32(object.uid);
    buffer.u32(object.attr);
    buffer.boolean(object.deleted.load(std::memory_order_relaxed));
    buffer.append(object.name, sizeof(object.name));
}

template <class Objects>
void install_sync_object(KernelState &kernel, Objects &objects, const SyncObjectHeader &header, typename Objects::mapped_type object) {
    object->uid = header.object_uid;
    object->attr = header.attr;
    object->deleted.store(header.deleted, std::memory_order_relaxed);
    std::memcpy(object->name, header.name, sizeof(object->name));
    if (header.registered) objects.emplace(header.uid, std::move(object));
    else kernel.snapshot_objects.emplace(header.uid, std::move(object));
}

bool write_kernel_sync(EmuEnvState &emuenv, Writer &writer, std::string &error) {
    KernelState &kernel = emuenv.kernel;
    BufferWriter buffer;
    const auto capture_time = kernel.capture_clock().timer_us;
    buffer.u32(2);
    buffer.u64(capture_time);
    const auto write_objects = [&](const auto &registered, auto write_payload, bool lightweight = false) {
        auto objects = retained_sync_objects(kernel, registered, lightweight);
        write_map(buffer, objects, [&](const auto &entry) {
            const std::lock_guard lock(entry.second->mutex);
            write_sync_header(buffer, entry.first, registered.contains(entry.first), *entry.second);
            write_payload(*entry.second);
        });
    };
    write_objects(kernel.simple_events, [&](const SimpleEvent &object) {
        buffer.u32(object.pattern); buffer.u64(object.last_user_data);
        buffer.boolean(object.auto_reset); buffer.boolean(object.cb_wakeup_only);
    });
    write_objects(kernel.timers, [&](const Timer &object) {
        buffer.boolean(object.is_started); buffer.boolean(object.is_repeat); buffer.boolean(object.is_pulse); buffer.boolean(object.event_set);
        buffer.u64(object.time); buffer.u64(object.next_event); buffer.u64(object.event_interval);
        buffer.u64(object.elapsed_time_at(capture_time));
    });
    write_objects(kernel.semaphores, [&](const Semaphore &object) {
        buffer.i32(object.max); buffer.i32(object.val); buffer.i32(object.init_val);
    });
    const auto write_mutex = [&](const Mutex &object) {
        buffer.i32(object.init_count); buffer.i32(object.lock_count);
        buffer.i32(object.owner ? object.owner->id : 0); buffer.u32(object.workarea.address());
    };
    write_objects(kernel.mutexes, write_mutex);
    write_objects(kernel.lwmutexes, write_mutex, true);
    const auto write_cond = [&](const Condvar &object) {
        buffer.i32(object.associated_mutex ? object.associated_mutex->uid : 0);
    };
    write_objects(kernel.condvars, write_cond);
    write_objects(kernel.lwcondvars, write_cond, true);
    write_objects(kernel.rwlocks, [&](const RWLock &object) {
        buffer.u32(static_cast<uint32_t>(object.state));
        buffer.u32(static_cast<uint32_t>(object.owners.size()));
        for (const auto &[owner, count] : object.owners) { buffer.i32(owner ? owner->id : 0); buffer.i32(count); }
    });
    write_objects(kernel.eventflags, [&](const EventFlag &object) { buffer.i32(object.flags); });
    write_objects(kernel.msgpipes, [&](const MsgPipe &object) {
        const auto capacity = object.data_buffer.Capacity();
        const auto used = object.data_buffer.Used();
        buffer.u64(capacity); buffer.u64(used);
        if (used) { std::vector<char> bytes(used); object.data_buffer.Peek(bytes.data(), bytes.size()); buffer.append(bytes.data(), bytes.size()); }
    });
    return writer.write_section(SectionId::KernelSync, buffer.data().data(), buffer.size(), true, error);
}

bool read_kernel_sync(EmuEnvState &emuenv, const std::vector<uint8_t> &data, std::string &error) {
    ParsedKernelSync parsed;
    if (!parse_kernel_sync(data, parsed, error)) return false;
    KernelState &kernel = emuenv.kernel;
    const uint64_t now = host_time_us();
    const auto owner_thread = [&](SceUID uid) {
        auto thread = kernel.get_thread(uid);
        if (thread) return thread;
        const auto it = kernel.snapshot_thread_owners.find(uid);
        return it == kernel.snapshot_thread_owners.end() ? ThreadStatePtr{} : it->second;
    };
    kernel.snapshot_objects.clear();
    kernel.simple_events.clear(); kernel.timers.clear(); kernel.semaphores.clear();
    kernel.mutexes.clear(); kernel.lwmutexes.clear(); kernel.condvars.clear(); kernel.lwcondvars.clear();
    kernel.rwlocks.clear(); kernel.eventflags.clear(); kernel.msgpipes.clear();
    for (const auto &saved : parsed.objects) {
        const auto &header = saved.header;
        const auto &v = saved.values;
        switch (saved.kind) {
        case SyncSnapshotKind::event: {
            auto object = std::make_shared<SimpleEvent>(header.attr);
            object->pattern = v[0]; object->last_user_data = v[1]; object->auto_reset = v[2]; object->cb_wakeup_only = v[3];
            install_sync_object(kernel, kernel.simple_events, header, std::move(object)); break;
        }
        case SyncSnapshotKind::timer: {
            auto object = std::make_shared<Timer>(header.attr);
            object->is_started = v[0]; object->is_repeat = v[1]; object->is_pulse = v[2]; object->event_set = v[3];
            object->time = v[4]; object->next_event = v[5]; object->event_interval = v[6];
            object->restore_elapsed_time(now, v[7]);
            if (object->is_started && object->next_event != std::numeric_limits<uint64_t>::max()) {
                // Translate the event relative to the captured epoch, including
                // an already overdue event or a host clock moved backwards.
                object->next_event = now + (object->next_event - parsed.saved_time);
            }
            install_sync_object(kernel, kernel.timers, header, std::move(object)); break;
        }
        case SyncSnapshotKind::semaphore: {
            auto object = std::make_shared<Semaphore>(header.attr);
            object->max = static_cast<int32_t>(v[0]); object->val = static_cast<int32_t>(v[1]); object->init_val = static_cast<int32_t>(v[2]);
            install_sync_object(kernel, kernel.semaphores, header, std::move(object)); break;
        }
        case SyncSnapshotKind::mutex: case SyncSnapshotKind::lw_mutex: {
            auto object = std::make_shared<Mutex>(header.attr);
            object->init_count = static_cast<int32_t>(v[0]); object->lock_count = static_cast<int32_t>(v[1]);
            object->owner = v[2] ? owner_thread(static_cast<SceUID>(v[2])) : nullptr;
            object->workarea = Ptr<SceKernelLwMutexWork>(static_cast<Address>(v[3]));
            auto &objects = saved.kind == SyncSnapshotKind::mutex ? kernel.mutexes : kernel.lwmutexes;
            install_sync_object(kernel, objects, header, std::move(object)); break;
        }
        case SyncSnapshotKind::cond: case SyncSnapshotKind::lw_cond: {
            auto object = std::make_shared<Condvar>(header.attr);
            object->lightweight = saved.kind == SyncSnapshotKind::lw_cond;
            const auto &mutexes = object->lightweight ? kernel.lwmutexes : kernel.mutexes;
            const auto uid = static_cast<SceUID>(v[0]);
            if (auto it = mutexes.find(uid); it != mutexes.end()) object->associated_mutex = it->second;
            else if (auto it = kernel.snapshot_objects.find(uid); it != kernel.snapshot_objects.end()) object->associated_mutex = std::dynamic_pointer_cast<Mutex>(it->second);
            if (!object->associated_mutex) { error = "missing condition mutex"; return false; }
            auto &objects = object->lightweight ? kernel.lwcondvars : kernel.condvars;
            install_sync_object(kernel, objects, header, std::move(object)); break;
        }
        case SyncSnapshotKind::rwlock: {
            auto object = std::make_shared<RWLock>(header.attr);
            object->state = static_cast<RWLockState>(v[0]);
            for (const auto &[uid, count] : saved.owners) object->owners.emplace(owner_thread(uid), count);
            install_sync_object(kernel, kernel.rwlocks, header, std::move(object)); break;
        }
        case SyncSnapshotKind::event_flag: {
            auto object = std::make_shared<EventFlag>(header.attr); object->flags = static_cast<int32_t>(v[0]);
            install_sync_object(kernel, kernel.eventflags, header, std::move(object)); break;
        }
        case SyncSnapshotKind::msgpipe: {
            auto object = std::make_shared<MsgPipe>(header.attr, static_cast<size_t>(v[0]));
            if (!saved.bytes.empty()) object->data_buffer.Insert(saved.bytes.data(), saved.bytes.size());
            install_sync_object(kernel, kernel.msgpipes, header, std::move(object)); break;
        }
        }
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

bool read_display(EmuEnvState &emuenv, const DisplaySnapshot &snapshot, std::string &error) {
    DisplayState &display = emuenv.display;
    // Viewport geometry belongs to the current host window/orientation.
    display.sce_frame = snapshot.sce_frame;
    {
        const std::lock_guard<std::mutex> lock(display.display_info_mutex);
        display.next_rendered_frame = snapshot.next_rendered_frame;
    }
    display.vblank_count.store(snapshot.vblank_count);
    display.last_setframe_vblank_count.store(snapshot.last_setframe_vblank_count);
    display.fps_hack = snapshot.fps_hack;
    {
        const std::lock_guard<std::mutex> lock(display.mutex);
        display.vblank_callbacks.clear();
        for (const SceUID uid : snapshot.callbacks) {
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
    buffer.u32(gxm::runtime_selected_context_address(emuenv));
    buffer.i32(gxm.display_queue_thread);
    buffer.u32(static_cast<uint32_t>(gxm.display_phase));
    buffer.u32(gxm.display_previous_entry_point);

    std::vector<gxm::ContextSnapshot> contexts;
    std::vector<renderer::CommandSnapshot> commands;
    std::vector<gxm::BatchSnapshot> batches;
    if (!gxm::capture_contexts(emuenv, contexts, commands, batches, error))
        return false;
    buffer.u32(static_cast<uint32_t>(contexts.size()));
    for (const gxm::ContextSnapshot &context : contexts) {
        buffer.u32(context.address);
        buffer.boolean(context.deferred);
        buffer.u32(context.render_target_address);
        buffer.value(context.renderer_record);
        buffer.value(context.state);
        buffer.boolean(context.last_precomputed);
        buffer.u64(context.command_next_free_pos);
        buffer.u32(context.alloc_space.address());
        buffer.u32(context.alloc_space_end.address());
        buffer.u32(context.command_allocator_size);
        buffer.u32(context.alloc_space_start);
        buffer.u32(context.pending_first);
        buffer.u32(context.pending_last);
        buffer.boolean(context.was_vert_default_uniform_reserved);
        buffer.boolean(context.was_frag_default_uniform_reserved);
        buffer.u32(static_cast<uint32_t>(context.deferred_lists.size()));
        for (const auto &list : context.deferred_lists) {
            buffer.u32(list.guest_address);
            buffer.u64(list.identity);
            buffer.u32(list.first);
            buffer.u32(list.last);
            buffer.boolean(list.current);
            buffer.u32(static_cast<uint32_t>(list.ranges.size()));
            for (const auto &[start, end] : list.ranges) {
                buffer.u32(start);
                buffer.u32(end);
            }
        }
    }
    buffer.u32(static_cast<uint32_t>(commands.size()));
    for (const auto &command : commands) {
        buffer.u8(static_cast<uint8_t>(command.opcode));
        buffer.boolean(command.deferred_allocation);
        buffer.u32(command.next);
        buffer.u64(command.completion_id);
        buffer.append(command.data.data(), command.data.size());
        buffer.u32(command.render_target_address);
        buffer.boolean(command.has_color_surface);
        buffer.boolean(command.has_depth_surface);
        if (command.has_color_surface) buffer.value(command.color_surface);
        if (command.has_depth_surface) buffer.value(command.depth_surface);
        buffer.u32(command.transfer_color_key_value);
        buffer.u32(command.transfer_color_key_mask);
        buffer.u32(command.transfer_color_key_mode);
        buffer.u32(command.transfer_src_type);
        buffer.u32(command.transfer_dst_type);
        buffer.u32(command.transfer_fill_color);
        const auto write_image = [&](const renderer::TransferImageSnapshot &image) {
            buffer.u32(image.format);
            buffer.u32(image.address);
            buffer.u32(image.x);
            buffer.u32(image.y);
            buffer.u32(image.width);
            buffer.u32(image.height);
            buffer.i32(image.stride);
        };
        write_image(command.transfer_src);
        write_image(command.transfer_dst);
        buffer.boolean(command.has_display_frame);
        buffer.u32(command.display_frame.base);
        buffer.u32(command.display_frame.pitch);
        buffer.u32(command.display_frame.pixelformat);
        buffer.i32(command.display_frame.width);
        buffer.i32(command.display_frame.height);
        buffer.u32(command.new_frame_context_address);
        buffer.str(command.screen_filter);
    }
    buffer.u32(static_cast<uint32_t>(batches.size()));
    for (const auto &batch : batches) {
        buffer.u32(batch.context_address);
        buffer.u32(batch.first);
        buffer.u32(batch.last);
    }

    std::vector<renderer::FinishSnapshot> finishes;
    if (!renderer::capture_finish_operations(*emuenv.renderer, finishes, error)
        || !renderer::validate_finish_operations(finishes, commands, error)) return false;
    buffer.u32(static_cast<uint32_t>(finishes.size()));
    for (const auto &finish : finishes) {
        buffer.u64(finish.id);
        buffer.i32(finish.owner);
        buffer.u32(finish.context_address);
        buffer.u32(static_cast<uint32_t>(finish.phase));
        buffer.boolean(finish.fence_done);
        buffer.boolean(finish.drain_done);
        buffer.i32(finish.result);
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
    {
        const std::lock_guard lock(gxm.display_submissions_mutex);
        buffer.u32(static_cast<uint32_t>(gxm.pending_display_submissions.size()));
        for (const auto &[producer, pending] : gxm.pending_display_submissions) {
            buffer.i32(producer);
            const auto &entry = pending->callback;
            buffer.u32(entry.data);
            buffer.u32(entry.old_sync.address());
            buffer.u32(entry.new_sync.address());
            buffer.u32(entry.old_sync_timestamp);
            buffer.u32(entry.new_sync_timestamp);
            buffer.boolean(entry.frame_predicted);
            buffer.boolean(pending->has_prediction);
            buffer.u32(pending->prediction.base.address());
            buffer.u32(pending->prediction.pitch);
            buffer.u32(pending->prediction.pixelformat);
            buffer.i32(pending->prediction.image_size.x);
            buffer.i32(pending->prediction.image_size.y);
        }
    }

    write_map(buffer, gxm.memory_mapped_regions, [&buffer](const auto &entry) {
        buffer.u32(entry.first);
        buffer.u32(entry.second.offset);
        buffer.u32(entry.second.size);
        buffer.u32(entry.second.perm);
    });

    return writer.write_section(SectionId::Gxm, buffer.data().data(), buffer.size(), true, error);
}

bool read_gxm(EmuEnvState &emuenv, const std::vector<uint8_t> &data, std::string &error) {
    GxmSnapshot snapshot;
    if (!parse_gxm_state(data, snapshot, error)) return false;
    GxmState &gxm = emuenv.gxm;
    const auto &params = snapshot.params;
    const auto &global_timestamp = snapshot.global_timestamp;
    const auto &last_display_global = snapshot.last_display_global;
    const auto &notification_region = snapshot.notification_region;
    const auto &last_immediate_context = snapshot.last_immediate_context;
    const auto &contexts = snapshot.contexts;
    const auto &sync_objects = snapshot.sync_objects;
    const auto &render_targets = snapshot.render_targets;
    const auto &fragment_programs = snapshot.fragment_programs;
    const auto &vertex_programs = snapshot.vertex_programs;
    const auto &shader_patchers = snapshot.shader_patchers;
    const auto &regions = snapshot.regions;
    gxm.restored_display_queue = snapshot.display_entries;

    emuenv.renderer->gxp_ptr_map.clear();
    LOG_DEBUG("[savestate] gxm: patchers");
    gxm::restore_shader_patchers(emuenv, shader_patchers);
    LOG_DEBUG("[savestate] gxm: fragment programs");
    gxm::restore_fragment_programs(emuenv, fragment_programs);
    LOG_DEBUG("[savestate] gxm: vertex programs");
    gxm::restore_vertex_programs(emuenv, vertex_programs);
    LOG_DEBUG("[savestate] gxm: memory regions");
    gxm::restore_memory_regions(emuenv, regions);
    LOG_DEBUG("[savestate] gxm: sync objects");
    gxm::restore_sync_objects(emuenv, sync_objects);
    LOG_DEBUG("[savestate] gxm: render targets");
    gxm::restore_render_targets(emuenv, render_targets);
    LOG_DEBUG("[savestate] gxm: contexts");
    if (!gxm::restore_contexts(emuenv, contexts, snapshot.commands, snapshot.batches, error))
        return false;
    if (!renderer::restore_finish_operations(*emuenv.renderer, snapshot.finish_operations, error))
        return false;
    LOG_DEBUG("[savestate] gxm: done");

    gxm.params = params;
    gxm.global_timestamp.store(global_timestamp);
    gxm.last_display_global = last_display_global;
    gxm.notification_region = Ptr<uint32_t>(notification_region);
    gxm.last_immediate_context = last_immediate_context;
    gxm.display_queue_thread = snapshot.display_queue_thread;
    gxm.display_phase = static_cast<DisplayWorkerPhase>(snapshot.display_worker_phase);
    gxm.display_previous_entry_point = snapshot.display_previous_entry;
    {
        const std::lock_guard lock(gxm.display_submissions_mutex);
        gxm.pending_display_submissions.clear();
        for (const auto &[producer, pending] : snapshot.display_submissions)
            gxm.pending_display_submissions.emplace(producer, std::make_shared<PendingDisplaySubmission>(pending));
    }

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

bool write_obj_store(EmuEnvState &emuenv, Writer &writer, std::string &error) {
    BufferWriter buffer;
    write_sysmem(emuenv, buffer);
    return writer.write_section(SectionId::ObjStore, buffer.data().data(), buffer.size(), true, error);
}

bool read_obj_store(EmuEnvState &emuenv, const std::vector<uint8_t> &data, std::string &error) {
    SysmemSnapshot snapshot;
    if (!parse_sysmem_state(data, snapshot, error)) return false;
    auto *sysmem = emuenv.kernel.obj_store.get<SysmemState>();
    const std::lock_guard lock(sysmem->mutex);
    Blocks blocks, vm_blocks;
    for (const auto &[uid, block] : snapshot.blocks)
        blocks.emplace(uid, std::make_shared<KernelMemBlock>(block));
    for (const auto &[uid, block] : snapshot.vm_blocks) {
        const auto owner = blocks.find(uid);
        if (owner == blocks.end()) { error = "VM block has no sysmem owner"; return false; }
        vm_blocks.emplace(uid, owner->second);
    }
    sysmem->next_uid = snapshot.next_uid;
    sysmem->allocated_user = snapshot.allocated_user;
    sysmem->allocated_cdram = snapshot.allocated_cdram;
    sysmem->allocated_phycont = snapshot.allocated_phycont;
    sysmem->blocks = std::move(blocks);
    sysmem->vm_blocks = std::move(vm_blocks);
    return true;
}

static void write_blob(BufferWriter &buffer, const std::vector<uint8_t> &blob) {
    buffer.u32(static_cast<uint32_t>(blob.size()));
    if (!blob.empty())
        buffer.append(blob.data(), blob.size());
}


bool write_ngs(EmuEnvState &emuenv, Writer &writer, std::string &error) {
    BufferWriter buffer;
    Ptr<ngs::VoiceDefinition> definitions;
    std::vector<ngs::SystemInitInfo> systems;
    std::vector<ngs::RackInitInfo> racks;
    if (!ngs::capture_state(emuenv.ngs, emuenv.mem, definitions, systems, racks)) {
        error = "audio processing has not reached a snapshot boundary";
        return false;
    }

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
                buffer.u32(module.callback);
                buffer.u32(module.user_data);
                write_blob(buffer, module.guest_state_data);
                write_blob(buffer, module.parameters);
                write_blob(buffer, module.last_info);
                write_blob(buffer, module.logical_state);
            }
        }
    }

    return writer.write_section(SectionId::Ngs, buffer.data().data(), buffer.size(), true, error);
}

bool read_ngs(EmuEnvState &emuenv, const NgsSnapshot &snapshot, std::string &error) {
    if (!ngs::restore_state(emuenv.ngs, emuenv.mem, Ptr<ngs::VoiceDefinition>(snapshot.definitions), snapshot.systems, snapshot.racks)) {
        error = "failed to restore audio module state";
        return false;
    }
    return true;
}

bool preflight_audio(const std::vector<uint8_t> &data, std::string &error);

bool write_audio(EmuEnvState &emuenv, Writer &writer, std::string &error) {
    constexpr size_t kMaxAudioStateBytes = 64u * 1024u * 1024u;
    constexpr size_t kMaxAudioPortStateBytes = 16u * 1024u * 1024u + 256u;
    BufferWriter buffer;
    uint32_t port_count = 0;
    {
        const std::lock_guard<std::mutex> lock(emuenv.audio.mutex);
        if (emuenv.audio.next_port_id <= 0) {
            error = "invalid audio state";
            return false;
        }
        for (const auto &[id, port] : emuenv.audio.out_ports) {
            if (port && !port->stopping) {
                port_count++;
            }
        }
        if (port_count > 256) {
            error = "too many audio ports";
            return false;
        }
        if (port_count != 0 && !emuenv.audio.adapter) {
            error = "audio adapter is unavailable";
            return false;
        }
        buffer.u32(static_cast<uint32_t>(emuenv.audio.next_port_id));
        buffer.u32(port_count);
        const uint32_t codec = emuenv.audio.adapter ? emuenv.audio.adapter->state_codec() : 0;
        const uint64_t now_us = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        for (const auto &[id, port] : emuenv.audio.out_ports) {
            if (!port || port->stopping)
                continue;
            std::vector<uint8_t> pending_state;
            if (!emuenv.audio.adapter->save_port_state(*port, pending_state) || pending_state.empty() ||
                pending_state.size() > kMaxAudioPortStateBytes || buffer.size() > kMaxAudioStateBytes - pending_state.size() - 8) {
                error = "failed to capture bounded audio playback state";
                return false;
            }
            buffer.i32(id);
            buffer.i32(port->left_channel_volume);
            buffer.i32(port->right_channel_volume);
            buffer.value(port->volume);
            buffer.i32(port->type);
            buffer.i32(port->len);
            buffer.i32(port->freq);
            buffer.i32(port->mode);
            buffer.u64(port->len_microseconds);
            const uint64_t elapsed = now_us >= port->last_output ? now_us - port->last_output : 0;
            buffer.u64(std::min(elapsed, port->len_microseconds));
            buffer.u32(codec);
            buffer.u32(static_cast<uint32_t>(pending_state.size()));
            buffer.append(pending_state.data(), pending_state.size());
            if (buffer.size() > kMaxAudioStateBytes) {
                error = "audio state exceeds size limit";
                return false;
            }
        }
    }

    if (!preflight_audio(buffer.data(), error))
        return false;
    return writer.write_section(SectionId::Audio, buffer.data().data(), buffer.size(), true, error);
}

struct AudioPortSnapshot {
    int32_t id = 0;
    int32_t left_volume = 0;
    int32_t right_volume = 0;
    float volume = 1.0f;
    int32_t type = 0;
    int32_t len = 0;
    int32_t freq = 0;
    int32_t mode = 0;
    uint64_t len_microseconds = 0;
    uint64_t elapsed_microseconds = 0;
    uint32_t codec = 0;
    std::vector<uint8_t> pending_state;
};

struct AudioSnapshot {
    uint32_t next_port_id = 0;
    std::vector<AudioPortSnapshot> ports;
};

bool parse_audio_state(const std::vector<uint8_t> &data, AudioSnapshot &snapshot, std::string &error) {
    constexpr size_t kMaxAudioStateBytes = 64u * 1024u * 1024u;
    constexpr size_t kMaxAudioPortStateBytes = 16u * 1024u * 1024u + 256u;
    constexpr int32_t kMaxPortLength = 1 << 20;
    constexpr int32_t kMaxSampleRate = 192000;
    if (data.size() > kMaxAudioStateBytes) {
        error = "audio state exceeds size limit";
        return false;
    }
    BufferReader reader(data.data(), data.size());
    AudioSnapshot parsed;
    parsed.next_port_id = reader.u32();
    const uint32_t port_count = reader.u32();
    if (!reader.ok() || parsed.next_port_id == 0 || parsed.next_port_id >= static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
        port_count > 256 || port_count > reader.remaining() / 64) {
        error = "invalid audio port table";
        return false;
    }

    parsed.ports.resize(port_count);
    std::map<int32_t, bool> port_ids;
    int32_t max_port_id = 0;
    for (AudioPortSnapshot &port : parsed.ports) {
        port.id = reader.i32();
        port.left_volume = reader.i32();
        port.right_volume = reader.i32();
        port.volume = reader.value<float>();
        port.type = reader.i32();
        port.len = reader.i32();
        port.freq = reader.i32();
        port.mode = reader.i32();
        port.len_microseconds = reader.u64();
        port.elapsed_microseconds = reader.u64();
        port.codec = reader.u32();
        const uint32_t pending_size = reader.u32();
        if (!reader.ok() || pending_size == 0 || pending_size > kMaxAudioPortStateBytes || pending_size > reader.remaining()) {
            error = "invalid audio playback queue";
            return false;
        }
        port.pending_state.resize(pending_size);
        if (!reader.bytes(port.pending_state.data(), port.pending_state.size())) {
            error = "truncated audio playback queue";
            return false;
        }

        const int channels = port.mode == 0 ? 1 : 2;
        if (port.id <= 0 || !port_ids.emplace(port.id, true).second || port.type < 0 || port.type > 2 ||
            port.mode < 0 || port.mode > 1 || port.len <= 0 || port.len > kMaxPortLength ||
            port.freq <= 0 || port.freq > kMaxSampleRate || (port.type == 0 && port.freq != 48000) ||
            port.left_volume < 0 || port.left_volume > SCE_AUDIO_OUT_MAX_VOL ||
            port.right_volume < 0 || port.right_volume > SCE_AUDIO_OUT_MAX_VOL ||
            !std::isfinite(port.volume) || port.volume < 0.0f || port.volume > 1.0f) {
            error = "invalid audio port configuration";
            return false;
        }
        max_port_id = std::max(max_port_id, port.id);
        const uint64_t expected_len_us = (static_cast<uint64_t>(port.len) * 1'000'000ull) / static_cast<uint64_t>(port.freq);
        if ((port.len_microseconds != 0 && port.len_microseconds != expected_len_us) ||
            port.elapsed_microseconds > port.len_microseconds ||
            !validate_audio_port_snapshot(port.codec, port.len * channels * static_cast<int32_t>(sizeof(int16_t)), port.pending_state)) {
            error = "invalid audio playback state";
            return false;
        }
    }
    if (!reader.ok() || reader.remaining() != 0 || parsed.next_port_id <= static_cast<uint32_t>(max_port_id)) {
        error = "invalid audio port table";
        return false;
    }
    snapshot = std::move(parsed);
    return true;
}

bool preflight_audio(const std::vector<uint8_t> &data, std::string &error) {
    AudioSnapshot snapshot;
    return parse_audio_state(data, snapshot, error);
}

bool preflight_audio(const std::vector<uint8_t> &data, const std::vector<ThreadState::Snapshot> &threads,
    const MemoryImage &memory, uint32_t current_codec, std::string &error) {
    AudioSnapshot snapshot;
    if (!parse_audio_state(data, snapshot, error))
        return false;
    for (const auto &port : snapshot.ports) {
        if (port.codec != current_codec) {
            error = "saved audio queue uses a different backend";
            return false;
        }
    }
    for (const auto &thread : threads) {
        for (const auto &wait : thread.waits) {
            if (wait.operation != WaitOperation::audio_output)
                continue;
            const auto port = std::find_if(snapshot.ports.begin(), snapshot.ports.end(),
                [&](const auto &port) { return uint32_t(port.id) == wait.args[0]; });
            if (port == snapshot.ports.end()) {
                error = "audio output continuation references a missing playback port";
                return false;
            }
            const int bytes = port->len * (port->mode == 0 ? 1 : 2) * int(sizeof(int16_t));
            if (!validate_audio_wait_configuration(wait,
                    {port->id, port->len, bytes, port->freq, port->mode, port->len_microseconds, port->codec},
                    [&](Address address, size_t size) { return memory.contains(address, size); }, error))
                return false;
        }
    }
    return true;
}

bool read_audio(EmuEnvState &emuenv, const std::vector<uint8_t> &data, std::string &error) {
    AudioSnapshot snapshot;
    if (!parse_audio_state(data, snapshot, error))
        return false;
    if (!emuenv.audio.adapter) {
        error = "audio adapter is unavailable";
        return false;
    }
    for (const AudioPortSnapshot &info : snapshot.ports) {
        if (info.codec != emuenv.audio.adapter->state_codec()) {
            error = "saved audio queue uses a different backend";
            return false;
        }
    }

    std::map<int, AudioOutPortPtr> reopened;
    const uint64_t now_us = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    for (const AudioPortSnapshot &info : snapshot.ports) {
        const int channels = (info.mode == 0) ? 1 : 2;
        AudioOutPortPtr port = emuenv.audio.open_port_for_restore(channels, info.freq, info.len);
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
        emuenv.audio.set_volume(*port, info.volume);
        port->len_microseconds = info.len_microseconds;
        port->last_output = now_us - std::min(now_us, info.elapsed_microseconds);
        if (!emuenv.audio.adapter->restore_port_state(*port, info.pending_state)) {
            error = "failed to restore audio playback queue";
            return false;
        }
        reopened.emplace(info.id, port);
    }

    {
        const std::lock_guard<std::mutex> lock(emuenv.audio.mutex);
        emuenv.audio.out_ports = std::move(reopened);
        emuenv.audio.next_port_id = static_cast<int>(snapshot.next_port_id);
    }
    LOG_CRITICAL("[savestate-audio] restored {} audio ports (next_id={})", snapshot.ports.size(), snapshot.next_port_id);
    return true;
}

bool preflight_input(const std::vector<uint8_t> &data, std::string &error);

bool write_input(EmuEnvState &emuenv, Writer &writer, std::string &error) {
    BufferWriter buffer;
    buffer.i32(static_cast<int32_t>(emuenv.ctrl.input_mode));
    buffer.i32(static_cast<int32_t>(emuenv.ctrl.input_mode_ext));
    for (int i = 0; i < 5; i++)
        buffer.u64(emuenv.ctrl.last_vcount[i]);
    if (!preflight_input(buffer.data(), error))
        return false;
    return writer.write_section(SectionId::Input, buffer.data().data(), buffer.size(), true, error);
}

struct InputSnapshot {
    int32_t input_mode = 0;
    int32_t input_mode_ext = 0;
    std::array<uint64_t, 5> last_vcount{};
};

bool parse_input_state(const std::vector<uint8_t> &data, InputSnapshot &snapshot, std::string &error) {
    BufferReader reader(data.data(), data.size());
    InputSnapshot parsed;
    parsed.input_mode = reader.i32();
    parsed.input_mode_ext = reader.i32();
    for (uint64_t &vcount : parsed.last_vcount)
        vcount = reader.u64();
    if (!reader.ok() || reader.remaining() != 0 ||
        parsed.input_mode < SCE_CTRL_MODE_DIGITAL || parsed.input_mode > SCE_CTRL_MODE_ANALOG_WIDE ||
        parsed.input_mode_ext < SCE_CTRL_MODE_DIGITAL || parsed.input_mode_ext > SCE_CTRL_MODE_ANALOG_WIDE) {
        error = "invalid input state";
        return false;
    }
    snapshot = std::move(parsed);
    return true;
}

bool preflight_input(const std::vector<uint8_t> &data, std::string &error) {
    InputSnapshot snapshot;
    return parse_input_state(data, snapshot, error);
}

bool read_input(EmuEnvState &emuenv, const std::vector<uint8_t> &data, std::string &error) {
    InputSnapshot snapshot;
    if (!parse_input_state(data, snapshot, error))
        return false;

    emuenv.ctrl.input_mode = static_cast<SceCtrlPadInputMode>(snapshot.input_mode);
    emuenv.ctrl.input_mode_ext = static_cast<SceCtrlPadInputMode>(snapshot.input_mode_ext);
    std::copy(snapshot.last_vcount.begin(), snapshot.last_vcount.end(), emuenv.ctrl.last_vcount);
    LOG_CRITICAL("[savestate-input] restored input_mode={} input_mode_ext={}", snapshot.input_mode, snapshot.input_mode_ext);
    return true;
}

bool preflight_io(const std::vector<uint8_t> &data, std::string &error);

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

    if (!preflight_io(buffer.data(), error))
        return false;
    return writer.write_section(SectionId::Io, buffer.data().data(), buffer.size(), true, error);
}

struct IoTtyInfo {
    int32_t fd = 0;
    int32_t type = 0;
};

struct IoFileInfo {
    int32_t fd = 0;
    std::string path;
    int32_t open_mode = 0;
    int64_t position = 0;
};

struct IoDirInfo {
    int32_t fd = 0;
    std::string path;
};

struct IoSnapshot {
    int32_t next_fd = 0;
    std::vector<IoTtyInfo> ttys;
    std::vector<IoFileInfo> files;
    std::vector<IoDirInfo> dirs;
};

bool read_bounded_io_path(BufferReader &reader, std::string &path) {
    constexpr uint32_t kMaxPathBytes = 4096;
    const uint32_t length = reader.u32();
    if (!reader.ok() || length == 0 || length > kMaxPathBytes || length > reader.remaining())
        return false;
    path.resize(length);
    if (!reader.bytes(path.data(), length) || path.find('\0') != std::string::npos)
        return false;
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    const fs::path parsed(normalized);
    for (const fs::path &part : parsed) {
        if (part == "." || part == "..")
            return false;
    }
    return true;
}

bool valid_io_open_mode(int32_t mode) {
    constexpr int32_t kKnownFlags = SCE_O_RDONLY | SCE_O_WRONLY | SCE_O_NBLOCK | SCE_O_RDLOCK | SCE_O_WRLOCK |
        SCE_O_APPEND | SCE_O_CREAT | SCE_O_TRUNC | SCE_O_EXCL | SCE_O_SCAN | SCE_O_RCOM | SCE_O_NOBUF |
        SCE_O_NOWAIT | SCE_O_FDEXCL | SCE_O_PWLOCK | SCE_O_FGAMEDATA;
    const int32_t access = mode & (SCE_O_RDONLY | SCE_O_WRONLY);
    return mode >= 0 && (mode & ~kKnownFlags) == 0 && access != 0;
}

bool parse_io_state(const std::vector<uint8_t> &data, IoSnapshot &snapshot, std::string &error) {
    BufferReader reader(data.data(), data.size());
    IoSnapshot parsed;
    parsed.next_fd = reader.i32();

    const uint32_t tty_count = reader.u32();
    if (!reader.ok() || tty_count > 4096 || tty_count > reader.remaining() / 8) {
        error = "invalid IO tty table";
        return false;
    }
    parsed.ttys.resize(tty_count);
    for (IoTtyInfo &entry : parsed.ttys) {
        entry.fd = reader.i32();
        entry.type = reader.i32();
    }

    const uint32_t file_count = reader.u32();
    if (!reader.ok() || file_count > 4096 || file_count > reader.remaining() / 20) {
        error = "invalid IO file table";
        return false;
    }
    parsed.files.resize(file_count);
    for (IoFileInfo &file : parsed.files) {
        file.fd = reader.i32();
        if (!reader.ok() || !read_bounded_io_path(reader, file.path)) {
            error = "invalid IO file path";
            return false;
        }
        file.open_mode = reader.i32();
        file.position = reader.i64();
    }

    const uint32_t dir_count = reader.u32();
    if (!reader.ok() || dir_count > 4096 || dir_count > reader.remaining() / 8) {
        error = "invalid IO dir table";
        return false;
    }
    parsed.dirs.resize(dir_count);
    for (IoDirInfo &entry : parsed.dirs) {
        entry.fd = reader.i32();
        if (!reader.ok() || !read_bounded_io_path(reader, entry.path)) {
            error = "invalid IO directory path";
            return false;
        }
    }

    if (!reader.ok() || reader.remaining() != 0 || parsed.next_fd < 0) {
        error = "invalid IO state";
        return false;
    }

    std::map<int32_t, bool> used_fds;
    int32_t maximum_fd = -1;
    auto add_fd = [&](int32_t fd) {
        if (fd < 0 || fd == std::numeric_limits<int32_t>::max() || !used_fds.emplace(fd, true).second)
            return false;
        maximum_fd = std::max(maximum_fd, fd);
        return true;
    };
    for (const IoTtyInfo &tty : parsed.ttys) {
        if (!add_fd(tty.fd) || tty.type < TTY_IN || tty.type > TTY_INOUT) {
            error = "invalid IO tty entry";
            return false;
        }
    }
    for (const IoFileInfo &file : parsed.files) {
        if (!add_fd(file.fd) || file.position < 0 || !valid_io_open_mode(file.open_mode)) {
            error = "invalid IO file entry";
            return false;
        }
    }
    for (const IoDirInfo &dir : parsed.dirs) {
        if (!add_fd(dir.fd)) {
            error = "invalid IO directory entry";
            return false;
        }
    }
    if (parsed.next_fd <= maximum_fd) {
        error = "invalid IO next descriptor";
        return false;
    }

    snapshot = std::move(parsed);
    return true;
}

bool preflight_io(const std::vector<uint8_t> &data, std::string &error) {
    IoSnapshot snapshot;
    return parse_io_state(data, snapshot, error);
}

bool read_io(EmuEnvState &emuenv, const std::vector<uint8_t> &data, std::string &error) {
    IoSnapshot snapshot;
    if (!parse_io_state(data, snapshot, error))
        return false;

    // Prepare all replacement handles outside the live tables. Never create or
    // truncate files while restoring a saved descriptor.
    IOState staged;
    IOState &io = emuenv.io;
    staged.device_paths = io.device_paths;
    staged.redirect_stdio = io.redirect_stdio;
    staged.case_isens_find_enabled = io.case_isens_find_enabled;
    staged.cachemap = io.cachemap;
    for (const IoTtyInfo &tty : snapshot.ttys)
        staged.tty_files.emplace(tty.fd, static_cast<TtyType>(tty.type));

    uint32_t reopened_files = 0;
    uint32_t reopened_dirs = 0;
    for (const IoFileInfo &file : snapshot.files) {
        const int32_t safe_mode = file.open_mode & ~(SCE_O_CREAT | SCE_O_TRUNC | SCE_O_EXCL);
        staged.next_fd = file.fd;
        const SceUID opened = open_file(staged, file.path.c_str(), safe_mode, emuenv.vita_fs_path, "savestate");
        if (opened != file.fd) {
            error = "failed to reopen saved IO file: " + file.path;
            return false;
        }
        auto entry = staged.std_files.find(opened);
        if (entry == staged.std_files.end() || !entry->second.seek(file.position, SCE_SEEK_SET) || entry->second.tell() != file.position) {
            error = "failed to restore saved IO file position: " + file.path;
            return false;
        }
        ++reopened_files;
    }
    for (const IoDirInfo &dir : snapshot.dirs) {
        staged.next_fd = dir.fd;
        const SceUID opened = open_dir(staged, dir.path.c_str(), emuenv.vita_fs_path, "savestate");
        if (opened != dir.fd) {
            error = "failed to reopen saved IO directory: " + dir.path;
            return false;
        }
        ++reopened_dirs;
    }
    staged.next_fd = snapshot.next_fd;

    {
        const std::lock_guard<std::mutex> lock(io.file_mutex);
        io.tty_files.swap(staged.tty_files);
        io.std_files.swap(staged.std_files);
        io.dir_entries.swap(staged.dir_entries);
        io.next_fd = staged.next_fd;
    }

    LOG_CRITICAL("[savestate-io] restored files={}/{} dirs={}/{} tty={} next_fd={}", reopened_files, snapshot.files.size(), reopened_dirs, snapshot.dirs.size(), snapshot.ttys.size(), io.next_fd);
    return true;
}

bool write_kernel(EmuEnvState &emuenv, Writer &writer, std::string &error) {
    BufferWriter base;
    write_kernel_base(emuenv, base);
    write_callbacks(emuenv.kernel, base);
    return writer.write_section(SectionId::Kernel, base.data().data(), base.size(), true, error);
}

bool read_kernel(EmuEnvState &emuenv, const KernelBaseSnapshot &snapshot, std::string &error) {
    if (!read_kernel_base(emuenv, snapshot, error))
        return false;
    emuenv.kernel.snapshot_callbacks = snapshot.callbacks;
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

    ScopedSnapshotGate gxm_host(emuenv.gxm.display_snapshot_gate,
        emuenv.gxm.display_host_thread.joinable());
    if (!gxm_host) {
        result.status = Status::InternalError;
        result.error = "GXM display callback did not reach a snapshot boundary";
        return result;
    }
    ScopedVblankFreeze vblank(emuenv.display, std::chrono::seconds(2));
    if (!vblank) {
        result.status = Status::InternalError;
        result.error = "display producer did not reach a snapshot boundary";
        return result;
    }
    ScopedWorldStop world(emuenv.kernel);
    const int not_parked = world.stop();
    if (not_parked != 0) {
        result.status = Status::InternalError;
        result.error = "guest threads did not stop";
        return result;
    }

    ScopedSnapshotGate rendering(emuenv.renderer->snapshot_gate);
    if (!rendering) {
        result.status = Status::InternalError;
        result.error = "renderer did not drain pending commands";
        return result;
    }
    std::string error;
    std::vector<gxm::ContextSnapshot> context_check;
    std::vector<renderer::CommandSnapshot> command_check;
    std::vector<gxm::BatchSnapshot> batch_check;
    GpuSnapshot gpu_snapshot;
    std::vector<uint8_t> gpu_data;
    // Pending scenes cannot be silently discarded or flushed into guest RAM.
    if (!gxm::capture_contexts(emuenv, context_check, command_check, batch_check, error)) {
        result.status = Status::InternalError;
        result.error = error;
        return result;
    }
    if (!wait_gpu_snapshot_idle(emuenv, error)) {
        result.status = Status::InternalError;
        result.error = error;
        return result;
    }
    GxmSnapshot finish_check;
    finish_check.contexts = context_check;
    finish_check.commands = command_check;
    std::vector<ThreadState::Snapshot> finish_threads;
    if (!renderer::capture_finish_operations(*emuenv.renderer, finish_check.finish_operations, error)
        || !capture_thread_snapshots(emuenv.kernel, 0, finish_threads, error)
        || !validate_gxm_finish_continuations(finish_check, finish_threads, error)) {
        result.status = Status::InternalError;
        result.error = error;
        return result;
    }
    if (!capture_gpu_state(emuenv, gpu_snapshot, error) || !encode_gpu_state(gpu_snapshot, gpu_data, error)) {
        result.status = Status::InternalError;
        result.error = error;
        return result;
    }

    Writer writer;
    if (!writer.open(path, error)) {
        result.status = Status::IoError;
        result.error = error;
        return result;
    }

    const auto fail = [&](Status status, const std::string &message) {
        writer.abort();
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
    if (!writer.write_section(SectionId::Gpu, gpu_data.data(), gpu_data.size(), true, error))
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
    if (!reader.validate_sections(error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }
    auto memory_image = MemoryImage::preflight(reader, emuenv.cache_path / "savestate-staging", error);
    if (!memory_image) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }
    std::vector<uint8_t> kernel_data;
    std::vector<uint8_t> kernel_sync_data;
    std::vector<uint8_t> threads_data;
    std::vector<uint8_t> display_data;
    std::vector<uint8_t> gxm_data;
    std::vector<uint8_t> gpu_data;
    std::vector<uint8_t> ngs_data;
    std::vector<uint8_t> objstore_data;
    std::vector<uint8_t> audio_data;
    std::vector<uint8_t> input_data;
    std::vector<uint8_t> io_data;

    if (!reader.read_section(SectionId::Kernel, kernel_data, error)
        || !reader.read_section(SectionId::KernelSync, kernel_sync_data, error)
        || !reader.read_section(SectionId::Threads, threads_data, error)
        || !reader.read_section(SectionId::Display, display_data, error)
        || !reader.read_section(SectionId::Gxm, gxm_data, error)
        || !reader.read_section(SectionId::Gpu, gpu_data, error)
        || !reader.read_section(SectionId::ObjStore, objstore_data, error)
        || !reader.read_section(SectionId::Ngs, ngs_data, error)
        || !reader.read_section(SectionId::Audio, audio_data, error)
        || !reader.read_section(SectionId::Input, input_data, error)
        || !reader.read_section(SectionId::Io, io_data, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }

    GpuSnapshot gpu_snapshot;
    GxmSnapshot gxm_snapshot;
    SysmemSnapshot sysmem_snapshot;
    KernelBaseSnapshot kernel_snapshot;
    NgsSnapshot ngs_snapshot;
    DisplaySnapshot display_snapshot;
    ParsedKernelSync sync_snapshot;
    std::vector<ThreadState::Snapshot> thread_snapshots;
    const auto valid_range = [&](Address address, size_t size) { return memory_image->contains(address, size); };
    const auto external_tag = [&](uint32_t tag) { return emuenv.kernel.callback_resume_handlers.contains(tag); };
    if (!parse_kernel_base(kernel_data, kernel_snapshot, error)
        || !validate_kernel_base(kernel_snapshot, *memory_image, error)
        || !validate_kernel_modules(kernel_snapshot, *memory_image, error)
        || !parse_kernel_sync(kernel_sync_data, sync_snapshot, error)
        || !parse_thread_snapshots(threads_data, thread_snapshots, error)
        || !validate_kernel_continuations(thread_snapshots, kernel_snapshot.callbacks, sync_snapshot, valid_range, external_tag, error)
        || !validate_kernel_import_abis(thread_snapshots, *memory_image, error)
        || !validate_kernel_thread_allocations(thread_snapshots, kernel_snapshot.tls_msize, *memory_image, error)
        || !parse_gpu_state(gpu_data, gpu_snapshot, error)
        || !validate_gpu_state(gpu_snapshot, *memory_image, error)
        || !parse_gxm_state(gxm_data, gxm_snapshot, error)
        || !validate_gxm_state(gxm_snapshot, *memory_image, error)
        || !validate_gpu_gxm_references(gpu_snapshot, gxm_snapshot, error)
        || !parse_sysmem_state(objstore_data, sysmem_snapshot, error)
        || !validate_sysmem_state(sysmem_snapshot, *memory_image, error)
        || !validate_gxm_display_continuations(gxm_snapshot, thread_snapshots, *memory_image, error, &sysmem_snapshot)
        || !validate_gxm_finish_continuations(gxm_snapshot, thread_snapshots, error)
        || !parse_ngs_state(ngs_data, ngs_snapshot, error)
        || !validate_ngs_state(ngs_snapshot, *memory_image, error)
        || !parse_display_state(display_data, display_snapshot, error)
        || !validate_display_state(display_snapshot, *memory_image, error)
        || !preflight_audio(audio_data, thread_snapshots, *memory_image,
            emuenv.audio.adapter ? emuenv.audio.adapter->state_codec() : 0, error)
        || !preflight_input(input_data, error)
        || !preflight_io(io_data, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }

    if (emuenv.renderer->current_backend != renderer::Backend::Vulkan
        || static_cast<uint32_t>(emuenv.renderer->mapping_method) != gpu_snapshot.mapping_method
        || emuenv.renderer->res_multiplier != gpu_snapshot.resolution) {
        result.status = Status::UnsupportedVersion;
        result.error = "GPU configuration differs from save state";
        return result;
    }

    std::set<SceUID> kernel_ids;
    const auto unique_uid = [&](SceUID uid) {
        return uid > 0 && uid < kernel_snapshot.next_uid && kernel_ids.insert(uid).second;
    };
    bool valid_ids = true;
    bool main_thread_found = kernel_snapshot.main_thread_id == 0;
    for (const auto &[uid, block] : kernel_snapshot.codec_blocks) valid_ids &= unique_uid(uid);
    for (const auto &[uid, module] : kernel_snapshot.loaded_modules) valid_ids &= unique_uid(uid);
    for (const auto &thread : thread_snapshots) {
        valid_ids &= unique_uid(thread.id);
        main_thread_found |= thread.registered && thread.id == kernel_snapshot.main_thread_id;
    }
    std::set<SceUID> callback_ids;
    for (const auto &callback : kernel_snapshot.callbacks) {
        valid_ids &= unique_uid(callback.uid);
        callback_ids.insert(callback.uid);
    }
    for (const auto &object : sync_snapshot.objects) valid_ids &= unique_uid(object.header.uid);
    for (const auto uid : display_snapshot.callbacks) valid_ids &= callback_ids.contains(uid);
    if (!valid_ids || !main_thread_found) {
        result.status = Status::InvalidFile;
        result.error = "inconsistent kernel UID references";
        return result;
    }

    LOG_DEBUG("[savestate] load: sections read, stopping world (cross_session={})", cross_session);
    ScopedSnapshotGate gxm_host(emuenv.gxm.display_snapshot_gate,
        emuenv.gxm.display_host_thread.joinable());
    if (!gxm_host) {
        result.status = Status::InternalError;
        result.error = "GXM display callback did not reach a snapshot boundary";
        return result;
    }
    ScopedVblankFreeze vblank(emuenv.display, std::chrono::seconds(2));
    if (!vblank) {
        result.status = Status::InternalError;
        result.error = "display producer did not reach a snapshot boundary";
        return result;
    }
    ScopedWorldStop world(emuenv.kernel);
    if (world.stop() != 0) {
        result.status = Status::InternalError;
        result.error = "guest threads did not stop";
        LOG_CRITICAL("[savestate-error] load: guest threads did not stop");
        return result;
    }
    LOG_DEBUG("[savestate] load: world stopped");
    ScopedSnapshotGate rendering(emuenv.renderer->snapshot_gate);
    if (!rendering) {
        result.status = Status::InternalError;
        result.error = "renderer did not drain pending commands";
        return result;
    }
    emuenv.renderer->wait_gpu_idle();
    renderer::discard_pending_batches(*emuenv.renderer);
    rendering.release();

    ScopedRestoreFailure restore_failure(emuenv, result);
    result.session_usable = false;
    const bool session_paused = emuenv.kernel.is_threads_paused();


    // Guest sync waits can leave the render thread blocked inside a leading
    // WaitSyncObject command; invalidate the sync objects so it can drain.
    gxm::invalidate_sync_objects(emuenv.gxm);
    LOG_DEBUG("[savestate] load: sync objects invalidated");

    // Destroy the GPU-side runtime while the render thread can still process
    // the destroy commands, then park it so the GPU goes idle before RAM is
    // overwritten.
    emuenv.kernel.process_exit();
    if (!prepare_gpu_teardown(emuenv, error)) {
        result.status = Status::InternalError;
        result.error = error;
        return result;
    }
    gxm::destroy_runtime_objects(emuenv);
    LOG_DEBUG("[savestate] load: gxm runtime destroyed");
    renderer::stop_render_thread(*emuenv.renderer);
    LOG_DEBUG("[savestate] load: render thread stopped");
    renderer::abort_finish_operations(*emuenv.renderer);

    clear_display_waiters_for_restore(emuenv.display);
    {
        const std::lock_guard lock(emuenv.display.mutex);
        emuenv.display.vblank_callbacks.clear();
    }
    emuenv.kernel.clear_paused_threads_state();
    emuenv.kernel.reset_world_stop_state();
    LOG_DEBUG("[savestate] load: guest threads exited");

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
    LOG_DEBUG("[savestate] load: old kernel objects released");

    // NGS host objects live in guest memory and cannot survive the RAM restore;
    // release them now (their memory is still valid) and rebuild after loading.
    ngs::deinit(emuenv.ngs, emuenv.mem);
    LOG_DEBUG("[savestate] load: ngs released");

    report(progress, 0.10f, "memory");
    if (!read_memory(emuenv, *memory_image, progress, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }
    LOG_DEBUG("[savestate] load: memory restored");

    // Cached GPU textures/surfaces were built from the pre-load guest RAM.
    emuenv.renderer->reset_caches();
    if (!prepare_gpu_state_restore(emuenv, error)) {
        result.status = Status::InternalError;
        result.error = error;
        return result;
    }
    LOG_DEBUG("[savestate] load: renderer caches and frame runtime reset");

    report(progress, 0.88f, "kernel");
    if (!read_kernel(emuenv, kernel_snapshot, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }
    LOG_DEBUG("[savestate] load: kernel restored");

    // Restart the renderer before guest threads are recreated: they may issue
    // GPU commands as soon as they start running.
    {
        const std::lock_guard lock(emuenv.display.display_info_mutex);
        emuenv.display.next_rendered_frame = {};
    }
    renderer::start_render_thread(*emuenv.renderer, emuenv.display, emuenv.gxm, emuenv.mem, emuenv.cfg);
    LOG_DEBUG("[savestate] load: renderer restarted");

    report(progress, 0.95f, "runtime");
    if (!read_obj_store(emuenv, objstore_data, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }
    LOG_DEBUG("[savestate] load: objstore restored");
    if (!read_gxm(emuenv, gxm_data, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }
    LOG_DEBUG("[savestate] load: gxm restored");
    ScopedSnapshotGate restored_rendering(emuenv.renderer->snapshot_gate);
    if (!restored_rendering) {
        result.status = Status::InternalError;
        result.error = "restored renderer did not reach a snapshot boundary";
        return result;
    }
    emuenv.renderer->wait_gpu_idle();
    if (!restore_gpu_state(emuenv, gpu_snapshot, error)) {
        result.status = Status::InternalError;
        result.error = error;
        return result;
    }


    if (!read_ngs(emuenv, ngs_snapshot, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }
    LOG_DEBUG("[savestate] load: ngs restored");

    // Audio ports are host objects the restored guest state still refers to.
    if (!audio_data.empty() && !read_audio(emuenv, audio_data, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }
    LOG_DEBUG("[savestate] load: audio restored");

    if (!input_data.empty() && !read_input(emuenv, input_data, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }
    LOG_DEBUG("[savestate] load: input restored");

    if (!io_data.empty() && !read_io(emuenv, io_data, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }
    LOG_DEBUG("[savestate] load: io restored");

    // Create the guest threads parked, restore the kernel objects they depend
    // on, and only then let them run.
    report(progress, 0.92f, "threads");
    std::vector<ThreadState::Snapshot> deferred_threads;
    if (!read_threads(emuenv, threads_data, deferred_threads, session_paused, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }
    LOG_DEBUG("[savestate] load: threads recreated");
    if (!read_display(emuenv, display_snapshot, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }
    LOG_DEBUG("[savestate] load: display restored");


    if (!read_kernel_sync(emuenv, kernel_sync_data, error)) {
        result.status = Status::InvalidFile;
        result.error = error;
        return result;
    }
    LOG_DEBUG("[savestate] load: kernel sync restored");

    gxm_host.release();
    if (!gxm::restart_display_queue(emuenv, true)) {
        result.status = Status::InternalError;
        result.error = "failed to restart the saved display queue";
        return result;
    }
    LOG_DEBUG("[savestate] load: display queue restarted");

    register_display_wait_continuations(emuenv.display, kernel);
    register_audio_wait_handlers(kernel, emuenv.audio, emuenv.mem);
    gxm::register_display_queue_wait_handlers(emuenv);
    renderer::register_finish_wait_handlers(kernel, *emuenv.renderer);
    for (const auto &snapshot : deferred_threads) {
        if (!snapshot.registered)
            continue;
        const auto thread = kernel.get_thread(snapshot.id);
        if (!thread || !thread->restore_wait_queues()) {
            result.status = Status::InternalError;
            result.error = "failed to relink saved thread wait";
            return result;
        }
    }
    kernel.snapshot_objects.clear();
    kernel.snapshot_thread_owners.clear();
    for (const auto &snapshot : deferred_threads) {
        if (snapshot.registered)
            kernel.get_thread(snapshot.id)->activate_restored_continuations();
    }
    LOG_DEBUG("[savestate] load: threads started");
    if (!gxm::activate_saved_batches(emuenv, gxm_snapshot.renderer_context_address, error)) {
        result.status = Status::InternalError;
        result.error = error;
        return result;
    }
    result.session_usable = true;
    restored_rendering.release();

    for (const auto &[id, thread] : emuenv.kernel.threads) {
        if (!thread || !thread->cpu)
            continue;
        const uint32_t sp = read_sp(*thread->cpu);
        const uint32_t pc = read_pc(*thread->cpu);
        if ((sp && !is_valid_addr(emuenv.mem, sp)) || (pc && !is_valid_addr(emuenv.mem, pc)))
            LOG_CRITICAL("[savestate-error] load: thread {} has invalid sp=0x{:X} pc=0x{:X} status={}", id, sp, pc, static_cast<int>(thread->status));
    }


    report(progress, 1.0f, "done");
    world.release();
    emuenv.gxm.display_snapshot_gate.release();
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
