// Optional local NVIDIA NGX Vulkan adapter. No NVIDIA SDK material is included in this repository.
// SPDX-License-Identifier: GPL-3.0+
#include "kzgs_vendor_backend.h"

#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_vk.h>
#include <windows.h>

#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
	using namespace kzgs::vendor;
	constexpr const char* ProjectId = "542a810e-f69d-4ecc-89e0-4704cd026d12";
	constexpr const char* EngineVersion = "kzgs-provider-1";

	struct Discovery
	{
		std::wstring applicationData;
		std::wstring moduleDirectory;
		const wchar_t* searchPath = nullptr;
		NVSDK_NGX_FeatureCommonInfo common{};
		NVSDK_NGX_FeatureDiscoveryInfo feature{};

		Discovery()
		{
			wchar_t path[32768]{};
			DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", path, static_cast<DWORD>(std::size(path)));
			if (!length || length >= std::size(path))
				length = GetCurrentDirectoryW(static_cast<DWORD>(std::size(path)), path);
			if (length && length < std::size(path))
				applicationData.assign(path, length);
			length = GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path, static_cast<DWORD>(std::size(path)));
			if (length && length < std::size(path))
			{
				moduleDirectory.assign(path, length);
				const size_t slash = moduleDirectory.find_last_of(L"\\/");
				if (slash != std::wstring::npos)
					moduleDirectory.resize(slash);
				else
					moduleDirectory.clear();
			}
			if (!moduleDirectory.empty())
			{
				searchPath = moduleDirectory.c_str();
				common.PathListInfo.Path = &searchPath;
				common.PathListInfo.Length = 1;
			}
			feature.SDKVersion = NVSDK_NGX_Version_API;
			feature.FeatureID = NVSDK_NGX_Feature_SuperSampling;
			feature.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
			feature.Identifier.v.ProjectDesc = {ProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, EngineVersion};
			feature.ApplicationDataPath = applicationData.c_str();
			feature.FeatureInfo = &common;
		}
	};

	Discovery& discovery()
	{
		static Discovery value;
		return value;
	}

	Result ngxFailure(NVSDK_NGX_Result code, KzgsProviderResult operation, const char* message)
	{
		KzgsProviderReason reason = KZGS_PROVIDER_REASON_PROVIDER_FAILURE;
		if (code == NVSDK_NGX_Result_FAIL_FeatureNotSupported || code == NVSDK_NGX_Result_FAIL_OutOfDate)
			reason = KZGS_PROVIDER_REASON_UNSUPPORTED_DEVICE;
		else if (code == NVSDK_NGX_Result_FAIL_OutOfGPUMemory)
			reason = KZGS_PROVIDER_REASON_OUT_OF_MEMORY;
		else if (code == NVSDK_NGX_Result_FAIL_UnsupportedInputFormat || code == NVSDK_NGX_Result_FAIL_UnsupportedFormat)
			reason = KZGS_PROVIDER_REASON_UNSUPPORTED_FORMAT;
		else if (code == NVSDK_NGX_Result_FAIL_MissingInput)
			reason = KZGS_PROVIDER_REASON_MISSING_INPUT;
		return failure(operation, reason, message, static_cast<int32_t>(code));
	}

	bool copyExtensions(Requirements& out, uint32_t count, const VkExtensionProperties* properties)
	{
		if (count > 256 || (count && !properties))
			return false;
		for (uint32_t i = 0; i < count; ++i)
		{
			const size_t length = strnlen_s(properties[i].extensionName, VK_MAX_EXTENSION_NAME_SIZE);
			if (!length || length == VK_MAX_EXTENSION_NAME_SIZE)
				return false;
			out.names.emplace_back(properties[i].extensionName, length);
		}
		return true;
	}

	Result instanceRequirements(Requirements& out)
	{
		Discovery& config = discovery();
		if (config.applicationData.empty())
			return failure(KZGS_PROVIDER_ERROR_EXTENSIONS, KZGS_PROVIDER_REASON_PROVIDER_FAILURE,
				"NGX requires a writable application-data directory.");
		uint32_t count = 0;
		VkExtensionProperties* properties = nullptr;
		const NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(
			&config.feature, &count, &properties);
		if (NVSDK_NGX_FAILED(result))
			return ngxFailure(result, KZGS_PROVIDER_ERROR_EXTENSIONS, "NGX instance extension query failed.");
		if (!copyExtensions(out, count, properties))
			return failure(KZGS_PROVIDER_ERROR_EXTENSIONS, KZGS_PROVIDER_REASON_MISSING_EXTENSION,
				"NGX returned a malformed instance extension list.");
		return {};
	}

	Result deviceRequirements(const KzgsProviderDeviceQuery& query, Requirements& out)
	{
		Discovery& config = discovery();
		NVSDK_NGX_FeatureRequirement support{};
		NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_GetFeatureRequirements(
			query.instance, query.physical_device, &config.feature, &support);
		if (NVSDK_NGX_FAILED(result))
			return ngxFailure(result, KZGS_PROVIDER_ERROR_UNSUPPORTED, "NGX DLSS capability query failed.");
		if (support.FeatureSupported != NVSDK_NGX_FeatureSupportResult_Supported)
			return failure(KZGS_PROVIDER_ERROR_UNSUPPORTED, KZGS_PROVIDER_REASON_UNSUPPORTED_DEVICE,
				"NGX reports that DLSS is unsupported on this device or driver.", static_cast<int32_t>(support.FeatureSupported));
		uint32_t count = 0;
		VkExtensionProperties* properties = nullptr;
		result = NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(
			query.instance, query.physical_device, &config.feature, &count, &properties);
		if (NVSDK_NGX_FAILED(result))
			return ngxFailure(result, KZGS_PROVIDER_ERROR_EXTENSIONS, "NGX device extension query failed.");
		if (!copyExtensions(out, count, properties))
			return failure(KZGS_PROVIDER_ERROR_EXTENSIONS, KZGS_PROVIDER_REASON_MISSING_EXTENSION,
				"NGX returned a malformed device extension list.");
		// Conservatively require the storage-image write feature used in NVIDIA's Vulkan example.
		out.core.shaderStorageImageWriteWithoutFormat = VK_TRUE;
		return {};
	}

	struct DeviceState { uint32_t references = 0; };
	std::mutex deviceMutex;
	std::map<VkDevice, DeviceState> devices;
	bool abandonedDevice = false;

	struct DlssContext
	{
		KzgsProviderCreateInfo info{};
		NVSDK_NGX_Parameter* parameters = nullptr;
		NVSDK_NGX_Handle* feature = nullptr;
		NVSDK_NGX_PerfQuality_Value quality = NVSDK_NGX_PerfQuality_Value_MaxQuality;
		uint32_t depthFlags = 0;
		bool initialized = false;
		bool created = false;
		bool creationFailed = false;
	};

	Result acquire(DlssContext& context)
	{
		std::lock_guard lock(deviceMutex);
		if (abandonedDevice)
			return failure(KZGS_PROVIDER_ERROR_DEVICE_LOST, KZGS_PROVIDER_REASON_DEVICE_LOST,
				"An NGX device was lost; restart before using DLSS again.");
		if (!devices.empty() && devices.find(context.info.device) == devices.end())
			return failure(KZGS_PROVIDER_ERROR_UNSUPPORTED, KZGS_PROVIDER_REASON_UNSUPPORTED_DEVICE,
				"Concurrent NGX contexts on different Vulkan devices are not supported.");
		Discovery& config = discovery();
		auto [found, inserted] = devices.try_emplace(context.info.device);
		if (inserted)
		{
			const NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_Init_with_ProjectID(ProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM,
				EngineVersion, config.applicationData.c_str(), context.info.instance, context.info.physical_device,
				context.info.device, context.info.get_instance_proc_addr, context.info.get_device_proc_addr,
				&config.common, NVSDK_NGX_Version_API);
			if (NVSDK_NGX_FAILED(result))
			{
				devices.erase(found);
				return ngxFailure(result, KZGS_PROVIDER_ERROR_CREATE, "NGX Vulkan initialization failed.");
			}
		}
		++found->second.references;
		context.initialized = true;
		return {};
	}

	Result chooseQuality(DlssContext& context)
	{
		const unsigned int width = static_cast<unsigned int>(context.info.source_rect.right - context.info.source_rect.left);
		const unsigned int height = static_cast<unsigned int>(context.info.source_rect.bottom - context.info.source_rect.top);
		const NVSDK_NGX_PerfQuality_Value qualities[] = {NVSDK_NGX_PerfQuality_Value_MaxQuality,
			NVSDK_NGX_PerfQuality_Value_Balanced, NVSDK_NGX_PerfQuality_Value_MaxPerf,
			NVSDK_NGX_PerfQuality_Value_UltraPerformance, NVSDK_NGX_PerfQuality_Value_UltraQuality,
			NVSDK_NGX_PerfQuality_Value_DLAA};
		uint64_t best = std::numeric_limits<uint64_t>::max();
		bool found = false;
		for (const auto quality : qualities)
		{
			unsigned int optimalWidth = 0, optimalHeight = 0, maxWidth = 0, maxHeight = 0, minWidth = 0, minHeight = 0;
			float sharpness = 0;
			const NVSDK_NGX_Result result = NGX_DLSS_GET_OPTIMAL_SETTINGS(context.parameters,
				context.info.destination_extent.width, context.info.destination_extent.height, quality,
				&optimalWidth, &optimalHeight, &maxWidth, &maxHeight, &minWidth, &minHeight, &sharpness);
			if (NVSDK_NGX_FAILED(result) || !optimalWidth || !optimalHeight ||
				width < minWidth || width > maxWidth || height < minHeight || height > maxHeight)
				continue;
			const uint64_t delta = static_cast<uint64_t>(std::abs(static_cast<int64_t>(width) - optimalWidth)) +
				static_cast<uint64_t>(std::abs(static_cast<int64_t>(height) - optimalHeight));
			if (delta < best)
			{
				best = delta;
				context.quality = quality;
				found = true;
			}
		}
		return found ? Result{} : failure(KZGS_PROVIDER_ERROR_CREATE, KZGS_PROVIDER_REASON_UNSUPPORTED_FORMAT,
			"The source/output resolution is outside all NGX DLSS quality ranges.");
	}

	Result create(const KzgsProviderCreateInfo& info, void** out)
	{
		if (info.sharpening)
			return failure(KZGS_PROVIDER_ERROR_CREATE, KZGS_PROVIDER_REASON_UNSUPPORTED_FORMAT,
				"Current DLSS SDK sharpening is unsupported; set provider sharpening to zero.");
		auto context = std::make_unique<DlssContext>();
		context->info = info;
		Result result = acquire(*context);
		if (result.code != KZGS_PROVIDER_OK)
			return result;
		// Transfer ownership before SDK calls so the common ABI destroys partially initialized state.
		*out = context.release();
		auto& value = *static_cast<DlssContext*>(*out);
		NVSDK_NGX_Result native = NVSDK_NGX_VULKAN_GetCapabilityParameters(&value.parameters);
		if (NVSDK_NGX_FAILED(native) || !value.parameters)
			return ngxFailure(native, KZGS_PROVIDER_ERROR_CREATE, "NGX capability parameters are unavailable.");
		int available = 0;
		native = NVSDK_NGX_Parameter_GetI(value.parameters, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
		if (NVSDK_NGX_FAILED(native))
			return ngxFailure(native, KZGS_PROVIDER_ERROR_CREATE, "NGX reports DLSS is unavailable.");
		if (!available)
			return failure(KZGS_PROVIDER_ERROR_CREATE, KZGS_PROVIDER_REASON_UNSUPPORTED_DEVICE,
				"NGX reports DLSS is unavailable.", static_cast<int32_t>(native));
		return chooseQuality(value);
	}

	NVSDK_NGX_Resource_VK resource(const KzgsProviderImage& image, bool writable)
	{
		return NVSDK_NGX_Create_ImageView_Resource_VK(image.view, image.image, image.subresource,
			image.format, image.extent.width, image.extent.height, writable);
	}

	Result evaluate(void* opaque, const KzgsProviderFrame& frame, bool reset)
	{
		auto& context = *static_cast<DlssContext*>(opaque);
		if (context.creationFailed)
			return failure(KZGS_PROVIDER_ERROR_EVALUATE, KZGS_PROVIDER_REASON_PROVIDER_FAILURE,
				"DLSS feature creation failed previously; retire and recreate the context.");
		const uint32_t reverse = frame.camera->flags & KZGS_PROVIDER_CAMERA_REVERSED_DEPTH;
		if (context.created && reverse != context.depthFlags)
			return failure(KZGS_PROVIDER_ERROR_INPUT, KZGS_PROVIDER_REASON_MISSING_INPUT,
				"Changing the depth convention requires a new DLSS context.");
		if (!context.created)
		{
			NVSDK_NGX_DLSS_Create_Params params{};
			params.Feature.InWidth = static_cast<unsigned int>(frame.source_rect.right - frame.source_rect.left);
			params.Feature.InHeight = static_cast<unsigned int>(frame.source_rect.bottom - frame.source_rect.top);
			params.Feature.InTargetWidth = context.info.destination_extent.width;
			params.Feature.InTargetHeight = context.info.destination_extent.height;
			params.Feature.InPerfQualityValue = context.quality;
			params.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
				NVSDK_NGX_DLSS_Feature_Flags_AutoExposure |
				(reverse ? NVSDK_NGX_DLSS_Feature_Flags_DepthInverted : 0) |
				(context.info.color_encoding == KZGS_PROVIDER_COLOR_LINEAR_HDR ? NVSDK_NGX_DLSS_Feature_Flags_IsHDR : 0);
			context.creationFailed = true;
			const NVSDK_NGX_Result result = NGX_VULKAN_CREATE_DLSS_EXT1(context.info.device,
				frame.sync->command_buffer, 1, 1, &context.feature, context.parameters, &params);
			if (NVSDK_NGX_FAILED(result) || !context.feature)
				return ngxFailure(result, KZGS_PROVIDER_ERROR_EVALUATE, "NGX DLSS feature creation failed.");
			context.created = true;
			context.creationFailed = false;
			context.depthFlags = reverse;
			const auto barrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(
				context.info.get_device_proc_addr(context.info.device, "vkCmdPipelineBarrier"));
			VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
			memory.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
			memory.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
			barrier(frame.sync->command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				0, 1, &memory, 0, nullptr, 0, nullptr);
			reset = true;
		}
		NVSDK_NGX_Resource_VK color = resource(*frame.color, false);
		NVSDK_NGX_Resource_VK output = resource(*frame.output, true);
		NVSDK_NGX_Resource_VK depth = resource(*frame.depth, false);
		NVSDK_NGX_Resource_VK motion = resource(*frame.motion, false);
		NVSDK_NGX_VK_DLSS_Eval_Params params{};
		params.Feature.pInColor = &color;
		params.Feature.pInOutput = &output;
		params.pInDepth = &depth;
		params.pInMotionVectors = &motion;
		params.InJitterOffsetX = frame.jitter_x;
		params.InJitterOffsetY = frame.jitter_y;
		params.InMVScaleX = frame.motion_scale_x;
		params.InMVScaleY = frame.motion_scale_y;
		params.InRenderSubrectDimensions = {static_cast<unsigned int>(frame.source_rect.right - frame.source_rect.left),
			static_cast<unsigned int>(frame.source_rect.bottom - frame.source_rect.top)};
		params.InColorSubrectBase = {static_cast<unsigned int>(frame.source_rect.left), static_cast<unsigned int>(frame.source_rect.top)};
		params.InDepthSubrectBase = params.InColorSubrectBase;
		params.InMVSubrectBase = params.InColorSubrectBase;
		params.InReset = reset ? 1 : 0;
		params.InPreExposure = 1.0f;
		params.InExposureScale = 1.0f;
		const NVSDK_NGX_Result result = NGX_VULKAN_EVALUATE_DLSS_EXT(
			frame.sync->command_buffer, context.feature, context.parameters, &params);
		return NVSDK_NGX_FAILED(result) ? ngxFailure(result, KZGS_PROVIDER_ERROR_EVALUATE, "NGX DLSS evaluation failed.") : Result{};
	}

	void destroy(void* opaque, bool lost)
	{
		std::unique_ptr<DlssContext> context(static_cast<DlssContext*>(opaque));
		if (!context)
			return;
		std::lock_guard lock(deviceMutex);
		if (lost)
			abandonedDevice = true;
		if (!abandonedDevice)
		{
			if (context->feature)
				NVSDK_NGX_VULKAN_ReleaseFeature(context->feature);
			if (context->parameters)
				NVSDK_NGX_VULKAN_DestroyParameters(context->parameters);
		}
		if (context->initialized)
		{
			const auto found = devices.find(context->info.device);
			if (found != devices.end() && --found->second.references == 0)
			{
				if (!abandonedDevice)
					NVSDK_NGX_VULKAN_Shutdown1(context->info.device);
				devices.erase(found);
			}
		}
	}

	const Backend DlssBackend{KZGS_PROVIDER_KIND_DLSS, "NVIDIA DLSS (local SDK)", VK_IMAGE_LAYOUT_GENERAL,
		instanceRequirements, deviceRequirements, create, evaluate, destroy};
}

namespace kzgs::vendor { const Backend& backend() { return DlssBackend; } }
