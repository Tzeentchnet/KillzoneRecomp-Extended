// Tests real adapter negotiation against a fake XeSS API; no SDK or GPU is involved.
// SPDX-License-Identifier: GPL-3.0+
#include "kzgs_upscaler_provider.h"
#include <xess/xess_vk.h>

#include <cstdio>
#include <cstring>
#include <type_traits>

namespace
{
	int failures = 0;
	uint32_t minVersion = VK_API_VERSION_1_1;
	uint16_t runtimeMajor = 3;
	bool unsupportedExtraFeature = false;
	bool missingExtension = false;
	bool failDriver = false;
	int featureQueries = 0;
#define CHECK(expression) do { if (!(expression)) { \
	std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression); ++failures; } } while (0)

	template <typename T> T handle(uintptr_t value)
	{
		if constexpr (std::is_pointer_v<T>)
			return reinterpret_cast<T>(value);
		else
			return static_cast<T>(value);
	}

	VkResult VKAPI_CALL enumerate(VkPhysicalDevice, const char*, uint32_t* count, VkExtensionProperties* properties)
	{
		static const char* names[] = {VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME,
			VK_KHR_SHADER_INTEGER_DOT_PRODUCT_EXTENSION_NAME};
		if (!properties)
		{
			*count = missingExtension ? 0 : 2;
			return VK_SUCCESS;
		}
		for (uint32_t i = 0; i < *count; ++i)
		{
			const size_t length = std::strlen(names[i]) + 1;
			CHECK(length <= sizeof(properties[i].extensionName));
			if (length <= sizeof(properties[i].extensionName))
				std::memcpy(properties[i].extensionName, names[i], length);
		}
		return VK_SUCCESS;
	}

	PFN_vkVoidFunction VKAPI_CALL instanceProc(VkInstance, const char* name)
	{
		return std::strcmp(name, "vkEnumerateDeviceExtensionProperties") == 0 ?
			reinterpret_cast<PFN_vkVoidFunction>(enumerate) : nullptr;
	}

	void VKAPI_CALL barrier(VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags, VkDependencyFlags,
		uint32_t, const VkMemoryBarrier*, uint32_t, const VkBufferMemoryBarrier*, uint32_t, const VkImageMemoryBarrier*) {}

	PFN_vkVoidFunction VKAPI_CALL deviceProc(VkDevice, const char*)
	{
		return reinterpret_cast<PFN_vkVoidFunction>(barrier);
	}
}

extern "C" xess_result_t xessGetVersion(xess_version_t* version)
{
	*version = {runtimeMajor, 0, 2, 0};
	return XESS_RESULT_SUCCESS;
}

extern "C" xess_result_t xessVKGetRequiredInstanceExtensions(uint32_t* count, const char* const** names, uint32_t* version)
{
	static const char* values[] = {VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME};
	*count = 1;
	*names = values;
	*version = minVersion;
	return failDriver ? XESS_RESULT_ERROR_UNSUPPORTED_DRIVER : XESS_RESULT_SUCCESS;
}

extern "C" xess_result_t xessVKGetRequiredDeviceExtensions(VkInstance, VkPhysicalDevice, uint32_t* count, const char* const** names)
{
	static const char* values[] = {VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME};
	*count = 1;
	*names = values;
	return XESS_RESULT_SUCCESS;
}

extern "C" xess_result_t xessVKGetRequiredDeviceFeatures(VkInstance, VkPhysicalDevice, void** chain)
{
	++featureQueries;
	static VkPhysicalDeviceFeatures2 core{};
	static VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT mutableType{};
	static VkPhysicalDeviceVulkan12Features v12{};
	static VkPhysicalDeviceVulkan13Features v13{};
	core = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
	mutableType = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT};
	v12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
	v13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
	core.features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
	mutableType.mutableDescriptorType = VK_TRUE;
	v12.shaderInt8 = VK_TRUE;
	v12.bufferDeviceAddress = unsupportedExtraFeature ? VK_TRUE : VK_FALSE;
	v13.shaderIntegerDotProduct = VK_TRUE;
	core.pNext = &mutableType;
	mutableType.pNext = &v12;
	v12.pNext = &v13;
	*chain = &core;
	return XESS_RESULT_SUCCESS;
}

int main()
{
	KzgsProviderApi api{sizeof(api)};
	KzgsProviderStatus status{sizeof(status)};
	CHECK(kzgs_upscaler_get_api(KZGS_PROVIDER_ABI_VERSION, &api, &status) == KZGS_PROVIDER_OK);
	KzgsProviderInfo info{sizeof(info)};
	CHECK(api.query_info(&info, &status) == KZGS_PROVIDER_OK && info.kind == KZGS_PROVIDER_KIND_XESS);
	CHECK((info.required_inputs & (KZGS_PROVIDER_INPUT_DEPTH | KZGS_PROVIDER_INPUT_MOTION |
		KZGS_PROVIDER_INPUT_JITTER | KZGS_PROVIDER_INPUT_CAMERA)) ==
		(KZGS_PROVIDER_INPUT_DEPTH | KZGS_PROVIDER_INPUT_MOTION | KZGS_PROVIDER_INPUT_JITTER | KZGS_PROVIDER_INPUT_CAMERA));

	KzgsProviderRequirements requirements{sizeof(requirements)};
	CHECK(api.instance_requirements(&requirements, &status) == KZGS_PROVIDER_OK);
	CHECK(requirements.extension_count == 1);
	runtimeMajor = 2;
	CHECK(api.instance_requirements(&requirements, &status) == KZGS_PROVIDER_ERROR_VERSION && status.native_code == 2);
	runtimeMajor = 3;
	minVersion = 0;
	CHECK(api.instance_requirements(&requirements, &status) == KZGS_PROVIDER_ERROR_VERSION);
	minVersion = VK_API_VERSION_1_2;
	CHECK(api.instance_requirements(&requirements, &status) == KZGS_PROVIDER_ERROR_VERSION);
	minVersion = VK_API_VERSION_1_1;
	failDriver = true;
	CHECK(api.instance_requirements(&requirements, &status) == KZGS_PROVIDER_ERROR_EXTENSIONS);
	CHECK(status.native_code == XESS_RESULT_ERROR_UNSUPPORTED_DRIVER &&
		status.reason == KZGS_PROVIDER_REASON_UNSUPPORTED_DEVICE);
	failDriver = false;

	KzgsProviderDeviceQuery query{sizeof(query), handle<VkInstance>(1), handle<VkPhysicalDevice>(2), instanceProc};
	requirements = {sizeof(requirements)};
	CHECK(api.device_requirements(&query, &requirements, &status) == KZGS_PROVIDER_OK);
	CHECK(requirements.extension_count == 3 && requirements.feature_block_count == 3);
	CHECK(requirements.core_features.shaderStorageImageWriteWithoutFormat == VK_TRUE);
	CHECK(requirements.feature_blocks[0].s_type == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT);
	CHECK(requirements.feature_blocks[1].s_type == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES);
	CHECK(requirements.feature_blocks[1].required[0] == VK_FALSE && requirements.feature_blocks[1].required[1] == VK_TRUE);
	CHECK(requirements.feature_blocks[2].s_type == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES);
	unsupportedExtraFeature = true;
	CHECK(api.device_requirements(&query, &requirements, &status) == KZGS_PROVIDER_ERROR_FEATURES);
	unsupportedExtraFeature = false;
	missingExtension = true;
	CHECK(api.device_requirements(&query, &requirements, &status) == KZGS_PROVIDER_ERROR_EXTENSIONS);
	missingExtension = false;

	KzgsProviderCreateInfo createInfo{};
	createInfo.struct_size = sizeof(createInfo);
	createInfo.instance = query.instance;
	createInfo.physical_device = query.physical_device;
	createInfo.device = handle<VkDevice>(3);
	createInfo.queue = handle<VkQueue>(4);
	createInfo.get_instance_proc_addr = instanceProc;
	createInfo.get_device_proc_addr = deviceProc;
	createInfo.source_extent = {1280, 720};
	createInfo.source_rect = {0, 0, 1280, 720};
	createInfo.destination_extent = {1920, 1080};
	createInfo.source_format = createInfo.destination_format = VK_FORMAT_R8G8B8A8_UNORM;
	createInfo.color_encoding = KZGS_PROVIDER_COLOR_LINEAR_SDR;
	void* context = nullptr;
	CHECK(api.create(&createInfo, &context, &status) == KZGS_PROVIDER_ERROR_CREATE);
	CHECK(!context && status.message && std::strstr(status.message, "may submit GPU work"));
	CHECK(featureQueries >= 3);
	if (!failures)
		std::puts("XeSS negotiation: PASS (test double only; SDK initialization is blocked)");
	return failures ? 1 : 0;
}
