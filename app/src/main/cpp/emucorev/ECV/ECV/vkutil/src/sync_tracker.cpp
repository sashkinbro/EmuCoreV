#include "vkutil/sync_tracker.h"

namespace vkutil::sync {

Config &config() {
    static Config c;
    return c;
}

Stats &stats() {
    static Stats s;
    return s;
}

vk::PipelineStageFlags to_legacy(Stage s, bool is_dst) {
    VkPipelineStageFlags legacy = 0;
    const uint64_t v = static_cast<uint64_t>(static_cast<VkPipelineStageFlags2>(s));
    // the first 32 bits are identical in both enums
    legacy = static_cast<VkPipelineStageFlags>(v & 0xFFFFFFFFull);
    // sync2-only stages
    if (v & (VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT))
        legacy |= VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (v & (VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT | VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT))
        legacy |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    if (v & VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT)
        legacy |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT
            | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
    if (legacy == 0)
        legacy = is_dst ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    return vk::PipelineStageFlags(legacy);
}

vk::AccessFlags to_legacy(Access a) {
    const uint64_t v = static_cast<uint64_t>(static_cast<VkAccessFlags2>(a));
    VkAccessFlags legacy = static_cast<VkAccessFlags>(v & 0xFFFFFFFFull);
    if (v & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT)
        legacy |= VK_ACCESS_SHADER_READ_BIT;
    if (v & VK_ACCESS_2_SHADER_STORAGE_READ_BIT)
        legacy |= VK_ACCESS_SHADER_READ_BIT;
    if (v & VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT)
        legacy |= VK_ACCESS_SHADER_WRITE_BIT;
    return vk::AccessFlags(legacy);
}

static void log_barrier(const char *kind, const State *st, Hazard hz, const Barrier &b) {
#ifdef NDEBUG
    (void)kind; (void)st; (void)hz; (void)b; // hazard logging is debug-only
    return;
#else
    Config &c = config();
    if (!c.log_hazards || !c.log)
        return;
    char line[256];
    std::snprintf(line, sizeof(line), "[SYNC] %s %s %s: src=0x%llx/0x%llx dst=0x%llx/0x%llx layout %d->%d epoch=%u", kind,
        st && st->name ? st->name : "?", hazard_name(hz), static_cast<unsigned long long>(static_cast<VkPipelineStageFlags2>(b.src_stage)),
        static_cast<unsigned long long>(static_cast<VkAccessFlags2>(b.src_access)), static_cast<unsigned long long>(static_cast<VkPipelineStageFlags2>(b.dst_stage)),
        static_cast<unsigned long long>(static_cast<VkAccessFlags2>(b.dst_access)), static_cast<int>(b.old_layout), static_cast<int>(b.new_layout),
        st ? st->epoch : 0u);
    c.log(line);
#endif
}

void Batch::use_image(State &st, vk::Image image, const vk::ImageSubresourceRange &range, Stage stage, Access access, vk::ImageLayout layout, bool discard) {
    Stats &stt = stats();
    stt.uses++;
    Barrier b;
    const UseResult r = track(st, stage, access, layout, discard, b);
    if (!r.barrier) {
        stt.elided++;
        return;
    }
    count_hazard(r.hazard);
    log_barrier("image", &st, r.hazard, b);
    for (ImageEntry &e : images) {
        if (e.image == image && e.range.aspectMask == range.aspectMask && e.range.baseMipLevel == range.baseMipLevel
            && e.range.levelCount == range.levelCount && e.range.baseArrayLayer == range.baseArrayLayer && e.range.layerCount == range.layerCount) {
            merge(e.b, b);
            stt.merged++;
            return;
        }
    }
    images.push_back({ b, image, range });
}

void Batch::explicit_image(vk::Image image, const vk::ImageSubresourceRange &range, const Barrier &b) {
    stats().uses++;
    log_barrier("explicit-image", nullptr, Hazard::Layout, b);
    count_hazard(Hazard::Layout);
    images.push_back({ b, image, range });
}

void Batch::use_buffer(State &st, vk::Buffer buffer, Stage stage, Access access) {
    Stats &stt = stats();
    stt.uses++;
    Barrier b;
    const UseResult r = track(st, stage, access, vk::ImageLayout::eUndefined, false, b);
    if (!r.barrier) {
        stt.elided++;
        return;
    }
    count_hazard(r.hazard);
    log_barrier("buffer", &st, r.hazard, b);
    for (BufferEntry &e : buffers) {
        if (e.buffer == buffer) {
            merge(e.b, b);
            stt.merged++;
            return;
        }
    }
    buffers.push_back({ b, buffer });
}

void Batch::global(Stage src_stage, Access src_access, Stage dst_stage, Access dst_access) {
    stats().uses++;
    Barrier b;
    b.src_stage = src_stage;
    b.src_access = src_access;
    b.dst_stage = dst_stage;
    b.dst_access = dst_access;
    b.valid = true;
    log_barrier("global", nullptr, Hazard::None, b);
    if (has_global) {
        stats().merged++;
        merge(global_b, b);
    } else {
        global_b = b;
        has_global = true;
    }
}

Hazard Batch::observe(State &st, Stage stage, Access access, vk::ImageLayout layout) {
    if (!config().observe_samples)
        return Hazard::None;
    stats().uses++;
    State probe = st;
    Barrier b;
    const UseResult r = track(probe, stage, access, layout, false, b);
    if (!r.barrier) {
        stats().elided++;
        st = probe;
        return Hazard::None;
    }
    // cannot record inside a render pass: report, and leave the state untouched so every use is flagged
    stats().unsynced_in_pass++;
    (r.hazard == Hazard::Raw ? stats().unsynced_raw : r.hazard == Hazard::Layout ? stats().unsynced_layout : stats().unsynced_other)++;
    log_barrier("UNSYNCED-IN-PASS", &st, r.hazard, b);
    return r.hazard;
}

namespace {
struct LegacyGroup {
    vk::PipelineStageFlags src, dst;
    std::vector<vk::MemoryBarrier> mem;
    std::vector<vk::BufferMemoryBarrier> buf;
    std::vector<vk::ImageMemoryBarrier> img;
};
} // namespace

void Batch::flush(vk::CommandBuffer cmd) {
    if (empty())
        return;
    Config &cfg = config();
    Stats &stt = stats();
    stt.flushes++;

    // a by-region batch is a render pass self-dependency: its masks must stay exactly what the pass declared
    if (cfg.full_barrier && !by_region) {
        stt.full_barrier++;
        const Stage all = S::eAllCommands;
        const Access mem = A::eMemoryRead | A::eMemoryWrite;
        for (ImageEntry &e : images) {
            e.b.src_stage = e.b.dst_stage = all;
            e.b.src_access = e.b.dst_access = mem;
        }
        for (BufferEntry &e : buffers) {
            e.b.src_stage = e.b.dst_stage = all;
            e.b.src_access = e.b.dst_access = mem;
        }
        if (has_global) {
            global_b.src_stage = global_b.dst_stage = all;
            global_b.src_access = global_b.dst_access = mem;
        }
    }
    stt.image_barriers += images.size();
    stt.buffer_barriers += buffers.size();
    stt.global_barriers += has_global ? 1 : 0;

    if (images.size() == 1 && buffers.empty() && !has_global) {
        // fast path: stack only. Image::transition_to / explicit_barrier come through here thousands of times per second
        const ImageEntry &e = images[0];
        if (cfg.use_sync2) {
            const vk::ImageMemoryBarrier2 b2{ .srcStageMask = e.b.src_stage, .srcAccessMask = e.b.src_access, .dstStageMask = e.b.dst_stage,
                .dstAccessMask = e.b.dst_access, .oldLayout = e.b.old_layout, .newLayout = e.b.new_layout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = e.image,
                .subresourceRange = e.range };
            vk::DependencyInfo dep{};
            if (by_region)
                dep.dependencyFlags = vk::DependencyFlagBits::eByRegion;
            dep.setImageMemoryBarriers(b2);
            cmd.pipelineBarrier2KHR(dep);
        } else {
            const vk::ImageMemoryBarrier b1{ .srcAccessMask = to_legacy(e.b.src_access), .dstAccessMask = to_legacy(e.b.dst_access),
                .oldLayout = e.b.old_layout, .newLayout = e.b.new_layout, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = e.image, .subresourceRange = e.range };
            cmd.pipelineBarrier(to_legacy(e.b.src_stage, false), to_legacy(e.b.dst_stage, true),
                by_region ? vk::DependencyFlagBits::eByRegion : vk::DependencyFlags(), {}, {}, b1);
        }
        stt.api_calls++;
        images.clear();
        by_region = false;
        return;
    }

    if (cfg.use_sync2) {
        std::vector<vk::ImageMemoryBarrier2> img2;
        std::vector<vk::BufferMemoryBarrier2> buf2;
        vk::MemoryBarrier2 mem2;
        for (const ImageEntry &e : images) {
            img2.push_back(vk::ImageMemoryBarrier2{
                .srcStageMask = e.b.src_stage, .srcAccessMask = e.b.src_access, .dstStageMask = e.b.dst_stage, .dstAccessMask = e.b.dst_access,
                .oldLayout = e.b.old_layout, .newLayout = e.b.new_layout, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = e.image, .subresourceRange = e.range });
        }
        for (const BufferEntry &e : buffers) {
            buf2.push_back(vk::BufferMemoryBarrier2{
                .srcStageMask = e.b.src_stage, .srcAccessMask = e.b.src_access, .dstStageMask = e.b.dst_stage, .dstAccessMask = e.b.dst_access,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .buffer = e.buffer,
                .offset = 0, .size = VK_WHOLE_SIZE });
        }
        vk::DependencyInfo dep{};
        if (by_region)
            dep.dependencyFlags = vk::DependencyFlagBits::eByRegion;
        if (has_global) {
            mem2 = vk::MemoryBarrier2{ .srcStageMask = global_b.src_stage, .srcAccessMask = global_b.src_access,
                .dstStageMask = global_b.dst_stage, .dstAccessMask = global_b.dst_access };
            dep.setMemoryBarriers(mem2);
        }
        dep.setImageMemoryBarriers(img2);
        dep.setBufferMemoryBarriers(buf2);
        cmd.pipelineBarrier2KHR(dep);
        stt.api_calls++;
    } else {
        // legacy: one vkCmdPipelineBarrier per distinct (srcStage, dstStage) pair
        std::vector<LegacyGroup> groups;
        auto group_for = [&](Stage src, Stage dst) -> LegacyGroup & {
            const vk::PipelineStageFlags ls = to_legacy(src, false), ld = to_legacy(dst, true);
            for (LegacyGroup &g : groups)
                if (g.src == ls && g.dst == ld)
                    return g;
            groups.push_back({ ls, ld, {}, {}, {} });
            return groups.back();
        };
        if (has_global) {
            group_for(global_b.src_stage, global_b.dst_stage).mem.push_back(vk::MemoryBarrier{
                .srcAccessMask = to_legacy(global_b.src_access), .dstAccessMask = to_legacy(global_b.dst_access) });
        }
        for (const ImageEntry &e : images) {
            group_for(e.b.src_stage, e.b.dst_stage).img.push_back(vk::ImageMemoryBarrier{
                .srcAccessMask = to_legacy(e.b.src_access), .dstAccessMask = to_legacy(e.b.dst_access), .oldLayout = e.b.old_layout,
                .newLayout = e.b.new_layout, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = e.image, .subresourceRange = e.range });
        }
        for (const BufferEntry &e : buffers) {
            group_for(e.b.src_stage, e.b.dst_stage).buf.push_back(vk::BufferMemoryBarrier{
                .srcAccessMask = to_legacy(e.b.src_access), .dstAccessMask = to_legacy(e.b.dst_access),
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .buffer = e.buffer,
                .offset = 0, .size = VK_WHOLE_SIZE });
        }
        for (LegacyGroup &g : groups) {
            cmd.pipelineBarrier(g.src, g.dst, by_region ? vk::DependencyFlagBits::eByRegion : vk::DependencyFlags(), g.mem, g.buf, g.img);
            stt.api_calls++;
        }
    }
    images.clear();
    buffers.clear();
    has_global = false;
    by_region = false;
    global_b = Barrier{};
}

void debug_full_barrier(vk::CommandBuffer cmd) {
    if (!config().full_barrier)
        return;
    Batch b;
    b.global(S::eAllCommands, A::eMemoryRead | A::eMemoryWrite, S::eAllCommands, A::eMemoryRead | A::eMemoryWrite);
    b.flush(cmd);
}

} // namespace vkutil::sync

// Lab hook (tools/oracles/core-hooks/lab.cpp declares this weak): one `key=value` summary line of the tracker counters. Debug builds only.
#ifndef NDEBUG
extern "C" int ecv_sync_summary(char *buf, unsigned long n) {
    using namespace vkutil::sync;
    Stats &s = stats();
    Config &c = config();
    return std::snprintf(buf, n,
        "sync2=%d full_barrier=%d uses=%llu elided=%llu image_barriers=%llu buffer_barriers=%llu global_barriers=%llu api_calls=%llu merged=%llu "
        "raw=%llu war=%llu waw=%llu layout=%llu init=%llu unsynced_in_pass=%llu pass_covered=%llu pass_explicit=%llu "
        "validation_hazards=%llu validation_errors=%llu feedback_reads=%llu unsynced_raw=%llu unsynced_layout=%llu unsynced_other=%llu scenes=%llu scenes_depth_unbacked=%llu scenes_restarted=%llu scenes_unbacked_restarted=%llu loads_to_clear=%llu loads_to_dontcare=%llu loads_kept=%llu depth_stores_dropped=%llu",
        c.use_sync2 ? 1 : 0, c.full_barrier ? 1 : 0, static_cast<unsigned long long>(s.uses.load()), static_cast<unsigned long long>(s.elided.load()),
        static_cast<unsigned long long>(s.image_barriers.load()), static_cast<unsigned long long>(s.buffer_barriers.load()),
        static_cast<unsigned long long>(s.global_barriers.load()), static_cast<unsigned long long>(s.api_calls.load()), static_cast<unsigned long long>(s.merged.load()),
        static_cast<unsigned long long>(s.raw.load()), static_cast<unsigned long long>(s.war.load()), static_cast<unsigned long long>(s.waw.load()),
        static_cast<unsigned long long>(s.layout.load()), static_cast<unsigned long long>(s.init.load()),
        static_cast<unsigned long long>(s.unsynced_in_pass.load()), static_cast<unsigned long long>(s.pass_covered.load()),
        static_cast<unsigned long long>(s.pass_explicit.load()), static_cast<unsigned long long>(s.validation_hazards.load()),
        static_cast<unsigned long long>(s.validation_errors.load()), static_cast<unsigned long long>(s.feedback_reads.load()),
        static_cast<unsigned long long>(s.unsynced_raw.load()), static_cast<unsigned long long>(s.unsynced_layout.load()),
        static_cast<unsigned long long>(s.unsynced_other.load()), static_cast<unsigned long long>(s.scenes.load()),
        static_cast<unsigned long long>(s.scenes_depth_unbacked.load()), static_cast<unsigned long long>(s.scenes_restarted.load()),
        static_cast<unsigned long long>(s.scenes_unbacked_restarted.load()), static_cast<unsigned long long>(s.loads_to_clear.load()),
        static_cast<unsigned long long>(s.loads_to_dontcare.load()), static_cast<unsigned long long>(s.loads_kept.load()), static_cast<unsigned long long>(s.depth_stores_dropped.load()));
}
#endif
