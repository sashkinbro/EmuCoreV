// Start-up self-test of the framebuffer-fetch (subpass input) path.
//
// Why: on some tilers (Adreno 840 stock driver, B025) two CONSECUTIVE fetching draws inside ONE render pass instance read wrong tile data.
// Splitting the pass between such draws is always correct but costs a store+load per draw (Mali: Rayman 60 -> 31 fps). The split must
// therefore only be enabled where the path is proven wrong, and that is decided by this test, never by a vendor/device name.
//
// What: the real colour format (R16G16B16A16_SFLOAT + the parallel R16G16B16A16_UINT raw attachment + the real depth-stencil format),
// the REAL render pass handed in by the caller (same attachments, self-dependency and rasterization-order flags as the game's passes),
// a subpass-input fragment shader of the same shape as the translator's (float input or raw u16 input, writes both outputs) and
// N overlapping quads drawn one after the other, each blending the value it fetched with a constant. The result is read back and compared
// with a CPU model, for: (1) in-pass, raw input (the failing GR mode), (2) in-pass, float input, (3) one pass instance per draw (the
// reference path, must match the model or the test itself is not trusted), (4) ALL quads in ONE instanced draw call, raw input: overlapping
// primitives inside a single draw are ordered only by the destination-read (rasterization order) path, the case Mali ROAA breaks in other
// projects (EmuCoreX lessons item 1) and the one particle batches of Rayman/GR rely on.
//
// Self-contained on purpose (header only, no renderer state): the caller passes handles and gets a report back, so the shared capability
// layer (gpu_caps.h / choose_strategy) can call it as the "verified" probe of the FbFetchPath rungs.
//
//   GLSL of the shaders (compiled with glslc, words in gpu_caps_selftest_spv.h):
//   vertex:   gl_Position = vec4(pc.rect.xy + corner(gl_VertexIndex) * pc.rect.zw, 0.5, 1.0);               (triangle strip, 4 vertices)
//   fragment: old = use_raw < 0.5 ? subpassLoad(F16 input 0) : from_raw(subpassLoad(U16 input 1));
//             res = old * 0.75 + pc.color * 0.25; out0 = res; out1 = to_raw(res);                         (push constants, 48 bytes)
#pragma once

#include <vkutil/vkutil.h>

#include <renderer/vulkan/gpu_caps_selftest_spv.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace renderer::vulkan::selftest {

enum class Result : uint8_t { Pass, Fail, NotRun };

inline const char *to_string(Result r) {
    switch (r) {
    case Result::Pass: return "pass";
    case Result::Fail: return "fail";
    default: return "notrun";
    }
}

struct FbFetchInput {
    vk::Device device;
    vk::PhysicalDevice physical_device;
    vk::Queue queue;
    uint32_t queue_family = 0;
    // The renderer's real render pass for an F16 surface with the raw attachment:
    // retrieve_render_pass(F16, depth_load=true, stencil_load=true, force_store=true, transient=false, no_color=false, has_raw=true).
    // Attachments: 0 = R16G16B16A16_SFLOAT, 1 = R16G16B16A16_UINT, 2 = depth-stencil (loaded).
    vk::RenderPass render_pass;
    vk::Format depth_stencil_format = vk::Format::eD32SfloatS8Uint;
    bool rasterization_order_access = false; // the render pass was created with the ROAA subpass flag and pipelines must carry the blend flag
    uint32_t width = 2048; // the failing case needs a large target (GR: 2160x1224 at 3x)
    uint32_t height = 1152;
    uint32_t draws = 64;
};

struct FbFetchReport {
    Result result = Result::NotRun;
    // fraction of pixels that differ from the CPU model by more than the tolerance
    double mismatch_inplace_raw = -1.0;
    double mismatch_inplace_float = -1.0;
    double mismatch_split = -1.0;
    double mismatch_batch = -1.0; // ONE draw call with all quads (overlapping primitives inside a draw: the destination-read order of ROAA)
    double ms = 0.0;
    std::string note;
};

namespace detail {

inline uint16_t float_to_half(float f) {
    uint32_t x;
    std::memcpy(&x, &f, 4);
    const uint32_t sign = (x >> 16) & 0x8000u;
    int32_t e = static_cast<int32_t>((x >> 23) & 0xFF) - 127 + 15;
    uint32_t m = x & 0x7FFFFFu;
    if (e <= 0) {
        if (e < -10)
            return static_cast<uint16_t>(sign);
        m |= 0x800000u;
        const uint32_t shift = static_cast<uint32_t>(14 - e);
        uint32_t h = m >> shift;
        if ((m >> (shift - 1)) & 1u)
            h += 1; // round to nearest (ties not handled: values here are never subnormal ties)
        return static_cast<uint16_t>(sign | h);
    }
    if (e >= 31)
        return static_cast<uint16_t>(sign | 0x7C00u);
    uint32_t h = (static_cast<uint32_t>(e) << 10) | (m >> 13);
    const uint32_t rest = m & 0x1FFFu;
    if (rest > 0x1000u || (rest == 0x1000u && (h & 1u)))
        h += 1;
    return static_cast<uint16_t>(sign | h);
}

inline float half_to_float(uint16_t h) {
    const uint32_t sign = (static_cast<uint32_t>(h) & 0x8000u) << 16;
    const uint32_t e = (h >> 10) & 0x1Fu;
    const uint32_t m = h & 0x3FFu;
    uint32_t x;
    if (e == 0) {
        if (m == 0) {
            x = sign;
        } else {
            float f = std::ldexp(static_cast<float>(m), -24);
            if (sign)
                f = -f;
            return f;
        }
    } else if (e == 31) {
        x = sign | 0x7F800000u | (m << 13);
    } else {
        x = sign | ((e + 112) << 23) | (m << 13);
    }
    float f;
    std::memcpy(&f, &x, 4);
    return f;
}

struct Quad {
    uint32_t x, y, w, h; // pixels
    float color[4];
};

inline std::vector<Quad> make_quads(uint32_t W, uint32_t H, uint32_t n) {
    std::vector<Quad> q;
    uint32_t s = 0x12345678u;
    auto rnd = [&s]() {
        s = s * 1664525u + 1013904223u;
        return (s >> 8) & 0xFFFFu;
    };
    for (uint32_t i = 0; i < n; i++) {
        Quad a{};
        a.w = W / 8 + rnd() % (W / 3);
        a.h = H / 8 + rnd() % (H / 3);
        a.x = rnd() % (W - a.w);
        a.y = rnd() % (H - a.h);
        for (int c = 0; c < 4; c++)
            a.color[c] = static_cast<float>(rnd() % 1000) / 1000.0f;
        q.push_back(a);
    }
    return q;
}

inline uint16_t initial_texel(uint32_t x, uint32_t y, uint32_t c) {
    const float v = 0.1f + 0.8f * static_cast<float>((x * 3u + y * 5u + c * 97u) % 251u) / 250.0f;
    return float_to_half(v);
}

struct Mem {
    vk::DeviceMemory memory;
};

struct Resources {
    vk::Device device;
    vk::Image color, raw, depth;
    vk::ImageView color_view, raw_view, depth_view;
    vk::DeviceMemory color_mem, raw_mem, depth_mem, buf_mem, ubo_mem;
    vk::Buffer buf, ubo;
    vk::Framebuffer fb;
    vk::DescriptorSetLayout set_layout;
    vk::DescriptorPool pool;
    vk::PipelineLayout pipe_layout;
    vk::ShaderModule vs, fs;
    vk::Pipeline pipeline;
    vk::CommandPool cmd_pool;
    vk::Fence fence;
    ~Resources() {
        if (!device)
            return;
        if (fence) device.destroyFence(fence);
        if (cmd_pool) device.destroyCommandPool(cmd_pool);
        if (pipeline) device.destroyPipeline(pipeline);
        if (vs) device.destroyShaderModule(vs);
        if (fs) device.destroyShaderModule(fs);
        if (pipe_layout) device.destroyPipelineLayout(pipe_layout);
        if (pool) device.destroyDescriptorPool(pool);
        if (set_layout) device.destroyDescriptorSetLayout(set_layout);
        if (fb) device.destroyFramebuffer(fb);
        if (color_view) device.destroyImageView(color_view);
        if (raw_view) device.destroyImageView(raw_view);
        if (depth_view) device.destroyImageView(depth_view);
        if (color) device.destroyImage(color);
        if (raw) device.destroyImage(raw);
        if (depth) device.destroyImage(depth);
        if (buf) device.destroyBuffer(buf);
        if (ubo) device.destroyBuffer(ubo);
        if (color_mem) device.freeMemory(color_mem);
        if (raw_mem) device.freeMemory(raw_mem);
        if (depth_mem) device.freeMemory(depth_mem);
        if (buf_mem) device.freeMemory(buf_mem);
        if (ubo_mem) device.freeMemory(ubo_mem);
    }
};

inline uint32_t find_memory(const vk::PhysicalDeviceMemoryProperties &mp, uint32_t bits, vk::MemoryPropertyFlags want) {
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want)
            return i;
    return ~0u;
}

} // namespace detail

inline FbFetchReport run_fbfetch_selftest(const FbFetchInput &in) {
    using namespace detail;
    FbFetchReport rep;
    const auto t0 = std::chrono::steady_clock::now();
    try {
        if (!in.device || !in.render_pass || !in.queue) {
            rep.note = "missing handles";
            return rep;
        }
        const uint32_t W = in.width, H = in.height;
        const vk::Device dev = in.device;
        const vk::PhysicalDeviceMemoryProperties mp = in.physical_device.getMemoryProperties();
        Resources r;
        r.device = dev;

        auto make_image = [&](vk::Format fmt, vk::ImageUsageFlags usage, vk::ImageCreateFlags flags, vk::Image &img, vk::DeviceMemory &mem) {
            vk::ImageCreateInfo ci{
                .flags = flags,
                .imageType = vk::ImageType::e2D,
                .format = fmt,
                .extent = { W, H, 1 },
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = vk::SampleCountFlagBits::e1,
                .tiling = vk::ImageTiling::eOptimal,
                .usage = usage,
                .sharingMode = vk::SharingMode::eExclusive,
                .initialLayout = vk::ImageLayout::eUndefined
            };
            img = dev.createImage(ci);
            const vk::MemoryRequirements req = dev.getImageMemoryRequirements(img);
            uint32_t type = find_memory(mp, req.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
            if (type == ~0u)
                type = find_memory(mp, req.memoryTypeBits, vk::MemoryPropertyFlags());
            mem = dev.allocateMemory(vk::MemoryAllocateInfo{ .allocationSize = req.size, .memoryTypeIndex = type });
            dev.bindImageMemory(img, mem, 0);
        };
        make_image(vk::Format::eR16G16B16A16Sfloat,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eInputAttachment,
            vk::ImageCreateFlagBits::eMutableFormat, r.color, r.color_mem);
        make_image(vk::Format::eR16G16B16A16Uint,
            vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc,
            vk::ImageCreateFlagBits::eMutableFormat, r.raw, r.raw_mem);
        make_image(in.depth_stencil_format,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
            vk::ImageCreateFlags(), r.depth, r.depth_mem);

        const vk::ImageSubresourceRange color_range{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
        const vk::ImageSubresourceRange ds_range{ vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil, 0, 1, 0, 1 };
        r.color_view = dev.createImageView(vk::ImageViewCreateInfo{ .image = r.color, .viewType = vk::ImageViewType::e2D, .format = vk::Format::eR16G16B16A16Sfloat, .subresourceRange = color_range });
        r.raw_view = dev.createImageView(vk::ImageViewCreateInfo{ .image = r.raw, .viewType = vk::ImageViewType::e2D, .format = vk::Format::eR16G16B16A16Uint, .subresourceRange = color_range });
        r.depth_view = dev.createImageView(vk::ImageViewCreateInfo{ .image = r.depth, .viewType = vk::ImageViewType::e2D, .format = in.depth_stencil_format, .subresourceRange = ds_range });

        const vk::ImageView fb_views[] = { r.color_view, r.raw_view, r.depth_view };
        vk::FramebufferCreateInfo fbi{ .renderPass = in.render_pass, .attachmentCount = 3, .pAttachments = fb_views, .width = W, .height = H, .layers = 1 };
        r.fb = dev.createFramebuffer(fbi);

        // staging / readback buffer (8 bytes per texel)
        const vk::DeviceSize buf_size = static_cast<vk::DeviceSize>(W) * H * 8;
        r.buf = dev.createBuffer(vk::BufferCreateInfo{ .size = buf_size, .usage = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst, .sharingMode = vk::SharingMode::eExclusive });
        {
            const vk::MemoryRequirements req = dev.getBufferMemoryRequirements(r.buf);
            const uint32_t type = find_memory(mp, req.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            if (type == ~0u) {
                rep.note = "no host visible memory";
                return rep;
            }
            r.buf_mem = dev.allocateMemory(vk::MemoryAllocateInfo{ .allocationSize = req.size, .memoryTypeIndex = type });
            dev.bindBufferMemory(r.buf, r.buf_mem, 0);
        }

        // descriptors: two input attachments (float, raw)
        const vk::DescriptorSetLayoutBinding bindings[3] = {
            { .binding = 0, .descriptorType = vk::DescriptorType::eInputAttachment, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment },
            { .binding = 1, .descriptorType = vk::DescriptorType::eInputAttachment, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment },
            { .binding = 2, .descriptorType = vk::DescriptorType::eUniformBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eVertex }
        };
        r.set_layout = dev.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo{ .bindingCount = 3, .pBindings = bindings });
        const vk::DescriptorPoolSize pool_sizes[2] = { { vk::DescriptorType::eInputAttachment, 2 }, { vk::DescriptorType::eUniformBuffer, 1 } };
        r.pool = dev.createDescriptorPool(vk::DescriptorPoolCreateInfo{ .maxSets = 1, .poolSizeCount = 2, .pPoolSizes = pool_sizes });
        const vk::DescriptorSet set = dev.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{ .descriptorPool = r.pool, .descriptorSetCount = 1, .pSetLayouts = &r.set_layout })[0];
        const vk::DescriptorImageInfo di_color{ nullptr, r.color_view, vk::ImageLayout::eGeneral };
        const vk::DescriptorImageInfo di_raw{ nullptr, r.raw_view, vk::ImageLayout::eGeneral };
        // all quads for the single-draw (instanced) run: rect + colour per instance, std140 arrays of 128 vec4
        constexpr uint32_t max_batch = 128;
        const std::vector<Quad> quads = make_quads(W, H, std::min(in.draws, max_batch));
        const vk::DeviceSize ubo_size = 2ull * max_batch * 16;
        r.ubo = dev.createBuffer(vk::BufferCreateInfo{ .size = ubo_size, .usage = vk::BufferUsageFlagBits::eUniformBuffer, .sharingMode = vk::SharingMode::eExclusive });
        {
            const vk::MemoryRequirements req = dev.getBufferMemoryRequirements(r.ubo);
            const uint32_t type = find_memory(mp, req.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            if (type == ~0u) {
                rep.note = "no host visible memory (ubo)";
                return rep;
            }
            r.ubo_mem = dev.allocateMemory(vk::MemoryAllocateInfo{ .allocationSize = req.size, .memoryTypeIndex = type });
            dev.bindBufferMemory(r.ubo, r.ubo_mem, 0);
            float *u = static_cast<float *>(dev.mapMemory(r.ubo_mem, 0, ubo_size));
            std::memset(u, 0, static_cast<size_t>(ubo_size));
            for (size_t i = 0; i < quads.size(); i++) {
                const Quad &q = quads[i];
                float *rc = u + i * 4;
                float *cc = u + (max_batch + i) * 4;
                rc[0] = static_cast<float>(q.x) / static_cast<float>(W) * 2.0f - 1.0f;
                rc[1] = static_cast<float>(q.y) / static_cast<float>(H) * 2.0f - 1.0f;
                rc[2] = static_cast<float>(q.w) / static_cast<float>(W) * 2.0f;
                rc[3] = static_cast<float>(q.h) / static_cast<float>(H) * 2.0f;
                std::memcpy(cc, q.color, 16);
            }
            dev.unmapMemory(r.ubo_mem);
        }
        const vk::DescriptorBufferInfo di_ubo{ r.ubo, 0, ubo_size };
        const vk::WriteDescriptorSet writes[3] = {
            { .dstSet = set, .dstBinding = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eInputAttachment, .pImageInfo = &di_color },
            { .dstSet = set, .dstBinding = 1, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eInputAttachment, .pImageInfo = &di_raw },
            { .dstSet = set, .dstBinding = 2, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eUniformBuffer, .pBufferInfo = &di_ubo }
        };
        dev.updateDescriptorSets(3, writes, 0, nullptr);

        const vk::PushConstantRange pcr{ vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, 48 };
        r.pipe_layout = dev.createPipelineLayout(vk::PipelineLayoutCreateInfo{ .setLayoutCount = 1, .pSetLayouts = &r.set_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &pcr });
        r.vs = dev.createShaderModule(vk::ShaderModuleCreateInfo{ .codeSize = sizeof(spv::fetch_vert), .pCode = spv::fetch_vert });
        r.fs = dev.createShaderModule(vk::ShaderModuleCreateInfo{ .codeSize = sizeof(spv::fetch_frag), .pCode = spv::fetch_frag });

        const vk::PipelineShaderStageCreateInfo stages[2] = {
            { .stage = vk::ShaderStageFlagBits::eVertex, .module = r.vs, .pName = "main" },
            { .stage = vk::ShaderStageFlagBits::eFragment, .module = r.fs, .pName = "main" }
        };
        const vk::PipelineVertexInputStateCreateInfo vi{};
        const vk::PipelineInputAssemblyStateCreateInfo ia{ .topology = vk::PrimitiveTopology::eTriangleStrip };
        const vk::Viewport vp{ 0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H), 0.0f, 1.0f };
        const vk::Rect2D sc{ { 0, 0 }, { W, H } };
        const vk::PipelineViewportStateCreateInfo vps{ .viewportCount = 1, .pViewports = &vp, .scissorCount = 1, .pScissors = &sc };
        const vk::PipelineRasterizationStateCreateInfo rs{ .polygonMode = vk::PolygonMode::eFill, .cullMode = vk::CullModeFlagBits::eNone, .frontFace = vk::FrontFace::eCounterClockwise, .lineWidth = 1.0f };
        const vk::PipelineMultisampleStateCreateInfo ms{ .rasterizationSamples = vk::SampleCountFlagBits::e1 };
        const vk::PipelineDepthStencilStateCreateInfo ds{ .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_FALSE, .depthCompareOp = vk::CompareOp::eLessOrEqual };
        const vk::PipelineColorBlendAttachmentState cba[2] = {
            { .blendEnable = VK_FALSE, .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA },
            { .blendEnable = VK_FALSE, .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA }
        };
        vk::PipelineColorBlendStateCreateInfo cb{ .attachmentCount = 2, .pAttachments = cba };
        if (in.rasterization_order_access)
            cb.flags = vk::PipelineColorBlendStateCreateFlagBits::eRasterizationOrderAttachmentAccessEXT;
        vk::GraphicsPipelineCreateInfo gp{
            .stageCount = 2, .pStages = stages, .pVertexInputState = &vi, .pInputAssemblyState = &ia, .pViewportState = &vps,
            .pRasterizationState = &rs, .pMultisampleState = &ms, .pDepthStencilState = &ds, .pColorBlendState = &cb,
            .layout = r.pipe_layout, .renderPass = in.render_pass, .subpass = 0
        };
        auto pr = dev.createGraphicsPipeline(nullptr, gp);
        if (pr.result != vk::Result::eSuccess) {
            rep.note = "pipeline creation failed";
            return rep;
        }
        r.pipeline = pr.value;

        r.cmd_pool = dev.createCommandPool(vk::CommandPoolCreateInfo{ .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer, .queueFamilyIndex = in.queue_family });
        r.fence = dev.createFence(vk::FenceCreateInfo{});

        auto submit_wait = [&](auto &&record) {
            vk::CommandBuffer cb_ = dev.allocateCommandBuffers(vk::CommandBufferAllocateInfo{ .commandPool = r.cmd_pool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1 })[0];
            cb_.begin(vk::CommandBufferBeginInfo{ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
            record(cb_);
            cb_.end();
            const vk::SubmitInfo si{ .commandBufferCount = 1, .pCommandBuffers = &cb_ };
            in.queue.submit(si, r.fence);
            const vk::Result wr = dev.waitForFences(r.fence, VK_TRUE, 10ull * 1000 * 1000 * 1000);
            dev.resetFences(r.fence);
            dev.freeCommandBuffers(r.cmd_pool, cb_);
            return wr == vk::Result::eSuccess;
        };

        auto image_barrier = [&](vk::CommandBuffer c, vk::Image img, vk::ImageLayout from, vk::ImageLayout to, vk::AccessFlags sa, vk::AccessFlags da,
                                 vk::PipelineStageFlags ss, vk::PipelineStageFlags ds_, const vk::ImageSubresourceRange &range) {
            // through the same emitter as the renderer (sync2 or legacy, as the device supports): legacy and sync2 share the low 32 bits of both masks
            namespace sync = vkutil::sync;
            sync::explicit_barrier(c, img, range,
                { sync::Stage(static_cast<VkPipelineStageFlags2>(static_cast<VkPipelineStageFlags>(ss))), sync::Stage(static_cast<VkPipelineStageFlags2>(static_cast<VkPipelineStageFlags>(ds_))),
                    sync::Access(static_cast<VkAccessFlags2>(static_cast<VkAccessFlags>(sa))), sync::Access(static_cast<VkAccessFlags2>(static_cast<VkAccessFlags>(da))), from, to, true });
        };

        // fill the staging buffer with the initial pattern
        auto fill_initial = [&]() {
            uint16_t *p = static_cast<uint16_t *>(dev.mapMemory(r.buf_mem, 0, buf_size));
            for (uint32_t y = 0; y < H; y++)
                for (uint32_t x = 0; x < W; x++)
                    for (uint32_t c = 0; c < 4; c++)
                        p[(static_cast<size_t>(y) * W + x) * 4 + c] = initial_texel(x, y, c);
            dev.unmapMemory(r.buf_mem);
        };

        bool depth_ready = false;
        bool first_upload = true;
        auto upload_initial = [&]() {
            fill_initial();
            return submit_wait([&](vk::CommandBuffer c) {
                const vk::ImageLayout from = first_upload ? vk::ImageLayout::eUndefined : vk::ImageLayout::eGeneral;
                image_barrier(c, r.color, from, vk::ImageLayout::eTransferDstOptimal, vk::AccessFlagBits::eMemoryWrite | vk::AccessFlagBits::eMemoryRead, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eTransfer, color_range);
                image_barrier(c, r.raw, from, vk::ImageLayout::eTransferDstOptimal, vk::AccessFlagBits::eMemoryWrite | vk::AccessFlagBits::eMemoryRead, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eTransfer, color_range);
                const vk::BufferImageCopy region{ .imageSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 }, .imageExtent = { W, H, 1 } };
                c.copyBufferToImage(r.buf, r.color, vk::ImageLayout::eTransferDstOptimal, region);
                c.copyBufferToImage(r.buf, r.raw, vk::ImageLayout::eTransferDstOptimal, region);
                image_barrier(c, r.color, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite, vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eAllCommands, color_range);
                image_barrier(c, r.raw, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite, vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eAllCommands, color_range);
                if (!depth_ready) {
                    image_barrier(c, r.depth, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, vk::AccessFlags(), vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, ds_range);
                    const vk::ClearDepthStencilValue dclear{ 1.0f, 0 };
                    c.clearDepthStencilImage(r.depth, vk::ImageLayout::eTransferDstOptimal, dclear, ds_range);
                    image_barrier(c, r.depth, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eDepthStencilReadOnlyOptimal, vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite, vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eAllCommands, ds_range);
                }
            });
        };

        struct PC {
            float rect[4];
            float color[4];
            float use_raw;
            float batch;
            float pad[2];
        };

        auto draw_all = [&](bool split, bool use_raw, bool batch) {
            return submit_wait([&](vk::CommandBuffer c) {
                auto begin = [&]() {
                    const vk::RenderPassBeginInfo bi{ .renderPass = in.render_pass, .framebuffer = r.fb, .renderArea = { { 0, 0 }, { W, H } } };
                    c.beginRenderPass(bi, vk::SubpassContents::eInline);
                    c.bindPipeline(vk::PipelineBindPoint::eGraphics, r.pipeline);
                    c.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, r.pipe_layout, 0, set, {});
                };
                begin();
                for (size_t i = 0; i < (batch ? 1 : quads.size()); i++) {
                    if (split && i > 0) {
                        c.endRenderPass();
                        begin();
                    }
                    // the same per-draw barrier the renderer records before a fetching draw (scene.cpp)
                    {
                        namespace sync = vkutil::sync;
                        const sync::Barrier self_dep{ sync::S::eColorAttachmentOutput, sync::S::eFragmentShader, sync::A::eColorAttachmentWrite, sync::A::eInputAttachmentRead,
                            vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral, true };
                        sync::Batch fetch_barrier;
                        fetch_barrier.set_by_region(true);
                        fetch_barrier.explicit_image(r.color, color_range, self_dep);
                        fetch_barrier.explicit_image(r.raw, color_range, self_dep);
                        fetch_barrier.flush(c);
                    }
                    const Quad &q = quads[batch ? 0 : i];
                    PC pc{};
                    pc.batch = batch ? 1.0f : 0.0f;
                    pc.rect[0] = static_cast<float>(q.x) / static_cast<float>(W) * 2.0f - 1.0f;
                    pc.rect[1] = static_cast<float>(q.y) / static_cast<float>(H) * 2.0f - 1.0f;
                    pc.rect[2] = static_cast<float>(q.w) / static_cast<float>(W) * 2.0f;
                    pc.rect[3] = static_cast<float>(q.h) / static_cast<float>(H) * 2.0f;
                    std::memcpy(pc.color, q.color, sizeof(pc.color));
                    pc.use_raw = use_raw ? 1.0f : 0.0f;
                    c.pushConstants(r.pipe_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(PC), &pc);
                    c.draw(4, batch ? static_cast<uint32_t>(quads.size()) : 1u, 0, 0);
                }
                c.endRenderPass();
                // make the colour image readable by the transfer
                image_barrier(c, r.color, vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eTransferRead,
                    vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eTransfer, color_range);
                const vk::BufferImageCopy region{ .imageSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 }, .imageExtent = { W, H, 1 } };
                c.copyImageToBuffer(r.color, vk::ImageLayout::eGeneral, r.buf, region);
            });
        };

        // CPU model of the draw sequence
        std::vector<uint16_t> expected(static_cast<size_t>(W) * H * 4);
        for (uint32_t y = 0; y < H; y++)
            for (uint32_t x = 0; x < W; x++)
                for (uint32_t c = 0; c < 4; c++)
                    expected[(static_cast<size_t>(y) * W + x) * 4 + c] = initial_texel(x, y, c);
        for (const Quad &q : quads)
            for (uint32_t y = q.y; y < q.y + q.h; y++)
                for (uint32_t x = q.x; x < q.x + q.w; x++)
                    for (uint32_t c = 0; c < 4; c++) {
                        uint16_t &t = expected[(static_cast<size_t>(y) * W + x) * 4 + c];
                        t = float_to_half(half_to_float(t) * 0.75f + q.color[c] * 0.25f);
                    }

        auto compare = [&]() {
            const uint16_t *p = static_cast<const uint16_t *>(dev.mapMemory(r.buf_mem, 0, buf_size));
            size_t bad = 0;
            for (size_t i = 0; i < expected.size(); i += 4) {
                bool diff = false;
                for (int c = 0; c < 4; c++) {
                    const float a = half_to_float(p[i + c]);
                    const float b = half_to_float(expected[i + c]);
                    if (!(std::fabs(a - b) <= 0.004f)) // also catches NaN
                        diff = true;
                }
                bad += diff ? 1 : 0;
            }
            dev.unmapMemory(r.buf_mem);
            return static_cast<double>(bad) / static_cast<double>(expected.size() / 4);
        };

        constexpr double fail_fraction = 0.0005; // the defect covered several percent of the target
        struct Run { bool split; bool raw; bool batch; double *out; };
        Run runs[4] = { { false, true, false, &rep.mismatch_inplace_raw }, { false, false, false, &rep.mismatch_inplace_float }, { true, true, false, &rep.mismatch_split },
            { false, true, true, &rep.mismatch_batch } };
        for (Run &run : runs) {
            if (!upload_initial()) {
                rep.note = "upload timed out";
                return rep;
            }
            first_upload = false;
            depth_ready = true;
            if (!draw_all(run.split, run.raw, run.batch)) {
                rep.note = "draw timed out";
                return rep;
            }
            *run.out = compare();
        }
        rep.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (rep.mismatch_split > fail_fraction) {
            // the reference path itself disagrees with the model: the test cannot be trusted on this driver
            rep.result = Result::NotRun;
            rep.note = "reference (split) path disagrees with the CPU model";
            return rep;
        }
        rep.result = (rep.mismatch_inplace_raw > fail_fraction || rep.mismatch_inplace_float > fail_fraction || rep.mismatch_batch > fail_fraction) ? Result::Fail : Result::Pass;
        return rep;
    } catch (const std::exception &e) {
        rep.note = std::string("exception: ") + e.what();
        return rep;
    }
}

} // namespace renderer::vulkan::selftest
