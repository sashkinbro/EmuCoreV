// EmuCoreV native shader adapter
// Copyright (C) 2026 EmuCoreV contributors
//
// This program is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software Foundation;
// either version 2 of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
// PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// librashader C API is MIT-licensed. Its header is supplied by the pinned configure-time
// dependency; see NOTICE.librashader for the dependency's separate license terms.
#if defined(EMUCOREV_HAVE_LIBRASHADER)
#define LIBRA_RUNTIME_VULKAN 1
#include <librashader.h>
#endif

#include "emucorev/shaders/vulkan_shader_chain.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace emucorev::shaders {
namespace {

#if defined(EMUCOREV_HAVE_LIBRASHADER)
std::string takeError(libra_error_t error) {
    if (!error)
        return {};
    char *message = nullptr;
    std::string result;
    if (libra_error_write(error, &message) == 0 && message) {
        result.assign(message);
        libra_error_free_string(&message);
    } else {
        result = "librashader error " + std::to_string(static_cast<int>(libra_error_errno(error)));
    }
    libra_error_free(&error);
    return result;
}
#endif

} // namespace

VulkanShaderChain::~VulkanShaderChain() {
    destroy();
}

void VulkanShaderChain::requestPreset(std::string absolute_path) {
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_preset_ = std::move(absolute_path);
        preset_pending_ = true;
    }
    setLastError({});
}

void VulkanShaderChain::setParameter(std::string name, float value) {
    if (name.empty() || name.size() > 128 || !std::isfinite(value))
        return;
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_parameters_[std::move(name)] = value;
    ++parameter_generation_;
}

void VulkanShaderChain::clearParameters() {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_parameters_.clear();
    ++parameter_generation_;
    reset_parameters_pending_ = true;
}

bool VulkanShaderChain::applyPending(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device,
    VkQueue queue, PFN_vkGetInstanceProcAddr entry, PFN_vkDeviceWaitIdle wait_idle, std::string &error) {
#if !defined(EMUCOREV_HAVE_LIBRASHADER)
    (void)instance;
    (void)physical_device;
    (void)device;
    (void)queue;
    (void)entry;
    (void)wait_idle;
    (void)error;
    return false;
#else
    std::string requested;
    bool has_preset_request = false;
    bool reset_parameters = false;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        has_preset_request = preset_pending_;
        if (has_preset_request) {
            requested = std::move(pending_preset_);
            pending_preset_.clear();
            preset_pending_ = false;
        }
        reset_parameters = reset_parameters_pending_;
        reset_parameters_pending_ = false;
    }

    if (!has_preset_request && !reset_parameters)
        return isActive();

    if (!has_preset_request)
        requested = active_preset_;
    if (requested.empty()) {
        destroy();
        failed_ = false;
        active_preset_.clear();
        setLastError({});
        return false;
    }
    if (requested == active_preset_ && isActive() && !reset_parameters)
        return true;

    // librashader owns descriptor sets and per-frame image views. Retire all command
    // buffers before releasing/replacing the chain, just as the existing screen-filter
    // destructors do for their Vulkan objects.
    if (device != VK_NULL_HANDLE) {
        if (!wait_idle) {
            error = "Vulkan device wait entry point is unavailable";
            setLastError(error);
            return false;
        }
        wait_idle(device);
    }
    destroy();
    active_preset_ = requested;
    if (!create(requested, instance, physical_device, device, queue, entry, error)) {
        failed_ = true;
        active_preset_.clear();
        setLastError(error);
        return false;
    }
    failed_ = false;
    setLastError({});
    frame_count_ = 0;
    applied_parameter_generation_ = 0;
    return true;
#endif
}

bool VulkanShaderChain::create(const std::string &path, VkInstance instance, VkPhysicalDevice physical_device,
    VkDevice device, VkQueue queue, PFN_vkGetInstanceProcAddr entry, std::string &error) {
#if !defined(EMUCOREV_HAVE_LIBRASHADER)
    (void)path;
    (void)instance;
    (void)physical_device;
    (void)device;
    (void)queue;
    (void)entry;
    error = "librashader Vulkan runtime was not included in this build";
    return false;
#else
    libra_shader_preset_t preset = nullptr;
    if (libra_error_t result = libra_preset_create(path.c_str(), &preset)) {
        error = takeError(result);
        return false;
    }

    libra_device_vk_t vulkan{};
    vulkan.physical_device = physical_device;
    vulkan.instance = instance;
    vulkan.device = device;
    vulkan.queue = queue;
    vulkan.entry = entry;

    filter_chain_vk_opt_t options{};
    options.version = LIBRASHADER_CURRENT_VERSION;
    options.frames_in_flight = 3;
    options.force_no_mipmaps = false;
    options.use_dynamic_rendering = false;
    // No disk cache is enabled until ECV provides a bounded, app-owned cache directory.
    options.disable_cache = true;

    libra_vk_filter_chain_t chain = nullptr;
    if (libra_error_t result = libra_vk_filter_chain_create(&preset, vulkan, &options, &chain)) {
        // The preset is invalidated by create, including its failure path.
        error = takeError(result);
        return false;
    }
    chain_ = chain;
    return true;
#endif
}

bool VulkanShaderChain::frame(VkCommandBuffer command_buffer, const ShaderChainImage &source,
    const ShaderChainImage &target, const ShaderChainViewport &viewport, std::string &error) {
#if !defined(EMUCOREV_HAVE_LIBRASHADER)
    (void)command_buffer;
    (void)source;
    (void)target;
    (void)viewport;
    error = "librashader Vulkan runtime was not included in this build";
    return false;
#else
    if (!isActive())
        return false;
    if (command_buffer == VK_NULL_HANDLE || source.image == VK_NULL_HANDLE || target.image == VK_NULL_HANDLE ||
        source.format == VK_FORMAT_UNDEFINED || target.format == VK_FORMAT_UNDEFINED ||
        source.width == 0 || source.height == 0 || target.width == 0 || target.height == 0 ||
        viewport.width == 0 || viewport.height == 0) {
        error = "invalid image or viewport supplied to shader chain";
        setLastError(error);
        return false;
    }

    applyPendingParameters();
    const libra_image_vk_t input{ source.image, source.format, source.width, source.height };
    const libra_image_vk_t output{ target.image, target.format, target.width, target.height };
    const libra_viewport_t output_view{ viewport.x, viewport.y, viewport.width, viewport.height };
    auto chain = static_cast<libra_vk_filter_chain_t>(chain_);
    if (libra_error_t result = libra_vk_filter_chain_frame(&chain, command_buffer, frame_count_, input, output,
            &output_view, nullptr, nullptr)) {
        error = takeError(result);
        failed_ = true;
        setLastError(error);
        return false;
    }
    ++frame_count_;
    setLastError({});
    return true;
#endif
}

void VulkanShaderChain::applyPendingParameters() {
#if defined(EMUCOREV_HAVE_LIBRASHADER)
    std::vector<std::pair<std::string, float>> parameters;
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        generation = parameter_generation_;
        if (generation == applied_parameter_generation_)
            return;
        parameters.assign(pending_parameters_.begin(), pending_parameters_.end());
    }

    auto chain = static_cast<libra_vk_filter_chain_t>(chain_);
    for (const auto &[name, value] : parameters) {
        // Overrides can outlive a preset selection. Unknown parameter names are skipped;
        // librashader reports them as ordinary parameter misses.
        if (libra_error_t result = libra_vk_filter_chain_set_param(&chain, name.c_str(), value))
            libra_error_free(&result);
    }
    applied_parameter_generation_ = generation;
#endif
}

void VulkanShaderChain::destroy() {
#if defined(EMUCOREV_HAVE_LIBRASHADER)
    if (chain_) {
        auto chain = static_cast<libra_vk_filter_chain_t>(chain_);
        if (libra_error_t result = libra_vk_filter_chain_free(&chain))
            libra_error_free(&result);
        chain_ = nullptr;
    }
#else
    chain_ = nullptr;
#endif
    active_preset_.clear();
    failed_ = false;
    frame_count_ = 0;
    applied_parameter_generation_ = 0;
    setLastError({});
}

std::string VulkanShaderChain::lastError() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return status_error_;
}

void VulkanShaderChain::setLastError(std::string error) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_error_ = std::move(error);
}

} // namespace emucorev::shaders
