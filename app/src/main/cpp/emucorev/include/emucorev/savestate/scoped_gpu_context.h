#pragma once

namespace renderer { struct Context; }

namespace emucorev::savestate {
// Cache reconstruction obtains the active context through State.context.
// Keep that implicit selection consistent with the checkpoint being rebuilt.
class ScopedGpuContextSelection final {
    renderer::Context *&selection;
    renderer::Context *previous;
public:
    ScopedGpuContextSelection(renderer::Context *&selection, renderer::Context &context)
        : selection(selection), previous(selection) { selection = &context; }
    ~ScopedGpuContextSelection() { selection = previous; }
    ScopedGpuContextSelection(const ScopedGpuContextSelection &) = delete;
    ScopedGpuContextSelection &operator=(const ScopedGpuContextSelection &) = delete;
};
}
