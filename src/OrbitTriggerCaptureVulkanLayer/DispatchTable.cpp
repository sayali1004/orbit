// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "DispatchTable.h"

#include <absl/base/casts.h>

// clang-format off
#include "vulkan/vulkan.h" // IWYU pragma: keep
#include "vulkan/vk_layer_dispatch_table.h" // IWYU pragma: keep
// clang-format on

namespace {
// All dispatchable objects have a pointer to the dispatch table. The loader's dispatch
// table pointer is used as a key for the dispatch map instead of the handle itself;
// GetDispatchTableKey returns that pointer as void*.
template <typename DispatchableType>
void* GetDispatchTableKey(DispatchableType dispatchable_object) {
  return *absl::bit_cast<void**>(dispatchable_object);
}
}  // namespace

void DispatchTable::CreateInstanceDispatchTable(
    const VkInstance& instance, const PFN_vkGetInstanceProcAddr& get_instance_proc_addr) {
  VkLayerInstanceDispatchTable dispatch_table;
  dispatch_table.GetInstanceProcAddr = absl::bit_cast<PFN_vkGetInstanceProcAddr>(
      get_instance_proc_addr(instance, "vkGetInstanceProcAddr"));
  dispatch_table.DestroyInstance =
      absl::bit_cast<PFN_vkDestroyInstance>(get_instance_proc_addr(instance, "vkDestroyInstance"));
  dispatch_table.EnumerateDeviceExtensionProperties =
      absl::bit_cast<PFN_vkEnumerateDeviceExtensionProperties>(
          get_instance_proc_addr(instance, "vkEnumerateDeviceExtensionProperties"));

  instance_dispatch_[GetDispatchTableKey(instance)] = dispatch_table;
}

void DispatchTable::CreateDeviceDispatchTable(const VkDevice& device,
                                              const PFN_vkGetDeviceProcAddr& get_device_proc_addr) {
  VkLayerDispatchTable dispatch_table;
  dispatch_table.GetDeviceProcAddr =
      absl::bit_cast<PFN_vkGetDeviceProcAddr>(get_device_proc_addr(device, "vkGetDeviceProcAddr"));
  dispatch_table.DestroyDevice =
      absl::bit_cast<PFN_vkDestroyDevice>(get_device_proc_addr(device, "vkDestroyDevice"));
  dispatch_table.QueuePresentKHR =
      absl::bit_cast<PFN_vkQueuePresentKHR>(get_device_proc_addr(device, "vkQueuePresentKHR"));

  device_dispatch_[GetDispatchTableKey(device)] = dispatch_table;
}

void DispatchTable::DestroyInstance(const VkInstance& instance) {
  instance_dispatch_.erase(GetDispatchTableKey(instance));
}

void DispatchTable::DestroyDevice(const VkDevice& device) {
  device_dispatch_.erase(GetDispatchTableKey(device));
}

PFN_vkVoidFunction DispatchTable::CallGetDeviceProcAddr(VkDevice device, const char* name) {
  return device_dispatch_[GetDispatchTableKey(device)].GetDeviceProcAddr(device, name);
}

PFN_vkVoidFunction DispatchTable::CallGetInstanceProcAddr(VkInstance instance, const char* name) {
  return instance_dispatch_.at(GetDispatchTableKey(instance)).GetInstanceProcAddr(instance, name);
}

VkResult DispatchTable::CallEnumerateDeviceExtensionProperties(
    const VkPhysicalDevice& physical_device, const char* layer_name, uint32_t* property_count,
    VkExtensionProperties* properties) {
  return instance_dispatch_.at(GetDispatchTableKey(physical_device))
      .EnumerateDeviceExtensionProperties(physical_device, layer_name, property_count, properties);
}

VkResult DispatchTable::CallQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* present_info) {
  return device_dispatch_.at(GetDispatchTableKey(queue)).QueuePresentKHR(queue, present_info);
}
