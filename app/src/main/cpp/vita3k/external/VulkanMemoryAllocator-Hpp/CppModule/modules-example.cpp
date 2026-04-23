#include <version>
#if defined(__cpp_lib_modules)
import std;
#else
// There are issues if you include the same thing after you import
#include <iostream>
#endif

import vk_mem_alloc_hpp;
import vulkan_hpp;

int main() {
  vk::raii::Context context{};

  const char *layers[] = {
      "VK_LAYER_KHRONOS_validation",
  };
  vk::raii::Instance instance{context, vk::InstanceCreateInfo{
                                           {},
                                           {},
                                           layers,
                                       }};
  vk::raii::PhysicalDevices physicalDevices{instance};
  vk::raii::PhysicalDevice physicalDevice{physicalDevices[0]};

  float queuePriority[] = {1.0f};
  vk::DeviceQueueCreateInfo queueCreateInfo{
      {},
      0,
      queuePriority,
  };
  vk::raii::Device device{physicalDevice, vk::DeviceCreateInfo{
                                              {},
                                              queueCreateInfo,
                                          }};

  vma::AllocatorCreateInfo allocatorInfo{};
  vma::VulkanFunctions funcs = vma::functionsFromDispatcher(
      instance.getDispatcher(), device.getDispatcher());
  allocatorInfo.setInstance(*instance)
      .setPhysicalDevice(*physicalDevice)
      .setDevice(*device)
      .setPVulkanFunctions(&funcs);

  vma::UniqueAllocator allocator = vma::createAllocatorUnique(allocatorInfo);

  auto [buffer_tmp, allocation] = allocator->createBufferUnique(
      vk::BufferCreateInfo{
          {},
          1024,
          vk::BufferUsageFlagBits::eTransferSrc,
      },
      vma::AllocationCreateInfo{
          vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
          vma::MemoryUsage::eAuto,
      });

  vk::raii::Buffer buffer{device, buffer_tmp.release()};

  std::cout << "Done\n";
}
