// SPDX-License-Identifier: GPL-3.0+
#include "kzgs_ffx_direct_backend.h"
#include "kzgs_ffx_vk_loader.h"

#include "ffx_vk.h"

namespace
{
	PFN_vkAllocateMemory s_allocateMemory;
	PFN_vkCreateBuffer s_createBuffer;
	PFN_vkCreateImage s_createImage;
	PFN_vkDestroyBuffer s_destroyBuffer;
	PFN_vkDestroyImage s_destroyImage;
	PFN_vkEnumerateDeviceExtensionProperties s_enumerateDeviceExtensionProperties;
	PFN_vkGetBufferMemoryRequirements s_getBufferMemoryRequirements;
	PFN_vkGetDeviceProcAddr s_getDeviceProcAddr;
	PFN_vkGetImageMemoryRequirements s_getImageMemoryRequirements;
	PFN_vkGetPhysicalDeviceFeatures2 s_getPhysicalDeviceFeatures2;
	PFN_vkGetPhysicalDeviceMemoryProperties s_getPhysicalDeviceMemoryProperties;
	PFN_vkGetPhysicalDeviceProperties s_getPhysicalDeviceProperties;
	PFN_vkGetPhysicalDeviceProperties2 s_getPhysicalDeviceProperties2;

	template <typename T>
	T InstanceProc(PFN_vkGetInstanceProcAddr getProc, VkInstance instance, const char* name)
	{
		return reinterpret_cast<T>(getProc(instance, name));
	}

	template <typename T>
	T DeviceProc(PFN_vkGetDeviceProcAddr getProc, VkDevice device, const char* name)
	{
		return reinterpret_cast<T>(getProc(device, name));
	}
}

FfxErrorCode GetResourceSizeFromDescription(FfxDevice device, const FfxCreateResourceDescription* description,
	uint64_t* size_in_bytes, uint64_t* alignment)
{
	return ffxGetResourceSizeFromDescriptionVK(device, description, size_in_bytes, alignment);
}

bool kzgsFfxSetVulkanProcs(VkInstance instance, VkDevice device,
	PFN_vkGetInstanceProcAddr getInstanceProcAddr, PFN_vkGetDeviceProcAddr getDeviceProcAddr)
{
	if (!instance || !device || !getInstanceProcAddr || !getDeviceProcAddr)
		return false;
	s_allocateMemory = DeviceProc<PFN_vkAllocateMemory>(getDeviceProcAddr, device, "vkAllocateMemory");
	s_createBuffer = DeviceProc<PFN_vkCreateBuffer>(getDeviceProcAddr, device, "vkCreateBuffer");
	s_createImage = DeviceProc<PFN_vkCreateImage>(getDeviceProcAddr, device, "vkCreateImage");
	s_destroyBuffer = DeviceProc<PFN_vkDestroyBuffer>(getDeviceProcAddr, device, "vkDestroyBuffer");
	s_destroyImage = DeviceProc<PFN_vkDestroyImage>(getDeviceProcAddr, device, "vkDestroyImage");
	s_enumerateDeviceExtensionProperties = InstanceProc<PFN_vkEnumerateDeviceExtensionProperties>(
		getInstanceProcAddr, instance, "vkEnumerateDeviceExtensionProperties");
	s_getBufferMemoryRequirements = DeviceProc<PFN_vkGetBufferMemoryRequirements>(
		getDeviceProcAddr, device, "vkGetBufferMemoryRequirements");
	s_getDeviceProcAddr = getDeviceProcAddr;
	s_getImageMemoryRequirements = DeviceProc<PFN_vkGetImageMemoryRequirements>(
		getDeviceProcAddr, device, "vkGetImageMemoryRequirements");
	s_getPhysicalDeviceFeatures2 = InstanceProc<PFN_vkGetPhysicalDeviceFeatures2>(
		getInstanceProcAddr, instance, "vkGetPhysicalDeviceFeatures2");
	s_getPhysicalDeviceMemoryProperties = InstanceProc<PFN_vkGetPhysicalDeviceMemoryProperties>(
		getInstanceProcAddr, instance, "vkGetPhysicalDeviceMemoryProperties");
	s_getPhysicalDeviceProperties = InstanceProc<PFN_vkGetPhysicalDeviceProperties>(
		getInstanceProcAddr, instance, "vkGetPhysicalDeviceProperties");
	s_getPhysicalDeviceProperties2 = InstanceProc<PFN_vkGetPhysicalDeviceProperties2>(
		getInstanceProcAddr, instance, "vkGetPhysicalDeviceProperties2");
	return s_allocateMemory && s_createBuffer && s_createImage && s_destroyBuffer && s_destroyImage &&
		s_enumerateDeviceExtensionProperties && s_getBufferMemoryRequirements && s_getDeviceProcAddr &&
		s_getImageMemoryRequirements &&
		s_getPhysicalDeviceFeatures2 && s_getPhysicalDeviceMemoryProperties && s_getPhysicalDeviceProperties &&
		s_getPhysicalDeviceProperties2;
}

extern "C"
{
	VKAPI_ATTR VkResult VKAPI_CALL kzgsFfxVkAllocateMemory(
		VkDevice device, const VkMemoryAllocateInfo* info, const VkAllocationCallbacks* allocator, VkDeviceMemory* memory)
	{
		return s_allocateMemory(device, info, allocator, memory);
	}

	VKAPI_ATTR VkResult VKAPI_CALL kzgsFfxVkCreateBuffer(
		VkDevice device, const VkBufferCreateInfo* info, const VkAllocationCallbacks* allocator, VkBuffer* buffer)
	{
		return s_createBuffer(device, info, allocator, buffer);
	}

	VKAPI_ATTR VkResult VKAPI_CALL kzgsFfxVkCreateImage(
		VkDevice device, const VkImageCreateInfo* info, const VkAllocationCallbacks* allocator, VkImage* image)
	{
		return s_createImage(device, info, allocator, image);
	}

	VKAPI_ATTR void VKAPI_CALL kzgsFfxVkDestroyBuffer(
		VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* allocator)
	{
		s_destroyBuffer(device, buffer, allocator);
	}

	VKAPI_ATTR void VKAPI_CALL kzgsFfxVkDestroyImage(
		VkDevice device, VkImage image, const VkAllocationCallbacks* allocator)
	{
		s_destroyImage(device, image, allocator);
	}

	VKAPI_ATTR VkResult VKAPI_CALL kzgsFfxVkEnumerateDeviceExtensionProperties(
		VkPhysicalDevice device, const char* layer, uint32_t* count, VkExtensionProperties* properties)
	{
		return s_enumerateDeviceExtensionProperties(device, layer, count, properties);
	}

	VKAPI_ATTR void VKAPI_CALL kzgsFfxVkGetBufferMemoryRequirements(
		VkDevice device, VkBuffer buffer, VkMemoryRequirements* requirements)
	{
		s_getBufferMemoryRequirements(device, buffer, requirements);
	}

	VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL kzgsFfxVkGetDeviceProcAddr(VkDevice device, const char* name)
	{
		return s_getDeviceProcAddr(device, name);
	}

	VKAPI_ATTR void VKAPI_CALL kzgsFfxVkGetImageMemoryRequirements(
		VkDevice device, VkImage image, VkMemoryRequirements* requirements)
	{
		s_getImageMemoryRequirements(device, image, requirements);
	}

	VKAPI_ATTR void VKAPI_CALL kzgsFfxVkGetPhysicalDeviceFeatures2(
		VkPhysicalDevice device, VkPhysicalDeviceFeatures2* features)
	{
		s_getPhysicalDeviceFeatures2(device, features);
	}

	VKAPI_ATTR void VKAPI_CALL kzgsFfxVkGetPhysicalDeviceMemoryProperties(
		VkPhysicalDevice device, VkPhysicalDeviceMemoryProperties* properties)
	{
		s_getPhysicalDeviceMemoryProperties(device, properties);
	}

	VKAPI_ATTR void VKAPI_CALL kzgsFfxVkGetPhysicalDeviceProperties(
		VkPhysicalDevice device, VkPhysicalDeviceProperties* properties)
	{
		s_getPhysicalDeviceProperties(device, properties);
	}

	VKAPI_ATTR void VKAPI_CALL kzgsFfxVkGetPhysicalDeviceProperties2(
		VkPhysicalDevice device, VkPhysicalDeviceProperties2* properties)
	{
		s_getPhysicalDeviceProperties2(device, properties);
	}
}
