// Tests the XeSS adapter against a fake API; no Intel SDK or GPU is involved.
// SPDX-License-Identifier: GPL-3.0+
#include "kzgs_upscaler_provider.h"
#include <xess/xess_vk.h>

#include <cstdio>
#include <cstring>
#include <thread>
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
	int scopeCalls = 0;
	int createCalls = 0;
	int initCalls = 0;
	int jitterCalls = 0;
	int velocityCalls = 0;
	int executeCalls = 0;
	int destroyCalls = 0;
	int barrierCalls = 0;
	int hostMode = 0;
	bool insideScope = false;
	bool failInit = false;
	bool failVelocity = false;
	bool failExecute = false;
	bool invalidQuality = false;
	float velocityX = 0.0f, velocityY = 0.0f;
	xess_vk_init_params_t capturedInit{};
	xess_vk_execute_params_t capturedExecute{};
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

	void VKAPI_CALL barrier(VkCommandBuffer, VkPipelineStageFlags source, VkPipelineStageFlags destination,
		VkDependencyFlags, uint32_t memoryCount, const VkMemoryBarrier* memory,
		uint32_t, const VkBufferMemoryBarrier*, uint32_t imageCount, const VkImageMemoryBarrier* images)
	{
		CHECK((source & VK_PIPELINE_STAGE_ALL_COMMANDS_BIT) && (destination & VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
		CHECK(memoryCount == 1 && memory && memory[0].srcAccessMask == VK_ACCESS_MEMORY_WRITE_BIT);
		CHECK(imageCount == 4 && images);
		if (imageCount == 4 && images)
		{
			CHECK(images[0].newLayout == ((barrierCalls % 2) ?
				VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
			CHECK(images[1].newLayout == images[0].newLayout && images[2].newLayout == images[0].newLayout);
			CHECK(images[3].newLayout == VK_IMAGE_LAYOUT_GENERAL);
		}
		++barrierCalls;
	}

	PFN_vkVoidFunction VKAPI_CALL deviceProc(VkDevice, const char*)
	{
		return reinterpret_cast<PFN_vkVoidFunction>(barrier);
	}

	KzgsProviderResult KZGS_PROVIDER_CALL runInitialization(void*, KzgsProviderInitializationWork work,
		void* workData, KzgsProviderStatus* status)
	{
		++scopeCalls;
		if (hostMode == 1)
		{
			status->native_code = -71;
			return KZGS_PROVIDER_ERROR_CREATE;
		}
		insideScope = true;
		work(workData);
		insideScope = false;
		if (hostMode == 2)
		{
			status->native_code = -72;
			return KZGS_PROVIDER_ERROR_CREATE;
		}
		return KZGS_PROVIDER_OK;
	}

	KzgsProviderImage image(uintptr_t number, VkFormat format, VkExtent2D extent,
		VkImageAspectFlags aspect, VkImageUsageFlags usage)
	{
		KzgsProviderImage result{};
		result.struct_size = sizeof(result);
		result.image = handle<VkImage>(number);
		result.view = handle<VkImageView>(number + 100);
		result.format = format;
		result.extent = extent;
		result.subresource = {aspect, 0, 1, 0, 1};
		result.layout = VK_IMAGE_LAYOUT_GENERAL;
		result.usage = usage;
		result.queue_family = 2;
		result.available_stages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		result.available_access = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		return result;
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

extern "C" xess_result_t xessVKCreateContext(VkInstance, VkPhysicalDevice, VkDevice, xess_context_handle_t* out)
{
	CHECK(insideScope);
	++createCalls;
	*out = handle<xess_context_handle_t>(100 + createCalls);
	return XESS_RESULT_SUCCESS;
}

extern "C" xess_result_t xessGetOptimalInputResolution(xess_context_handle_t, const xess_2d_t* output,
	xess_quality_settings_t, xess_2d_t* optimal, xess_2d_t* minimum, xess_2d_t* maximum)
{
	CHECK(insideScope && output->x == 1920 && output->y == 1080);
	*optimal = {1280, 720};
	*minimum = invalidQuality ? xess_2d_t{1500, 850} : xess_2d_t{640, 360};
	*maximum = {1600, 900};
	return XESS_RESULT_SUCCESS;
}

extern "C" xess_result_t xessVKInit(xess_context_handle_t, const xess_vk_init_params_t* params)
{
	CHECK(insideScope);
	++initCalls;
	capturedInit = *params;
	return failInit ? XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY : XESS_RESULT_SUCCESS;
}

extern "C" xess_result_t xessSetJitterScale(xess_context_handle_t, float x, float y)
{
	CHECK(insideScope && x == 1.0f && y == 1.0f);
	++jitterCalls;
	return XESS_RESULT_SUCCESS;
}

extern "C" xess_result_t xessSetVelocityScale(xess_context_handle_t, float x, float y)
{
	CHECK(!insideScope);
	++velocityCalls;
	velocityX = x;
	velocityY = y;
	return failVelocity ? XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY : XESS_RESULT_SUCCESS;
}

extern "C" xess_result_t xessVKExecute(xess_context_handle_t, VkCommandBuffer,
	const xess_vk_execute_params_t* params)
{
	CHECK(!insideScope);
	++executeCalls;
	capturedExecute = *params;
	return failExecute ? XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY : XESS_RESULT_SUCCESS;
}

extern "C" xess_result_t xessDestroyContext(xess_context_handle_t)
{
	CHECK(!insideScope);
	++destroyCalls;
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
	createInfo.queue_family = 2;
	createInfo.get_instance_proc_addr = instanceProc;
	createInfo.get_device_proc_addr = deviceProc;
	createInfo.source_extent = {1300, 760};
	createInfo.source_rect = {10, 20, 1290, 740};
	createInfo.destination_extent = {1920, 1080};
	createInfo.source_format = createInfo.destination_format = VK_FORMAT_R8G8B8A8_UNORM;
	createInfo.color_encoding = KZGS_PROVIDER_COLOR_LINEAR_SDR;
	void* context = nullptr;
	createInfo.struct_size = static_cast<uint32_t>(offsetof(KzgsProviderCreateInfo, initialization));
	CHECK(api.create(&createInfo, &context, &status) == KZGS_PROVIDER_ERROR_CREATE && !context);
	CHECK(status.reason == KZGS_PROVIDER_REASON_MISSING_INPUT && createCalls == 0);
	createInfo.struct_size = sizeof(createInfo);
	createInfo.depth_convention = KZGS_PROVIDER_DEPTH_REVERSED;
	CHECK(api.create(&createInfo, &context, &status) == KZGS_PROVIDER_ERROR_CREATE && !context);
	CHECK(status.reason == KZGS_PROVIDER_REASON_SYNCHRONIZATION && createCalls == 0);
	KzgsProviderInitialization initialization{sizeof(initialization), KZGS_PROVIDER_INIT_EXCLUSIVE_DEVICE_IDLE,
		nullptr, runInitialization};
	createInfo.initialization = &initialization;
	CHECK(api.create(&createInfo, &context, &status) == KZGS_PROVIDER_ERROR_CREATE && !context);
	CHECK(status.reason == KZGS_PROVIDER_REASON_UNQUALIFIED && createCalls == 0);
	initialization.guarantees |= KZGS_PROVIDER_INIT_SDK_EXECUTION_QUALIFIED;
	hostMode = 1;
	CHECK(api.create(&createInfo, &context, &status) == KZGS_PROVIDER_ERROR_CREATE && !context);
	CHECK(status.reason == KZGS_PROVIDER_REASON_SYNCHRONIZATION && status.native_code == -71 && createCalls == 0);
	hostMode = 0;
	failInit = true;
	CHECK(api.create(&createInfo, &context, &status) == KZGS_PROVIDER_ERROR_CREATE && !context);
	CHECK(status.reason == KZGS_PROVIDER_REASON_OUT_OF_MEMORY &&
		status.native_code == XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY && destroyCalls == 1 && jitterCalls == 0);
	failInit = false;
	invalidQuality = true;
	CHECK(api.create(&createInfo, &context, &status) == KZGS_PROVIDER_ERROR_CREATE && !context);
	CHECK(status.reason == KZGS_PROVIDER_REASON_UNSUPPORTED_FORMAT && destroyCalls == 2 && initCalls == 1);
	invalidQuality = false;
	CHECK(api.create(&createInfo, &context, &status) == KZGS_PROVIDER_OK && context);
	CHECK(scopeCalls == 4 && createCalls == 3 && initCalls == 2 && jitterCalls == 1);
	CHECK(capturedInit.outputResolution.x == 1920 && capturedInit.outputResolution.y == 1080 &&
		capturedInit.qualitySetting == XESS_QUALITY_SETTING_QUALITY &&
		capturedInit.creationNodeMask == 1 && capturedInit.visibleNodeMask == 1);
	CHECK((capturedInit.initFlags & XESS_INIT_FLAG_INVERTED_DEPTH) &&
		(capturedInit.initFlags & XESS_INIT_FLAG_LDR_INPUT_COLOR) &&
		!(capturedInit.initFlags & (XESS_INIT_FLAG_HIGH_RES_MV | XESS_INIT_FLAG_USE_NDC_VELOCITY |
			XESS_INIT_FLAG_JITTERED_MV | XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE)));

	KzgsProviderSync sync{sizeof(sync), handle<VkCommandBuffer>(5), createInfo.queue, 2, 1, 0};
	KzgsProviderImage color = image(10, createInfo.source_format, createInfo.source_extent,
		VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_USAGE_SAMPLED_BIT);
	KzgsProviderImage depth = image(11, VK_FORMAT_D32_SFLOAT, createInfo.source_extent,
		VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_USAGE_SAMPLED_BIT);
	KzgsProviderImage motion = image(12, VK_FORMAT_R16G16_SFLOAT, createInfo.source_extent,
		VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_USAGE_SAMPLED_BIT);
	KzgsProviderImage output = image(13, createInfo.destination_format, createInfo.destination_extent,
		VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_USAGE_STORAGE_BIT);
	KzgsProviderCamera camera{};
	camera.struct_size = sizeof(camera);
	camera.flags = KZGS_PROVIDER_CAMERA_REVERSED_DEPTH;
	camera.near_plane = 0.1f;
	camera.far_plane = 1000.0f;
	camera.vertical_fov_radians = 1.0f;
	for (int i = 0; i < 16; i += 5)
		camera.view_to_clip[i] = camera.previous_view_to_clip[i] =
			camera.world_to_view[i] = camera.previous_world_to_view[i] = 1.0f;
	KzgsProviderFrame frame{};
	frame.struct_size = sizeof(frame);
	frame.frame_id = frame.presentation_id = 1;
	frame.valid_inputs = KZGS_PROVIDER_INPUT_COLOR | KZGS_PROVIDER_INPUT_DEPTH | KZGS_PROVIDER_INPUT_MOTION |
		KZGS_PROVIDER_INPUT_JITTER | KZGS_PROVIDER_INPUT_CAMERA;
	frame.sync = &sync;
	frame.color = &color;
	frame.depth = &depth;
	frame.motion = &motion;
	frame.output = &output;
	frame.camera = &camera;
	frame.source_rect = createInfo.source_rect;
	frame.destination_rect = {0, 0, 1920, 1080};
	frame.color_encoding = createInfo.color_encoding;
	frame.jitter_x = 0.25f;
	frame.jitter_y = -0.375f;
	frame.motion_scale_x = -640.0f;
	frame.motion_scale_y = 360.0f;
	KzgsProviderResult wrongThread = KZGS_PROVIDER_OK;
	std::thread other([&] {
		KzgsProviderStatus otherStatus{sizeof(otherStatus)};
		wrongThread = api.evaluate(context, &frame, &otherStatus);
	});
	other.join();
	CHECK(wrongThread == KZGS_PROVIDER_ERROR_EVALUATE && barrierCalls == 0 && velocityCalls == 0);
	camera.flags = 0;
	CHECK(api.evaluate(context, &frame, &status) == KZGS_PROVIDER_ERROR_INPUT && barrierCalls == 0);
	camera.flags = KZGS_PROVIDER_CAMERA_REVERSED_DEPTH;
	CHECK(api.evaluate(context, &frame, &status) == KZGS_PROVIDER_OK);
	CHECK(barrierCalls == 2 && velocityCalls == 1 && executeCalls == 1 &&
		velocityX == -640.0f && velocityY == 360.0f);
	CHECK(capturedExecute.resetHistory == 1 && capturedExecute.jitterOffsetX == 0.25f &&
		capturedExecute.jitterOffsetY == -0.375f && capturedExecute.exposureScale == 1.0f);
	CHECK(capturedExecute.inputWidth == 1280 && capturedExecute.inputHeight == 720 &&
		capturedExecute.inputColorBase.x == 10 && capturedExecute.inputDepthBase.y == 20 &&
		capturedExecute.inputMotionVectorBase.x == 10);
	CHECK(capturedExecute.colorTexture.image == color.image && capturedExecute.depthTexture.image == depth.image &&
		capturedExecute.velocityTexture.image == motion.image && capturedExecute.outputTexture.image == output.image);
	frame.frame_id = frame.presentation_id = 2;
	sync.submission_token = 2;
	sync.completed_token = 1;
	frame.motion_scale_x = 1.0f;
	frame.motion_scale_y = -1.0f;
	CHECK(api.evaluate(context, &frame, &status) == KZGS_PROVIDER_OK);
	CHECK(capturedExecute.resetHistory == 0 && velocityX == 1.0f && velocityY == -1.0f);
	frame.frame_id = frame.presentation_id = 3;
	sync.submission_token = 3;
	sync.completed_token = 2;
	failVelocity = true;
	CHECK(api.evaluate(context, &frame, &status) == KZGS_PROVIDER_ERROR_EVALUATE);
	CHECK(status.reason == KZGS_PROVIDER_REASON_OUT_OF_MEMORY && executeCalls == 2 && barrierCalls == 6);
	failVelocity = false;
	frame.frame_id = frame.presentation_id = 4;
	sync.submission_token = 4;
	sync.completed_token = 3;
	failExecute = true;
	CHECK(api.evaluate(context, &frame, &status) == KZGS_PROVIDER_ERROR_EVALUATE);
	CHECK(status.reason == KZGS_PROVIDER_REASON_OUT_OF_MEMORY && capturedExecute.resetHistory == 1 && barrierCalls == 8);
	failExecute = false;
	frame.frame_id = frame.presentation_id = 5;
	sync.submission_token = 5;
	sync.completed_token = 4;
	CHECK(api.evaluate(context, &frame, &status) == KZGS_PROVIDER_OK);
	CHECK(capturedExecute.resetHistory == 1 && barrierCalls == 10);
	KzgsProviderDestroyInfo destroyInfo{sizeof(destroyInfo), KZGS_PROVIDER_DESTROY_COMPLETED, 5};
	api.destroy(context, &destroyInfo);
	CHECK(destroyCalls == 3);

	createInfo.source_format = createInfo.destination_format = VK_FORMAT_R16G16B16A16_SFLOAT;
	createInfo.color_encoding = KZGS_PROVIDER_COLOR_LINEAR_HDR;
	createInfo.depth_convention = KZGS_PROVIDER_DEPTH_FORWARD;
	CHECK(api.create(&createInfo, &context, &status) == KZGS_PROVIDER_OK && context);
	CHECK((capturedInit.initFlags & XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE) &&
		!(capturedInit.initFlags & (XESS_INIT_FLAG_LDR_INPUT_COLOR | XESS_INIT_FLAG_INVERTED_DEPTH)));
	api.destroy(context, &destroyInfo);
	CHECK(destroyCalls == 4);

	hostMode = 2;
	CHECK(api.create(&createInfo, &context, &status) == KZGS_PROVIDER_ERROR_SYNCHRONIZATION_UNRESOLVED && !context);
	CHECK(status.reason == KZGS_PROVIDER_REASON_SYNCHRONIZATION && status.native_code == -72 && destroyCalls == 4);
	hostMode = 0;
	const int callsAfterAbandon = createCalls;
	CHECK(api.create(&createInfo, &context, &status) == KZGS_PROVIDER_ERROR_DEVICE_LOST && !context);
	CHECK(createCalls == callsAfterAbandon && destroyCalls == 4);
	CHECK(featureQueries >= 3);
	if (!failures)
		std::puts("XeSS adapter: PASS (test double only; no Intel SDK or GPU)");
	return failures ? 1 : 0;
}
