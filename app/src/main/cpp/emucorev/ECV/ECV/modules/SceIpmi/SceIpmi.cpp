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

#include <module/module.h>
#include <io/state.h>
#include <kernel/state.h>
#include <unordered_map>
#include <string>
#include <cstring>

#include "modules/module_parent.h"

#include <util/tracy.h>

TRACY_MODULE_NAME(SceIpmi);

struct LocalIpmiState {
    std::mutex mutex;
    std::unordered_map<Address, std::string> services;
};
LIBRARY_INIT(SceIpmi) {
    emuenv.kernel.obj_store.create<LocalIpmiState>();
}

EXPORT(int, _ZN4IPMI6Client10disconnectEv) {
    return UNIMPLEMENTED();
}

EXPORT(int, _ZN4IPMI6Client11getUserDataEv) {
    return UNIMPLEMENTED();
}

struct BufferInfo {
    Ptr<void> pBuffer;
    SceSize bufferSize;
    SceSize bufferWrittenSize; // size written by method
};

EXPORT(int, _ZN4IPMI6Client12tryGetResultEjPiPvPmm, unsigned int a1, int *a2, void *a3, unsigned long *a4, unsigned long a5) {
    return UNIMPLEMENTED();
}

EXPORT(int, _ZN4IPMI6Client12tryGetResultEjjPiPNS_10BufferInfoEj) {
    return UNIMPLEMENTED();
}

EXPORT(int, _ZN4IPMI6Client13pollEventFlagEjjjPj) {
    return UNIMPLEMENTED();
}

EXPORT(int, _ZN4IPMI6Client13waitEventFlagEjjjPjS1_) {
    return UNIMPLEMENTED();
}

EXPORT(int, _ZN4IPMI6Client16invokeSyncMethodEjPKNS_8DataInfoEjPiPNS_10BufferInfoEj, Ptr<void> client, uint32_t method, Ptr<uint32_t> input, uint32_t count, Ptr<int> result, Ptr<BufferInfo> output, uint32_t output_count) {
#ifndef NDEBUG
    if (emuenv.io.app_path == "__VITA_NATIVE_TROPHIES__") {
        auto *state = emuenv.kernel.obj_store.get<LocalIpmiState>();
        const std::lock_guard lock(state->mutex);
        LOG_WARN("Trophy service {} method {:x} inputs {} outputs {}", state->services[client.address()], method, count, output_count);
        if (count == 1 && input && input.valid(emuenv.mem)) {
            const auto *descriptor = input.get(emuenv.mem);
            const auto payload = Ptr<uint32_t>(descriptor[0]);
            if (descriptor[1] == 24 && payload.valid(emuenv.mem)) {
                const auto *words = payload.get(emuenv.mem);
                LOG_WARN("Trophy create args {:x} {:x} {:x} {:x} {:x} {:x}", words[0], words[1], words[2], words[3], words[4], words[5]);
            }
        }
    }
#endif
    return UNIMPLEMENTED();
}

EXPORT(int, _ZN4IPMI6Client16invokeSyncMethodEjPKvjPiPvPjj, Ptr<void> client, uint32_t method, Ptr<void> input, uint32_t size, Ptr<int> result, Ptr<void> output, Ptr<uint32_t> written, uint32_t output_size) {
    if (emuenv.io.app_path == "__VITA_NATIVE_TROPHIES__" && output_size == 0 && method <= 1 && result && result.valid(emuenv.mem)) {
        auto *state = emuenv.kernel.obj_store.get<LocalIpmiState>();
        const std::lock_guard lock(state->mutex);
        const auto service = state->services.find(client.address());
        if (service != state->services.end() && (service->second == "SceDbUtilDcn" || service->second == "SceNpTrophy")) {
            *result.get(emuenv.mem) = 0;
            if (written && written.valid(emuenv.mem)) *written.get(emuenv.mem) = 0;
            return 0;
        }
    }
    return UNIMPLEMENTED();
}

EXPORT(int, _ZN4IPMI6Client17invokeAsyncMethodEjPKNS_8DataInfoEjPjPKNS0_12EventNotifeeE, Ptr<void> client, uint32_t method, Ptr<uint32_t> input, uint32_t count, Ptr<uint32_t> ticket, Ptr<void> notifee) {
#ifndef NDEBUG
    if (emuenv.io.app_path == "__VITA_NATIVE_TROPHIES__" && count == 1 && input && input.valid(emuenv.mem)) {
        const auto *descriptor = input.get(emuenv.mem);
        const auto payload = Ptr<uint32_t>(descriptor[0]);
        if (descriptor[1] == 8 && payload.valid(emuenv.mem)) {
            const auto *words = payload.get(emuenv.mem);
            LOG_WARN("Trophy async {:x} args {:x} {:x}", method, words[0], words[1]);
        }
    }
#endif
    return UNIMPLEMENTED();
}

EXPORT(int, _ZN4IPMI6Client17invokeAsyncMethodEjPKvjPiPKNS0_12EventNotifeeE) {
    return UNIMPLEMENTED();
}

EXPORT(int, _ZN4IPMI6Client19terminateConnectionEv) {
    return UNIMPLEMENTED();
}

// IPMI::Client::Config::estimateClientMemorySize()
EXPORT(int, _ZN4IPMI6Client6Config24estimateClientMemorySizeEv) {
    TRACY_FUNC(_ZN4IPMI6Client6Config24estimateClientMemorySizeEv);
    STUBBED("stubbed");
    return 0x100;
}

// IPMI::Client::create(IPMI::Client**, IPMI::Client::Config const*, void*, void*)
EXPORT(int, _ZN4IPMI6Client6createEPPS0_PKNS0_6ConfigEPvS6_, Ptr<void> *client, void const *config, Ptr<void> user_data, Ptr<void> client_memory) {
    TRACY_FUNC(_ZN4IPMI6Client6createEPPS0_PKNS0_6ConfigEPvS6_, client, config, user_data, client_memory);
    *client_memory.cast<Ptr<void>>().get(emuenv.mem) = get_client_vtable(emuenv.kernel, emuenv.mem);
    *client = client_memory;
    if (config && emuenv.io.app_path == "__VITA_NATIVE_TROPHIES__") {
        const char *name = static_cast<const char *>(config);
        auto *state = emuenv.kernel.obj_store.get<LocalIpmiState>();
        const std::lock_guard lock(state->mutex);
        state->services[client_memory.address()] = std::string(name, strnlen(name, 16));
    }
    STUBBED("Stubed");
    return 0;
}

EXPORT(int, _ZN4IPMI6Client6getMsgEjPvPjjS2_) {
    return UNIMPLEMENTED();
}

EXPORT(int, _ZN4IPMI6Client7connectEPKvjPi, void *client, void const *params, SceSize params_size, SceInt32 *error) {
    TRACY_FUNC(_ZN4IPMI6Client7connectEPKvjPi, client, error);
    *error = 0;
    return UNIMPLEMENTED();
}

EXPORT(int, _ZN4IPMI6Client7destroyEv) {
    return UNIMPLEMENTED();
}

EXPORT(int, _ZN4IPMI6Client9tryGetMsgEjPvPmm) {
    return UNIMPLEMENTED();
}

EXPORT(int, _ZN4IPMI6ClientD1Ev) {
    return UNIMPLEMENTED();
}

EXPORT(int, SceIpmi_296D44D4) {
    return UNIMPLEMENTED();
}

EXPORT(int, SceIpmi_BC3A3031) {
    return UNIMPLEMENTED();
}

EXPORT(int, SceIpmi_2C6DB642) {
    return UNIMPLEMENTED();
}

EXPORT(int, SceIpmi_006EFA9D) {
    return UNIMPLEMENTED();
}
