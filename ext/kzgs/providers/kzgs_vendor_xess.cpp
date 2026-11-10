// Optional local Intel XeSS Vulkan adapter; no Intel SDK material is included.
// SPDX-License-Identifier: GPL-3.0+
#include "kzgs_vendor_backend.h"
#include "kzgs_vendor_conventions.h"

#include <xess/xess_vk.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>

namespace
{
	using namespace kzgs::vendor;

	std::mutex sdkMutex;
	std::thread::id sdkThread;
	uint32_t liveContexts = 0;
	bool abandonedDevice = false;

	Result threadFailure()
	{
		if (abandonedDevice)
			return failure(KZGS_PROVIDER_ERROR_DEVICE_LOST, KZGS_PROVIDER_REASON_DEVICE_LOST,
				"XeSS work could not be retired safely; restart before using XeSS again.");
		if (liveContexts && sdkThread != std::this_thread::get_id())
			return failure(KZGS_PROVIDER_ERROR_UNSUPPORTED, KZGS_PROVIDER_REASON_PROVIDER_FAILURE,
				"XeSS SDK calls must remain on the initialization thread.");
		return {};
	}

	Result sdkFailure(xess_result_t code, KzgsProviderResult result, const char* message)
	{
		KzgsProviderReason reason = KZGS_PROVIDER_REASON_PROVIDER_FAILURE;
		if (code == XESS_RESULT_ERROR_UNSUPPORTED_DEVICE || code == XESS_RESULT_ERROR_UNSUPPORTED_DRIVER)
			reason = KZGS_PROVIDER_REASON_UNSUPPORTED_DEVICE;
		else if (code == XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY)
			reason = KZGS_PROVIDER_REASON_OUT_OF_MEMORY;
		return failure(result, reason, message, static_cast<int32_t>(code));
	}

	bool copyNames(Requirements& out, uint32_t count, const char* const* names)
	{
		if (count > 256 || (count && !names))
			return false;
		for (uint32_t i = 0; i < count; ++i)
		{
			if (!names[i] || !names[i][0] || strnlen_s(names[i], VK_MAX_EXTENSION_NAME_SIZE) == VK_MAX_EXTENSION_NAME_SIZE)
				return false;
			out.names.emplace_back(names[i]);
		}
		return true;
	}

	Result instanceRequirements(Requirements& out)
	{
		std::lock_guard lock(sdkMutex);
		const Result thread = threadFailure();
		if (thread.code != KZGS_PROVIDER_OK)
			return thread;
		xess_version_t version{};
		xess_result_t result = xessGetVersion(&version);
		if (result != XESS_RESULT_SUCCESS)
			return sdkFailure(result, KZGS_PROVIDER_ERROR_VERSION, "Could not query the XeSS runtime version.");
		if (version.major != 3)
			return failure(KZGS_PROVIDER_ERROR_VERSION, KZGS_PROVIDER_REASON_VERSION,
				"XeSS runtime major version 3 is required.", version.major);
		uint32_t count = 0;
		uint32_t apiVersion = 0;
		const char* const* names = nullptr;
		result = xessVKGetRequiredInstanceExtensions(&count, &names, &apiVersion);
		if (result != XESS_RESULT_SUCCESS)
			return sdkFailure(result, KZGS_PROVIDER_ERROR_EXTENSIONS, "XeSS instance extension query failed.");
		if (VK_API_VERSION_VARIANT(apiVersion) != 0 || VK_API_VERSION_MAJOR(apiVersion) != 1 ||
			VK_API_VERSION_MINOR(apiVersion) > 1)
			return failure(KZGS_PROVIDER_ERROR_VERSION, KZGS_PROVIDER_REASON_VERSION,
				"XeSS reported an invalid or unsupported minimum Vulkan instance API; this host uses Vulkan 1.1.", apiVersion);
		if (!copyNames(out, count, names))
			return failure(KZGS_PROVIDER_ERROR_EXTENSIONS, KZGS_PROVIDER_REASON_MISSING_EXTENSION,
				"XeSS returned a malformed instance extension list.");
		return {};
	}

	bool ensureExtension(const KzgsProviderDeviceQuery& query, Requirements& out, const char* name)
	{
		if (std::find(out.names.begin(), out.names.end(), name) != out.names.end())
			return true;
		const auto enumerate = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
			query.get_instance_proc_addr(query.instance, "vkEnumerateDeviceExtensionProperties"));
		if (!enumerate)
			return false;
		uint32_t count = 0;
		if (enumerate(query.physical_device, nullptr, &count, nullptr) != VK_SUCCESS || count > 4096)
			return false;
		std::vector<VkExtensionProperties> properties(count);
		if (count && enumerate(query.physical_device, nullptr, &count, properties.data()) != VK_SUCCESS)
			return false;
		properties.resize(count);
		for (const VkExtensionProperties& property : properties)
		{
			if (std::strcmp(property.extensionName, name) == 0)
			{
				out.names.emplace_back(name);
				return true;
			}
		}
		return false;
	}

	Result feature(Requirements& out, const KzgsProviderDeviceQuery& query, VkStructureType type,
		const VkBool32* values, size_t count, const char* extension)
	{
		for (size_t i = 0; i < count; ++i)
		{
			if (values[i] > VK_TRUE)
				return failure(KZGS_PROVIDER_ERROR_FEATURES, KZGS_PROVIDER_REASON_MISSING_FEATURE,
					"XeSS returned a malformed Vulkan feature bit.");
		}
		if (std::any_of(out.blocks.begin(), out.blocks.end(),
			[type](const KzgsProviderFeatureBlock& block) { return block.s_type == type; }))
			return failure(KZGS_PROVIDER_ERROR_FEATURES, KZGS_PROVIDER_REASON_MISSING_FEATURE,
				"XeSS returned duplicate Vulkan feature structures.");
		if (extension && !ensureExtension(query, out, extension))
			return failure(KZGS_PROVIDER_ERROR_EXTENSIONS, KZGS_PROVIDER_REASON_MISSING_EXTENSION,
				"A Vulkan extension needed for an XeSS feature is unavailable.");
		out.addFeature(type, values, count);
		return {};
	}

	template <typename T>
	bool onlySelectedBits(const T& value, size_t first, size_t last, size_t selected1, size_t selected2)
	{
		const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
		for (size_t offset = first; offset <= last; offset += sizeof(VkBool32))
		{
			VkBool32 bit = 0;
			std::memcpy(&bit, bytes + offset, sizeof(bit));
			if (bit > VK_TRUE || (bit && offset != selected1 && offset != selected2))
				return false;
		}
		return true;
	}

	Result deviceRequirements(const KzgsProviderDeviceQuery& query, Requirements& out)
	{
		std::lock_guard lock(sdkMutex);
		const Result thread = threadFailure();
		if (thread.code != KZGS_PROVIDER_OK)
			return thread;
		uint32_t count = 0;
		const char* const* names = nullptr;
		xess_result_t result = xessVKGetRequiredDeviceExtensions(query.instance, query.physical_device, &count, &names);
		if (result != XESS_RESULT_SUCCESS)
			return sdkFailure(result, KZGS_PROVIDER_ERROR_EXTENSIONS, "XeSS device extension query failed.");
		if (!copyNames(out, count, names))
			return failure(KZGS_PROVIDER_ERROR_EXTENSIONS, KZGS_PROVIDER_REASON_MISSING_EXTENSION,
				"XeSS returned a malformed device extension list.");

		VkPhysicalDeviceFeatures2 core{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
		void* chain = &core;
		result = xessVKGetRequiredDeviceFeatures(query.instance, query.physical_device, &chain);
		if (result != XESS_RESULT_SUCCESS)
			return sdkFailure(result, KZGS_PROVIDER_ERROR_FEATURES, "XeSS device feature query failed.");
		bool coreSeen = false;
		bool mutableSeen = false;
		uint32_t nodes = 0;
		for (auto* node = static_cast<VkBaseOutStructure*>(chain); node; node = node->pNext)
		{
			if (++nodes > 32)
				return failure(KZGS_PROVIDER_ERROR_FEATURES, KZGS_PROVIDER_REASON_MISSING_FEATURE,
					"XeSS feature chain is too long or cyclic.");
			Result converted;
			switch (node->sType)
			{
			case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2:
				if (coreSeen)
					return failure(KZGS_PROVIDER_ERROR_FEATURES, KZGS_PROVIDER_REASON_MISSING_FEATURE,
						"XeSS returned duplicate core features.");
				out.core = reinterpret_cast<const VkPhysicalDeviceFeatures2*>(node)->features;
				coreSeen = true;
				break;
			case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT:
			{
				const VkBool32 bit = reinterpret_cast<const VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT*>(node)->mutableDescriptorType;
				mutableSeen = bit == VK_TRUE;
				converted = feature(out, query, node->sType, &bit, 1, VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME);
				break;
			}
			case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES:
			{
				const auto& value = *reinterpret_cast<const VkPhysicalDeviceShaderFloat16Int8Features*>(node);
				const VkBool32 bits[] = {value.shaderFloat16, value.shaderInt8};
				converted = feature(out, query, node->sType, bits, 2, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);
				break;
			}
			case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES:
			{
				const VkBool32 bit = reinterpret_cast<const VkPhysicalDeviceShaderIntegerDotProductFeatures*>(node)->shaderIntegerDotProduct;
				converted = feature(out, query, node->sType, &bit, 1, VK_KHR_SHADER_INTEGER_DOT_PRODUCT_EXTENSION_NAME);
				break;
			}
			case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES:
			{
				const auto& value = *reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(node);
				if (!onlySelectedBits(value, offsetof(VkPhysicalDeviceVulkan12Features, samplerMirrorClampToEdge),
					offsetof(VkPhysicalDeviceVulkan12Features, subgroupBroadcastDynamicId),
					offsetof(VkPhysicalDeviceVulkan12Features, shaderFloat16),
					offsetof(VkPhysicalDeviceVulkan12Features, shaderInt8)))
					return failure(KZGS_PROVIDER_ERROR_FEATURES, KZGS_PROVIDER_REASON_MISSING_FEATURE,
						"XeSS requested Vulkan 1.2 features not representable by this host.");
				const VkBool32 bits[] = {value.shaderFloat16, value.shaderInt8};
				if (bits[0] || bits[1])
					converted = feature(out, query, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES,
						bits, 2, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);
				break;
			}
			case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES:
			{
				const auto& value = *reinterpret_cast<const VkPhysicalDeviceVulkan13Features*>(node);
				if (!onlySelectedBits(value, offsetof(VkPhysicalDeviceVulkan13Features, robustImageAccess),
					offsetof(VkPhysicalDeviceVulkan13Features, maintenance4),
					offsetof(VkPhysicalDeviceVulkan13Features, shaderIntegerDotProduct),
					offsetof(VkPhysicalDeviceVulkan13Features, shaderIntegerDotProduct)))
					return failure(KZGS_PROVIDER_ERROR_FEATURES, KZGS_PROVIDER_REASON_MISSING_FEATURE,
						"XeSS requested Vulkan 1.3 features not representable by this host.");
				const VkBool32 bit = value.shaderIntegerDotProduct;
				if (bit)
					converted = feature(out, query, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES,
						&bit, 1, VK_KHR_SHADER_INTEGER_DOT_PRODUCT_EXTENSION_NAME);
				break;
			}
			default:
				return failure(KZGS_PROVIDER_ERROR_FEATURES, KZGS_PROVIDER_REASON_MISSING_FEATURE,
					"XeSS requested an unsupported Vulkan feature structure.", static_cast<int32_t>(node->sType));
			}
			if (converted.code != KZGS_PROVIDER_OK)
				return converted;
		}
		if (!coreSeen || !out.core.shaderStorageImageWriteWithoutFormat || !mutableSeen)
			return failure(KZGS_PROVIDER_ERROR_FEATURES, KZGS_PROVIDER_REASON_MISSING_FEATURE,
				"XeSS did not report its documented required core and mutable-descriptor features.");
		return {};
	}

	struct XessContext
	{
		KzgsProviderCreateInfo info{};
		xess_context_handle_t handle = nullptr;
		xess_quality_settings_t quality = XESS_QUALITY_SETTING_QUALITY;
		bool counted = false;
	};

	Result chooseQuality(XessContext& context)
	{
		const uint32_t width = static_cast<uint32_t>(context.info.source_rect.right - context.info.source_rect.left);
		const uint32_t height = static_cast<uint32_t>(context.info.source_rect.bottom - context.info.source_rect.top);
		const xess_2d_t output{context.info.destination_extent.width, context.info.destination_extent.height};
		const uint64_t aspectX = static_cast<uint64_t>(width) * output.y;
		const uint64_t aspectY = static_cast<uint64_t>(height) * output.x;
		const uint64_t aspectError = aspectX > aspectY ? aspectX - aspectY : aspectY - aspectX;
		if (aspectError > std::max(output.x, output.y))
			return failure(KZGS_PROVIDER_ERROR_CREATE, KZGS_PROVIDER_REASON_UNSUPPORTED_FORMAT,
				"XeSS requires matching source and output aspect ratios.");

		const xess_quality_settings_t qualities[] = {XESS_QUALITY_SETTING_QUALITY,
			XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS, XESS_QUALITY_SETTING_ULTRA_QUALITY,
			XESS_QUALITY_SETTING_BALANCED, XESS_QUALITY_SETTING_PERFORMANCE,
			XESS_QUALITY_SETTING_ULTRA_PERFORMANCE, XESS_QUALITY_SETTING_AA};
		uint64_t best = std::numeric_limits<uint64_t>::max();
		bool found = false;
		for (const xess_quality_settings_t quality : qualities)
		{
			xess_2d_t optimal{}, minimum{}, maximum{};
			if (xessGetOptimalInputResolution(context.handle, &output, quality, &optimal, &minimum, &maximum) !=
					XESS_RESULT_SUCCESS || !optimal.x || !optimal.y || !minimum.x || !minimum.y ||
				minimum.x > maximum.x || minimum.y > maximum.y ||
				width < minimum.x || width > maximum.x || height < minimum.y || height > maximum.y)
				continue;
			const uint64_t dx = width > optimal.x ? width - optimal.x : optimal.x - width;
			const uint64_t dy = height > optimal.y ? height - optimal.y : optimal.y - height;
			if (dx + dy < best)
			{
				best = dx + dy;
				context.quality = quality;
				found = true;
			}
		}
		return found ? Result{} : failure(KZGS_PROVIDER_ERROR_CREATE, KZGS_PROVIDER_REASON_UNSUPPORTED_FORMAT,
			"The source/output resolution is outside all XeSS quality ranges.");
	}

	Result initialize(void* opaque)
	{
		auto& context = *static_cast<XessContext*>(opaque);
		std::lock_guard lock(sdkMutex);
		const Result thread = threadFailure();
		if (thread.code != KZGS_PROVIDER_OK)
			return thread;
		if (!pinProviderModule())
			return failure(KZGS_PROVIDER_ERROR_CREATE, KZGS_PROVIDER_REASON_PROVIDER_FAILURE,
				"Could not retain the XeSS provider module for safe SDK abandonment.");
		xess_result_t native = xessVKCreateContext(context.info.instance, context.info.physical_device,
			context.info.device, &context.handle);
		if (context.handle)
		{
			if (!liveContexts)
				sdkThread = std::this_thread::get_id();
			++liveContexts;
			context.counted = true;
		}
		if (native != XESS_RESULT_SUCCESS || !context.handle)
			return sdkFailure(native, KZGS_PROVIDER_ERROR_CREATE, "XeSS Vulkan context creation failed.");
		const Result quality = chooseQuality(context);
		if (quality.code != KZGS_PROVIDER_OK)
			return quality;

		xess_vk_init_params_t params{};
		params.outputResolution = {context.info.destination_extent.width, context.info.destination_extent.height};
		params.qualitySetting = context.quality;
		params.initFlags = (context.info.depth_convention == KZGS_PROVIDER_DEPTH_REVERSED ? XESS_INIT_FLAG_INVERTED_DEPTH : 0) |
			(context.info.color_encoding == KZGS_PROVIDER_COLOR_LINEAR_SDR ?
				XESS_INIT_FLAG_LDR_INPUT_COLOR : XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE);
		params.creationNodeMask = 1;
		params.visibleNodeMask = 1;
		native = xessVKInit(context.handle, &params);
		if (native != XESS_RESULT_SUCCESS)
			return sdkFailure(native, KZGS_PROVIDER_ERROR_CREATE, "XeSS Vulkan initialization failed.");
		native = xessSetJitterScale(context.handle, 1.0f, 1.0f);
		return native != XESS_RESULT_SUCCESS ?
			sdkFailure(native, KZGS_PROVIDER_ERROR_CREATE, "XeSS jitter-scale configuration failed.") : Result{};
	}

	Result create(const KzgsProviderCreateInfo& info, void** out)
	{
		if (info.sharpening)
			return failure(KZGS_PROVIDER_ERROR_CREATE, KZGS_PROVIDER_REASON_UNSUPPORTED_FORMAT,
				"XeSS adapter sharpening is unsupported; set provider sharpening to zero.");
		if (!KZGS_PROVIDER_HAS_FIELD(&info, KzgsProviderCreateInfo, depth_convention) ||
			(info.depth_convention != KZGS_PROVIDER_DEPTH_FORWARD && info.depth_convention != KZGS_PROVIDER_DEPTH_REVERSED))
			return failure(KZGS_PROVIDER_ERROR_CREATE, KZGS_PROVIDER_REASON_MISSING_INPUT,
				"A known, context-stable depth convention is required for XeSS.");
		auto context = std::make_unique<XessContext>();
		context->info = info;
		*out = context.release();
		const Result result = runInitialization(info, KZGS_PROVIDER_INIT_SDK_EXECUTION_QUALIFIED, initialize, *out);
		static_cast<XessContext*>(*out)->info.initialization = nullptr;
		return result;
	}

	xess_vk_image_view_info resource(const KzgsProviderImage& image)
	{
		return {image.view, image.image, image.subresource, image.format, image.extent.width, image.extent.height};
	}

	Result evaluate(void* opaque, const KzgsProviderFrame& frame, bool reset)
	{
		auto& context = *static_cast<XessContext*>(opaque);
		TemporalConversion conversion{};
		if (!convertTemporal(frame.jitter_x, frame.jitter_y, frame.motion_scale_x, frame.motion_scale_y,
				frame.camera->flags, context.info.depth_convention, conversion))
			return failure(KZGS_PROVIDER_ERROR_INPUT, KZGS_PROVIDER_REASON_MISSING_INPUT,
				"XeSS temporal parameters do not match the initialized depth convention.");
		std::lock_guard lock(sdkMutex);
		const Result thread = threadFailure();
		if (thread.code != KZGS_PROVIDER_OK)
			return thread;
		xess_result_t native = xessSetVelocityScale(context.handle, conversion.motionScaleX, conversion.motionScaleY);
		if (native != XESS_RESULT_SUCCESS)
			return sdkFailure(native, KZGS_PROVIDER_ERROR_EVALUATE, "XeSS velocity-scale configuration failed.");
		xess_vk_execute_params_t params{};
		params.colorTexture = resource(*frame.color);
		params.depthTexture = resource(*frame.depth);
		params.velocityTexture = resource(*frame.motion);
		params.outputTexture = resource(*frame.output);
		params.jitterOffsetX = conversion.jitterX;
		params.jitterOffsetY = conversion.jitterY;
		params.exposureScale = 1.0f;
		params.resetHistory = reset ? 1u : 0u;
		params.inputWidth = static_cast<uint32_t>(frame.source_rect.right - frame.source_rect.left);
		params.inputHeight = static_cast<uint32_t>(frame.source_rect.bottom - frame.source_rect.top);
		params.inputColorBase = {static_cast<uint32_t>(frame.source_rect.left), static_cast<uint32_t>(frame.source_rect.top)};
		params.inputMotionVectorBase = params.inputColorBase;
		params.inputDepthBase = params.inputColorBase;
		native = xessVKExecute(context.handle, frame.sync->command_buffer, &params);
		return native != XESS_RESULT_SUCCESS ?
			sdkFailure(native, KZGS_PROVIDER_ERROR_EVALUATE, "XeSS Vulkan evaluation failed.") : Result{};
	}

	void destroy(void* opaque, bool lost)
	{
		std::unique_ptr<XessContext> context(static_cast<XessContext*>(opaque));
		if (!context)
			return;
		std::lock_guard lock(sdkMutex);
		if (lost || (context->counted && sdkThread != std::this_thread::get_id()))
			abandonedDevice = true;
		if (context->handle && !abandonedDevice && xessDestroyContext(context->handle) != XESS_RESULT_SUCCESS)
			abandonedDevice = true;
		if (context->counted && --liveContexts == 0)
			sdkThread = {};
	}

	const Backend XessBackend{KZGS_PROVIDER_KIND_XESS, "Intel XeSS (local SDK)",
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, instanceRequirements, deviceRequirements, create, evaluate, destroy, true};
}

namespace kzgs::vendor { const Backend& backend() { return XessBackend; } }
