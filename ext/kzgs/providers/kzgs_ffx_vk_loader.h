// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <vulkan/vulkan.h>

bool kzgsFfxSetVulkanProcs(VkInstance instance, VkDevice device,
	PFN_vkGetInstanceProcAddr getInstanceProcAddr, PFN_vkGetDeviceProcAddr getDeviceProcAddr);

