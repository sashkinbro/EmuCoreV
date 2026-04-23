cmake_minimum_required(VERSION 3.29)

project(CppModules LANGUAGES CXX)

find_package(Vulkan REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

option(VMA_BUILD_WITH_VULKAN_MODULE ON)
option(VMA_BUILD_MODULE_VULKAN_DYNAMIC OFF)

option(VMA_BUILD_WITH_STD_MODULE OFF)

if(${VMA_BUILD_WITH_VULKAN_MODULE})
  message(STATUS "Building with Vulkan module at ${Vulkan_INCLUDE_DIRS}")
  add_library(Vulkan-Hpp-Module)

  target_sources(
    Vulkan-Hpp-Module
    PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${Vulkan_INCLUDE_DIRS}/vulkan FILES
           ${Vulkan_INCLUDE_DIRS}/vulkan/vulkan.cppm)

  if(${VMA_BUILD_MODULE_VULKAN_DYNAMIC})
    target_compile_definitions(Vulkan-Hpp-Module PUBLIC VK_NO_PROTOTYPES=1)
    target_link_libraries(Vulkan-Hpp-Module PUBLIC Vulkan::Headers)
  else()
    target_link_libraries(Vulkan-Hpp-Module PUBLIC Vulkan::Vulkan)
  endif()

  if(${VMA_BUILD_WITH_STD_MODULE})
    target_compile_features(Vulkan-Hpp-Module PUBLIC cxx_std_23)
    set_target_properties(Vulkan-Hpp-Module PROPERTIES CXX_MODULE_STD ON)
  else()
    target_compile_features(Vulkan-Hpp-Module PUBLIC cxx_std_20)
  endif()
else()
  message(STATUS "Building with Vulkan headers at ${Vulkan_INCLUDE_DIRS}")
  add_library(Vulkan-Hpp-Module INTERFACE)
  target_compile_features(Vulkan-Hpp-Module INTERFACE cxx_std_20)

  if(${VMA_BUILD_MODULE_VULKAN_DYNAMIC})
    target_compile_definitions(Vulkan-Hpp-Module INTERFACE VK_NO_PROTOTYPES=1)
    target_link_libraries(Vulkan-Hpp-Module INTERFACE Vulkan::Headers)
  else()
    target_link_libraries(Vulkan-Hpp-Module INTERFACE Vulkan::Vulkan)
  endif()
endif()

add_library(VulkanMemoryAllocator-Hpp-Module)
target_sources(
  VulkanMemoryAllocator-Hpp-Module PUBLIC FILE_SET CXX_MODULES BASE_DIRS ../src
                                          FILES ../src/vk_mem_alloc.cppm)

if(${VMA_BUILD_WITH_STD_MODULE})
  target_compile_features(VulkanMemoryAllocator-Hpp-Module PUBLIC cxx_std_23)
  set_target_properties(VulkanMemoryAllocator-Hpp-Module
                        PROPERTIES CXX_MODULE_STD ON)
else()
  target_compile_features(VulkanMemoryAllocator-Hpp-Module PUBLIC cxx_std_20)
endif()

target_link_libraries(
  VulkanMemoryAllocator-Hpp-Module
  PRIVATE Vulkan-Hpp-Module GPUOpen::VulkanMemoryAllocator
          VulkanMemoryAllocator-Hpp)

if(${VMA_BUILD_WITH_VULKAN_MODULE})
  target_compile_definitions(VulkanMemoryAllocator-Hpp-Module
                             PRIVATE VMA_ENABLE_VULKAN_HPP_MODULE)
endif()

if(VMA_BUILD_WITH_VULKAN_MODULE)
  option(VMA_BUILD_EXAMPLE ON)
  if(VMA_BUILD_EXAMPLE)
    add_executable(Modules-Example modules-example.cpp)
    target_compile_features(Modules-Example PRIVATE cxx_std_20)
    target_link_libraries(
      Modules-Example PRIVATE VulkanMemoryAllocator-Hpp-Module
                              Vulkan-Hpp-Module)

    if(${VMA_BUILD_WITH_STD_MODULE})
      target_compile_features(Modules-Example PUBLIC cxx_std_23)
      set_target_properties(Modules-Example PROPERTIES CXX_MODULE_STD ON)
    else()
      target_compile_features(Modules-Example PUBLIC cxx_std_20)
    endif()

  endif()
endif()
