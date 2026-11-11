// Source-built AMD FidelityFX FSR 3.1.6 Super Resolution adapter.
// SPDX-License-Identifier: GPL-3.0+
#include "kzgs_vendor_backend.h"
#include "kzgs_ffx_vk_loader.h"

#include <ffx_fsr3upscaler.h>
#include <ffx_vk.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace
{
	using namespace kzgs::vendor;

	struct Fsr3Context
	{
		KzgsProviderCreateInfo info{};
		std::vector<std::max_align_t> scratch;
		FfxInterface backend{};
		FfxFsr3UpscalerContext upscaler{};
		std::array<FfxResourceInternal, 3> shared{};
		uint32_t sharedCount = 0;
		bool contextCreated = false;
	};

	Result ffxFailure(FfxErrorCode code, KzgsProviderResult operation, const char* message)
	{
		return failure(operation,
			code == FFX_ERROR_OUT_OF_MEMORY ? KZGS_PROVIDER_REASON_OUT_OF_MEMORY :
				KZGS_PROVIDER_REASON_PROVIDER_FAILURE,
			message, static_cast<int32_t>(code));
	}

	Result instanceRequirements(Requirements&)
	{
		return {};
	}

	Result deviceRequirements(const KzgsProviderDeviceQuery&, Requirements& requirements)
	{
		requirements.core.shaderStorageImageWriteWithoutFormat = VK_TRUE;
		return {};
	}

	void release(Fsr3Context& context)
	{
		for (uint32_t i = 0; i < context.sharedCount; ++i)
			context.backend.fpDestroyResource(&context.backend, context.shared[i], 0);
		context.sharedCount = 0;
		if (context.contextCreated)
		{
			ffxFsr3UpscalerContextDestroy(&context.upscaler);
			context.contextCreated = false;
		}
	}

	Result initialize(void* opaque)
	{
		auto& context = *static_cast<Fsr3Context*>(opaque);
		if (!pinProviderModule())
			return failure(KZGS_PROVIDER_ERROR_CREATE, KZGS_PROVIDER_REASON_PROVIDER_FAILURE,
				"Could not retain the FSR 3 provider module for safe Vulkan teardown.");
		if (!kzgsFfxSetVulkanProcs(context.info.instance, context.info.device,
				context.info.get_instance_proc_addr, context.info.get_device_proc_addr))
			return failure(KZGS_PROVIDER_ERROR_CREATE, KZGS_PROVIDER_REASON_MISSING_FEATURE,
				"Required Vulkan 1.1 entry points are unavailable.");

		constexpr size_t maxContexts = 1;
		const size_t scratchBytes = ffxGetScratchMemorySizeVK(context.info.physical_device, maxContexts);
		if (!scratchBytes)
			return failure(KZGS_PROVIDER_ERROR_CREATE, KZGS_PROVIDER_REASON_PROVIDER_FAILURE,
				"FidelityFX returned an invalid Vulkan scratch-memory size.");
		context.scratch.resize((scratchBytes + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
		VkDeviceContext deviceContext{
			context.info.device, context.info.physical_device, context.info.get_device_proc_addr};
		FfxErrorCode native = ffxGetInterfaceVK(&context.backend, ffxGetDeviceVK(&deviceContext),
			context.scratch.data(), context.scratch.size() * sizeof(std::max_align_t), maxContexts);
		if (native != FFX_OK)
			return ffxFailure(native, KZGS_PROVIDER_ERROR_CREATE,
				"Could not initialize the FidelityFX Vulkan backend.");

		FfxFsr3UpscalerContextDescription description{};
		description.backendInterface = context.backend;
		description.maxRenderSize = {context.info.source_extent.width, context.info.source_extent.height};
		description.maxUpscaleSize = {
			context.info.destination_extent.width, context.info.destination_extent.height};
		description.flags = FFX_FSR3UPSCALER_ENABLE_AUTO_EXPOSURE;
		if (context.info.color_encoding == KZGS_PROVIDER_COLOR_LINEAR_HDR)
			description.flags |= FFX_FSR3UPSCALER_ENABLE_HIGH_DYNAMIC_RANGE;
		if (context.info.depth_convention == KZGS_PROVIDER_DEPTH_REVERSED)
			description.flags |= FFX_FSR3UPSCALER_ENABLE_DEPTH_INVERTED;
		native = ffxFsr3UpscalerContextCreate(&context.upscaler, &description);
		if (native != FFX_OK)
			return ffxFailure(native, KZGS_PROVIDER_ERROR_CREATE,
				"FidelityFX FSR 3 upscaler context creation failed.");
		context.contextCreated = true;

		FfxFsr3UpscalerSharedResourceDescriptions resources{};
		native = ffxFsr3UpscalerGetSharedResourceDescriptions(&context.upscaler, &resources);
		if (native != FFX_OK)
			return ffxFailure(native, KZGS_PROVIDER_ERROR_CREATE,
				"FidelityFX FSR 3 shared-resource query failed.");
		const FfxCreateResourceDescription* descriptions[] = {
			&resources.dilatedDepth,
			&resources.dilatedMotionVectors,
			&resources.reconstructedPrevNearestDepth,
		};
		for (const FfxCreateResourceDescription* resource : descriptions)
		{
			native = context.backend.fpCreateResource(
				&context.backend, resource, 0, &context.shared[context.sharedCount]);
			if (native != FFX_OK)
				return ffxFailure(native, KZGS_PROVIDER_ERROR_CREATE,
					"FidelityFX FSR 3 shared-resource allocation failed.");
			++context.sharedCount;
		}
		return {};
	}

	Result create(const KzgsProviderCreateInfo& info, void** out)
	{
		if (info.color_encoding != KZGS_PROVIDER_COLOR_LINEAR_SDR ||
			info.source_format != VK_FORMAT_R8G8B8A8_UNORM ||
			info.destination_format != VK_FORMAT_R8G8B8A8_UNORM)
			return failure(KZGS_PROVIDER_ERROR_CREATE, KZGS_PROVIDER_REASON_UNSUPPORTED_FORMAT,
				"The source-built FSR 3 path requires tightly packed linear-SDR RGBA8 input and output.");
		if (!KZGS_PROVIDER_HAS_FIELD(&info, KzgsProviderCreateInfo, depth_convention) ||
			info.depth_convention != KZGS_PROVIDER_DEPTH_FORWARD)
			return failure(KZGS_PROVIDER_ERROR_CREATE, KZGS_PROVIDER_REASON_MISSING_INPUT,
				"The experimental FSR 3 path requires forward finite depth.");
		auto context = std::make_unique<Fsr3Context>();
		context->info = info;
		*out = context.release();
		const Result result = runInitialization(info, KZGS_PROVIDER_INIT_SDK_EXECUTION_QUALIFIED,
			initialize, *out);
		static_cast<Fsr3Context*>(*out)->info.initialization = nullptr;
		return result;
	}

	FfxApiResource resource(const KzgsProviderImage& image, const wchar_t* name, bool writable)
	{
		VkImageCreateInfo createInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
		createInfo.imageType = VK_IMAGE_TYPE_2D;
		createInfo.format = image.format;
		createInfo.extent = {image.extent.width, image.extent.height, 1};
		createInfo.mipLevels = 1;
		createInfo.arrayLayers = 1;
		createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		createInfo.usage = image.usage;
		createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		const FfxApiResourceUsage usage = writable ?
			FFX_API_RESOURCE_USAGE_UAV : FFX_API_RESOURCE_USAGE_READ_ONLY;
		return ffxGetResourceVK(reinterpret_cast<void*>(image.image),
			ffxGetImageResourceDescriptionVK(image.image, createInfo, usage), name,
			writable ? FFX_API_RESOURCE_STATE_UNORDERED_ACCESS : FFX_API_RESOURCE_STATE_COMPUTE_READ);
	}

	Result evaluate(void* opaque, const KzgsProviderFrame& frame, bool reset)
	{
		auto& context = *static_cast<Fsr3Context*>(opaque);
		FfxFsr3UpscalerDispatchDescription dispatch{};
		dispatch.commandList = ffxGetCommandListVK(frame.sync->command_buffer);
		dispatch.color = resource(*frame.color, L"KZGS FSR3 color", false);
		dispatch.depth = resource(*frame.depth, L"KZGS FSR3 surrogate depth", false);
		dispatch.motionVectors = resource(*frame.motion, L"KZGS FSR3 optical flow", false);
		dispatch.output = resource(*frame.output, L"KZGS FSR3 output", true);
		if ((frame.valid_inputs & KZGS_PROVIDER_INPUT_REACTIVE_MASK) && frame.reactive_mask)
			dispatch.reactive = resource(*frame.reactive_mask, L"KZGS FSR3 reactive mask", false);
		if ((frame.valid_inputs & KZGS_PROVIDER_INPUT_COMPOSITION_MASK) && frame.composition_mask)
			dispatch.transparencyAndComposition =
				resource(*frame.composition_mask, L"KZGS FSR3 composition mask", false);
		dispatch.dilatedDepth = context.backend.fpGetResource(
			&context.backend, context.shared[0]);
		dispatch.dilatedMotionVectors = context.backend.fpGetResource(
			&context.backend, context.shared[1]);
		dispatch.reconstructedPrevNearestDepth = context.backend.fpGetResource(
			&context.backend, context.shared[2]);
		dispatch.jitterOffset = {frame.jitter_x, frame.jitter_y};
		dispatch.motionVectorScale = {frame.motion_scale_x, frame.motion_scale_y};
		dispatch.renderSize = {
			static_cast<uint32_t>(frame.source_rect.right - frame.source_rect.left),
			static_cast<uint32_t>(frame.source_rect.bottom - frame.source_rect.top)};
		dispatch.upscaleSize = {
			static_cast<uint32_t>(frame.destination_rect.right - frame.destination_rect.left),
			static_cast<uint32_t>(frame.destination_rect.bottom - frame.destination_rect.top)};
		dispatch.enableSharpening = frame.sharpening != 0;
		dispatch.sharpness = static_cast<float>(frame.sharpening) / 100.0f;
		dispatch.frameTimeDelta = frame.frame_time_ms;
		dispatch.preExposure = 1.0f;
		dispatch.reset = reset;
		dispatch.cameraNear = frame.camera->near_plane;
		dispatch.cameraFar = frame.camera->far_plane;
		dispatch.cameraFovAngleVertical = frame.camera->vertical_fov_radians;
		dispatch.viewSpaceToMetersFactor = 1.0f;
		const FfxErrorCode native = ffxFsr3UpscalerContextDispatch(&context.upscaler, &dispatch);
		return native == FFX_OK ? Result{} :
			ffxFailure(native, KZGS_PROVIDER_ERROR_EVALUATE, "FidelityFX FSR 3 dispatch failed.");
	}

	void destroy(void* opaque, bool lost)
	{
		std::unique_ptr<Fsr3Context> context(static_cast<Fsr3Context*>(opaque));
		if (!context)
			return;
		if (lost)
		{
			context.release();
			return;
		}
		release(*context);
	}

	const Backend Fsr3Backend{KZGS_PROVIDER_KIND_FSR3, "AMD FidelityFX FSR 3.1.6",
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, instanceRequirements, deviceRequirements,
		create, evaluate, destroy, false};
}

namespace kzgs::vendor
{
	const Backend& backend()
	{
		return Fsr3Backend;
	}
}
