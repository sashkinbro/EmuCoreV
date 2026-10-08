#pragma once

#include <map>
#include <utility>
#include <vector>

namespace renderer { struct Context; }

namespace emucorev::savestate {
// Deferred GXM contexts have a plain renderer::Context and no Vulkan worker,
// scene checkpoint, or backend clocks. Keep the frontend registry unchanged.
template <typename PhysicalContext, typename Address>
std::map<Address, PhysicalContext *> physical_gpu_contexts(
    const std::vector<std::pair<Address, renderer::Context *>> &runtime) {
    std::map<Address, PhysicalContext *> physical;
    for (const auto &[address, context] : runtime)
        if (auto *backend = dynamic_cast<PhysicalContext *>(context)) physical.emplace(address, backend);
    return physical;
}
}
