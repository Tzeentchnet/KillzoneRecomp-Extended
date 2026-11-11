// SPDX-License-Identifier: GPL-3.0+
#include "kzgs_provider_runtime.h"
#include "kzgs.h"
#include "kzgs_fsr1.h"
#include "kzgs_fsr1_runtime.h"
#include "kzgs_temporal_runtime.h"
#ifdef KZGS_HAS_FSR3
#include "kzgs_temporal_approximation_vulkan.h"
#endif

#include "GS/Renderers/Common/GSRenderer.h"
#include "GS/Renderers/Vulkan/GSTextureVK.h"
#include "common/RedtapeWindows.h"

#ifdef KZGS_PROVIDER_TESTING
#include "GS/GS.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
	struct FeatureDescription
	{
		VkStructureType type;
		size_t size;
		size_t offset;
		size_t count;
		const char* extension;
	};

#define FEATURE(type, stype, first, last, extension) \
	{stype, sizeof(type), offsetof(type, first), \
		(offsetof(type, last) - offsetof(type, first)) / sizeof(VkBool32) + 1, extension}
	constexpr FeatureDescription kFeatures[] = {
		FEATURE(VkPhysicalDevice16BitStorageFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES,
			storageBuffer16BitAccess, storageInputOutput16, nullptr),
		FEATURE(VkPhysicalDeviceVariablePointersFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES,
			variablePointersStorageBuffer, variablePointers, nullptr),
		FEATURE(VkPhysicalDevice8BitStorageFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES,
			storageBuffer8BitAccess, storagePushConstant8, VK_KHR_8BIT_STORAGE_EXTENSION_NAME),
		FEATURE(VkPhysicalDeviceShaderFloat16Int8Features,
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES,
			shaderFloat16, shaderInt8, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME),
		FEATURE(VkPhysicalDeviceDescriptorIndexingFeatures,
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
			shaderInputAttachmentArrayDynamicIndexing, runtimeDescriptorArray, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME),
		FEATURE(VkPhysicalDeviceBufferDeviceAddressFeatures,
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
			bufferDeviceAddress, bufferDeviceAddressMultiDevice, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME),
		FEATURE(VkPhysicalDeviceTimelineSemaphoreFeatures,
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
			timelineSemaphore, timelineSemaphore, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME),
		FEATURE(VkPhysicalDeviceScalarBlockLayoutFeatures,
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES,
			scalarBlockLayout, scalarBlockLayout, VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME),
		FEATURE(VkPhysicalDeviceShaderAtomicInt64Features,
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES,
			shaderBufferInt64Atomics, shaderSharedInt64Atomics, VK_KHR_SHADER_ATOMIC_INT64_EXTENSION_NAME),
		FEATURE(VkPhysicalDeviceVulkanMemoryModelFeatures,
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES,
			vulkanMemoryModel, vulkanMemoryModelAvailabilityVisibilityChains, VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME),
		FEATURE(VkPhysicalDeviceSynchronization2Features,
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
			synchronization2, synchronization2, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME),
		FEATURE(VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT,
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT,
			mutableDescriptorType, mutableDescriptorType, VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME),
		FEATURE(VkPhysicalDeviceShaderIntegerDotProductFeatures,
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES,
			shaderIntegerDotProduct, shaderIntegerDotProduct, VK_KHR_SHADER_INTEGER_DOT_PRODUCT_EXTENSION_NAME),
	};
#undef FEATURE

	struct FeatureNode
	{
		const FeatureDescription* description = nullptr;
		std::vector<std::max_align_t> storage;
		std::vector<VkBool32> required;

		VkBaseOutStructure* base() { return reinterpret_cast<VkBaseOutStructure*>(storage.data()); }
		unsigned char* bytes() { return reinterpret_cast<unsigned char*>(storage.data()); }
	};

	struct Context
	{
		void* handle = nullptr;
		std::unique_ptr<GSTexture> output;
		kzgs::fsr1::Plan plan{};
		VkFormat sourceFormat = VK_FORMAT_UNDEFINED;
		int sharpening = 0;
		uint64_t lastFence = 0;
#ifdef KZGS_HAS_FSR3
		std::unique_ptr<kzgs::TemporalApproximationVulkan> approximation;
#endif
	};

	struct State
	{
		HMODULE module = nullptr;
		KzgsUpscaler prepared = KzgsUpscaler::Off;
		kzgs::ProviderReport report = nullptr;
		KzgsProviderApi api{};
		KzgsProviderInfo info{};
		kzgs::ProviderStatus failure{};
		GSDeviceVK* device = nullptr;
		bool loaded = false;
		bool attemptedLoad = false;
		bool negotiationFailed = false;
		bool requirementsQueried = false;
		bool ready = false;
		bool enabled = false;
		bool wantsProvider = false;
		bool retainModule = false;
		bool deviceLost = false;
		bool resumeAfterDevice = false;
		bool reconfigurePending = false;
		int sharpening = 0;
		KzgsFsr3InputMode fsr3InputMode = KzgsFsr3InputMode::Strict;
		VkPhysicalDeviceFeatures requiredCore{};
		std::vector<std::string> deviceExtensions;
		std::vector<std::unique_ptr<FeatureNode>> featureNodes;
		std::unique_ptr<Context> active;
		std::deque<std::unique_ptr<Context>> retired;
		KzgsProviderResetReason resetReason = KZGS_PROVIDER_RESET_INITIAL;
		uint64_t nextFrame = 0;
		uint64_t lastRecordedFence = 0;
		uint64_t pendingPresentation = 0;
		bool awaitingPresent = false;
		bool vsyncAttempted = false;
	};

	State s;
	bool s_unresolvedDeviceWork = false;
	std::atomic<uint64_t> s_jitterSource{};
	std::atomic<uint64_t> s_jitterDisplay{};
	std::atomic<uint64_t> s_jitterGeneration{1};
	std::atomic<bool> s_jitterEnabled{};
#ifdef KZGS_PROVIDER_TESTING
	std::wstring s_testPath;
	bool s_testAllowSpatialMock = false;
#endif

	bool TestSpatialMockAllowed()
	{
#ifdef KZGS_PROVIDER_TESTING
		const size_t slash = s_testPath.find_last_of(L"\\/");
		const std::wstring name = s_testPath.substr(slash == std::wstring::npos ? 0 : slash + 1);
		return s_testAllowSpatialMock && name == L"kzgs_mock_provider.dll" && s.module && s.loaded &&
			GetProcAddress(s.module, "kzgs_mock_set_mode") && GetProcAddress(s.module, "kzgs_mock_get_stats") &&
			s.info.capabilities == KZGS_PROVIDER_CAP_SPATIAL &&
			s.info.required_inputs == KZGS_PROVIDER_INPUT_COLOR;
#else
		return false;
#endif
	}

	uint64_t PackExtent(uint32_t width, uint32_t height)
	{
		return (static_cast<uint64_t>(width) << 32) | height;
	}

	void DisableJitter()
	{
		if (s_jitterEnabled.exchange(false, std::memory_order_acq_rel))
			s_jitterGeneration.fetch_add(1, std::memory_order_release);
	}

	void PublishJitter(const kzgs::fsr1::Plan& plan)
	{
		const uint32_t sourceWidth = static_cast<uint32_t>(plan.source.right - plan.source.left);
		const uint32_t sourceHeight = static_cast<uint32_t>(plan.source.bottom - plan.source.top);
		const uint64_t source = PackExtent(sourceWidth, sourceHeight);
		const uint64_t display = PackExtent(
			static_cast<uint32_t>(plan.outputWidth), static_cast<uint32_t>(plan.outputHeight));
		const bool changed = s_jitterSource.load(std::memory_order_acquire) != source ||
			s_jitterDisplay.load(std::memory_order_acquire) != display ||
			!s_jitterEnabled.load(std::memory_order_acquire);
		s_jitterSource.store(source, std::memory_order_release);
		s_jitterDisplay.store(display, std::memory_order_release);
		s_jitterEnabled.store(true, std::memory_order_release);
		if (changed)
			s_jitterGeneration.fetch_add(1, std::memory_order_release);
	}

	KzgsProviderStatus EmptyStatus()
	{
		KzgsProviderStatus status{};
		status.struct_size = sizeof(status);
		return status;
	}

	void LatchTerminalFailure(KzgsProviderResult result)
	{
		if (result != KZGS_PROVIDER_ERROR_DEVICE_LOST &&
			result != KZGS_PROVIDER_ERROR_SYNCHRONIZATION_UNRESOLVED)
			return;
		s.deviceLost = true;
		s.ready = false;
		if (result == KZGS_PROVIDER_ERROR_SYNCHRONIZATION_UNRESOLVED)
		{
			s.retainModule = true;
			s_unresolvedDeviceWork = true;
		}
	}

	void Failure(KzgsUpscalerReason reason, KzgsProviderResult result,
		const KzgsProviderStatus* provider = nullptr, const char* message = nullptr, int32_t native = 0)
	{
		s.failure = {};
		s.failure.reason = reason;
		s.failure.result = result;
		s.failure.nativeCode = native;
		if (provider)
		{
			if (KZGS_PROVIDER_HAS_FIELD(provider, KzgsProviderStatus, reason))
				s.failure.providerReason = provider->reason;
			if (KZGS_PROVIDER_HAS_FIELD(provider, KzgsProviderStatus, native_code))
				s.failure.nativeCode = provider->native_code;
			if (KZGS_PROVIDER_HAS_FIELD(provider, KzgsProviderStatus, message) && provider->message)
				message = provider->message;
		}
		if (message)
		{
			const size_t length = strnlen_s(message, sizeof(s.failure.message) - 1);
			std::memcpy(s.failure.message, message, length);
			s.failure.message[length] = '\0';
		}
	}

	void Report(KzgsUpscalerReason reason, bool applied = false)
	{
		if (s.report)
		{
			kzgs::ProviderStatus status = s.failure;
			status.reason = reason;
			if (applied)
				status = {reason};
			s.report(status, applied);
		}
	}

	std::wstring ProviderPath(KzgsUpscaler provider)
	{
#ifdef KZGS_PROVIDER_TESTING
		if (!s_testPath.empty())
			return s_testPath;
#endif
		const wchar_t* fileName = nullptr;
		switch (provider)
		{
			case KzgsUpscaler::DLSS: fileName = L"kzgs_upscaler_dlss.dll"; break;
			case KzgsUpscaler::XeSS: fileName = L"kzgs_upscaler_xess.dll"; break;
			case KzgsUpscaler::FSR3: fileName = L"kzgs_upscaler_fsr3.dll"; break;
			default: return {};
		}
		std::wstring path(32768, L'\0');
		const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
		if (!length || length >= path.size())
			return {};
		path.resize(length);
		const size_t slash = path.find_last_of(L"\\/");
		if (slash == std::wstring::npos)
			return {};
		path.resize(slash + 1);
		path += L"upscalers\\";
		path += fileName;
		return path;
	}

	KzgsProviderKind ProviderKind(KzgsUpscaler provider)
	{
		switch (provider)
		{
			case KzgsUpscaler::DLSS: return KZGS_PROVIDER_KIND_DLSS;
			case KzgsUpscaler::XeSS: return KZGS_PROVIDER_KIND_XESS;
			case KzgsUpscaler::FSR3: return KZGS_PROVIDER_KIND_FSR3;
			default: return 0;
		}
	}

	bool CopyExtensions(const KzgsProviderRequirements& requirements, std::vector<std::string>& names)
	{
		if (!KZGS_PROVIDER_HAS_FIELD(&requirements, KzgsProviderRequirements, extensions) ||
			requirements.extension_count > 256 || (requirements.extension_count &&
				(!requirements.extensions ||
					reinterpret_cast<uintptr_t>(requirements.extensions) % alignof(const char*) != 0)))
			return false;
		names.clear();
		for (uint32_t i = 0; i < requirements.extension_count; ++i)
		{
			const char* name = requirements.extensions[i];
			if (!name)
				return false;
			const size_t length = strnlen_s(name, VK_MAX_EXTENSION_NAME_SIZE);
			if (!length || length == VK_MAX_EXTENSION_NAME_SIZE)
				return false;
			names.emplace_back(name, length);
		}
		return true;
	}

	bool AllAvailable(const std::vector<std::string>& names, const std::vector<VkExtensionProperties>& available)
	{
		return std::all_of(names.begin(), names.end(), [&](const std::string& name) {
			return std::any_of(available.begin(), available.end(), [&](const VkExtensionProperties& entry) {
				return name == entry.extensionName;
			});
		});
	}

	bool CopyFeatureBlocks(const KzgsProviderRequirements& requirements)
	{
		if (!KZGS_PROVIDER_HAS_FIELD(&requirements, KzgsProviderRequirements, feature_block_stride) ||
			requirements.feature_block_count > 32 ||
			(requirements.feature_block_count && (!requirements.feature_blocks ||
				requirements.feature_block_stride < KZGS_PROVIDER_FIELD_END(KzgsProviderFeatureBlock, required) ||
				requirements.feature_block_stride > 4096 ||
				requirements.feature_block_stride % alignof(KzgsProviderFeatureBlock) != 0 ||
				reinterpret_cast<uintptr_t>(requirements.feature_blocks) % alignof(KzgsProviderFeatureBlock) != 0)))
			return false;

		s.featureNodes.clear();
		for (uint32_t i = 0; i < requirements.feature_block_count; ++i)
		{
			const auto* const blockBytes = reinterpret_cast<const unsigned char*>(requirements.feature_blocks) +
				static_cast<size_t>(i) * requirements.feature_block_stride;
			const KzgsProviderFeatureBlock& block = *reinterpret_cast<const KzgsProviderFeatureBlock*>(blockBytes);
			if (!KZGS_PROVIDER_HAS_FIELD(&block, KzgsProviderFeatureBlock, required) ||
				block.struct_size > requirements.feature_block_stride)
				return false;
			const auto found = std::find_if(std::begin(kFeatures), std::end(kFeatures),
				[&](const FeatureDescription& feature) { return feature.type == block.s_type; });
			if (found == std::end(kFeatures) || !block.feature_count || block.feature_count > found->count ||
				!block.required || reinterpret_cast<uintptr_t>(block.required) % alignof(VkBool32) != 0 ||
				(found->extension && std::find(s.deviceExtensions.begin(), s.deviceExtensions.end(),
					found->extension) == s.deviceExtensions.end()) ||
				std::any_of(s.featureNodes.begin(), s.featureNodes.end(),
					[&](const std::unique_ptr<FeatureNode>& node) { return node->description->type == block.s_type; }))
				return false;

			auto node = std::make_unique<FeatureNode>();
			node->description = &*found;
			node->storage.resize((found->size + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
			std::memset(node->storage.data(), 0, node->storage.size() * sizeof(std::max_align_t));
			node->base()->sType = block.s_type;
			node->required.resize(found->count, VK_FALSE);
			for (uint32_t bit = 0; bit < block.feature_count; ++bit)
			{
				if (block.required[bit] > VK_TRUE)
					return false;
				node->required[bit] = block.required[bit];
			}
			s.featureNodes.push_back(std::move(node));
		}
		for (size_t i = 0; i < s.featureNodes.size(); ++i)
			s.featureNodes[i]->base()->pNext =
				i + 1 < s.featureNodes.size() ? s.featureNodes[i + 1]->base() : nullptr;
		return true;
	}

	void DestroyContext(std::unique_ptr<Context>& context, KzgsProviderDestroyReason reason, uint64_t completed)
	{
#ifdef KZGS_HAS_FSR3
		if (context && context->approximation && reason == KZGS_PROVIDER_DESTROY_DEVICE_LOST)
			context->approximation->Abandon();
#endif
		if (context && context->handle)
		{
			KzgsProviderDestroyInfo info{sizeof(info), reason, completed};
			s.api.destroy(context->handle, &info);
			context->handle = nullptr;
		}
		context.reset();
	}

	void Collect()
	{
		if (!s.device || s_unresolvedDeviceWork)
			return;
		const uint64_t completed = s.device->GetCompletedFenceCounter();
		for (auto it = s.retired.begin(); it != s.retired.end();)
		{
			if ((*it)->lastFence <= completed)
			{
				DestroyContext(*it, KZGS_PROVIDER_DESTROY_COMPLETED, completed);
				it = s.retired.erase(it);
			}
			else
				++it;
		}
	}

	void Retire(KzgsProviderResetReason reason)
	{
		DisableJitter();
		s.awaitingPresent = false;
		s.resetReason = reason;
		if (s.active)
		{
			kzgs::Fsr1InvalidateCapture();
			s.retired.push_back(std::move(s.active));
		}
		Collect();
	}

	bool Matches(const Context& context, const kzgs::fsr1::Plan& plan, VkFormat format)
	{
		return context.plan.sourceWidth == plan.sourceWidth && context.plan.sourceHeight == plan.sourceHeight &&
			context.plan.source.left == plan.source.left && context.plan.source.top == plan.source.top &&
			context.plan.source.right == plan.source.right && context.plan.source.bottom == plan.source.bottom &&
			context.plan.outputWidth == plan.outputWidth && context.plan.outputHeight == plan.outputHeight &&
			context.sourceFormat == format && context.sharpening == s.sharpening;
	}

	KzgsProviderResult KZGS_PROVIDER_CALL RunInitialization(void* hostData,
		KzgsProviderInitializationWork work, void* workData, KzgsProviderStatus* status)
	{
		auto* device = static_cast<GSDeviceVK*>(hostData);
		if (!device || !work)
		{
			if (status)
			{
				const KzgsProviderStatus value{sizeof(value), KZGS_PROVIDER_ERROR_ARGUMENT,
					KZGS_PROVIDER_REASON_PROVIDER_FAILURE, 0,
					"The provider initialization callback is invalid."};
				std::memcpy(status, &value, std::min<size_t>(status->struct_size, sizeof(value)));
			}
			return KZGS_PROVIDER_ERROR_ARGUMENT;
		}
		device->WaitForGPUIdle();
		work(workData);
		if (status)
		{
			const KzgsProviderStatus value{sizeof(value), KZGS_PROVIDER_OK,
				KZGS_PROVIDER_REASON_NONE, 0, nullptr};
			std::memcpy(status, &value, std::min<size_t>(status->struct_size, sizeof(value)));
		}
		return KZGS_PROVIDER_OK;
	}

	bool CreateContext(const kzgs::fsr1::Plan& plan, VkFormat sourceFormat)
	{
		auto context = std::make_unique<Context>();
		context->plan = plan;
		context->sourceFormat = sourceFormat;
		context->sharpening = s.sharpening;
		context->output.reset(s.device->CreateShaderWriteTarget(
			plan.outputWidth, plan.outputHeight, GSTexture::Format::Color, false, false));
		if (!context->output)
		{
			Failure(KzgsUpscalerReason::InitializationFailed, KZGS_PROVIDER_ERROR_CREATE, nullptr,
				"Could not allocate the provider output.");
			return false;
		}

		KzgsProviderCreateInfo info{};
		info.struct_size = sizeof(info);
		info.instance = s.device->GetVulkanInstance();
		info.physical_device = s.device->GetPhysicalDevice();
		info.device = s.device->GetDevice();
		info.queue = s.device->GetGraphicsQueue();
		info.queue_family = s.device->GetGraphicsQueueFamilyIndex();
		info.get_instance_proc_addr = vkGetInstanceProcAddr;
		info.get_device_proc_addr = vkGetDeviceProcAddr;
		const bool experimentalFsr3 = s.prepared == KzgsUpscaler::FSR3 &&
			s.fsr3InputMode == KzgsFsr3InputMode::Experimental;
		if (experimentalFsr3)
		{
			info.source_extent = {
				static_cast<uint32_t>(plan.source.right - plan.source.left),
				static_cast<uint32_t>(plan.source.bottom - plan.source.top)};
			info.source_rect = {0, 0,
				static_cast<int32_t>(info.source_extent.width),
				static_cast<int32_t>(info.source_extent.height)};
		}
		else
		{
			info.source_extent = {
				static_cast<uint32_t>(plan.sourceWidth), static_cast<uint32_t>(plan.sourceHeight)};
			info.source_rect = {
				plan.source.left, plan.source.top, plan.source.right, plan.source.bottom};
		}
		info.source_format = sourceFormat;
		info.destination_extent = {static_cast<uint32_t>(plan.outputWidth), static_cast<uint32_t>(plan.outputHeight)};
		info.destination_format = static_cast<GSTextureVK*>(context->output.get())->GetVkFormat();
		info.color_encoding = experimentalFsr3 ?
			KZGS_PROVIDER_COLOR_LINEAR_SDR : KZGS_PROVIDER_COLOR_UNKNOWN;
		info.sharpening = static_cast<uint32_t>(s.sharpening);
		const KzgsProviderInitialization initialization{sizeof(initialization),
			KZGS_PROVIDER_INIT_EXCLUSIVE_DEVICE_IDLE |
				KZGS_PROVIDER_INIT_SDK_EXECUTION_QUALIFIED,
			s.device, RunInitialization};
		info.initialization = &initialization;
		info.depth_convention = experimentalFsr3 ?
			KZGS_PROVIDER_DEPTH_FORWARD : KZGS_PROVIDER_DEPTH_UNKNOWN;
		KzgsProviderStatus status = EmptyStatus();
		const KzgsProviderResult result = s.api.create(&info, &context->handle, &status);
		if (result != KZGS_PROVIDER_OK || !context->handle)
		{
			LatchTerminalFailure(result);
			Failure(result == KZGS_PROVIDER_ERROR_DEVICE_LOST ? KzgsUpscalerReason::DeviceLost :
				KzgsUpscalerReason::InitializationFailed,
				result == KZGS_PROVIDER_OK ? KZGS_PROVIDER_ERROR_CREATE : result, &status);
			if (result == KZGS_PROVIDER_ERROR_SYNCHRONIZATION_UNRESOLVED)
				context.reset(); // The host output has not been exposed to the provider.
			else
				DestroyContext(context, s.deviceLost ? KZGS_PROVIDER_DESTROY_DEVICE_LOST :
					KZGS_PROVIDER_DESTROY_COMPLETED, s.device->GetCompletedFenceCounter());
			return false;
		}

		KzgsProviderResetInfo reset{sizeof(reset), s.resetReason, s.device->GetCompletedFenceCounter()};
		status = EmptyStatus();
		const KzgsProviderResult resetResult = s.api.reset(context->handle, &reset, &status);
		if (resetResult != KZGS_PROVIDER_OK)
		{
			LatchTerminalFailure(resetResult);
			Failure(resetResult == KZGS_PROVIDER_ERROR_DEVICE_LOST ? KzgsUpscalerReason::DeviceLost :
				KzgsUpscalerReason::ResetFailed,
				resetResult, &status);
			if (resetResult == KZGS_PROVIDER_ERROR_SYNCHRONIZATION_UNRESOLVED)
				context.reset(); // The host output has not been exposed to the provider.
			else
				DestroyContext(context, s.deviceLost ? KZGS_PROVIDER_DESTROY_DEVICE_LOST :
					KZGS_PROVIDER_DESTROY_COMPLETED, s.device->GetCompletedFenceCounter());
			return false;
		}
#ifdef KZGS_HAS_FSR3
		if (experimentalFsr3)
		{
			std::string approximationError;
			context->approximation = kzgs::TemporalApproximationVulkan::Create(*s.device,
				info.source_extent, approximationError);
			if (!context->approximation)
			{
				Failure(KzgsUpscalerReason::InitializationFailed, KZGS_PROVIDER_ERROR_CREATE,
					nullptr, approximationError.c_str());
				DestroyContext(context, KZGS_PROVIDER_DESTROY_COMPLETED,
					s.device->GetCompletedFenceCounter());
				return false;
			}
		}
#else
		if (experimentalFsr3)
		{
			Failure(KzgsUpscalerReason::InitializationFailed, KZGS_PROVIDER_ERROR_UNSUPPORTED,
				nullptr, "This build does not include the FSR 3 approximation subsystem.");
			DestroyContext(context, KZGS_PROVIDER_DESTROY_COMPLETED,
				s.device->GetCompletedFenceCounter());
			return false;
		}
#endif
		s.active = std::move(context);
		s.resetReason = KZGS_PROVIDER_RESET_INITIAL;
		return true;
	}

	KzgsProviderImage Image(const GSDeviceVK::ExternalImage& image, bool output)
	{
		KzgsProviderImage result{};
		result.struct_size = sizeof(result);
		result.image = image.image;
		result.view = image.view;
		result.format = image.format;
		result.extent = image.extent;
		result.subresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		result.layout = image.layout;
		result.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		if (output)
			result.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
		result.queue_family = s.device->GetGraphicsQueueFamilyIndex();
		result.available_stages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		result.available_access = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		return result;
	}

	struct RecordData
	{
		const GSPresentationUpscaleFrame* frame;
		uint64_t frameId;
		bool experimentalFsr3;
		KzgsTemporalFrame temporal;
	};

	bool Record(void* userdata, const GSDeviceVK::ExternalCommands& commands)
	{
		const RecordData& data = *static_cast<const RecordData*>(userdata);
		s.active->lastFence = commands.fence_counter;
		s.lastRecordedFence = std::max(s.lastRecordedFence, commands.fence_counter);
		KzgsProviderFrame frame{};
		frame.struct_size = sizeof(frame);
		frame.frame_id = data.frameId;
		frame.presentation_id = data.frame->presentation_id;
		const KzgsProviderSync sync{sizeof(sync), commands.command_buffer, s.device->GetGraphicsQueue(),
			s.device->GetGraphicsQueueFamilyIndex(), commands.fence_counter, s.device->GetCompletedFenceCounter()};
		const KzgsProviderImage output = Image(commands.output, true);
		frame.sync = &sync;
		frame.output = &output;
		frame.destination_rect = {0, 0, s.active->plan.outputWidth, s.active->plan.outputHeight};
		frame.presentation_rect[0] = data.frame->destination_rect.x;
		frame.presentation_rect[1] = data.frame->destination_rect.y;
		frame.presentation_rect[2] = data.frame->destination_rect.z;
		frame.presentation_rect[3] = data.frame->destination_rect.w;
		frame.sharpening = static_cast<uint32_t>(s.sharpening);
		KzgsProviderImage color{};
		KzgsProviderCamera camera{};
#ifdef KZGS_HAS_FSR3
		kzgs::TemporalApproximationImages approximation{};
#endif
		KzgsTemporalFrame temporal = data.temporal;
		if (data.experimentalFsr3)
		{
#ifdef KZGS_HAS_FSR3
			const KzgsProviderRect sourceRect{s.active->plan.source.left, s.active->plan.source.top,
				s.active->plan.source.right, s.active->plan.source.bottom};
			if (!s.active->approximation ||
				!s.active->approximation->Record(commands, sourceRect,
					temporal.jitterX, temporal.jitterY,
					temporal.resetReasons != 0, approximation))
			{
				const char* message = s.active->approximation ?
					s.active->approximation->LastError() :
					"The FSR 3 approximation context is unavailable.";
				Failure(KzgsUpscalerReason::DispatchFailed, KZGS_PROVIDER_ERROR_EVALUATE,
					nullptr, message);
				return false;
			}
			const auto temporalImage = [&](const KzgsProviderImage& image, uint64_t id) {
				KzgsTemporalImage result;
				result.frameId = temporal.frameId;
				result.sceneId = temporal.sceneId;
				result.resourceId = id;
				result.width = image.extent.width;
				result.height = image.extent.height;
				result.rect = {0, 0, static_cast<int32_t>(image.extent.width),
					static_cast<int32_t>(image.extent.height)};
				result.backend = KzgsTemporalBackend::Vulkan;
				result.nativeFormat = static_cast<uint32_t>(image.format);
				result.rendererFormat = static_cast<uint32_t>(GSTexture::Format::Color);
				return result;
			};
			temporal.mergedColor = temporalImage(approximation.color, approximation.colorId);
			temporal.depth = temporalImage(approximation.depth, approximation.depthId);
			temporal.motion = temporalImage(approximation.motion, approximation.motionId);
			temporal.reactive = temporalImage(approximation.reactive, approximation.reactiveId);
			temporal.validInputs |= KZGS_TEMPORAL_DEPTH | KZGS_TEMPORAL_MOTION |
				KZGS_TEMPORAL_REACTIVE;
			temporal.approximateInputs |= KZGS_TEMPORAL_DEPTH | KZGS_TEMPORAL_MOTION |
				KZGS_TEMPORAL_REACTIVE;
			temporal.depthDirection = KzgsTemporalDepthDirection::Forward;
			temporal.motionUnits = KzgsTemporalMotionUnits::Pixels;
			temporal.motionDirection = KzgsTemporalMotionDirection::PreviousMinusCurrent;
			temporal.motionIncludesJitter = false;
			temporal.colorEncoding = KzgsTemporalColorEncoding::LinearSDR;
			const kzgs::temporal::Decision decision = kzgs::TemporalBeginDispatch(temporal);
			if (!decision.ready)
			{
				s.active->approximation->PresentationComplete(false);
				Failure(KzgsUpscalerReason::RequiredInputUnavailable, KZGS_PROVIDER_ERROR_INPUT,
					nullptr, "The experimental FSR 3 temporal frame failed validation.");
				s.failure.providerReason = KZGS_PROVIDER_REASON_UNQUALIFIED;
				return false;
			}
			frame.valid_inputs = temporal.validInputs & UINT64_C(0xff);
			frame.approximate_inputs = temporal.approximateInputs & UINT64_C(0xff);
			frame.color = &approximation.color;
			frame.depth = &approximation.depth;
			frame.motion = &approximation.motion;
			frame.reactive_mask = &approximation.reactive;
			frame.source_rect = {0, 0,
				static_cast<int32_t>(approximation.color.extent.width),
				static_cast<int32_t>(approximation.color.extent.height)};
			frame.color_encoding = KZGS_PROVIDER_COLOR_LINEAR_SDR;
			frame.jitter_x = temporal.jitterX;
			frame.jitter_y = temporal.jitterY;
			frame.motion_scale_x = 1.0f;
			frame.motion_scale_y = 1.0f;
			frame.frame_time_ms = temporal.frameTimeMs;
			camera.struct_size = sizeof(camera);
			camera.near_plane = temporal.camera.nearPlane;
			camera.far_plane = temporal.camera.farPlane;
			camera.vertical_fov_radians = temporal.camera.verticalFovRadians;
			camera.valid_fields = KZGS_PROVIDER_CAMERA_VALID_SCALARS;
			frame.camera = &camera;
#else
			Failure(KzgsUpscalerReason::DispatchFailed, KZGS_PROVIDER_ERROR_UNSUPPORTED,
				nullptr, "This build does not include FSR 3 approximation support.");
			return false;
#endif
		}
		else
		{
			color = Image(commands.source, false);
			frame.valid_inputs = KZGS_PROVIDER_INPUT_COLOR;
			frame.color = &color;
			frame.source_rect = {s.active->plan.source.left, s.active->plan.source.top,
				s.active->plan.source.right, s.active->plan.source.bottom};
			frame.color_encoding = KZGS_PROVIDER_COLOR_UNKNOWN;
		}
		KzgsProviderStatus status = EmptyStatus();
		const KzgsProviderResult result = s.api.evaluate(s.active->handle, &frame, &status);
		if (result != KZGS_PROVIDER_OK)
		{
			if (data.experimentalFsr3)
			{
				kzgs::TemporalDispatchResult(false);
#ifdef KZGS_HAS_FSR3
				s.active->approximation->PresentationComplete(false);
#endif
			}
			kzgs::TemporalReset(KZGS_TEMPORAL_RESET_DISPATCH_FAILED);
			LatchTerminalFailure(result);
			Failure(result == KZGS_PROVIDER_ERROR_DEVICE_LOST ? KzgsUpscalerReason::DeviceLost :
				KzgsUpscalerReason::DispatchFailed, result, &status);
			return false;
		}
		if (data.experimentalFsr3)
			kzgs::TemporalDispatchResult(true);
		return true;
	}

	bool Upscale(void*, const GSPresentationUpscaleFrame& frame, GSPresentationUpscaleOutput& output)
	{
		s.vsyncAttempted = true;
		const bool experimentalFsr3 = s.prepared == KzgsUpscaler::FSR3 &&
			s.fsr3InputMode == KzgsFsr3InputMode::Experimental &&
			KZGS_PROVIDER_VERSION_MINOR(s.api.abi_version) >= 2 &&
			(s.info.capabilities & KZGS_PROVIDER_CAP_APPROXIMATE_INPUTS);
		if (!TestSpatialMockAllowed() && !experimentalFsr3)
		{
			kzgs::TemporalReset(KZGS_TEMPORAL_RESET_MISSING_INPUT);
			Retire(KZGS_PROVIDER_RESET_DISCONTINUITY);
			Failure(KzgsUpscalerReason::RequiredInputUnavailable, KZGS_PROVIDER_ERROR_INPUT, nullptr,
				"Matched temporal inputs are not qualified for this provider and input policy.");
			s.failure.providerReason = KZGS_PROVIDER_REASON_MISSING_INPUT;
			Report(s.failure.reason);
			return false;
		}
		if (s.deviceLost)
		{
			Report(s_unresolvedDeviceWork ? s.failure.reason : KzgsUpscalerReason::DeviceLost);
			return false;
		}
		if (!s.enabled || !s.device || frame.device != s.device || !frame.texture ||
			frame.texture->GetFormat() != GSTexture::Format::Color)
		{
			kzgs::TemporalReset(KZGS_TEMPORAL_RESET_DISPATCH_FAILED);
			Failure(KzgsUpscalerReason::DispatchFailed, KZGS_PROVIDER_ERROR_ARGUMENT);
			Report(s.failure.reason);
			return false;
		}
		Collect();
		if (frame.is_redraw || frame.source_may_repeat)
		{
			kzgs::TemporalReset(KZGS_TEMPORAL_RESET_DUPLICATE);
			Retire(KZGS_PROVIDER_RESET_DISCONTINUITY);
			Failure(KzgsUpscalerReason::DuplicatePresent, KZGS_PROVIDER_OK);
			Report(s.failure.reason);
			return false;
		}
		if (s.active && (s.awaitingPresent || !s.pendingPresentation ||
			frame.presentation_id != s.pendingPresentation + 1))
			Retire(KZGS_PROVIDER_RESET_DISCONTINUITY);

		const VkPhysicalDeviceLimits& limits = s.device->GetDeviceProperties().limits;
		kzgs::fsr1::Plan plan{};
		const auto result = kzgs::fsr1::MakePlan(frame.texture->GetWidth(), frame.texture->GetHeight(),
			{frame.source_rect.x, frame.source_rect.y, frame.source_rect.z, frame.source_rect.w},
			frame.destination_rect.x, frame.destination_rect.y, frame.destination_rect.z, frame.destination_rect.w,
			std::min(s.device->GetMaxTextureSize(), limits.maxImageDimension2D),
			limits.maxComputeWorkGroupCount[0], limits.maxComputeWorkGroupCount[1], plan);
		if (result != kzgs::fsr1::PlanResult::Ready)
		{
			Retire(KZGS_PROVIDER_RESET_DISCONTINUITY);
			Failure(result == kzgs::fsr1::PlanResult::NoUpscaleNeeded ? KzgsUpscalerReason::NoUpscaleNeeded :
				KzgsUpscalerReason::InvalidDimensions, KZGS_PROVIDER_ERROR_ARGUMENT);
			Report(s.failure.reason);
			return false;
		}
		const VkFormat format = static_cast<GSTextureVK*>(frame.texture)->GetVkFormat();
		if (s.active && !Matches(*s.active, plan, format))
			Retire(KZGS_PROVIDER_RESET_RESIZE);
		if (!s.active && s.retired.size() >= 128)
		{
			Failure(KzgsUpscalerReason::InitializationFailed, KZGS_PROVIDER_ERROR_CREATE, nullptr,
				"Provider contexts are awaiting GPU completion.");
			Report(s.failure.reason);
			return false;
		}
		if (!s.active && !CreateContext(plan, format))
		{
			Report(s.failure.reason);
			return false;
		}
		if (experimentalFsr3)
			PublishJitter(plan);

		KzgsTemporalFrame temporal;
		if (experimentalFsr3)
		{
			constexpr uint64_t hostRequired = KZGS_TEMPORAL_COLOR | KZGS_TEMPORAL_JITTER |
				KZGS_TEMPORAL_CAMERA | KZGS_TEMPORAL_FRAME_TIME;
			if (!kzgs::TemporalGetPendingFrame(temporal) ||
				(temporal.validInputs & hostRequired) != hostRequired ||
				!temporal.motionPreviousFrameId)
			{
				kzgs::TemporalReset(KZGS_TEMPORAL_RESET_MISSING_INPUT);
				Failure(KzgsUpscalerReason::RequiredInputUnavailable, KZGS_PROVIDER_ERROR_INPUT,
					nullptr, "Waiting for matched FSR 3 jitter, camera scalars, and game frame time.");
				s.failure.providerReason = KZGS_PROVIDER_REASON_MISSING_INPUT;
				Report(s.failure.reason);
				return false;
			}
		}
		s.failure = {};
		const RecordData data{&frame, ++s.nextFrame, experimentalFsr3, temporal};
		if (!s.device->ExecuteExternalCommands(frame.texture, s.active->output.get(), Record,
				const_cast<RecordData*>(&data)))
		{
			kzgs::TemporalReset(KZGS_TEMPORAL_RESET_DISPATCH_FAILED);
			if (s.failure.reason != KzgsUpscalerReason::DispatchFailed &&
				s.failure.reason != KzgsUpscalerReason::DeviceLost)
				Failure(KzgsUpscalerReason::DispatchFailed, KZGS_PROVIDER_ERROR_EVALUATE, nullptr,
					"External command recording was rejected.");
			Retire(KZGS_PROVIDER_RESET_DISCONTINUITY);
			Report(s.failure.reason);
			return false;
		}
		s.pendingPresentation = frame.presentation_id;
		s.awaitingPresent = true;
		output.texture = s.active->output.get();
		output.source_rect = GSVector4i(0, 0, plan.outputWidth, plan.outputHeight);
		return true;
	}

	void Presented(void*, uint64_t presentation, bool succeeded, bool used)
	{
		if (!s.device || !s.enabled || !s.awaitingPresent || presentation != s.pendingPresentation)
			return;
		s.awaitingPresent = false;
		const bool committed = succeeded && used;
#ifdef KZGS_HAS_FSR3
		if (s.active && s.active->approximation)
			s.active->approximation->PresentationComplete(committed);
#endif
		if (s.prepared == KzgsUpscaler::FSR3 &&
			s.fsr3InputMode == KzgsFsr3InputMode::Experimental)
			kzgs::TemporalPresentationResult(presentation, committed);
		// Presentation or CAS may have split recording across command buffers. The current
		// counter after presentation conservatively covers all reads of the output image.
		s.active->lastFence = std::max(s.active->lastFence, s.device->GetCurrentFenceCounter());
		s.lastRecordedFence = std::max(s.lastRecordedFence, s.active->lastFence);
		if (!committed)
		{
			kzgs::TemporalReset(KZGS_TEMPORAL_RESET_PRESENT_FAILED);
			Retire(KZGS_PROVIDER_RESET_PRESENT_FAILED);
			Failure(KzgsUpscalerReason::PresentFailed, KZGS_PROVIDER_ERROR_EVALUATE);
			Report(s.failure.reason);
		}
		else
			Report(KzgsUpscalerReason::Active, true);
	}
}

namespace kzgs
{
	void ProviderPrepare(KzgsUpscaler requested, bool vulkan, ProviderReport report)
	{
		DisableJitter();
		s = {};
		s.report = report;
		s.prepared = requested;
		if (!vulkan || !kzgsIsTemporalUpscaler(requested))
			return;
		s.attemptedLoad = true;
		if (s_unresolvedDeviceWork)
		{
			Failure(KzgsUpscalerReason::InitializationFailed, KZGS_PROVIDER_ERROR_INTERNAL, nullptr,
				"A previous Vulkan device could not prove GPU completion; providers remain disabled.");
			return;
		}
		const std::wstring path = ProviderPath(requested);
		s.module = path.empty() ? nullptr : LoadLibraryExW(path.c_str(), nullptr,
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
		if (!s.module)
		{
			Failure(KzgsUpscalerReason::ProviderMissing, KZGS_PROVIDER_ERROR_UNSUPPORTED, nullptr,
				"Provider DLL could not be loaded.", static_cast<int32_t>(GetLastError()));
			return;
		}
		const auto getApi = reinterpret_cast<KzgsProviderGetApi>(
			GetProcAddress(s.module, "kzgs_upscaler_get_api"));
		if (!getApi)
		{
			Failure(KzgsUpscalerReason::ProviderInvalid, KZGS_PROVIDER_ERROR_VERSION, nullptr,
				"Provider entry point is missing.");
			return;
		}
		s.api.struct_size = sizeof(s.api);
		KzgsProviderStatus status = EmptyStatus();
		const KzgsProviderResult apiResult = getApi(KZGS_PROVIDER_ABI_VERSION, &s.api, &status);
		if (apiResult != KZGS_PROVIDER_OK ||
			!KZGS_PROVIDER_HAS_FIELD(&s.api, KzgsProviderApi, destroy) ||
			KZGS_PROVIDER_VERSION_MAJOR(s.api.abi_version) != KZGS_PROVIDER_VERSION_MAJOR(KZGS_PROVIDER_ABI_VERSION) ||
			KZGS_PROVIDER_VERSION_MINOR(s.api.abi_version) > KZGS_PROVIDER_VERSION_MINOR(KZGS_PROVIDER_ABI_VERSION))
		{
			Failure(KzgsUpscalerReason::ProviderVersionMismatch,
				apiResult == KZGS_PROVIDER_OK ? KZGS_PROVIDER_ERROR_VERSION : apiResult, &status);
			return;
		}
		if (!s.api.query_info || !s.api.instance_requirements || !s.api.device_requirements ||
			!s.api.create || !s.api.evaluate || !s.api.reset || !s.api.destroy)
		{
			Failure(KzgsUpscalerReason::ProviderInvalid, KZGS_PROVIDER_ERROR_VERSION, nullptr,
				"Provider API is missing required functions.");
			return;
		}
		s.info.struct_size = sizeof(s.info);
		status = EmptyStatus();
		const KzgsProviderResult infoResult = s.api.query_info(&s.info, &status);
		const KzgsProviderKind kind = ProviderKind(requested);
		if (infoResult != KZGS_PROVIDER_OK ||
			!KZGS_PROVIDER_HAS_FIELD(&s.info, KzgsProviderInfo, supported_color_encodings) ||
			s.info.kind != kind || !(s.info.capabilities & (KZGS_PROVIDER_CAP_SPATIAL | KZGS_PROVIDER_CAP_TEMPORAL)) ||
			!(s.info.required_inputs & KZGS_PROVIDER_INPUT_COLOR) ||
			(s.info.required_inputs & ~s.info.supported_inputs))
		{
			Failure(KzgsUpscalerReason::ProviderInvalid,
				infoResult == KZGS_PROVIDER_OK ? KZGS_PROVIDER_ERROR_UNSUPPORTED : infoResult, &status);
			return;
		}
		s.loaded = true;
	}

	void ProviderRemove()
	{
		DisableJitter();
		if (s.module && !s.retainModule)
			FreeLibrary(s.module);
		s = {};
	}

	bool ProviderHasNegotiation()
	{
		return s.loaded;
	}

	void ProviderInstanceExtensions(void*, const std::vector<VkExtensionProperties>& available,
		std::vector<std::string>& requested)
	{
		if (!s.loaded)
			return;
		s.negotiationFailed = false;
		s.requirementsQueried = false;
		s.ready = false;
		KzgsProviderRequirements requirements{};
		requirements.struct_size = sizeof(requirements);
		KzgsProviderStatus status = EmptyStatus();
		const KzgsProviderResult result = s.api.instance_requirements(&requirements, &status);
		if (result != KZGS_PROVIDER_OK || !CopyExtensions(requirements, requested))
		{
			Failure(KzgsUpscalerReason::InstanceExtensionUnavailable,
				result == KZGS_PROVIDER_OK ? KZGS_PROVIDER_ERROR_EXTENSIONS : result, &status);
			s.negotiationFailed = true;
		}
		else if (!AllAvailable(requested, available))
		{
			Failure(KzgsUpscalerReason::InstanceExtensionUnavailable, KZGS_PROVIDER_ERROR_EXTENSIONS,
				nullptr, "A required Vulkan instance extension is unavailable.");
			s.negotiationFailed = true;
		}
	}

	void ProviderDeviceExtensions(void*, VkInstance instance, VkPhysicalDevice physical,
		const std::vector<VkExtensionProperties>& available, std::vector<std::string>& requested)
	{
		if (!s.loaded || s.negotiationFailed)
			return;
		KzgsProviderDeviceQuery query{sizeof(query), instance, physical, vkGetInstanceProcAddr};
		KzgsProviderRequirements requirements{};
		requirements.struct_size = sizeof(requirements);
		KzgsProviderStatus status = EmptyStatus();
		const KzgsProviderResult result = s.api.device_requirements(&query, &requirements, &status);
		if (result != KZGS_PROVIDER_OK || !CopyExtensions(requirements, s.deviceExtensions))
		{
			Failure(KzgsUpscalerReason::DeviceExtensionUnavailable,
				result == KZGS_PROVIDER_OK ? KZGS_PROVIDER_ERROR_EXTENSIONS : result, &status);
			s.negotiationFailed = true;
			return;
		}
		requested = s.deviceExtensions;
		if (!AllAvailable(requested, available))
		{
			Failure(KzgsUpscalerReason::DeviceExtensionUnavailable, KZGS_PROVIDER_ERROR_EXTENSIONS,
				nullptr, "A required Vulkan device extension is unavailable.");
			s.negotiationFailed = true;
			return;
		}
		if (!KZGS_PROVIDER_HAS_FIELD(&requirements, KzgsProviderRequirements, core_features) ||
			!CopyFeatureBlocks(requirements))
		{
			Failure(KzgsUpscalerReason::DeviceFeatureUnavailable, KZGS_PROVIDER_ERROR_FEATURES,
				nullptr, "An unsupported or malformed Vulkan feature block was requested.");
			s.negotiationFailed = true;
			return;
		}
		s.requiredCore = requirements.core_features;
		s.requirementsQueried = true;
	}

	bool ProviderDeviceFeatures(void*, VkInstance, VkPhysicalDevice physical,
		GSDeviceVK::IntegrationDeviceFeatures& requested)
	{
		if (!s.loaded)
			return true;
		if (s.negotiationFailed || !s.requirementsQueried)
			return false;
		VkPhysicalDeviceFeatures available{};
		vkGetPhysicalDeviceFeatures(physical, &available);
		constexpr size_t count = sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32);
		std::array<VkBool32, count> have{}, need{};
		std::memcpy(have.data(), &available, sizeof(available));
		std::memcpy(need.data(), &s.requiredCore, sizeof(s.requiredCore));
		for (size_t i = 0; i < count; ++i)
		{
			if (need[i] > VK_TRUE || (need[i] && !have[i]))
			{
				Failure(KzgsUpscalerReason::DeviceFeatureUnavailable, KZGS_PROVIDER_ERROR_FEATURES,
					nullptr, "A required Vulkan core feature is unavailable.");
				s.negotiationFailed = true;
				return false;
			}
		}

		if (!s.featureNodes.empty())
		{
			VkPhysicalDeviceFeatures2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
			query.pNext = s.featureNodes.front()->base();
			vkGetPhysicalDeviceFeatures2(physical, &query);
			for (const auto& node : s.featureNodes)
			{
				for (size_t i = 0; i < node->description->count; ++i)
				{
					VkBool32 supported = VK_FALSE;
					std::memcpy(&supported, node->bytes() + node->description->offset + i * sizeof(VkBool32),
						sizeof(supported));
					if (node->required[i] && !supported)
					{
						Failure(KzgsUpscalerReason::DeviceFeatureUnavailable, KZGS_PROVIDER_ERROR_FEATURES,
							nullptr, "A required Vulkan extension feature is unavailable.");
						s.negotiationFailed = true;
						return false;
					}
				}
				std::memcpy(node->bytes() + node->description->offset, node->required.data(),
					node->required.size() * sizeof(VkBool32));
			}
			requested.extension_chain = s.featureNodes.front()->base();
		}
		requested.core = s.requiredCore;
		return true;
	}

	void ProviderDeviceCreated(GSDeviceVK& device, bool instanceExtensions, bool deviceExtensions, bool features)
	{
		s.device = &device;
		s.deviceLost = false;
		if (s_unresolvedDeviceWork)
		{
			s.ready = false;
			s.resumeAfterDevice = false;
			s.reconfigurePending = false;
			return;
		}
		if (!s.loaded || s.negotiationFailed)
		{
			s.reconfigurePending = s.resumeAfterDevice;
			return;
		}
		if (!instanceExtensions)
			Failure(KzgsUpscalerReason::InstanceExtensionUnavailable, KZGS_PROVIDER_ERROR_EXTENSIONS);
		else if (!deviceExtensions)
			Failure(KzgsUpscalerReason::DeviceExtensionUnavailable, KZGS_PROVIDER_ERROR_EXTENSIONS);
		else if (!features)
			Failure(KzgsUpscalerReason::DeviceFeatureUnavailable, KZGS_PROVIDER_ERROR_FEATURES);
		else
			s.ready = true;
		s.reconfigurePending = s.resumeAfterDevice;
	}

	void ProviderDeviceDestroying(GSDeviceVK& device, VkResult idleResult)
	{
		if (s.device != &device)
			return;
		const bool resume = s.wantsProvider;
		ProviderStop();
		s.resumeAfterDevice = resume;
		if (s_unresolvedDeviceWork)
		{
			for (auto& context : s.retired)
				context->output.release();
			s.retired.clear();
			s.retainModule = true;
			s.resumeAfterDevice = false;
		}
		else if (idleResult == VK_SUCCESS || idleResult == VK_ERROR_DEVICE_LOST)
		{
			const auto reason = idleResult == VK_SUCCESS ? KZGS_PROVIDER_DESTROY_COMPLETED :
				KZGS_PROVIDER_DESTROY_DEVICE_LOST;
			const uint64_t completed = idleResult == VK_SUCCESS ?
				std::max(device.GetCompletedFenceCounter(), s.lastRecordedFence) : device.GetCompletedFenceCounter();
			for (auto& context : s.retired)
				DestroyContext(context, reason, completed);
			s.retired.clear();
		}
		else
		{
			// A failed idle wait does not prove that provider code/resources are no longer in use.
			for (auto& context : s.retired)
				context->output.release();
			s.retired.clear();
			s.retainModule = true;
			s_unresolvedDeviceWork = true;
			s.resumeAfterDevice = false;
			Failure(KzgsUpscalerReason::InitializationFailed, KZGS_PROVIDER_ERROR_INTERNAL, nullptr,
				"Vulkan device idle failed; provider resources and module were retained.",
				static_cast<int32_t>(idleResult));
			Report(s.failure.reason);
		}
		s.device = nullptr;
		s.ready = false;
		s.lastRecordedFence = 0;
		s.nextFrame = 0;
		s.pendingPresentation = 0;
		if (idleResult == VK_ERROR_DEVICE_LOST)
		{
			s.deviceLost = true;
			Failure(KzgsUpscalerReason::DeviceLost, KZGS_PROVIDER_ERROR_DEVICE_LOST, nullptr,
				"Vulkan device was lost.", static_cast<int32_t>(idleResult));
			Report(s.failure.reason);
		}
	}

	void ProviderStop()
	{
		const bool wasActive = s.enabled || s.active != nullptr;
		if (s.enabled)
			GSSetPresentationUpscaleHooks({});
		s.enabled = false;
		s.wantsProvider = false;
		s.resumeAfterDevice = false;
		s.reconfigurePending = false;
		if (wasActive)
			Retire(KZGS_PROVIDER_RESET_CONFIGURATION);
		else
			Collect();
	}

	void ProviderReset(KzgsProviderResetReason reason)
	{
		Retire(reason);
	}

	void ProviderBeginVsync()
	{
		if (s.reconfigurePending)
		{
			s.reconfigurePending = false;
			s.resumeAfterDevice = false;
			const ProviderStatus status =
				ProviderConfigure(s.prepared, true, s.sharpening, s.fsr3InputMode);
			if (s.report)
				s.report(status, false);
		}
		s.vsyncAttempted = false;
	}

	void ProviderEndVsync()
	{
		if (s.enabled && !s.vsyncAttempted)
		{
			Retire(KZGS_PROVIDER_RESET_DISCONTINUITY);
			if (!s.deviceLost)
			{
				Failure(KzgsUpscalerReason::WaitingForFrame, KZGS_PROVIDER_OK);
				Report(s.failure.reason);
			}
		}
		if (s.reconfigurePending)
			ProviderBeginVsync();
	}

	ProviderStatus ProviderConfigure(KzgsUpscaler requested, bool vulkan, int sharpening,
		KzgsFsr3InputMode fsr3InputMode)
	{
		if (!kzgsIsTemporalUpscaler(requested))
		{
			ProviderStop();
			return {KzgsUpscalerReason::Disabled};
		}
		if (!vulkan)
		{
			ProviderStop();
			return {KzgsUpscalerReason::NotImplemented};
		}
		if (requested != s.prepared)
		{
			ProviderStop();
			return {KzgsUpscalerReason::DeviceRecreationRequired};
		}
		if (!s.attemptedLoad)
		{
			ProviderStop();
			return {KzgsUpscalerReason::DeviceRecreationRequired};
		}
		if (s_unresolvedDeviceWork)
		{
			ProviderStop();
			return s.failure;
		}
		const int previousSharpening = s.sharpening;
		const KzgsFsr3InputMode previousInputMode = s.fsr3InputMode;
		s.sharpening = sharpening;
		s.fsr3InputMode = fsr3InputMode;
		if (!s.loaded || !s.ready)
		{
			ProviderStop();
			s.wantsProvider = true;
			return s.failure.reason == KzgsUpscalerReason::Disabled ?
				ProviderStatus{KzgsUpscalerReason::InitializationFailed} : s.failure;
		}
		if (!s.device || !s.device->HasSurface())
		{
			ProviderStop();
			s.wantsProvider = true;
			return {KzgsUpscalerReason::NoSurface};
		}
		const bool spatialMock = TestSpatialMockAllowed();
		const bool experimentalFsr3 = requested == KzgsUpscaler::FSR3 &&
			fsr3InputMode == KzgsFsr3InputMode::Experimental;
		if (!spatialMock && !experimentalFsr3)
		{
			ProviderStop();
			s.wantsProvider = true;
			TemporalReset(KZGS_TEMPORAL_RESET_MISSING_INPUT);
			Failure(KzgsUpscalerReason::RequiredInputUnavailable, KZGS_PROVIDER_ERROR_INPUT, nullptr,
				requested == KzgsUpscaler::FSR3 ?
					"Strict FSR 3 inputs are unavailable; enable the explicit experimental input mode to use approximations." :
					"Matched temporal inputs are not qualified.");
			s.failure.providerReason = KZGS_PROVIDER_REASON_MISSING_INPUT;
			return s.failure;
		}
		if (experimentalFsr3 &&
			(KZGS_PROVIDER_VERSION_MINOR(s.api.abi_version) < 2 ||
			 !(s.info.capabilities & KZGS_PROVIDER_CAP_APPROXIMATE_INPUTS)))
		{
			ProviderStop();
			s.wantsProvider = true;
			Failure(KzgsUpscalerReason::ProviderVersionMismatch, KZGS_PROVIDER_ERROR_VERSION, nullptr,
				"The FSR 3 provider does not support ABI 1.2 approximation provenance.");
			return s.failure;
		}
		const KzgsProviderColorEncoding requiredEncoding = experimentalFsr3 ?
			KZGS_PROVIDER_COLOR_LINEAR_SDR : KZGS_PROVIDER_COLOR_UNKNOWN;
		if (!(s.info.supported_color_encodings & KZGS_PROVIDER_COLOR_BIT(requiredEncoding)))
		{
			ProviderStop();
			s.wantsProvider = true;
			Failure(KzgsUpscalerReason::UnsupportedColorEncoding, KZGS_PROVIDER_ERROR_UNSUPPORTED, nullptr,
				"The source color encoding is not established.");
			return s.failure;
		}
		if (s.enabled && (previousSharpening != sharpening || previousInputMode != fsr3InputMode))
			Retire(KZGS_PROVIDER_RESET_CONFIGURATION);
		s.enabled = true;
		s.wantsProvider = true;
		GSPresentationUpscaleHooks hooks;
		hooks.upscale = Upscale;
		hooks.presented = Presented;
		hooks.presented_frame = [](void*, const GSPresentationUpscaleFrame& frame) {
			Fsr1CachePresentedFrame(frame);
		};
		GSSetPresentationUpscaleHooks(hooks);
		return {KzgsUpscalerReason::WaitingForFrame};
	}

	bool ProviderCanCapture()
	{
		return s.enabled && s.device && s.device->HasSurface();
	}

	void ProviderCaptureUse(GSTexture* texture)
	{
		if (!s.device || !s.active || texture != s.active->output.get())
			return;
		s.active->lastFence = std::max(s.active->lastFence, s.device->GetCurrentFenceCounter());
		s.lastRecordedFence = std::max(s.lastRecordedFence, s.active->lastFence);
	}

	bool ProviderGetFsr3JitterState(KzgsFsr3JitterState& out)
	{
		for (int attempt = 0; attempt < 3; ++attempt)
		{
			const uint64_t generation = s_jitterGeneration.load(std::memory_order_acquire);
			const uint64_t source = s_jitterSource.load(std::memory_order_acquire);
			const uint64_t display = s_jitterDisplay.load(std::memory_order_acquire);
			const bool enabled = s_jitterEnabled.load(std::memory_order_acquire);
			if (generation != s_jitterGeneration.load(std::memory_order_acquire))
				continue;
			out.generation = generation;
			out.sourceWidth = static_cast<uint32_t>(source >> 32);
			out.sourceHeight = static_cast<uint32_t>(source);
			out.displayWidth = static_cast<uint32_t>(display >> 32);
			out.displayHeight = static_cast<uint32_t>(display);
			out.enabled = enabled;
			return enabled && out.sourceWidth && out.sourceHeight &&
				out.displayWidth && out.displayHeight;
		}
		out = {};
		return false;
	}

#ifdef KZGS_PROVIDER_TESTING
	void ProviderTestSetPath(const wchar_t* path)
	{
		s_testPath = path ? path : L"";
	}

	void ProviderTestAllowSpatialMock(bool allow)
	{
		s_testAllowSpatialMock = allow;
	}

	bool ProviderTestUpscale(GSTexture* source, int left, int top, int right, int bottom,
		int width, int height, uint64_t presentation, bool redraw, GSTexture*& output)
	{
		if (!source || !s.device || !s.enabled)
			return false;
		const GSVector4i rect(left, top, right, bottom);
		const GSPresentationUpscaleFrame frame{s.device, source, rect,
			GSVector4(rect) / GSVector4(source->GetSize()).xyxy(),
			GSVector4(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)), presentation, redraw, redraw};
		GSPresentationUpscaleOutput result;
		const bool ok = Upscale(nullptr, frame, result);
		output = ok ? result.texture : nullptr;
		return ok;
	}

	void ProviderTestPresented(uint64_t presentation, bool succeeded, bool used)
	{
		Presented(nullptr, presentation, succeeded, used);
	}

	size_t ProviderTestRetiredCount() { return s.retired.size(); }
	void ProviderTestCollect() { Collect(); }
	void ProviderTestRedraw()
	{
		if (g_gs_renderer)
			g_gs_renderer->PresentCurrentFrame();
	}
	bool ProviderTestReopenDevice()
	{
		return GSreopen(true, false, GSGetCurrentRenderer(), std::nullopt);
	}
#endif
}
