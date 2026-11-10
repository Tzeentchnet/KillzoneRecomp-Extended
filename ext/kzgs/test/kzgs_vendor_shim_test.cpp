// Provider-independent ABI tests using a deliberately fake SDK backend.
// SPDX-License-Identifier: GPL-3.0+
#include "kzgs_vendor_backend.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>

namespace
{
	int failures = 0;
	int calls = 0;
	int barrierCalls = 0;
	bool lastReset = false;
	bool lostDestroy = false;
	bool failEvaluate = false;
	bool failCreate = false;
	bool failDestroy = false;
	int destroyCalls = 0;
	float lastJitterX = 0;
	float lastJitterY = 0;
	float lastScaleX = 0;
	float lastScaleY = 0;
	uint32_t lastDepthFlags = 0;
	int initializationCalls = 0;
	int hostMode = 0;
	bool initializationFails = false;

#define CHECK(expression) do { if (!(expression)) { \
	std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression); ++failures; } } while (0)

	template <typename T> T handle(uintptr_t value)
	{
		if constexpr (std::is_pointer_v<T>)
			return reinterpret_cast<T>(value);
		else
			return static_cast<T>(value);
	}

	void VKAPI_CALL barrier(VkCommandBuffer, VkPipelineStageFlags sourceStages, VkPipelineStageFlags destinationStages,
		VkDependencyFlags, uint32_t memoryCount, const VkMemoryBarrier* memory, uint32_t, const VkBufferMemoryBarrier*,
		uint32_t count, const VkImageMemoryBarrier* images)
	{
		CHECK((sourceStages & VK_PIPELINE_STAGE_ALL_COMMANDS_BIT) &&
			(destinationStages & VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
		CHECK(memoryCount == 1 && memory && memory[0].srcAccessMask == VK_ACCESS_MEMORY_WRITE_BIT &&
			(memory[0].dstAccessMask & (VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT)) ==
				(VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT));
		CHECK(count == 4);
		CHECK(images[0].image != VK_NULL_HANDLE);
		CHECK(images[3].newLayout == VK_IMAGE_LAYOUT_GENERAL);
		if ((barrierCalls % 2) == 0)
			CHECK(images[0].newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		else
			CHECK(images[0].newLayout == VK_IMAGE_LAYOUT_GENERAL);
		++barrierCalls;
	}

	PFN_vkVoidFunction VKAPI_CALL deviceProc(VkDevice, const char* name)
	{
		return std::strcmp(name, "vkCmdPipelineBarrier") == 0 ? reinterpret_cast<PFN_vkVoidFunction>(barrier) : nullptr;
	}

	PFN_vkVoidFunction VKAPI_CALL instanceProc(VkInstance, const char*) { return nullptr; }

	kzgs::vendor::Result instanceRequirements(kzgs::vendor::Requirements& out)
	{
		out.names.emplace_back("VK_KZGS_test_instance");
		return {};
	}

	kzgs::vendor::Result deviceRequirements(const KzgsProviderDeviceQuery&, kzgs::vendor::Requirements& out)
	{
		out.names.emplace_back("VK_KZGS_test_device");
		out.core.shaderStorageImageWriteWithoutFormat = VK_TRUE;
		const VkBool32 bits[] = {VK_TRUE, VK_FALSE, VK_FALSE, VK_FALSE};
		out.addFeature(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES, bits, 4);
		return {};
	}

	kzgs::vendor::Result create(const KzgsProviderCreateInfo&, void** out)
	{
		*out = &calls;
		return failCreate ? kzgs::vendor::failure(KZGS_PROVIDER_ERROR_CREATE,
			KZGS_PROVIDER_REASON_PROVIDER_FAILURE, "Deliberate partial creation failure.") : kzgs::vendor::Result{};
	}

	kzgs::vendor::Result initialize(void*)
	{
		++initializationCalls;
		return initializationFails ? kzgs::vendor::failure(KZGS_PROVIDER_ERROR_CREATE,
			KZGS_PROVIDER_REASON_OUT_OF_MEMORY, "Fake SDK initialization failed.", -31) : kzgs::vendor::Result{};
	}

	KzgsProviderResult KZGS_PROVIDER_CALL runInitialization(void*, KzgsProviderInitializationWork work,
		void* workData, KzgsProviderStatus* status)
	{
		if (hostMode == 1)
		{
			status->native_code = -12;
			return KZGS_PROVIDER_ERROR_CREATE;
		}
		if (hostMode != 2)
			work(workData);
		if (hostMode == 3)
		{
			status->native_code = -13;
			return KZGS_PROVIDER_ERROR_CREATE;
		}
		if (hostMode == 4)
			work(workData);
		return KZGS_PROVIDER_OK;
	}

	kzgs::vendor::Result evaluate(void*, const KzgsProviderFrame& frame, bool reset)
	{
		++calls;
		lastReset = reset;
		lastJitterX = frame.jitter_x;
		lastJitterY = frame.jitter_y;
		lastScaleX = frame.motion_scale_x;
		lastScaleY = frame.motion_scale_y;
		lastDepthFlags = frame.camera->flags;
		if (failEvaluate)
			return kzgs::vendor::failure(KZGS_PROVIDER_ERROR_EVALUATE, KZGS_PROVIDER_REASON_PROVIDER_FAILURE,
				"Deliberate double failure.", -42);
		return {};
	}

	void destroy(void*, bool lost)
	{
		++destroyCalls;
		lostDestroy = lost;
		if (failDestroy)
			throw 1;
	}

	const kzgs::vendor::Backend fakeBackend{
		KZGS_PROVIDER_KIND_DLSS, "test double (not an SDK)", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		instanceRequirements, deviceRequirements, create, evaluate, destroy};

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

namespace kzgs::vendor { const Backend& backend() { return fakeBackend; } }

int main()
{
	KzgsProviderStatus status{sizeof(status)};
	KzgsProviderApi api{sizeof(api)};
	CHECK(kzgs_upscaler_get_api(KZGS_PROVIDER_VERSION(2, 0), &api, &status) == KZGS_PROVIDER_ERROR_VERSION);
	CHECK(kzgs_upscaler_get_api(KZGS_PROVIDER_VERSION(1, 0), &api, &status) == KZGS_PROVIDER_OK);
	CHECK(api.abi_version == KZGS_PROVIDER_VERSION(1, 0));
	api.struct_size = sizeof(api);
	CHECK(kzgs_upscaler_get_api(KZGS_PROVIDER_VERSION(1, 9), &api, &status) == KZGS_PROVIDER_OK);
	CHECK(api.abi_version == KZGS_PROVIDER_ABI_VERSION);
	api.struct_size = sizeof(api);
	CHECK(kzgs_upscaler_get_api(KZGS_PROVIDER_ABI_VERSION, &api, &status) == KZGS_PROVIDER_OK);
	KzgsProviderInfo info{sizeof(info)};
	CHECK(api.query_info(&info, &status) == KZGS_PROVIDER_OK);
	const KzgsProviderInputs required = KZGS_PROVIDER_INPUT_COLOR | KZGS_PROVIDER_INPUT_DEPTH |
		KZGS_PROVIDER_INPUT_MOTION | KZGS_PROVIDER_INPUT_JITTER | KZGS_PROVIDER_INPUT_CAMERA;
	CHECK(info.required_inputs == required);
	CHECK(info.capabilities == KZGS_PROVIDER_CAP_TEMPORAL);
	CHECK(!(info.supported_color_encodings & KZGS_PROVIDER_COLOR_BIT(KZGS_PROVIDER_COLOR_UNKNOWN)));

	struct SmallInfo { uint32_t size; uint32_t canary; } small{sizeof(uint32_t), 0x12345678};
	CHECK(api.query_info(reinterpret_cast<KzgsProviderInfo*>(&small), nullptr) == KZGS_PROVIDER_OK);
	CHECK(small.size == sizeof(KzgsProviderInfo) && small.canary == 0x12345678);

	KzgsProviderRequirements req{sizeof(req)};
	CHECK(api.instance_requirements(&req, &status) == KZGS_PROVIDER_OK);
	CHECK(req.extension_count == 1 && std::strcmp(req.extensions[0], "VK_KZGS_test_instance") == 0);
	KzgsProviderDeviceQuery query{sizeof(query), handle<VkInstance>(1), handle<VkPhysicalDevice>(2), instanceProc};
	req = {sizeof(req)};
	CHECK(api.device_requirements(&query, &req, &status) == KZGS_PROVIDER_OK);
	CHECK(req.core_features.shaderStorageImageWriteWithoutFormat && req.feature_block_count == 1);
	CHECK(req.feature_blocks[0].required[0] == VK_TRUE && req.feature_blocks[0].required[1] == VK_FALSE);

	KzgsProviderCreateInfo createInfo{};
	createInfo.struct_size = sizeof(createInfo);
	createInfo.instance = handle<VkInstance>(1);
	createInfo.physical_device = handle<VkPhysicalDevice>(2);
	createInfo.device = handle<VkDevice>(3);
	createInfo.queue = handle<VkQueue>(4);
	createInfo.queue_family = 2;
	createInfo.get_instance_proc_addr = instanceProc;
	createInfo.get_device_proc_addr = deviceProc;
	createInfo.source_extent = {1280, 720};
	createInfo.source_rect = {10, 20, 1210, 700};
	createInfo.source_format = VK_FORMAT_R8G8B8A8_UNORM;
	createInfo.destination_extent = {1920, 1080};
	createInfo.destination_format = VK_FORMAT_R8G8B8A8_UNORM;
	createInfo.color_encoding = KZGS_PROVIDER_COLOR_UNKNOWN;
	void* context = nullptr;
	CHECK(api.create(&createInfo, &context, &status) == KZGS_PROVIDER_ERROR_INPUT && !context);
	createInfo.color_encoding = KZGS_PROVIDER_COLOR_LINEAR_SDR;
	constexpr size_t legacySize = offsetof(KzgsProviderCreateInfo, initialization);
	struct alignas(KzgsProviderCreateInfo) LegacyCreate
	{
		unsigned char bytes[legacySize];
		uint64_t canary;
	} legacy{};
	std::memcpy(legacy.bytes, &createInfo, legacySize);
	reinterpret_cast<KzgsProviderCreateInfo*>(legacy.bytes)->struct_size = static_cast<uint32_t>(legacySize);
	legacy.canary = UINT64_C(0x123456789abcdef0);
	CHECK(api.create(reinterpret_cast<const KzgsProviderCreateInfo*>(legacy.bytes), &context, &status) ==
		KZGS_PROVIDER_OK && context);
	KzgsProviderDestroyInfo legacyDestroy{sizeof(legacyDestroy), KZGS_PROVIDER_DESTROY_COMPLETED, 0};
	api.destroy(context, &legacyDestroy);
	CHECK(legacy.canary == UINT64_C(0x123456789abcdef0));
	context = nullptr;

	const uint32_t qualification = KZGS_PROVIDER_INIT_SDK_EXECUTION_QUALIFIED |
		KZGS_PROVIDER_INIT_NGX_GENERAL_LAYOUT_QUALIFIED;
	auto initResult = kzgs::vendor::runInitialization(createInfo, qualification, initialize, nullptr);
	CHECK(initResult.code == KZGS_PROVIDER_ERROR_CREATE &&
		initResult.reason == KZGS_PROVIDER_REASON_SYNCHRONIZATION && initializationCalls == 0);
	KzgsProviderInitialization initialization{sizeof(initialization), KZGS_PROVIDER_INIT_EXCLUSIVE_DEVICE_IDLE,
		nullptr, runInitialization};
	createInfo.initialization = &initialization;
	initResult = kzgs::vendor::runInitialization(createInfo, qualification, initialize, nullptr);
	CHECK(initResult.reason == KZGS_PROVIDER_REASON_UNQUALIFIED && initializationCalls == 0);
	initialization.guarantees |= qualification;
	initialization.struct_size = static_cast<uint32_t>(offsetof(KzgsProviderInitialization, run_exclusive));
	initResult = kzgs::vendor::runInitialization(createInfo, qualification, initialize, nullptr);
	CHECK(initResult.reason == KZGS_PROVIDER_REASON_SYNCHRONIZATION && initializationCalls == 0);
	initialization.struct_size = sizeof(initialization);
	hostMode = 1;
	initResult = kzgs::vendor::runInitialization(createInfo, qualification, initialize, nullptr);
	CHECK(initResult.reason == KZGS_PROVIDER_REASON_SYNCHRONIZATION && initResult.native == -12 && initializationCalls == 0);
	hostMode = 2;
	initResult = kzgs::vendor::runInitialization(createInfo, qualification, initialize, nullptr);
	CHECK(initResult.reason == KZGS_PROVIDER_REASON_SYNCHRONIZATION && initializationCalls == 0);
	hostMode = 0;
	initializationFails = true;
	initResult = kzgs::vendor::runInitialization(createInfo, qualification, initialize, nullptr);
	CHECK(initResult.reason == KZGS_PROVIDER_REASON_OUT_OF_MEMORY && initResult.native == -31 && initializationCalls == 1);
	initializationFails = false;
	initResult = kzgs::vendor::runInitialization(createInfo, qualification, initialize, nullptr);
	CHECK(initResult.code == KZGS_PROVIDER_OK && initializationCalls == 2);
	hostMode = 3;
	initResult = kzgs::vendor::runInitialization(createInfo, qualification, initialize, nullptr);
	CHECK(initResult.code == KZGS_PROVIDER_ERROR_DEVICE_LOST && initResult.native == -13 && initializationCalls == 3);
	hostMode = 4;
	initResult = kzgs::vendor::runInitialization(createInfo, qualification, initialize, nullptr);
	CHECK(initResult.code == KZGS_PROVIDER_ERROR_DEVICE_LOST && initializationCalls == 4);
	hostMode = 0;
	createInfo.initialization = nullptr;
	CHECK(api.create(&createInfo, &context, &status) == KZGS_PROVIDER_OK && context);
	CHECK(barrierCalls == 0 && calls == 0);

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
	frame.frame_id = 1;
	frame.presentation_id = 1;
	frame.valid_inputs = KZGS_PROVIDER_INPUT_COLOR;
	frame.sync = &sync;
	frame.color = &color;
	frame.output = &output;
	frame.source_rect = createInfo.source_rect;
	frame.destination_rect = {0, 0, 1920, 1080};
	frame.color_encoding = createInfo.color_encoding;
	CHECK(api.evaluate(context, &frame, &status) == KZGS_PROVIDER_ERROR_INPUT);
	CHECK(status.reason == KZGS_PROVIDER_REASON_MISSING_INPUT && calls == 0 && barrierCalls == 0);
	frame.valid_inputs = required;
	frame.depth = &depth;
	frame.motion = &motion;
	frame.camera = &camera;
	frame.jitter_x = 0.25f;
	frame.jitter_y = -0.375f;
	frame.motion_scale_x = -640.0f;
	frame.motion_scale_y = 360.0f;
	camera.far_plane = 0.0f;
	CHECK(api.evaluate(context, &frame, &status) == KZGS_PROVIDER_ERROR_INPUT && calls == 0);
	camera.far_plane = 1000.0f;
	frame.jitter_x = std::numeric_limits<float>::quiet_NaN();
	CHECK(api.evaluate(context, &frame, &status) == KZGS_PROVIDER_ERROR_INPUT && calls == 0);
	frame.jitter_x = 0.25f;
	motion.format = VK_FORMAT_R8G8B8A8_UNORM;
	CHECK(api.evaluate(context, &frame, &status) == KZGS_PROVIDER_ERROR_INPUT && calls == 0);
	motion.format = VK_FORMAT_R16G16_SFLOAT;
	depth.format = VK_FORMAT_D24_UNORM_S8_UINT;
	CHECK(api.evaluate(context, &frame, &status) == KZGS_PROVIDER_ERROR_INPUT && calls == 0);
	depth.format = VK_FORMAT_D32_SFLOAT;
	CHECK(api.evaluate(context, &frame, &status) == KZGS_PROVIDER_OK);
	CHECK(calls == 1 && barrierCalls == 2 && lastReset);
	CHECK(lastJitterX == 0.25f && lastJitterY == -0.375f);
	CHECK(lastScaleX == -640.0f && lastScaleY == 360.0f && lastDepthFlags == camera.flags);
	frame.frame_id = 2;
	CHECK(api.evaluate(context, &frame, &status) == KZGS_PROVIDER_ERROR_ARGUMENT && calls == 1);
	frame.frame_id = 1;
	CHECK(api.evaluate(context, &frame, &status) == KZGS_PROVIDER_ERROR_ARGUMENT && calls == 1);
	KzgsProviderResetInfo resetInfo{sizeof(resetInfo), KZGS_PROVIDER_RESET_PRESENT_FAILED, 1};
	CHECK(api.reset(context, &resetInfo, &status) == KZGS_PROVIDER_OK);
	frame.frame_id = 2;
	// A failed presentation can reuse its candidate ID after history is reset.
	frame.presentation_id = 1;
	sync.submission_token = 2;
	sync.completed_token = 1;
	failEvaluate = true;
	CHECK(api.evaluate(context, &frame, &status) == KZGS_PROVIDER_ERROR_EVALUATE);
	CHECK(lastReset && status.native_code == -42 && barrierCalls == 4);
	failEvaluate = false;
	frame.frame_id = 3;
	frame.presentation_id = 1;
	sync.submission_token = 3;
	sync.completed_token = 2;
	CHECK(api.evaluate(context, &frame, &status) == KZGS_PROVIDER_OK && lastReset && barrierCalls == 6);
	KzgsProviderDestroyInfo destroyInfo{sizeof(destroyInfo), KZGS_PROVIDER_DESTROY_COMPLETED, 3};
	api.destroy(context, &destroyInfo);
	CHECK(!lostDestroy);
	CHECK(api.create(&createInfo, &context, &status) == KZGS_PROVIDER_OK);
	frame.frame_id = 4;
	frame.presentation_id = 2;
	sync.submission_token = 5;
	sync.completed_token = 3;
	failEvaluate = true;
	CHECK(api.evaluate(context, &frame, &status) == KZGS_PROVIDER_ERROR_EVALUATE);
	sync.submission_token = 4;
	failEvaluate = false;
	CHECK(api.evaluate(context, &frame, &status) == KZGS_PROVIDER_ERROR_ARGUMENT);
	destroyInfo.completed_token = 4;
	api.destroy(context, &destroyInfo);
	CHECK(lostDestroy); // Failed evaluation may still have recorded commands for token 5.
	CHECK(api.create(&createInfo, &context, &status) == KZGS_PROVIDER_OK);
	destroyInfo.reason = KZGS_PROVIDER_DESTROY_DEVICE_LOST;
	api.destroy(context, &destroyInfo);
	CHECK(lostDestroy);
	failCreate = true;
	failDestroy = true;
	const int destroysBeforeFailedCreate = destroyCalls;
	CHECK(api.create(&createInfo, &context, &status) == KZGS_PROVIDER_ERROR_CREATE && !context);
	CHECK(destroyCalls == destroysBeforeFailedCreate + 1);
	if (!failures)
		std::puts("vendor ABI shim: PASS (test double only; no SDK or GPU)");
	return failures ? 1 : 0;
}
