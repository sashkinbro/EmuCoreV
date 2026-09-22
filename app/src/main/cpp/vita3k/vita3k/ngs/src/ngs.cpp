// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include <cpu/functions.h>
#include <kernel/state.h>

#include <ngs/modules/atrac9.h>
#include <ngs/state.h>
#include <ngs/system.h>
#include <util/lock_and_find.h>
#include <util/log.h>

#include <util/vector_utils.h>

namespace ngs {
Rack::Rack(System *mama, const Ptr<void> memspace, const uint32_t memspace_size)
    : MempoolObject(memspace, memspace_size)
    , system(mama) {}

System::System(const Ptr<void> memspace, const uint32_t memspace_size)
    : MempoolObject(memspace, memspace_size)
    , max_voices(0)
    , granularity(0)
    , sample_rate(0) {}

bool Patch::is_active() const {
    return output_sub_index != -1;
}

void VoiceInputManager::init(const uint32_t granularity, const uint16_t total_input) {
    inputs.resize(std::max<uint16_t>(1, total_input));

    for (auto &input : inputs) {
        // FLTP and maximum channel count
        input.resize(granularity * 8);
    }

    reset_inputs();
}

void VoiceInputManager::reset_inputs() {
    for (auto &input : inputs) {
        std::fill(input.begin(), input.end(), 0);
    }
}

VoiceInputManager::PCMInput *VoiceInputManager::get_input_buffer_queue(const int32_t index) {
    if (index >= inputs.size()) {
        return nullptr;
    }

    return &inputs[index];
}

int32_t VoiceInputManager::receive(const MemState &mem, ngs::Patch *patch, const VoiceProduct &product) {
    Voice *source = patch->source.get(mem);
    Voice *dest = patch->dest.get(mem);
    if (!source || !dest) {
        return -1;
    }

    PCMInput *input = get_input_buffer_queue(patch->dest_index);

    if (!input) {
        return -1;
    }

    float *dest_buffer = reinterpret_cast<float *>(input->data());
    const float *data_to_mix_in = reinterpret_cast<const float *>(product.data);

    float volume_matrix[2][2];
    memcpy(volume_matrix, patch->volume_matrix, sizeof(volume_matrix));

    // we always use stereo internally, so make sure not to add too many channels
    if (source->rack->channels_per_voice == 1) {
        volume_matrix[1][0] = 0.0f;
        volume_matrix[1][1] = 0.0f;
    }

    if (dest->rack->channels_per_voice == 1) {
        volume_matrix[0][1] = 0.0f;
        volume_matrix[1][1] = 0.0f;
    }

    // Try mixing, also with the use of this volume matrix
    // Dest is our voice to receive this data.
    for (int32_t k = 0; k < dest->rack->system->granularity; k++) {
        dest_buffer[k * 2] = std::clamp(dest_buffer[k * 2] + data_to_mix_in[k * 2] * volume_matrix[0][0]
                + data_to_mix_in[k * 2 + 1] * volume_matrix[1][0],
            -1.0f, 1.0f);
        dest_buffer[k * 2 + 1] = std::clamp(dest_buffer[k * 2 + 1] + data_to_mix_in[k * 2] * volume_matrix[0][1] + data_to_mix_in[k * 2 + 1] * volume_matrix[1][1], -1.0f, 1.0f);
    }

    return 0;
}

ModuleData::ModuleData()
    : callback(0)
    , user_data(0)
    , is_bypassed(false)
    , flags(0) {
}

SceNgsBufferInfo *ModuleData::lock_params(const MemState &mem) {
    const std::lock_guard<std::mutex> guard(*parent->voice_mutex);

    // Save a copy of previous set of data
    if (flags & PARAMS_LOCK) {
        return nullptr;
    }

    const uint8_t *current_data = info.data.cast<const uint8_t>().get(mem);
    last_info.resize(info.size);
    memcpy(last_info.data(), current_data, info.size);

    flags |= PARAMS_LOCK;

    return &info;
}

bool ModuleData::unlock_params(const MemState &mem) {
    const std::lock_guard<std::mutex> guard(*parent->voice_mutex);

    parent->rack->modules[index]->on_param_change(mem, *this);

    if (flags & PARAMS_LOCK) {
        flags &= ~PARAMS_LOCK;
        return true;
    }

    return false;
}

void ModuleData::invoke_callback(KernelState &kernel, const MemState &mem, const SceUID thread_id, const uint32_t reason1,
    const uint32_t reason2, Address reason_ptr) {
    return parent->invoke_callback(kernel, mem, thread_id, callback, user_data, parent->rack->modules[index]->module_id(),
        reason1, reason2, reason_ptr);
}

void Voice::init(Rack *mama) {
    rack = mama;
    state = VoiceState::VOICE_STATE_AVAILABLE;
    is_pending = false;
    is_paused = false;
    is_keyed_off = false;

    datas.resize(mama->modules.size());

    for (uint32_t i = 0; i < MAX_OUTPUT_PORT; i++)
        patches[i].resize(mama->patches_per_output);

    uint16_t input_count = 0;
    if (size_inputs_by_input_mixer_count) {
        for (const auto &module : mama->modules) {
            if (module && module->module_id() == input_mixer_module_id)
                input_count++;
        }
    }
    inputs.init(rack->system->granularity, std::max<uint16_t>(1, input_count));
    voice_mutex = std::make_unique<std::mutex>();
}

Ptr<Patch> Voice::patch(const MemState &mem, const int32_t index, int32_t subindex, int32_t dest_index, Ptr<Voice> source, Ptr<Voice> dest) {
    const std::lock_guard<std::mutex> guard(*voice_mutex);

    if (index >= MAX_OUTPUT_PORT) {
        // We don't have enough port for you!
        return {};
    }

    // Look if another patch has already been there
    if (subindex == -1) {
        for (int32_t i = 0; i < patches[index].size(); i++) {
            if (!patches[index][i] || (patches[index])[i].get(mem)->output_sub_index == -1) {
                subindex = i;
                break;
            }
        }
    }

    if (subindex >= patches[index].size()) {
        return {};
    }

    if (patches[index][subindex] && patches[index][subindex].get(mem)->output_sub_index != -1) {
        // You just hit an occupied subindex! You won't get to eat, stay in detention.
        return {};
    }

    if (!patches[index][subindex]) {
        // Create the patch incase it's not yet existed
        patches[index][subindex] = rack->alloc_and_init<Patch>(mem);
    }

    Patch *patch = patches[index][subindex].get(mem);

    patch->output_sub_index = subindex;
    patch->output_index = index;
    patch->dest_index = dest_index;
    patch->dest = dest;
    patch->source = source;

    // Initialize the matrix
    memset(patch->volume_matrix, 0, sizeof(patch->volume_matrix));
    if (default_patch_volume_is_unity) {
        patch->volume_matrix[0][0] = 1.0f;
        patch->volume_matrix[1][1] = 1.0f;
    }

    return patches[index][subindex];
}

bool Voice::remove_patch(const MemState &mem, const Ptr<Patch> patch) {
    if (!patch || !voice_mutex) {
        return false;
    }
    const std::lock_guard<std::mutex> guard(*voice_mutex);
    bool found = false;
    for (auto &patches_1 : patches) {
        if (std::ranges::contains(patches_1, patch)) {
            found = true;
            break;
        }
    }
    if (!found)
        return false;

    // Try to unroute. Free the destination index
    patch.get(mem)->output_sub_index = -1;

    return true;
}

ModuleData *Voice::module_storage(const uint32_t index) {
    if (index >= datas.size()) {
        return nullptr;
    }

    return &datas[index];
}

void Voice::transition(const MemState &mem, const VoiceState new_state) {
    const VoiceState old = state;
    state = new_state;

    for (size_t i = 0; i < datas.size(); i++) {
        rack->modules[i]->on_state_change(mem, datas[i], old);
    }
}

bool Voice::parse_params(const MemState &mem, const SceNgsModuleParamHeader *header) {
    ModuleData *storage = module_storage(header->module_id);

    if (!storage)
        return false;

    if (storage->flags & ModuleData::PARAMS_LOCK)
        return false;

    const auto *descr = reinterpret_cast<const SceNgsParamsDescriptor *>(header + 1);
    if (descr->size > storage->info.size)
        return false;

    memcpy(storage->info.data.get(mem), descr, descr->size);

    return true;
}

SceInt32 Voice::parse_params_block(const MemState &mem, const SceNgsModuleParamHeader *header, const SceUInt32 size) {
    const SceUInt8 *data = reinterpret_cast<const SceUInt8 *>(header);
    const SceUInt8 *data_end = data + size;

    SceInt32 num_error = 0;

    // after first loop, check if other module exist
    while (data < data_end) {
        if (!parse_params(mem, header))
            num_error++;

        // increment by the size of the header alone + the descriptor size
        data += sizeof(SceNgsModuleParamHeader) + reinterpret_cast<const SceNgsParamsDescriptor *>(header + 1)->size;

        // set new header for next module
        header = reinterpret_cast<const SceNgsModuleParamHeader *>(data);
    }

    return num_error;
}

bool Voice::set_preset(const MemState &mem, const SceNgsVoicePreset *preset) {
    // we ignore the name for now
    const uint8_t *data_origin = reinterpret_cast<const uint8_t *>(preset);

    if (preset->preset_data_offset) {
        const auto *preset_data = reinterpret_cast<const SceNgsModuleParamHeader *>(data_origin + preset->preset_data_offset);
        auto nb_errors = parse_params_block(mem, preset_data, preset->preset_data_size);
        if (nb_errors > 0)
            return false;
    }

    if (preset->bypass_flags_offset) {
        const auto *bypass_flags = reinterpret_cast<const SceUInt32 *>(data_origin + preset->bypass_flags_offset);
        // should we disable bypass on all modules first?
        for (SceUInt32 i = 0; i < preset->bypass_flags_nb; i++) {
            ModuleData *module_data = module_storage(*bypass_flags);
            if (!module_data)
                return false;
            module_data->is_bypassed = true;

            bypass_flags++;
        }
    }

    return true;
}

void Voice::invoke_callback(KernelState &kernel, const MemState &mem, const SceUID thread_id, Ptr<void> callback, Ptr<void> user_data,
    const uint32_t module_id, const uint32_t reason1, const uint32_t reason2, Address reason_ptr) {
    if (!callback) {
        return;
    }

    const ThreadStatePtr thread = kernel.get_thread(thread_id);
    const Address callback_info_addr = stack_alloc(*thread->cpu, sizeof(SceNgsCallbackInfo));

    SceNgsCallbackInfo *info = Ptr<SceNgsCallbackInfo>(callback_info_addr).get(mem);
    info->rack_handle = Ptr<void>(rack, mem);
    info->voice_handle = Ptr<void>(this, mem);
    info->module_id = module_id;
    info->callback_reason = reason1;
    info->callback_reason_2 = reason2;
    info->callback_ptr = Ptr<void>(reason_ptr);
    info->userdata = user_data;

    thread->run_callback(callback.address(), { callback_info_addr });
    stack_free(*thread->cpu, sizeof(SceNgsCallbackInfo));
}

uint32_t System::get_required_memspace_size(SceNgsSystemInitParams *parameters) {
    return sizeof(System);
}

uint32_t Rack::get_required_memspace_size(MemState &mem, SceNgsRackDescription *description) {
    uint32_t buffer_size = 0;
    if (description->definition)
        // multiply by 2 because there are 2 copies of each buffer
        buffer_size = 2 * get_voice_definition_size(description->definition.get(mem));

    return sizeof(ngs::Rack) + description->voice_count * (sizeof(ngs::Voice) + buffer_size + description->patches_per_output * description->definition.get(mem)->output_count * sizeof(ngs::Patch));
}

bool init(State &ngs, MemState &mem) {
    voice_definition_init(ngs, mem);

    return true;
}

void deinit(State &ngs, MemState &mem) {
    while (!ngs.systems.empty()) {
        release_system(ngs, mem, ngs.systems.back());
    }

    Atrac9Module::free_swr_contexts();

    ngs.system_infos.clear();
    ngs.rack_infos.clear();
    ngs.definitions = Ptr<VoiceDefinition>(0);
}

void capture_state(State &ngs, const MemState &mem, Ptr<VoiceDefinition> &definitions, std::vector<SystemInitInfo> &system_infos, std::vector<RackInitInfo> &rack_infos) {
    definitions = ngs.definitions;
    system_infos = ngs.system_infos;
    rack_infos = ngs.rack_infos;

    // Active voices of each scheduler, as (rack, voice) indices.
    for (SystemInitInfo &info : system_infos) {
        System *system = Ptr<System>(info.address).get(mem);
        if (!system)
            continue;
        for (Voice *voice : system->voice_scheduler.queue) {
            for (size_t r = 0; r < system->racks.size(); r++) {
                Rack *rack = system->racks[r];
                if (!rack)
                    continue;
                bool found = false;
                for (size_t v = 0; v < rack->voices.size(); v++) {
                    if (rack->voices[v].get(mem) == voice) {
                        info.queued_voices.emplace_back(static_cast<uint32_t>(r), static_cast<uint32_t>(v));
                        found = true;
                        break;
                    }
                }
                if (found)
                    break;
            }
        }
    }

    for (RackInitInfo &info : rack_infos) {
        Rack *rack = info.info.data.cast<Rack>().get(mem);
        if (!rack)
            continue;
        info.blocks = rack->allocator.blocks;
        info.voices.resize(rack->voices.size());
        for (size_t v = 0; v < rack->voices.size(); v++) {
            Voice *voice = rack->voices[v].get(mem);
            if (!voice)
                continue;
            VoiceInfo &voice_info = info.voices[v];
            voice_info.address = rack->voices[v].address();
            voice_info.state = static_cast<uint32_t>(voice->state);
            voice_info.is_pending = voice->is_pending;
            voice_info.is_paused = voice->is_paused;
            voice_info.is_keyed_off = voice->is_keyed_off;
            voice_info.frame_count = voice->frame_count;
            memcpy(voice_info.implicit_volume_matrix, voice->implicit_volume_matrix, sizeof(voice_info.implicit_volume_matrix));
            voice_info.finished_callback = voice->finished_callback.address();
            voice_info.finished_callback_user_data = voice->finished_callback_user_data.address();
            voice_info.patches.resize(voice->patches.size());
            for (size_t p = 0; p < voice->patches.size(); p++) {
                voice_info.patches[p].reserve(voice->patches[p].size());
                for (const Ptr<Patch> &patch : voice->patches[p])
                    voice_info.patches[p].push_back(patch.address());
            }
            voice_info.modules.resize(voice->datas.size());
            for (size_t m = 0; m < voice->datas.size(); m++) {
                const ModuleData &data = voice->datas[m];
                ModuleDataInfo &module_info = voice_info.modules[m];
                module_info.guest_state_data = data.guest_state_data;
                module_info.last_info = data.last_info;
                module_info.is_bypassed = data.is_bypassed;
                module_info.flags = data.flags;
                if (data.info.data && data.info.size) {
                    module_info.parameters.resize(data.info.size);
                    memcpy(module_info.parameters.data(), data.info.data.get(mem), data.info.size);
                }
                if (m < rack->modules.size() && rack->modules[m]) {
                    rack->modules[m]->capture_logical_state(voice->datas[m], module_info.logical_state);
                }
            }
        }
    }
}

void restore_state(State &ngs, const MemState &mem, Ptr<VoiceDefinition> definitions, const std::vector<SystemInitInfo> &system_infos, const std::vector<RackInitInfo> &rack_infos) {
    ngs.definitions = definitions;
    ngs.system_infos.clear();
    ngs.rack_infos.clear();

    for (const SystemInitInfo &info : system_infos) {
        SceNgsSystemInitParams params = info.params;
        init_system(ngs, mem, &params, info.memspace, info.memspace_size);
    }

    static std::atomic<uint64_t> diag_restore_logs{ 0 };

    for (const RackInitInfo &info : rack_infos) {
        System *system = info.system.get(mem);
        if (!system)
            continue;
        SceNgsBufferInfo buffer = info.info;
        SceNgsRackDescription description = info.description;
        init_rack(ngs, mem, system, &buffer, &description);

        Rack *rack = info.info.data.cast<Rack>().get(mem);
        if (!rack)
            continue;

        // The mempool keeps dynamic allocations (patches) alive.
        if (!info.blocks.empty())
            rack->allocator.blocks = info.blocks;

        uint32_t diag_voices = 0;
        uint32_t diag_modules = 0;
        uint32_t diag_logical = 0;
        uint32_t diag_params = 0;
        uint32_t diag_reinit = 0;
        uint32_t diag_queued = 0;

        // Rebuild the host-side voices in place, then re-link their patches.
        for (size_t v = 0; v < rack->voices.size(); v++) {
            if (v >= info.voices.size())
                break;
            const VoiceInfo &voice_info = info.voices[v];
            if (voice_info.address == 0)
                continue;
            Ptr<Voice> voice_ptr(voice_info.address);
            Voice *voice = voice_ptr.get(mem);
            if (!voice)
                continue;

            // Remember the parameter buffers init_rack allocated for this voice:
            // re-creating the Voice object resets its ModuleData.
            std::vector<Ptr<void>> param_buffers;
            std::vector<uint32_t> param_sizes;
            if (voice == rack->voices[v].get(mem)) {
                param_buffers.reserve(voice->datas.size());
                param_sizes.reserve(voice->datas.size());
                for (const ModuleData &old_data : voice->datas) {
                    param_buffers.push_back(old_data.info.data);
                    param_sizes.push_back(old_data.info.size);
                }
            }

            rack->voices[v] = voice_ptr;
            voice->~Voice();
            new (voice) Voice();
            voice->init(rack);
            for (size_t m = 0; m < voice->datas.size(); m++) {
                if (m < param_buffers.size()) {
                    voice->datas[m].info.data = param_buffers[m];
                    voice->datas[m].info.size = param_sizes[m];
                } else if (m < rack->modules.size() && rack->modules[m]) {
                    const uint32_t size = rack->modules[m]->get_buffer_parameter_size();
                    if (size) {
                        voice->datas[m].info.size = size;
                        voice->datas[m].info.data = rack->alloc_raw(size);
                        rack->alloc_raw(size);
                    }
                }
            }
            voice->state = static_cast<VoiceState>(voice_info.state);
            voice->is_pending = voice_info.is_pending;
            voice->is_paused = voice_info.is_paused;
            voice->is_keyed_off = voice_info.is_keyed_off;
            voice->frame_count = voice_info.frame_count;
            memcpy(voice->implicit_volume_matrix, voice_info.implicit_volume_matrix, sizeof(voice->implicit_volume_matrix));
            voice->finished_callback = Ptr<void>(voice_info.finished_callback);
            voice->finished_callback_user_data = Ptr<void>(voice_info.finished_callback_user_data);
            for (size_t p = 0; p < voice->patches.size() && p < voice_info.patches.size(); p++) {
                voice->patches[p].resize(voice_info.patches[p].size());
                for (size_t k = 0; k < voice_info.patches[p].size(); k++)
                    voice->patches[p][k] = Ptr<Patch>(voice_info.patches[p][k]);
            }

            diag_voices++;
            for (size_t m = 0; m < voice->datas.size() && m < voice_info.modules.size(); m++) {
                ModuleData &data = voice->datas[m];
                const ModuleDataInfo &module_info = voice_info.modules[m];
                data.parent = voice;
                data.index = static_cast<uint32_t>(m);
                diag_modules++;
                if (!module_info.logical_state.empty())
                    diag_logical++;
                if (!module_info.parameters.empty())
                    diag_params++;
                data.guest_state_data = module_info.guest_state_data;
                data.last_info = module_info.last_info;
                data.is_bypassed = module_info.is_bypassed;
                data.flags = module_info.flags;
                if (data.info.data && data.info.size && module_info.parameters.size() == data.info.size) {
                    memcpy(data.info.data.get(mem), module_info.parameters.data(), data.info.size);
                } else if (!module_info.parameters.empty()) {
                    // The rebuilt parameter buffer does not match the saved one:
                    // block processing so the game cannot decode stale state.
                    data.needs_reinit = true;
                    diag_reinit++;
                }
                if (m < rack->modules.size() && rack->modules[m] && !module_info.logical_state.empty()) {
                    rack->modules[m]->restore_logical_state(data, module_info.logical_state);
                }
            }
        }

        const uint64_t diag_n = diag_restore_logs.fetch_add(1, std::memory_order_relaxed) + 1;
        if (diag_n <= 8 || (diag_n % 64) == 0)
            LOG_CRITICAL("[savestate-ngs] rack restored voices={} modules={} logical_blobs={} param_blobs={} needs_reinit={} saved_voices={} blocks={}",
                diag_voices, diag_modules, diag_logical, diag_params, diag_reinit, info.voices.size(), info.blocks.size());

        (void)diag_queued;
    }

    // Voices that were being mixed before the save must run again.
    for (const SystemInitInfo &info : system_infos) {
        System *system = Ptr<System>(info.address).get(mem);
        if (!system)
            continue;
        for (const auto &[rack_index, voice_index] : info.queued_voices) {
            if (rack_index >= system->racks.size())
                continue;
            Rack *rack = system->racks[rack_index];
            if (!rack || voice_index >= rack->voices.size())
                continue;
            system->voice_scheduler.requeue_voice(mem, rack->voices[voice_index].get(mem));
        }
        if (!info.queued_voices.empty()) {
            const uint64_t diag_n = diag_restore_logs.fetch_add(1, std::memory_order_relaxed) + 1;
            if (diag_n <= 8 || (diag_n % 64) == 0)
                LOG_CRITICAL("[savestate-ngs] system requeued {} voices (queue={})", info.queued_voices.size(), system->voice_scheduler.queue.size());
        }
    }
}

bool init_system(State &ngs, const MemState &mem, SceNgsSystemInitParams *parameters, Ptr<void> memspace, const uint32_t memspace_size) {
    // Reserve first memory allocation for our System struct
    System *sys = memspace.cast<System>().get(mem);
    sys = new (sys) System(memspace, memspace_size);

    sys->racks.resize(parameters->max_racks);

    sys->max_voices = parameters->max_voices;
    sys->granularity = parameters->granularity;
    sys->sample_rate = parameters->sample_rate;

    // Alloc first block for System struct
    if (!sys->alloc_raw(sizeof(System))) {
        return false;
    }

    ngs.systems.push_back(sys);

    SystemInitInfo info;
    info.address = memspace.address();
    info.memspace = memspace;
    info.memspace_size = memspace_size;
    info.params = *parameters;
    ngs.system_infos.push_back(info);

    return true;
}

void release_system(State &ngs, const MemState &mem, System *system) {
    // this function assumes no ngs mutex is being held
    const Address system_address = Ptr<const void>(system, mem).address();

    for (Rack *rack : system->racks) {
        if (!rack)
            continue;
        for (const auto &voice : rack->voices) {
            system->voice_scheduler.deque_voice(voice.get(mem));
            voice.get(mem)->~Voice();
        }
        rack->~Rack();
    }

    system->racks.clear();

    vector_utils::erase_first(ngs.systems, system);
    std::erase_if(ngs.system_infos, [system_address](const SystemInitInfo &info) { return info.address == system_address; });
    std::erase_if(ngs.rack_infos, [system_address](const RackInitInfo &info) { return info.system_address == system_address; });
    system->~System();
}

bool init_rack(State &ngs, const MemState &mem, System *system, SceNgsBufferInfo *init_info, const SceNgsRackDescription *description) {
    Rack *rack = init_info->data.cast<Rack>().get(mem);
    rack = new (rack) Rack(system, init_info->data, init_info->size);

    // Alloc first block for Rack
    if (!rack->alloc<Rack>()) {
        return false;
    }

    if (description->definition)
        apply_voice_definition(description->definition.get(mem), rack->modules);
    else
        rack->modules.clear();

    // Initialize voice definition
    rack->channels_per_voice = description->channels_per_voice;
    rack->max_patches_per_input = description->max_patches_per_input;
    rack->patches_per_output = description->patches_per_output;

    // Alloc spaces for voice
    rack->voices.resize(description->voice_count);
    LOG_WARN("[NGSLIFE] init_rack {} ({} voices)", fmt::ptr(rack), description->voice_count);
    rack->vdef = description->definition.get(mem);

    for (auto &voice : rack->voices) {
        voice = rack->alloc<Voice>();

        if (!voice) {
            return false;
        }

        Voice *v = voice.get(mem);
        new (v) Voice();
        v->init(rack);

        // Allocate parameter buffer info for each voice
        for (size_t i = 0; i < rack->modules.size(); i++) {
            v->datas[i].info.size = rack->modules[i]->get_buffer_parameter_size();
            v->datas[i].info.data = rack->alloc_raw(v->datas[i].info.size);
            // from the behavior of games, it looks like the other info buffer (there are two copies because of VoiceLock) is located right after the first
            // one, so copy this behavior, to avoid a game overwriting some important ngs struct
            rack->alloc_raw(v->datas[i].info.size);

            v->datas[i].parent = v;
            v->datas[i].index = static_cast<uint32_t>(i);
            rack->modules[i]->initialize_voice_data(v->datas[i]);
        }
    }

    system->racks.push_back(rack);

    RackInitInfo info;
    info.address = init_info->data.address();
    info.system_address = Ptr<const void>(system, mem).address();
    info.system = Ptr<System>(system, mem);
    info.info = *init_info;
    info.description = *description;
    ngs.rack_infos.push_back(info);

    return true;
}

void release_rack(State &ngs, const MemState &mem, System *system, Rack *rack) {
    LOG_WARN("[NGSLIFE] release_rack {} ({} voices)", fmt::ptr(rack), rack ? rack->voices.size() : 0);
    // this function should only be called outside of ngs update and with the scheduler mutex acquired (except when releasing the system)
    if (!rack)
        return;

    const Address rack_address = Ptr<const void>(rack, mem).address();
    std::erase_if(ngs.rack_infos, [rack_address](const RackInitInfo &info) { return info.address == rack_address; });

    // remove all queued voices
    for (const auto &voice : rack->voices) {
        Voice *v = voice.get(mem);
        system->voice_scheduler.deque_voice(voice.get(mem));
        // clean up host resources per voice before destroying
        for (size_t i = 0; i < rack->modules.size() && i < v->datas.size(); i++) {
            if (rack->modules[i])
                rack->modules[i]->cleanup_voice_state(v->datas[i]);
        }
        // no need to free the voice from the rack
        v->~Voice();
    }

    // remove from system
    vector_utils::erase_first(system->racks, rack);

    // free pointer memory
    rack->~Rack();
}

Ptr<VoiceDefinition> get_voice_definition(State &ngs, MemState &mem, ngs::BussType type) {
    return ngs.definitions + static_cast<int>(type);
}
} // namespace ngs
