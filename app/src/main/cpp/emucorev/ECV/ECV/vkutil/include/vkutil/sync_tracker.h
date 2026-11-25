// Central GPU resource-state tracker (docs/gpu-sync-tracker.md).
//
// Every image / buffer the renderer owns carries a sync::State (layout, last writer, outstanding readers, what the last
// write is already visible to, submit epoch). All hazards between uses are derived from that state by sync::track():
// callers say WHAT they are about to do (stage, access, layout) and the tracker emits the minimal barrier into a Batch,
// which is flushed once per command buffer. Nobody builds a vk::*MemoryBarrier by hand any more.
//
// The logic here is pure (no device calls except in Batch::flush) so it is covered by host tests
// (app/src/test/core-host/sync_tracker_tests.cpp).
#pragma once

// vkutil.h defines the vulkan.hpp configuration (no constructors, no prototypes) and then includes this header back (pragma once)
#include <vkutil/vkutil.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

namespace vkutil::sync {

using Stage = vk::PipelineStageFlags2;
using Access = vk::AccessFlags2;
using S = vk::PipelineStageFlagBits2;
using A = vk::AccessFlagBits2;

// every access bit that writes memory (a layout transition is also treated as a write)
inline constexpr Access write_mask = A::eShaderWrite | A::eColorAttachmentWrite | A::eDepthStencilAttachmentWrite | A::eTransferWrite
    | A::eHostWrite | A::eMemoryWrite | A::eShaderStorageWrite;
inline constexpr Stage all_graphics_and_compute_stages = S::eVertexShader | S::eFragmentShader | S::eComputeShader
    | S::eColorAttachmentOutput | S::eEarlyFragmentTests | S::eLateFragmentTests | S::eTransfer;

inline bool any(Stage s) { return static_cast<uint64_t>(static_cast<VkPipelineStageFlags2>(s)) != 0; }
inline bool any(Access a) { return static_cast<uint64_t>(static_cast<VkAccessFlags2>(a)) != 0; }
inline bool is_subset(Stage sub, Stage super) { return (sub & ~super) == Stage{}; }
inline bool is_subset(Access sub, Access super) { return (sub & ~super) == Access{}; }

enum class Hazard : uint8_t {
    None = 0,
    Init, // first use of the resource / layout set up from undefined
    Raw, // read after write not yet visible to the reader
    War, // write after outstanding reads
    Waw, // write after write
    Layout, // layout change (always a full dependency on everything before)
};

// What an attachment is known to contain (EmuCoreX "tracked content state"): Dirty = unknown / rendered, LOAD it; Cleared = every texel equals
// `cleared_to`, a pass can CLEAR instead of LOAD; Invalidated = nobody needs the content (discarded and not written since), DONT_CARE.
enum class Content : uint8_t { Dirty, Cleared, Invalidated };
struct ClearValue {
    float color[4] = { 0, 0, 0, 0 };
    float depth = 1.0f;
    uint32_t stencil = 0;
};
enum class LoadChoice : uint8_t { Load, Clear, DontCare };
// Load op for an attachment the pass was going to LOAD (`wants_content`). Never turns a non-load into a load.
inline LoadChoice derive_load(Content c, bool wants_content) {
    if (!wants_content)
        return LoadChoice::DontCare; // callers keep their own clear/dont-care choice for attachments that are not loaded
    switch (c) {
    case Content::Cleared: return LoadChoice::Clear;
    case Content::Invalidated: return LoadChoice::DontCare;
    default: return LoadChoice::Load;
    }
}

// Tracked state of one image or buffer (all subresources are tracked together).
struct State {
    vk::ImageLayout layout = vk::ImageLayout::eUndefined; // images only
    Stage write_stages{}; // last writer (a layout transition counts as a write at its destination stage/access)
    Access write_access{};
    Stage read_stages{}; // readers since the last write
    // the last write has been made available and visible to every (stage in vis_stages) x (access in vis_access)
    Stage vis_stages{};
    Access vis_access{};
    Content content = Content::Dirty;
    ClearValue cleared_to{};
    uint32_t epoch = 0; // submit epoch of the last use (diagnostics: a hazard across epochs is an inter-submit hazard)
    const char *name = nullptr; // static string for logs, optional

    void reset() { *this = State{ .name = name }; }
};

struct Barrier {
    Stage src_stage{}, dst_stage{};
    Access src_access{}, dst_access{};
    vk::ImageLayout old_layout = vk::ImageLayout::eUndefined, new_layout = vk::ImageLayout::eUndefined;
    bool valid = false;
};

struct UseResult {
    Hazard hazard = Hazard::None;
    bool barrier = false; // `out` holds a barrier that must be recorded before the access
};

// Pure state machine. `new_layout` Undefined = a buffer or "do not care / keep" (images pass their layout).
// `discard` = previous content is not needed: the layout transition starts from Undefined.
inline UseResult track(State &st, Stage stage, Access access, vk::ImageLayout new_layout, bool discard, Barrier &out) {
    UseResult res;
    const bool is_write = any(access & write_mask);
    const bool is_image = new_layout != vk::ImageLayout::eUndefined;
    const bool layout_change = is_image && st.layout != new_layout;
    const bool has_prior = any(st.write_stages) || any(st.read_stages);

    Stage src_stage{};
    Access src_access{};
    Hazard hz = Hazard::None;

    if (layout_change) {
        // a transition orders after EVERYTHING that touched the image and makes the writes available
        src_stage = st.write_stages | st.read_stages;
        src_access = st.write_access;
        hz = has_prior ? Hazard::Layout : Hazard::Init;
        if (discard)
            // contents are dropped: still wait for the previous users (WAR/WAW on the memory) but nothing needs to be made available
            src_access = Access{};
    } else if (any(st.write_stages)) {
        const bool covered = is_subset(stage, st.vis_stages) && is_subset(access, st.vis_access);
        if (!covered) {
            src_stage = st.write_stages | (is_write ? st.read_stages : Stage{});
            src_access = st.write_access;
            hz = is_write ? Hazard::Waw : Hazard::Raw;
        } else if (is_write && any(st.read_stages)) {
            src_stage = st.read_stages;
            hz = Hazard::War;
        }
    } else if (is_write && any(st.read_stages)) {
        src_stage = st.read_stages;
        hz = Hazard::War;
    }
    // WAR on top of an uncovered RAW/WAW is already included through read_stages above; a pure write after uncovered write+reads
    if (hz == Hazard::Waw && any(st.read_stages))
        hz = Hazard::War;

    if (hz != Hazard::None) {
        out.src_stage = any(src_stage) ? src_stage : Stage{};
        out.src_access = src_access;
        // widen the destination with what the last write was already visible to so `vis` stays a product set
        out.dst_stage = stage | (is_write || layout_change ? Stage{} : st.vis_stages);
        out.dst_access = access | (is_write || layout_change ? Access{} : st.vis_access);
        out.old_layout = (discard || (is_image && st.layout == vk::ImageLayout::eUndefined)) ? vk::ImageLayout::eUndefined : st.layout;
        out.new_layout = is_image ? new_layout : vk::ImageLayout::eUndefined;
        out.valid = true;
        res.barrier = true;
    }
    res.hazard = hz;

    // ---- content state ----
    if (is_write)
        st.content = Content::Dirty;
    else if (discard && layout_change)
        st.content = Content::Invalidated;

    // ---- state update ----
    if (layout_change) {
        st.layout = new_layout;
        // the transition is a write performed at (stage, access); later users chain from it
        st.write_stages = stage;
        st.write_access = access;
        st.read_stages = is_write ? Stage{} : stage;
        st.vis_stages = stage;
        st.vis_access = access;
    } else if (is_write) {
        st.write_stages = stage;
        st.write_access = access & write_mask;
        st.read_stages = Stage{};
        st.vis_stages = stage;
        st.vis_access = access;
    } else {
        st.read_stages |= stage;
        if (res.barrier) {
            st.vis_stages = out.dst_stage;
            st.vis_access = out.dst_access;
        }
        // reading something that was never written: nothing pending
    }
    return res;
}

// An access performed by a render pass whose own external dependencies (render_pass_sync.h) already cover
// `dep_src_*` -> `dep_dst_*`: returns the explicit barrier only when the tracked state is NOT covered by them.
struct PassCoverage {
    Stage raw_src_stages{}; // [0]: attachment writes of earlier work
    Access raw_src_access{};
    Stage war_src_stages{}; // [1]: earlier readers
    vk::ImageLayout initial_layout = vk::ImageLayout::eUndefined; // layout the render pass expects
    Stage dst_stages{};
    Access dst_access{};
};

// Begin a render-pass access of one attachment. A layout mismatch or an uncovered previous use produces a barrier
// (to be recorded before vkCmdBeginRenderPass); otherwise the pass dependencies are trusted and no barrier is added.
inline UseResult track_pass_begin(State &st, const PassCoverage &cov, Barrier &out) {
    // what a plain use() would need
    State probe = st;
    Barrier need;
    const bool pass_discards = cov.initial_layout == vk::ImageLayout::eUndefined;
    // an Undefined initial layout never mismatches: the pass takes whatever layout is there and discards it
    const vk::ImageLayout want = pass_discards ? st.layout : cov.initial_layout;
    UseResult r = track(probe, cov.dst_stages, cov.dst_access, want, false, need);
    if (!r.barrier)
        return UseResult{ r.hazard, false };
    const bool layout_ok = st.layout == want;
    const bool writes_covered = is_subset(need.src_stage & ~st.read_stages, cov.raw_src_stages)
        && is_subset(need.src_access & write_mask, cov.raw_src_access);
    const bool reads_covered = is_subset(need.src_stage & st.read_stages, cov.war_src_stages | cov.raw_src_stages);
    if (layout_ok && writes_covered && reads_covered) {
        // the pass dependencies cover it: keep the tracked state as if the barrier had happened
        st = probe;
        return UseResult{ Hazard::None, false };
    }
    st = probe;
    out = need;
    return r;
}

// State after a render pass that wrote this attachment: writes at `write_*`, made visible by the pass' outgoing
// dependency to `vis_*`, image left in `final_layout`.
inline void track_pass_end(State &st, vk::ImageLayout final_layout, Stage write_stages, Access write_access, Stage vis_stages, Access vis_access) {
    st.layout = final_layout;
    st.write_stages = write_stages;
    st.write_access = write_access & write_mask;
    st.read_stages = Stage{};
    st.vis_stages = vis_stages;
    st.vis_access = vis_access;
}

// ---------------------------------------------------------------------------------------------------------------------
// global configuration + statistics

struct Config {
    bool use_sync2 = false; // vkCmdPipelineBarrier2KHR instead of vkCmdPipelineBarrier (gpu_caps decides)
    bool full_barrier = false; // debug/oracle: every barrier becomes ALL_COMMANDS / MEMORY_READ|WRITE
    bool log_hazards = false; // debug/oracle: one log line per emitted barrier
    bool observe_samples = false; // debug/oracle/lab gate: check every sampled surface against the tracked state (off: no per-draw cost)
    void (*log)(const char *line) = nullptr;
};
Config &config();

struct Stats {
    std::atomic<uint64_t> uses{ 0 }; // tracked uses
    std::atomic<uint64_t> elided{ 0 }; // uses that needed no barrier
    std::atomic<uint64_t> image_barriers{ 0 }, buffer_barriers{ 0 }, global_barriers{ 0 };
    std::atomic<uint64_t> full_barrier{ 0 }; // flushes widened to ALL_COMMANDS by the oracle fallback
    std::atomic<uint64_t> flushes{ 0 }, api_calls{ 0 }, merged{ 0 };
    std::atomic<uint64_t> raw{ 0 }, war{ 0 }, waw{ 0 }, layout{ 0 }, init{ 0 };
    std::atomic<uint64_t> feedback_reads{ 0 }; // samples of the attachment being rendered (feedback loop): covered by the self-dependency, not a hazard
    std::atomic<uint64_t> unsynced_raw{ 0 }, unsynced_layout{ 0 }, unsynced_other{ 0 };
    std::atomic<uint64_t> unsynced_in_pass{ 0 }; // uses that needed a barrier but happened inside a render pass
    std::atomic<uint64_t> pass_covered{ 0 }, pass_explicit{ 0 };
    // load/store look-ahead statistics (docs/gpu-sync-tracker.md section 9): scenes, scenes whose depth has no guest surface behind it,
    // scenes whose pass started more than once, and the intersection. unbacked && !restarted = scenes whose depth store was dead.
    // attachments the pass was going to LOAD that the content state turned into CLEAR / DONT_CARE (dynamic rendering path)
    std::atomic<uint64_t> loads_to_clear{ 0 }, loads_to_dontcare{ 0 }, loads_kept{ 0 };
    std::atomic<uint64_t> scenes{ 0 }, scenes_depth_unbacked{ 0 }, scenes_restarted{ 0 }, scenes_unbacked_restarted{ 0 };
    std::atomic<uint64_t> validation_hazards{ 0 }, validation_errors{ 0 }; // from VK_LAYER_KHRONOS_validation (debug)
};
Stats &stats();

inline const char *hazard_name(Hazard h) {
    switch (h) {
    case Hazard::Init: return "INIT";
    case Hazard::Raw: return "RAW";
    case Hazard::War: return "WAR";
    case Hazard::Waw: return "WAW";
    case Hazard::Layout: return "LAYOUT";
    default: return "NONE";
    }
}

inline void count_hazard(Hazard h) {
    Stats &s = stats();
    switch (h) {
    case Hazard::Init: s.init++; break;
    case Hazard::Raw: s.raw++; break;
    case Hazard::War: s.war++; break;
    case Hazard::Waw: s.waw++; break;
    case Hazard::Layout: s.layout++; break;
    default: break;
    }
}

// Does a validation-layer message report a synchronization hazard? Current layers print "<WRITE_AFTER_READ|READ_AFTER_WRITE|WRITE_AFTER_WRITE|
// WRITE_RACING_WRITE|...> hazard detected"; older ones used VUID-style "SYNC-HAZARD-*" ids.
inline bool is_sync_hazard_message(std::string_view msg) {
    return msg.find("hazard detected") != std::string_view::npos || msg.find("SYNC-HAZARD") != std::string_view::npos;
}

// sync2 stage/access -> legacy masks
vk::PipelineStageFlags to_legacy(Stage s, bool is_dst);
vk::AccessFlags to_legacy(Access a);

// A group of barriers recorded into one command buffer between two real commands.
class Batch {
public:
    // images: tracked use, see track()
    void use_image(State &st, vk::Image image, const vk::ImageSubresourceRange &range, Stage stage, Access access, vk::ImageLayout layout, bool discard = false);
    // buffers
    void use_buffer(State &st, vk::Buffer buffer, Stage stage, Access access);
    // Barrier on a SUB-range of an image whose per-subresource layouts the caller manages itself (mip-chain generation).
    // The whole-image tracked state is not touched: the caller sets it afterwards with track_pass_end().
    void explicit_image(vk::Image image, const vk::ImageSubresourceRange &range, const Barrier &b);
    // untracked memory barrier (device-address writes, host mirrors...): counted and logged separately
    void global(Stage src_stage, Access src_access, Stage dst_stage, Access dst_access);
    // the render pass is active: track the state but never emit; a needed barrier is counted as an unsynchronised use
    static Hazard observe(State &st, Stage stage, Access access, vk::ImageLayout layout);

    // true for barriers recorded INSIDE a render pass for a subpass self-dependency (VK_DEPENDENCY_BY_REGION_BIT)
    void set_by_region(bool v) { by_region = v; }

    bool empty() const { return images.empty() && buffers.empty() && !has_global; }
    // records everything pending and clears the batch
    void flush(vk::CommandBuffer cmd);

private:
    struct ImageEntry {
        Barrier b;
        vk::Image image;
        vk::ImageSubresourceRange range;
    };
    struct BufferEntry {
        Barrier b;
        vk::Buffer buffer;
    };
    std::vector<ImageEntry> images;
    std::vector<BufferEntry> buffers;
    Barrier global_b;
    bool has_global = false;
    bool by_region = false;
};

// One explicit barrier on an image the tracker does not own the state of (swapchain images, images owned by a third-party
// shader chain, mip sub-ranges). Still goes through the batch emitter: sync2/legacy choice, full-barrier fallback, counters.
inline void explicit_barrier(vk::CommandBuffer cmd, vk::Image image, const vk::ImageSubresourceRange &range, const Barrier &b) {
    Batch batch;
    batch.explicit_image(image, range, b);
    batch.flush(cmd);
}

// debug/oracle A/B: full all-commands barrier recorded outside of any render pass (no-op unless config().full_barrier)
void debug_full_barrier(vk::CommandBuffer cmd);

// Merge `add` into `into` (two barriers of the same resource in one batch). `into.old_layout` is kept, the final layout wins.
inline void merge(Barrier &into, const Barrier &add) {
    into.src_stage |= add.src_stage;
    into.dst_stage |= add.dst_stage;
    into.src_access |= add.src_access;
    into.dst_access |= add.dst_access;
    into.new_layout = add.new_layout;
}

} // namespace vkutil::sync
