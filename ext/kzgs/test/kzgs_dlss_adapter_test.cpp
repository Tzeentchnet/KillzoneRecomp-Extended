// The real adapter compiled against a fake NGX API. No NVIDIA SDK or GPU is involved.
// SPDX-License-Identifier: GPL-3.0+
#include "kzgs_upscaler_provider.h"
#include <nvsdk_ngx_helpers_vk.h>

#include <cstdio>
#include <cstring>
#include <type_traits>

namespace
{
	int failures = 0;
	int initCalls = 0;
	int createCalls = 0;
	int evalCalls = 0;
	int releaseCalls = 0;
	int destroyParamsCalls = 0;
	int shutdownCalls = 0;
	int imageBarriers = 0;
	int memoryBarriers = 0;
	int initializationScopes = 0;
	bool unsupported = false;
	bool failEval = false;
	bool failCreate = false;
	int createFlags = 0;
	NVSDK_NGX_VK_DLSS_Eval_Params captured{};
	NVSDK_NGX_Resource_VK capturedColor{}, capturedOutput{}, capturedDepth{}, capturedMotion{};
	NVSDK_NGX_Parameter* const fakeParameters = reinterpret_cast<NVSDK_NGX_Parameter*>(uintptr_t{77});
	NVSDK_NGX_Handle* const fakeFeature = reinterpret_cast<NVSDK_NGX_Handle*>(uintptr_t{78});

#define CHECK(expression) do { if (!(expression)) { \
	std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression); ++failures; } } while (0)
	template <typename T> T handle(uintptr_t value)
	{
		if constexpr (std::is_pointer_v<T>)
			return reinterpret_cast<T>(value);
		else
			return static_cast<T>(value);
	}

	void VKAPI_CALL barrier(VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags, VkDependencyFlags,
		uint32_t memoryCount, const VkMemoryBarrier*, uint32_t, const VkBufferMemoryBarrier*,
		uint32_t imageCount, const VkImageMemoryBarrier* barriers)
	{
		if (memoryCount)
			++memoryBarriers;
		if (imageCount)
		{
			CHECK(imageCount == 4 && barriers[0].newLayout == VK_IMAGE_LAYOUT_GENERAL);
			++imageBarriers;
		}
	}

	PFN_vkVoidFunction VKAPI_CALL deviceProc(VkDevice, const char* name)
	{
		return std::strcmp(name, "vkCmdPipelineBarrier") == 0 ? reinterpret_cast<PFN_vkVoidFunction>(barrier) : nullptr;
	}
	PFN_vkVoidFunction VKAPI_CALL instanceProc(VkInstance, const char*) { return nullptr; }

	KzgsProviderResult KZGS_PROVIDER_CALL runInitialization(void*, KzgsProviderInitializationWork work,
		void* workData, KzgsProviderStatus*)
	{
		++initializationScopes;
		work(workData);
		return KZGS_PROVIDER_OK;
	}

	KzgsProviderImage image(uintptr_t number, VkFormat format, VkExtent2D extent, VkImageAspectFlags aspect, VkImageUsageFlags usage)
	{
		KzgsProviderImage value{};
		value.struct_size = sizeof(value);
		value.image = handle<VkImage>(number);
		value.view = handle<VkImageView>(number + 100);
		value.format = format;
		value.extent = extent;
		value.subresource = {aspect, 0, 1, 0, 1};
		value.layout = VK_IMAGE_LAYOUT_GENERAL;
		value.usage = usage;
		value.queue_family = 2;
		value.available_stages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		value.available_access = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		return value;
	}
}

extern "C" NVSDK_NGX_Result NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(
	const NVSDK_NGX_FeatureDiscoveryInfo* discovery, uint32_t* count, VkExtensionProperties** properties)
{
	static VkExtensionProperties value{VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, 1};
	CHECK(discovery && discovery->FeatureID == NVSDK_NGX_Feature_SuperSampling);
	*count = 1;
	*properties = &value;
	return NVSDK_NGX_Result_Success;
}
extern "C" NVSDK_NGX_Result NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(
	VkInstance, VkPhysicalDevice, const NVSDK_NGX_FeatureDiscoveryInfo*, uint32_t* count, VkExtensionProperties** properties)
{
	static VkExtensionProperties value{VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME, 1};
	*count = 1;
	*properties = &value;
	return NVSDK_NGX_Result_Success;
}
extern "C" NVSDK_NGX_Result NVSDK_NGX_VULKAN_GetFeatureRequirements(
	VkInstance, VkPhysicalDevice, const NVSDK_NGX_FeatureDiscoveryInfo*, NVSDK_NGX_FeatureRequirement* support)
{
	support->FeatureSupported = static_cast<NVSDK_NGX_Feature_Support_Result>(unsupported ? 4 : 0);
	return NVSDK_NGX_Result_Success;
}
extern "C" NVSDK_NGX_Result NVSDK_NGX_VULKAN_Init_with_ProjectID(const char* project, NVSDK_NGX_EngineType, const char*,
	const wchar_t* data, VkInstance, VkPhysicalDevice, VkDevice, PFN_vkGetInstanceProcAddr, PFN_vkGetDeviceProcAddr,
	const NVSDK_NGX_FeatureCommonInfo*, NVSDK_NGX_Version)
{
	CHECK(project && std::strlen(project) == 36 && data && *data);
	++initCalls;
	return NVSDK_NGX_Result_Success;
}
extern "C" NVSDK_NGX_Result NVSDK_NGX_VULKAN_GetCapabilityParameters(NVSDK_NGX_Parameter** out)
{
	*out = fakeParameters;
	return NVSDK_NGX_Result_Success;
}
extern "C" NVSDK_NGX_Result NVSDK_NGX_Parameter_GetI(NVSDK_NGX_Parameter*, const char*, int* out)
{
	*out = 1;
	return NVSDK_NGX_Result_Success;
}
extern "C" NVSDK_NGX_Result NGX_DLSS_GET_OPTIMAL_SETTINGS(NVSDK_NGX_Parameter*, unsigned int, unsigned int,
	NVSDK_NGX_PerfQuality_Value, unsigned int* width, unsigned int* height, unsigned int* maxWidth,
	unsigned int* maxHeight, unsigned int* minWidth, unsigned int* minHeight, float* sharpness)
{
	*width = 1200; *height = 680; *maxWidth = 1600; *maxHeight = 900;
	*minWidth = 640; *minHeight = 360; *sharpness = 0;
	return NVSDK_NGX_Result_Success;
}
extern "C" NVSDK_NGX_Resource_VK NVSDK_NGX_Create_ImageView_Resource_VK(VkImageView view, VkImage image,
	VkImageSubresourceRange subresource, VkFormat format, unsigned int width, unsigned int height, bool writable)
{
	return {view, image, subresource, format, width, height, writable};
}
extern "C" NVSDK_NGX_Result NGX_VULKAN_CREATE_DLSS_EXT1(VkDevice, VkCommandBuffer,
	unsigned int, unsigned int, NVSDK_NGX_Handle** out, NVSDK_NGX_Parameter*, NVSDK_NGX_DLSS_Create_Params* params)
{
	++createCalls;
	createFlags = params->InFeatureCreateFlags;
	CHECK(params->Feature.InWidth == 1200 && params->Feature.InHeight == 680);
	*out = fakeFeature;
	return failCreate ? NVSDK_NGX_Result_FAIL_OutOfGPUMemory : NVSDK_NGX_Result_Success;
}
extern "C" NVSDK_NGX_Result NGX_VULKAN_EVALUATE_DLSS_EXT(VkCommandBuffer, NVSDK_NGX_Handle*,
	NVSDK_NGX_Parameter*, NVSDK_NGX_VK_DLSS_Eval_Params* params)
{
	++evalCalls;
	captured = *params;
	capturedColor = *params->Feature.pInColor;
	capturedOutput = *params->Feature.pInOutput;
	capturedDepth = *params->pInDepth;
	capturedMotion = *params->pInMotionVectors;
	return failEval ? NVSDK_NGX_Result_FAIL_OutOfGPUMemory : NVSDK_NGX_Result_Success;
}
extern "C" NVSDK_NGX_Result NVSDK_NGX_VULKAN_ReleaseFeature(NVSDK_NGX_Handle*) { ++releaseCalls; return NVSDK_NGX_Result_Success; }
extern "C" NVSDK_NGX_Result NVSDK_NGX_VULKAN_DestroyParameters(NVSDK_NGX_Parameter*) { ++destroyParamsCalls; return NVSDK_NGX_Result_Success; }
extern "C" NVSDK_NGX_Result NVSDK_NGX_VULKAN_Shutdown1(VkDevice) { ++shutdownCalls; return NVSDK_NGX_Result_Success; }

int main()
{
	KzgsProviderApi api{sizeof(api)};
	KzgsProviderStatus status{sizeof(status)};
	CHECK(kzgs_upscaler_get_api(KZGS_PROVIDER_ABI_VERSION, &api, &status) == KZGS_PROVIDER_OK);
	KzgsProviderInfo info{sizeof(info)};
	CHECK(api.query_info(&info, &status) == KZGS_PROVIDER_OK && info.kind == KZGS_PROVIDER_KIND_DLSS);
	KzgsProviderRequirements requirements{sizeof(requirements)};
	CHECK(api.instance_requirements(&requirements, &status) == KZGS_PROVIDER_OK && requirements.extension_count == 1);
	KzgsProviderDeviceQuery query{sizeof(query), handle<VkInstance>(1), handle<VkPhysicalDevice>(2), instanceProc};
	unsupported = true;
	CHECK(api.device_requirements(&query, &requirements, &status) == KZGS_PROVIDER_ERROR_UNSUPPORTED);
	CHECK(status.reason == KZGS_PROVIDER_REASON_UNSUPPORTED_DEVICE && status.native_code == 4);
	unsupported = false;
	CHECK(api.device_requirements(&query, &requirements, &status) == KZGS_PROVIDER_OK && requirements.extension_count == 1);
	CHECK(requirements.core_features.shaderStorageImageWriteWithoutFormat == VK_TRUE);

	KzgsProviderCreateInfo createInfo{};
	createInfo.struct_size = sizeof(createInfo);
	createInfo.instance = query.instance;
	createInfo.physical_device = query.physical_device;
	createInfo.device = handle<VkDevice>(3);
	createInfo.queue = handle<VkQueue>(4);
	createInfo.queue_family = 2;
	createInfo.get_instance_proc_addr = instanceProc;
	createInfo.get_device_proc_addr = deviceProc;
	createInfo.source_extent = {1280, 720};
	createInfo.source_rect = {10, 20, 1210, 700};
	createInfo.destination_extent = {1920, 1080};
	createInfo.source_format = createInfo.destination_format = VK_FORMAT_R8G8B8A8_UNORM;
	createInfo.color_encoding = KZGS_PROVIDER_COLOR_LINEAR_SDR;
	void* context = nullptr;
	createInfo.struct_size = static_cast<uint32_t>(offsetof(KzgsProviderCreateInfo, initialization));
	CHECK(api.create(&createInfo, &context, &status) == KZGS_PROVIDER_ERROR_CREATE && !context);
	CHECK(status.reason == KZGS_PROVIDER_REASON_MISSING_INPUT && initCalls == 0);
	createInfo.struct_size = sizeof(createInfo);
	createInfo.depth_convention = KZGS_PROVIDER_DEPTH_REVERSED;
	CHECK(api.create(&createInfo, &context, &status) == KZGS_PROVIDER_ERROR_CREATE && !context);
	CHECK(status.reason == KZGS_PROVIDER_REASON_SYNCHRONIZATION && initCalls == 0);
	KzgsProviderInitialization initialization{sizeof(initialization), KZGS_PROVIDER_INIT_EXCLUSIVE_DEVICE_IDLE,
		nullptr, runInitialization};
	createInfo.initialization = &initialization;
	CHECK(api.create(&createInfo, &context, &status) == KZGS_PROVIDER_ERROR_CREATE && !context);
	CHECK(status.reason == KZGS_PROVIDER_REASON_UNQUALIFIED && initCalls == 0);
	initialization.guarantees |= KZGS_PROVIDER_INIT_SDK_EXECUTION_QUALIFIED |
		KZGS_PROVIDER_INIT_NGX_GENERAL_LAYOUT_QUALIFIED;
	CHECK(api.create(&createInfo, &context, &status) == KZGS_PROVIDER_OK && context);
	CHECK(initCalls == 1 && initializationScopes == 1 && createCalls == 0 && imageBarriers == 0);
	KzgsProviderSync sync{sizeof(sync), handle<VkCommandBuffer>(5), createInfo.queue, 2, 1, 0};
	KzgsProviderImage color = image(10, createInfo.source_format, createInfo.source_extent, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_USAGE_SAMPLED_BIT);
	KzgsProviderImage depth = image(11, VK_FORMAT_D32_SFLOAT, createInfo.source_extent, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_USAGE_SAMPLED_BIT);
	KzgsProviderImage motion = image(12, VK_FORMAT_R16G16_SFLOAT, createInfo.source_extent, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_USAGE_SAMPLED_BIT);
	KzgsProviderImage output = image(13, createInfo.destination_format, createInfo.destination_extent, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_USAGE_STORAGE_BIT);
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
	frame.color = &color; frame.output = &output; frame.depth = &depth; frame.motion = &motion; frame.camera = &camera;
	frame.source_rect = createInfo.source_rect;
	frame.destination_rect = {0, 0, 1920, 1080};
	frame.color_encoding = createInfo.color_encoding;
	frame.jitter_x = 0.25f; frame.jitter_y = -0.375f;
	frame.motion_scale_x = -640.0f; frame.motion_scale_y = 360.0f;
	camera.flags = 0;
	CHECK(api.evaluate(context, &frame, &status) == KZGS_PROVIDER_ERROR_INPUT && imageBarriers == 0);
	camera.flags = KZGS_PROVIDER_CAMERA_REVERSED_DEPTH;
	CHECK(api.evaluate(context, &frame, &status) == KZGS_PROVIDER_OK);
	CHECK(createCalls == 1 && evalCalls == 1 && imageBarriers == 2 && memoryBarriers == 3);
	CHECK((createFlags & NVSDK_NGX_DLSS_Feature_Flags_MVLowRes) &&
		(createFlags & NVSDK_NGX_DLSS_Feature_Flags_DepthInverted) &&
		(createFlags & NVSDK_NGX_DLSS_Feature_Flags_AutoExposure) &&
		!(createFlags & NVSDK_NGX_DLSS_Feature_Flags_MVJittered));
	CHECK(captured.InReset == 1 && captured.InJitterOffsetX == 0.25f && captured.InJitterOffsetY == -0.375f);
	CHECK(captured.InMVScaleX == -640.0f && captured.InMVScaleY == 360.0f);
	CHECK(captured.InColorSubrectBase.X == 10 && captured.InDepthSubrectBase.Y == 20 &&
		captured.InMVSubrectBase.X == 10 && captured.InRenderSubrectDimensions.Width == 1200);
	CHECK(capturedColor.image == color.image && capturedDepth.image == depth.image &&
		capturedMotion.image == motion.image && capturedOutput.image == output.image && capturedOutput.writable);

	frame.frame_id = frame.presentation_id = 2;
	sync.submission_token = 2; sync.completed_token = 1;
	CHECK(api.evaluate(context, &frame, &status) == KZGS_PROVIDER_OK && captured.InReset == 0);
	KzgsProviderResetInfo resetInfo{sizeof(resetInfo), KZGS_PROVIDER_RESET_GAME, 2};
	CHECK(api.reset(context, &resetInfo, &status) == KZGS_PROVIDER_OK);
	frame.frame_id = frame.presentation_id = 3;
	sync.submission_token = 3; sync.completed_token = 2;
	failEval = true;
	CHECK(api.evaluate(context, &frame, &status) == KZGS_PROVIDER_ERROR_EVALUATE);
	CHECK(status.reason == KZGS_PROVIDER_REASON_OUT_OF_MEMORY && captured.InReset == 1 && imageBarriers == 6);
	failEval = false;
	void* second = nullptr;
	CHECK(api.create(&createInfo, &second, &status) == KZGS_PROVIDER_OK && second && initCalls == 1);
	KzgsProviderCreateInfo otherDevice = createInfo;
	otherDevice.device = handle<VkDevice>(33);
	void* third = nullptr;
	CHECK(api.create(&otherDevice, &third, &status) == KZGS_PROVIDER_ERROR_UNSUPPORTED && !third && initCalls == 1);
	KzgsProviderDestroyInfo destroyInfo{sizeof(destroyInfo), KZGS_PROVIDER_DESTROY_COMPLETED, 3};
	api.destroy(context, &destroyInfo);
	CHECK(releaseCalls == 1 && destroyParamsCalls == 1 && shutdownCalls == 0);
	api.destroy(second, &destroyInfo);
	CHECK(releaseCalls == 1 && destroyParamsCalls == 2 && shutdownCalls == 1);

	CHECK(api.create(&createInfo, &context, &status) == KZGS_PROVIDER_OK);
	frame.frame_id = frame.presentation_id = 4;
	sync.submission_token = 4; sync.completed_token = 3;
	failCreate = true;
	CHECK(api.evaluate(context, &frame, &status) == KZGS_PROVIDER_ERROR_EVALUATE && createCalls == 2);
	failCreate = false;
	frame.frame_id = frame.presentation_id = 5;
	sync.submission_token = 5; sync.completed_token = 4;
	CHECK(api.evaluate(context, &frame, &status) == KZGS_PROVIDER_ERROR_EVALUATE && createCalls == 2);
	destroyInfo.completed_token = 5;
	api.destroy(context, &destroyInfo);
	CHECK(releaseCalls == 2 && destroyParamsCalls == 3 && shutdownCalls == 2);

	CHECK(api.create(&createInfo, &context, &status) == KZGS_PROVIDER_OK);
	destroyInfo.reason = KZGS_PROVIDER_DESTROY_DEVICE_LOST;
	api.destroy(context, &destroyInfo);
	CHECK(releaseCalls == 2 && destroyParamsCalls == 3 && shutdownCalls == 2);
	CHECK(api.create(&createInfo, &context, &status) == KZGS_PROVIDER_ERROR_DEVICE_LOST && !context);
	if (!failures)
		std::puts("DLSS adapter: PASS (test double only; no NVIDIA SDK or GPU)");
	return failures ? 1 : 0;
}
