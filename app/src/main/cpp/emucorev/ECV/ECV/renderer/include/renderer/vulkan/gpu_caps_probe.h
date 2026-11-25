// Fills renderer::vulkan::GpuCaps from a physical device (gpu_caps.h has the pure selection logic).
#pragma once

#include <renderer/vulkan/gpu_caps.h>

#include <vkutil/vkutil.h> // vulkan.hpp configuration (dynamic dispatch, no constructors)

#include <vector>

namespace renderer::vulkan {

// `extensions`: everything the driver lists (not only what was enabled). Features are queried through vkGetPhysicalDeviceFeatures2
// so nothing here needs the extension to be enabled; `GpuCaps::sync2` is set by the caller from what it really enabled.
GpuCaps probe_gpu_caps(vk::PhysicalDevice device, const std::vector<vk::ExtensionProperties> &extensions, bool has_features2_khr);

// the one-line path table for the lab (ECVLAB `paths` line)
void publish_paths_line(const std::string &line);
const char *paths_line_c();

// oracle flag files <files dir>/ecv-force-path-<name> (debug builds only; empty in release)
ForcedPaths read_forced_paths(const std::string &flag_dir);

} // namespace renderer::vulkan
