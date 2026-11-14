// EmuCoreV shader integration layer. librashader remains an external C-ABI dependency.
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <vulkan/vulkan.h>

namespace emucorev::shaders {

inline constexpr bool vulkanShaderRuntimeAvailable() {
#if defined(EMUCOREV_HAVE_LIBRASHADER)
    return true;
#else
    return false;
#endif
}

struct ShaderChainImage {
    VkImage image = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct ShaderChainViewport {
    float x = 0.0f;
    float y = 0.0f;
    uint32_t width = 0;
    uint32_t height = 0;
};

// Owns librashader's Vulkan chain on the renderer thread. Calls that update user
// selections only publish bounded pending values; Vulkan objects are touched by frame().
class VulkanShaderChain {
public:
    VulkanShaderChain() = default;
    ~VulkanShaderChain();

    VulkanShaderChain(const VulkanShaderChain &) = delete;
    VulkanShaderChain &operator=(const VulkanShaderChain &) = delete;

    void requestPreset(std::string absolute_path);
    void setParameter(std::string name, float value);
    void clearParameters();

    // Applies a pending preset replacement on the renderer thread. An empty path resets.
    bool applyPending(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device,
        VkQueue queue, PFN_vkGetInstanceProcAddr entry, PFN_vkDeviceWaitIdle wait_idle, std::string &error);

    // Records a complete RetroArch pass chain. Input must be shader-read-only and output
    // color-attachment-optimal; callers own layout transitions and render-pass boundaries.
    bool frame(VkCommandBuffer command_buffer, const ShaderChainImage &source,
        const ShaderChainImage &target, const ShaderChainViewport &viewport, std::string &error);

    void destroy();
    bool isActive() const { return chain_ != nullptr && !failed_; }
    bool failed() const { return failed_; }
    size_t frameCount() const { return frame_count_; }
    const std::string &activePreset() const { return active_preset_; }
    std::string lastError() const;

private:
    bool create(const std::string &path, VkInstance instance, VkPhysicalDevice physical_device,
        VkDevice device, VkQueue queue, PFN_vkGetInstanceProcAddr entry, std::string &error);
    void applyPendingParameters();

    void *chain_ = nullptr;
    std::string active_preset_;
    size_t frame_count_ = 0;
    bool failed_ = false;
    uint64_t parameter_generation_ = 0;
    uint64_t applied_parameter_generation_ = 0;
    bool reset_parameters_pending_ = false;
    std::mutex pending_mutex_;
    std::string pending_preset_;
    bool preset_pending_ = false;
    std::map<std::string, float> pending_parameters_;
    mutable std::mutex status_mutex_;
    std::string status_error_;

    void setLastError(std::string error);
};

} // namespace emucorev::shaders
