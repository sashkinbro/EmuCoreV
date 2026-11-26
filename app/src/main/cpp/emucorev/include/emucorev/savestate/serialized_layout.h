#pragma once

// Recorded layouts of every struct a save state stores as raw bytes (engine version below).
//
// BufferWriter::value / BufferReader::value (and the bulk readers in gxm_state_io.cpp) refuse a struct
// that is not registered here or whose size differs from the recorded one, so a new field in any of
// these structs stops the build. When that happens: bump kEngineVersion in savestate.h (old saves are
// then rejected cleanly instead of being misread), update the size here and the version check below.
// The field-by-field snapshot structs at the end are pinned the same way, so a field added to them
// cannot be forgotten by the encoder/parser pair.

#include <cstddef>
#include <type_traits>

namespace emucorev::savestate {
template <typename T>
struct SerializedLayout {
    static constexpr size_t size = 0; // not registered
};
} // namespace emucorev::savestate

#define ECV_SERIALIZED_LAYOUT(Type, Size)                     \
    template <>                                               \
    struct emucorev::savestate::SerializedLayout<Type> {      \
        static constexpr size_t size = Size;                  \
    };                                                        \
    static_assert(sizeof(Type) == (Size), #Type " changed size: bump kEngineVersion and update serialized_layout.h")

#include <emucorev/savestate/savestate.h>

#include <iterator> // cpu/common.h uses std::back_inserter

#include <audio/state.h>
#include <cpu/state.h>
#include <display/state.h>
#include <gxm/display_queue.h>
#include <gxm/savestate.h>
#include <gxm/types.h>
#include <kernel/state.h>
#include <modules/sysmem_state.h>
#include <ngs/state.h>
#include <np/state.h>
#include <renderer/command_snapshot.h>
#include <renderer/finish.h>
#include <renderer/gxm_types.h>
#include <renderer/types.h>
#include <renderer/vulkan/context_snapshot.h>
#include <touch/state.h>

static_assert(emucorev::savestate::kEngineVersion == 20,
    "the sizes below are the engine 20 layout: re-check every entry when the engine version changes");

// The host test build (x86_64) and the app (arm64-v8a) are both LP64 with the same libc++ layouts.
static_assert(sizeof(void *) == 8, "save-state layouts are recorded for 64-bit builds");

// ---- raw structs (value<T>) ----
ECV_SERIALIZED_LAYOUT(SceGxmInitializeParams, 20);
ECV_SERIALIZED_LAYOUT(renderer::GxmRecordState, 552);
ECV_SERIALIZED_LAYOUT(GxmContextState, 1032); // 1024 up to engine 19 (back-face visibility index added in engine 20)
ECV_SERIALIZED_LAYOUT(DisplayCallback, 24);
ECV_SERIALIZED_LAYOUT(SceGxmColorSurface, 48);
ECV_SERIALIZED_LAYOUT(SceGxmDepthStencilSurface, 20);
ECV_SERIALIZED_LAYOUT(SceGxmRenderTargetParams, 20);
ECV_SERIALIZED_LAYOUT(SceGxmBlendInfo, 4);
ECV_SERIALIZED_LAYOUT(SceGxmShaderPatcherParams, 68);
ECV_SERIALIZED_LAYOUT(SceGxmVertexAttribute, 8);
ECV_SERIALIZED_LAYOUT(SceGxmVertexStream, 4);
ECV_SERIALIZED_LAYOUT(DisplayFrameInfo, 20);
ECV_SERIALIZED_LAYOUT(CPUContext, 340);
ECV_SERIALIZED_LAYOUT(KernelMemBlock, 56);
ECV_SERIALIZED_LAYOUT(SceKernelModuleInfo, 440);
ECV_SERIALIZED_LAYOUT(SceNgsSystemInitParams, 20);
ECV_SERIALIZED_LAYOUT(SceNgsBufferInfo, 8);
ECV_SERIALIZED_LAYOUT(SceNgsRackDescription, 24);
ECV_SERIALIZED_LAYOUT(SceTouchData, 144);
ECV_SERIALIZED_LAYOUT(np::CommunicationID, 12);

// ---- field-by-field snapshot structs (encoder and parser must both follow a change) ----
static_assert(sizeof(gxm::ContextSnapshot) == 1672, "gxm::ContextSnapshot changed: update encode/parse_gxm_state and bump kEngineVersion");
static_assert(sizeof(gxm::DeferredListSnapshot) == 56, "gxm::DeferredListSnapshot changed: update encode/parse_gxm_state and bump kEngineVersion");
static_assert(sizeof(gxm::SyncObjectSnapshot) == 20, "gxm::SyncObjectSnapshot changed: update encode/parse_gxm_state and bump kEngineVersion");
static_assert(sizeof(gxm::RenderTargetSnapshot) == 36, "gxm::RenderTargetSnapshot changed: update encode/parse_gxm_state and bump kEngineVersion");
static_assert(sizeof(renderer::CommandSnapshot) == 256, "renderer::CommandSnapshot changed: update encode/parse_gxm_state and bump kEngineVersion");
static_assert(sizeof(renderer::FinishSnapshot) == 32, "renderer::FinishSnapshot changed: update encode/parse_gxm_state and bump kEngineVersion");
static_assert(sizeof(renderer::vulkan::VKContextSnapshot) == 124, "VKContextSnapshot changed: update write/read_context in gpu_state_io.cpp and bump kEngineVersion");
static_assert(sizeof(PendingDisplaySubmission) == 48, "PendingDisplaySubmission changed: update encode/parse_gxm_state and bump kEngineVersion");
