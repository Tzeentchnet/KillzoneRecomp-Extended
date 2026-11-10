// SPDX-License-Identifier: GPL-3.0+
#include "kzgs_fsr1.h"
#include "kzgs_fsr1_runtime.h"

#include "Config.h"
#include "GS/Renderers/Common/GSDevice.h"
#include "GS/Renderers/Common/GSRenderer.h"
#include "GS/Renderers/Vulkan/GSDeviceVK.h"
#include "GS/Renderers/Vulkan/GSTextureVK.h"
#include "GS/Renderers/Vulkan/VKShaderCache.h"
#include "common/Console.h"
#include "common/StringUtil.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <deque>
#include <memory>
#include <string>

namespace
{
	using namespace kzgs::fsr1;

	struct PushConstants
	{
		Constants constants{};
		int32_t source[4]{};
		uint32_t output[2]{};
	};
	static_assert(offsetof(PushConstants, source) == 64);
	static_assert(offsetof(PushConstants, output) == 80);
	static_assert(sizeof(PushConstants) == 88);

	struct DescriptorBatch
	{
		u64 fence;
		std::array<VkDescriptorSet, 2> sets;
		u32 count;
	};

	struct State
	{
		GSDeviceVK* device = nullptr;
		kzgs::Fsr1Report report = nullptr;
		bool enabled = false;
		bool initialized = false;
		bool initializationAttempted = false;
		int sharpening = 0;
		VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
		VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
		VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
		VkPipeline easu = VK_NULL_HANDLE;
		VkPipeline rcas = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		std::deque<DescriptorBatch> descriptors;
		std::unique_ptr<GSTexture> output;
		std::unique_ptr<GSTexture> intermediate;
		Plan plan{};
		GSPresentationUpscaleFrame capture{};
		int captureWindowWidth = 0;
		int captureWindowHeight = 0;
		bool captureBilinear = false;
		bool captureStretch = false;
		bool captureValid = false;
	};

	State s;
#ifdef KZGS_FSR1_TESTING
	std::atomic<bool> s_failNextInitialization{false};
	std::atomic<uint64_t> s_dispatchCount{0};
	VkPipeline s_referenceEasu = VK_NULL_HANDLE;
	bool s_useNativeGather = false;
#endif

	void Report(KzgsUpscalerReason reason, bool applied = false)
	{
		if (s.report)
			s.report(reason, applied);
	}

	void FreeCompletedDescriptors()
	{
		const u64 completed = s.device->GetCompletedFenceCounter();
		while (!s.descriptors.empty() && s.descriptors.front().fence <= completed)
		{
			const DescriptorBatch& batch = s.descriptors.front();
			vkFreeDescriptorSets(s.device->GetDevice(), s.descriptorPool, batch.count, batch.sets.data());
			s.descriptors.pop_front();
		}
	}

	void DestroyResources()
	{
		s.captureValid = false;
		s.output.reset();
		s.intermediate.reset();
		if (!s.device)
			return;

		const VkDevice device = s.device->GetDevice();
#ifdef KZGS_FSR1_TESTING
		if (s_referenceEasu)
			vkDestroyPipeline(device, s_referenceEasu, nullptr);
		s_referenceEasu = VK_NULL_HANDLE;
		s_useNativeGather = false;
#endif
		if (s.easu)
			vkDestroyPipeline(device, s.easu, nullptr);
		if (s.rcas)
			vkDestroyPipeline(device, s.rcas, nullptr);
		if (s.pipelineLayout)
			vkDestroyPipelineLayout(device, s.pipelineLayout, nullptr);
		if (s.descriptorPool)
			vkDestroyDescriptorPool(device, s.descriptorPool, nullptr);
		if (s.descriptorLayout)
			vkDestroyDescriptorSetLayout(device, s.descriptorLayout, nullptr);
		if (s.sampler)
			vkDestroySampler(device, s.sampler, nullptr);
		s.easu = s.rcas = VK_NULL_HANDLE;
		s.pipelineLayout = VK_NULL_HANDLE;
		s.descriptorPool = VK_NULL_HANDLE;
		s.descriptorLayout = VK_NULL_HANDLE;
		s.sampler = VK_NULL_HANDLE;
		s.descriptors.clear();
		s.initialized = false;
		s.initializationAttempted = false;
	}

	bool LoadShader(const char* path, std::string& shader)
	{
		auto source = GSDevice::ReadShaderSource(path);
		auto portability = GSDevice::ReadShaderSource("shaders/kzgs/fsr1/ffx_a.h");
		auto fsr = GSDevice::ReadShaderSource("shaders/kzgs/fsr1/ffx_fsr1.h");
		if (!source || !portability || !fsr)
			return false;
		shader = std::move(*source);
		StringUtil::ReplaceAll(&shader, "#include \"ffx_a.h\"", *portability);
		StringUtil::ReplaceAll(&shader, "#include \"ffx_fsr1.h\"", *fsr);
		return true;
	}

	bool CreatePipeline(const char* path, VkPipeline& pipeline, bool nativeGather = false)
	{
		std::string source;
		if (!g_vulkan_shader_cache || !LoadShader(path, source))
			return false;
		if (nativeGather)
		{
			if (source.find("#define FSR_EASU_F 1") == std::string::npos ||
				source.find("#ifdef FSR1_NATIVE_GATHER_REFERENCE") == std::string::npos)
				return false;
			StringUtil::ReplaceAll(&source, "#define FSR_EASU_F 1",
				"#define FSR_EASU_F 1\n#define FSR1_NATIVE_GATHER_REFERENCE 1");
		}
		const VkDevice device = s.device->GetDevice();
		const VkShaderModule shader = g_vulkan_shader_cache->GetComputeShader(source);
		if (!shader)
			return false;

		VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
		info.layout = s.pipelineLayout;
		info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		info.stage.module = shader;
		info.stage.pName = "main";
		const VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
		vkDestroyShaderModule(device, shader, nullptr);
		return result == VK_SUCCESS;
	}

	bool Initialize()
	{
		if (s.initialized)
			return true;
		if (s.initializationAttempted || !s.device)
			return false;
		s.initializationAttempted = true;
		const VkDevice device = s.device->GetDevice();

		const VkDescriptorSetLayoutBinding bindings[] = {
			{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
			{1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		};
		VkDescriptorSetLayoutCreateInfo descriptorInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
		descriptorInfo.bindingCount = 2;
		descriptorInfo.pBindings = bindings;
		if (vkCreateDescriptorSetLayout(device, &descriptorInfo, nullptr, &s.descriptorLayout) != VK_SUCCESS)
			return false;
#ifdef KZGS_FSR1_TESTING
		if (s_failNextInitialization.exchange(false))
			return false;
#endif

		const VkDescriptorPoolSize poolSizes[] = {
			{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 128},
			{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 128},
		};
		VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		poolInfo.maxSets = 128;
		poolInfo.poolSizeCount = 2;
		poolInfo.pPoolSizes = poolSizes;
		if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &s.descriptorPool) != VK_SUCCESS)
			return false;

		const VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants)};
		VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &s.descriptorLayout;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &range;
		if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &s.pipelineLayout) != VK_SUCCESS)
			return false;

		VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
		samplerInfo.magFilter = VK_FILTER_NEAREST;
		samplerInfo.minFilter = VK_FILTER_NEAREST;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.maxLod = 0.0f;
		if (vkCreateSampler(device, &samplerInfo, nullptr, &s.sampler) != VK_SUCCESS)
			return false;

		if (!CreatePipeline("shaders/kzgs/fsr1_easu.glsl", s.easu) ||
			!CreatePipeline("shaders/kzgs/fsr1_rcas.glsl", s.rcas))
			return false;
		s.initialized = true;
		return true;
	}

	bool EnsureTexture(std::unique_ptr<GSTexture>& texture, int width, int height)
	{
		if (texture && texture->GetWidth() == width && texture->GetHeight() == height)
			return true;
		texture.reset(s.device->CreateShaderWriteTarget(width, height, GSTexture::Format::Color, false, false));
		return texture != nullptr;
	}

	void UpdateDescriptor(VkDescriptorSet set, VkImageView source, VkImageView destination)
	{
		const VkDescriptorImageInfo sourceInfo{s.sampler, source, VK_IMAGE_LAYOUT_GENERAL};
		const VkDescriptorImageInfo destinationInfo{VK_NULL_HANDLE, destination, VK_IMAGE_LAYOUT_GENERAL};
		VkWriteDescriptorSet writes[2]{};
		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = set;
		writes[0].dstBinding = 0;
		writes[0].descriptorCount = 1;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[0].pImageInfo = &sourceInfo;
		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = set;
		writes[1].dstBinding = 1;
		writes[1].descriptorCount = 1;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		writes[1].pImageInfo = &destinationInfo;
		vkUpdateDescriptorSets(s.device->GetDevice(), 2, writes, 0, nullptr);
	}

	void Dispatch(VkCommandBuffer command, VkPipeline pipeline, VkDescriptorSet descriptor,
		const PushConstants& constants)
	{
		vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
		vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, s.pipelineLayout,
			0, 1, &descriptor, 0, nullptr);
		vkCmdPushConstants(command, s.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
		vkCmdDispatch(command, s.plan.groupsX, s.plan.groupsY, 1);
	}

	bool Record(void*, const GSDeviceVK::ExternalCommands& commands)
	{
		if (commands.source.layout != VK_IMAGE_LAYOUT_GENERAL || commands.output.layout != VK_IMAGE_LAYOUT_GENERAL ||
			commands.output.format != VK_FORMAT_R8G8B8A8_UNORM)
			return false;

		FreeCompletedDescriptors();
		const bool sharpen = s.sharpening > 0;
		const u32 count = sharpen ? 2 : 1;
		const VkDescriptorSetLayout layouts[] = {s.descriptorLayout, s.descriptorLayout};
		VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
		alloc.descriptorPool = s.descriptorPool;
		alloc.descriptorSetCount = count;
		alloc.pSetLayouts = layouts;
		DescriptorBatch batch{commands.fence_counter, {}, count};
		if (vkAllocateDescriptorSets(s.device->GetDevice(), &alloc, batch.sets.data()) != VK_SUCCESS)
			return false;
		s.descriptors.push_back(batch);

		GSTextureVK* intermediate = sharpen ? static_cast<GSTextureVK*>(s.intermediate.get()) : nullptr;
		if (intermediate)
		{
			intermediate->SetUseFenceCounter(commands.fence_counter);
			intermediate->TransitionToLayout(commands.command_buffer, GSTextureVK::Layout::ExternalReadWrite);
			intermediate->SetState(GSTexture::State::Dirty);
			VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
			barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			barrier.oldLayout = barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
			barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = intermediate->GetImage();
			barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			vkCmdPipelineBarrier(commands.command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
		}

		UpdateDescriptor(batch.sets[0], commands.source.view,
			intermediate ? intermediate->GetView() : commands.output.view);
		PushConstants push;
		push.constants = EasuConstants(s.plan);
		push.source[0] = s.plan.source.left;
		push.source[1] = s.plan.source.top;
		push.source[2] = s.plan.source.right;
		push.source[3] = s.plan.source.bottom;
		push.output[0] = static_cast<u32>(s.plan.outputWidth);
		push.output[1] = static_cast<u32>(s.plan.outputHeight);
		VkPipeline easu = s.easu;
#ifdef KZGS_FSR1_TESTING
		if (s_useNativeGather)
			easu = s_referenceEasu;
#endif
		Dispatch(commands.command_buffer, easu, batch.sets[0], push);

		if (intermediate)
		{
			VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
			barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			barrier.oldLayout = barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
			barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = intermediate->GetImage();
			barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			vkCmdPipelineBarrier(commands.command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
			UpdateDescriptor(batch.sets[1], intermediate->GetView(), commands.output.view);
			push.constants[0] = RcasConstants(s.sharpening);
			Dispatch(commands.command_buffer, s.rcas, batch.sets[1], push);
		}
		return true;
	}

	bool Upscale(void*, const GSPresentationUpscaleFrame& frame, GSPresentationUpscaleOutput& output)
	{
		s.captureValid = false;
		if (!s.enabled || !s.device || frame.device != s.device || !frame.texture ||
			frame.texture->GetFormat() != GSTexture::Format::Color)
		{
			Report(KzgsUpscalerReason::DispatchFailed);
			return false;
		}

		const VkPhysicalDeviceLimits& limits = s.device->GetDeviceProperties().limits;
		const PlanResult result = MakePlan(frame.texture->GetWidth(), frame.texture->GetHeight(),
			{frame.source_rect.x, frame.source_rect.y, frame.source_rect.z, frame.source_rect.w},
			frame.destination_rect.x, frame.destination_rect.y, frame.destination_rect.z, frame.destination_rect.w,
			std::min(s.device->GetMaxTextureSize(), limits.maxImageDimension2D),
			limits.maxComputeWorkGroupCount[0], limits.maxComputeWorkGroupCount[1], s.plan);
		if (result != PlanResult::Ready)
		{
			Report(result == PlanResult::NoUpscaleNeeded ? KzgsUpscalerReason::NoUpscaleNeeded :
				KzgsUpscalerReason::InvalidDimensions);
			return false;
		}
		if (!Initialize())
		{
			DestroyResources();
			s.initializationAttempted = true;
			Report(KzgsUpscalerReason::InitializationFailed);
			return false;
		}
		if (!EnsureTexture(s.output, s.plan.outputWidth, s.plan.outputHeight) ||
			(s.sharpening > 0 && !EnsureTexture(s.intermediate, s.plan.outputWidth, s.plan.outputHeight)))
		{
			Report(KzgsUpscalerReason::DispatchFailed);
			return false;
		}
		if (s.sharpening == 0)
			s.intermediate.reset();
		if (!s.device->ExecuteExternalCommands(frame.texture, s.output.get(), Record, nullptr))
		{
			Report(KzgsUpscalerReason::DispatchFailed);
			return false;
		}
#ifdef KZGS_FSR1_TESTING
		s_dispatchCount.fetch_add(1, std::memory_order_relaxed);
#endif
		output.texture = s.output.get();
		output.source_rect = GSVector4i(0, 0, s.plan.outputWidth, s.plan.outputHeight);
		return true;
	}

	void PresentedFrame(void*, const GSPresentationUpscaleFrame& frame)
	{
		if (!s.device || frame.device != s.device || !frame.texture)
			return;
		s.capture = frame;
		s.captureWindowWidth = s.device->GetWindowWidth();
		s.captureWindowHeight = s.device->GetWindowHeight();
		s.captureBilinear = GSConfig.LinearPresent != GSPostBilinearMode::Off;
		s.captureStretch = EmuConfig.CurrentAspectRatio == AspectRatioType::Stretch;
		s.captureValid = s.captureWindowWidth > 0 && s.captureWindowHeight > 0;
	}

	void Presented(void*, u64, bool succeeded, bool used)
	{
		if (used)
			Report(succeeded ? KzgsUpscalerReason::Active : KzgsUpscalerReason::PresentFailed, succeeded);
	}

	void DeviceCreated(void*, GSDeviceVK& device, bool, bool)
	{
		s.device = &device;
	}

	void DeviceDestroying(void*, GSDeviceVK& device)
	{
		if (s.device != &device)
			return;
		DestroyResources();
		s.device = nullptr;
	}
}

namespace kzgs
{
	void Fsr1InstallDeviceHooks(Fsr1Report report)
	{
		s.report = report;
		GSDeviceVK::IntegrationHooks hooks;
		hooks.device_created = DeviceCreated;
		hooks.device_destroying = DeviceDestroying;
		GSDeviceVK::SetIntegrationHooks(hooks);
	}

	void Fsr1RemoveDeviceHooks()
	{
		GSSetPresentationUpscaleHooks({});
		GSDeviceVK::SetIntegrationHooks({});
		s.enabled = false;
		s.report = nullptr;
		s.captureValid = false;
	}

	void Fsr1Configure(bool enabled, int sharpening)
	{
		s.captureValid = false;
		if (!enabled && !s.initialized)
			s.initializationAttempted = false;
		s.enabled = enabled;
		s.sharpening = sharpening;
		GSPresentationUpscaleHooks hooks;
		if (enabled)
		{
			hooks.upscale = Upscale;
			hooks.presented = Presented;
			hooks.presented_frame = PresentedFrame;
		}
		GSSetPresentationUpscaleHooks(hooks);
	}

	void Fsr1InvalidateCapture()
	{
		s.captureValid = false;
	}

	bool Fsr1CanCapture()
	{
		return s.enabled && s.device && s.device->HasSurface();
	}

	Fsr1ReadbackResult Fsr1ReadPresented(std::vector<uint32_t>& pixels, uint32_t& width, uint32_t& height,
		uint32_t requestedWidth, uint32_t requestedHeight)
	{
		if (!s.captureValid || !s.device || s.capture.device != s.device)
			return Fsr1ReadbackResult::Unavailable;
		if (!requestedWidth || !requestedHeight || requestedWidth > s.device->GetMaxTextureSize() ||
			requestedHeight > s.device->GetMaxTextureSize())
			return Fsr1ReadbackResult::Failed;

		std::unique_ptr<GSTexture> target(s.device->CreateRenderTarget(
			static_cast<int>(requestedWidth), static_cast<int>(requestedHeight), GSTexture::Format::Color, true, false));
		auto download = s.device->CreateDownloadTexture(requestedWidth, requestedHeight, GSTexture::Format::Color);
		if (!target || !download)
			return Fsr1ReadbackResult::Failed;

		GSVector4 draw = s.capture.destination_rect;
		if (requestedWidth != static_cast<u32>(s.captureWindowWidth) ||
			requestedHeight != static_cast<u32>(s.captureWindowHeight))
		{
			if (s.captureStretch)
				draw = GSVector4(0.0f, 0.0f, static_cast<float>(requestedWidth),
					static_cast<float>(requestedHeight));
			else
			{
				const float aspect = (draw.z - draw.x) / (draw.w - draw.y);
				float w = static_cast<float>(requestedWidth);
				float h = w / aspect;
				if (h > static_cast<float>(requestedHeight))
				{
					h = static_cast<float>(requestedHeight);
					w = h * aspect;
				}
				const float x = (static_cast<float>(requestedWidth) - w) * 0.5f;
				const float y = (static_cast<float>(requestedHeight) - h) * 0.5f;
				draw = GSVector4(x, y, x + w, y + h);
			}
		}
		s.device->StretchRect(s.capture.texture, s.capture.source_uv, target.get(), draw,
			ShaderConvert::COPY, s.captureBilinear ? Biln : Nearest);
		const GSVector4i rect(0, 0, static_cast<int>(requestedWidth), static_cast<int>(requestedHeight));
		download->CopyFromTexture(rect, target.get(), rect, 0);
		download->Flush();
		if (!download->Map(rect))
			return Fsr1ReadbackResult::Failed;
		pixels.resize(static_cast<size_t>(requestedWidth) * requestedHeight);
		StringUtil::StrideMemCpy(pixels.data(), requestedWidth * sizeof(uint32_t), download->GetMapPointer(),
			download->GetMapPitch(), requestedWidth * sizeof(uint32_t), requestedHeight);
		download->Unmap();
		width = requestedWidth;
		height = requestedHeight;
		return Fsr1ReadbackResult::Success;
	}

#ifdef KZGS_FSR1_TESTING
	void Fsr1TestFailNextInitialization()
	{
		s_failNextInitialization.store(true, std::memory_order_relaxed);
	}

	uint64_t Fsr1TestDispatchCount()
	{
		return s_dispatchCount.load(std::memory_order_relaxed);
	}

	bool Fsr1TestUseNativeGather(bool enabled)
	{
		if (enabled && (!s.initialized || (!s_referenceEasu &&
			!CreatePipeline("shaders/kzgs/fsr1_easu.glsl", s_referenceEasu, true))))
			return false;
		s_useNativeGather = enabled;
		return true;
	}

	bool Fsr1TestUpscale(GSTexture* source, int left, int top, int right, int bottom,
		int outputWidth, int outputHeight, int sharpening, GSTexture*& output)
	{
		if (!source || !s.device || !s.enabled)
			return false;
		const int savedSharpening = s.sharpening;
		s.sharpening = sharpening;
		const GSVector4i rect(left, top, right, bottom);
		const GSPresentationUpscaleFrame frame{s.device, source, rect,
			GSVector4(rect) / GSVector4(source->GetSize()).xyxy(),
			GSVector4(0.0f, 0.0f, static_cast<float>(outputWidth), static_cast<float>(outputHeight)), 0, false};
		GSPresentationUpscaleOutput result;
		const bool ok = Upscale(nullptr, frame, result);
		s.sharpening = savedSharpening;
		output = ok ? result.texture : nullptr;
		return ok;
	}
#endif
}
