module;

#include <version>
#if defined( __cpp_lib_modules )
#define VMA_ENABLE_STD_MODULE
#endif

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "vk_mem_alloc.hpp"

export module vk_mem_alloc_hpp;

export namespace VMA_HPP_NAMESPACE {
  using VMA_HPP_NAMESPACE::operator|;
  using VMA_HPP_NAMESPACE::operator&;
  using VMA_HPP_NAMESPACE::operator^;
  using VMA_HPP_NAMESPACE::operator~;
  using VMA_HPP_NAMESPACE::to_string;
  using VMA_HPP_NAMESPACE::functionsFromDispatcher;
  using VMA_HPP_NAMESPACE::AllocatorCreateFlagBits;
  using VMA_HPP_NAMESPACE::AllocatorCreateFlags;
  using VMA_HPP_NAMESPACE::MemoryUsage;
  using VMA_HPP_NAMESPACE::AllocationCreateFlagBits;
  using VMA_HPP_NAMESPACE::AllocationCreateFlags;
  using VMA_HPP_NAMESPACE::PoolCreateFlagBits;
  using VMA_HPP_NAMESPACE::PoolCreateFlags;
  using VMA_HPP_NAMESPACE::DefragmentationFlagBits;
  using VMA_HPP_NAMESPACE::DefragmentationFlags;
  using VMA_HPP_NAMESPACE::DefragmentationMoveOperation;
  using VMA_HPP_NAMESPACE::VirtualBlockCreateFlagBits;
  using VMA_HPP_NAMESPACE::VirtualBlockCreateFlags;
  using VMA_HPP_NAMESPACE::VirtualAllocationCreateFlagBits;
  using VMA_HPP_NAMESPACE::VirtualAllocationCreateFlags;
  using VMA_HPP_NAMESPACE::DeviceMemoryCallbacks;
  using VMA_HPP_NAMESPACE::VulkanFunctions;
  using VMA_HPP_NAMESPACE::AllocatorCreateInfo;
  using VMA_HPP_NAMESPACE::AllocatorInfo;
  using VMA_HPP_NAMESPACE::Statistics;
  using VMA_HPP_NAMESPACE::DetailedStatistics;
  using VMA_HPP_NAMESPACE::TotalStatistics;
  using VMA_HPP_NAMESPACE::Budget;
  using VMA_HPP_NAMESPACE::AllocationCreateInfo;
  using VMA_HPP_NAMESPACE::PoolCreateInfo;
  using VMA_HPP_NAMESPACE::AllocationInfo;
  using VMA_HPP_NAMESPACE::AllocationInfo2;
  using VMA_HPP_NAMESPACE::DefragmentationInfo;
  using VMA_HPP_NAMESPACE::DefragmentationMove;
  using VMA_HPP_NAMESPACE::DefragmentationPassMoveInfo;
  using VMA_HPP_NAMESPACE::DefragmentationStats;
  using VMA_HPP_NAMESPACE::VirtualBlockCreateInfo;
  using VMA_HPP_NAMESPACE::VirtualAllocationCreateInfo;
  using VMA_HPP_NAMESPACE::VirtualAllocationInfo;
  using VMA_HPP_NAMESPACE::createAllocator;
  using VMA_HPP_NAMESPACE::createVirtualBlock;
  using VMA_HPP_NAMESPACE::Allocator;
  using VMA_HPP_NAMESPACE::Pool;
  using VMA_HPP_NAMESPACE::Allocation;
  using VMA_HPP_NAMESPACE::DefragmentationContext;
  using VMA_HPP_NAMESPACE::VirtualAllocation;
  using VMA_HPP_NAMESPACE::VirtualBlock;
}

#ifndef VULKAN_HPP_NO_SMART_HANDLE
export namespace VMA_HPP_NAMESPACE {
  using VMA_HPP_NAMESPACE::createAllocatorUnique;
  using VMA_HPP_NAMESPACE::createVirtualBlockUnique;
  using VMA_HPP_NAMESPACE::UniqueBuffer;
  using VMA_HPP_NAMESPACE::UniqueImage;
  using VMA_HPP_NAMESPACE::UniqueAllocator;
  using VMA_HPP_NAMESPACE::UniquePool;
  using VMA_HPP_NAMESPACE::UniqueAllocation;
  using VMA_HPP_NAMESPACE::UniqueVirtualAllocation;
  using VMA_HPP_NAMESPACE::UniqueVirtualBlock;
  using VMA_HPP_NAMESPACE::createAllocatorUnique;
  using VMA_HPP_NAMESPACE::createVirtualBlockUnique;
}
#endif

module : private;

#ifndef VULKAN_HPP_NO_SMART_HANDLE
// Instantiate unique handle templates.
// This is a workaround for MSVC bugs, but wouldn't harm on other compilers anyway.
namespace VULKAN_HPP_NAMESPACE {
  template class UniqueHandle<Buffer, VMA_HPP_NAMESPACE::Dispatcher>;
  template class UniqueHandle<Image, VMA_HPP_NAMESPACE::Dispatcher>;
  template class UniqueHandle<VMA_HPP_NAMESPACE::Allocator, VMA_HPP_NAMESPACE::Dispatcher>;
  template class UniqueHandle<VMA_HPP_NAMESPACE::Pool, VMA_HPP_NAMESPACE::Dispatcher>;
  template class UniqueHandle<VMA_HPP_NAMESPACE::Allocation, VMA_HPP_NAMESPACE::Dispatcher>;
  template class UniqueHandle<VMA_HPP_NAMESPACE::VirtualAllocation, VMA_HPP_NAMESPACE::Dispatcher>;
  template class UniqueHandle<VMA_HPP_NAMESPACE::VirtualBlock, VMA_HPP_NAMESPACE::Dispatcher>;
}
#endif
