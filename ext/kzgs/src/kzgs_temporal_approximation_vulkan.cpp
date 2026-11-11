// SPDX-License-Identifier: GPL-3.0+
#include "kzgs_temporal_approximation_vulkan.h"

#include "../providers/kzgs_ffx_vk_loader.h"

#include "GS/Renderers/Vulkan/VKShaderCache.h"

#include <ffx_opticalflow.h>
#include <ffx_vk.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>

namespace
{
	constexpr uint32_t kDescriptorCapacity = 64;
	std::atomic<uint64_t> s_resourceSequence{1};

	constexpr char kConversionShader[] = R"glsl(
#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform isampler2D flowInput;
layout(set = 0, binding = 1) uniform sampler2D currentColor;
layout(set = 0, binding = 2) uniform sampler2D previousColor;
layout(set = 0, binding = 3, rg16f) writeonly uniform image2D motionOutput;
layout(set = 0, binding = 4, r8) writeonly uniform image2D reactiveOutput;

layout(push_constant) uniform Parameters
{
	ivec2 size;
	vec2 currentJitter;
	vec2 previousJitter;
	uint hasHistory;
	uint padding;
} parameters;

void main()
{
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pixel, parameters.size)))
		return;

	vec2 rawFlow = vec2(texelFetch(flowInput, pixel / 8, 0).xy);
	vec2 motion = parameters.hasHistory != 0u ?
		rawFlow + parameters.currentJitter - parameters.previousJitter : vec2(0.0);
	imageStore(motionOutput, pixel, vec4(motion, 0.0, 0.0));

	float reactive = 0.0;
	if (parameters.hasHistory != 0u)
	{
		vec2 size = vec2(parameters.size);
		vec2 uv = (vec2(pixel) + vec2(0.5)) / size;
		vec3 current = texture(currentColor, uv).rgb;
		vec3 previous = texture(previousColor, uv + rawFlow / size).rgb;
		float residual = max(max(abs(current.r - previous.r), abs(current.g - previous.g)),
			abs(current.b - previous.b));
		reactive = clamp((residual - 0.02) * 4.0, 0.0, 1.0);
	}
	imageStore(reactiveOutput, pixel, vec4(reactive, 0.0, 0.0, 0.0));
}
)glsl";

	bool FormatFeatures(VkPhysicalDevice physical, VkFormat format, VkFormatFeatureFlags required)
	{
		VkFormatProperties properties{};
		vkGetPhysicalDeviceFormatProperties(physical, format, &properties);
		return (properties.optimalTilingFeatures & required) == required;
	}
}

namespace kzgs
{
	struct TemporalApproximationVulkan::Impl
	{
		struct Image
		{
			VkImage image = VK_NULL_HANDLE;
			VmaAllocation allocation = VK_NULL_HANDLE;
			VkImageView view = VK_NULL_HANDLE;
			VkFormat format = VK_FORMAT_UNDEFINED;
			VkExtent2D extent{};
			VkImageUsageFlags usage = 0;
			VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
			uint64_t id = 0;
		};

		struct DescriptorSlot
		{
			VkDescriptorSet set = VK_NULL_HANDLE;
			uint64_t fence = 0;
		};

		struct PushConstants
		{
			int32_t size[2]{};
			float currentJitter[2]{};
			float previousJitter[2]{};
			uint32_t hasHistory = 0;
			uint32_t padding = 0;
		};

		GSDeviceVK& device;
		VkExtent2D extent{};
		std::vector<std::max_align_t> scratch;
		FfxInterface backend{};
		FfxOpticalflowContext opticalFlow{};
		bool opticalFlowCreated = false;
		bool abandoned = false;
		std::array<Image, 2> color{};
		Image flow{}, sceneChange{}, motion{}, depth{}, reactive{};
		VkSampler sampler = VK_NULL_HANDLE;
		VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
		VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
		VkPipeline pipeline = VK_NULL_HANDLE;
		VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
		std::vector<DescriptorSlot> descriptors;
		bool hasHistory = false;
		bool pending = false;
		bool resetNext = true;
		uint32_t committedIndex = 0;
		uint32_t pendingIndex = 0;
		float previousJitter[2]{};
		float pendingJitter[2]{};
		std::string error;

		Impl(GSDeviceVK& device_, VkExtent2D extent_) : device(device_), extent(extent_) {}

		~Impl()
		{
			if (abandoned)
				return;
			if (opticalFlowCreated)
				ffxOpticalflowContextDestroy(&opticalFlow);
			if (pipeline)
				vkDestroyPipeline(device.GetDevice(), pipeline, nullptr);
			if (pipelineLayout)
				vkDestroyPipelineLayout(device.GetDevice(), pipelineLayout, nullptr);
			if (descriptorPool)
				vkDestroyDescriptorPool(device.GetDevice(), descriptorPool, nullptr);
			if (descriptorLayout)
				vkDestroyDescriptorSetLayout(device.GetDevice(), descriptorLayout, nullptr);
			if (sampler)
				vkDestroySampler(device.GetDevice(), sampler, nullptr);
			destroyImage(reactive);
			destroyImage(depth);
			destroyImage(motion);
			destroyImage(sceneChange);
			destroyImage(flow);
			destroyImage(color[1]);
			destroyImage(color[0]);
		}

		void destroyImage(Image& image)
		{
			if (image.view)
				vkDestroyImageView(device.GetDevice(), image.view, nullptr);
			if (image.image)
				vmaDestroyImage(device.GetAllocator(), image.image, image.allocation);
			image = {};
		}

		bool createImage(Image& image, VkFormat format, VkExtent2D size, VkImageUsageFlags usage)
		{
			VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
			imageInfo.imageType = VK_IMAGE_TYPE_2D;
			imageInfo.format = format;
			imageInfo.extent = {size.width, size.height, 1};
			imageInfo.mipLevels = 1;
			imageInfo.arrayLayers = 1;
			imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageInfo.usage = usage;
			imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			VmaAllocationCreateInfo allocationInfo{};
			allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
			if (vmaCreateImage(device.GetAllocator(), &imageInfo, &allocationInfo,
					&image.image, &image.allocation, nullptr) != VK_SUCCESS)
			{
				error = "Could not allocate an FSR 3 approximation image.";
				return false;
			}
			VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
			viewInfo.image = image.image;
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = format;
			viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			if (vkCreateImageView(device.GetDevice(), &viewInfo, nullptr, &image.view) != VK_SUCCESS)
			{
				error = "Could not create an FSR 3 approximation image view.";
				return false;
			}
			image.format = format;
			image.extent = size;
			image.usage = usage;
			image.id = s_resourceSequence.fetch_add(1, std::memory_order_relaxed);
			return true;
		}

		void barrier(VkCommandBuffer commandBuffer, Image& image, VkImageLayout layout,
			VkPipelineStageFlags sourceStage, VkAccessFlags sourceAccess,
			VkPipelineStageFlags destinationStage, VkAccessFlags destinationAccess)
		{
			VkImageMemoryBarrier barrierInfo{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
			barrierInfo.srcAccessMask =
				image.layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : sourceAccess;
			barrierInfo.dstAccessMask = destinationAccess;
			barrierInfo.oldLayout = image.layout;
			barrierInfo.newLayout = layout;
			barrierInfo.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrierInfo.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrierInfo.image = image.image;
			barrierInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			vkCmdPipelineBarrier(commandBuffer,
				image.layout == VK_IMAGE_LAYOUT_UNDEFINED ?
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : sourceStage,
				destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrierInfo);
			image.layout = layout;
		}

		FfxApiResource resource(const Image& image, const wchar_t* name,
			FfxApiResourceState state, FfxApiResourceUsage usage)
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
			return ffxGetResourceVK(reinterpret_cast<void*>(image.image),
				ffxGetImageResourceDescriptionVK(image.image, createInfo, usage), name, state);
		}

		bool initialize()
		{
			if (!extent.width || !extent.height)
			{
				error = "The FSR 3 approximation extent is empty.";
				return false;
			}
			const VkFormatFeatureFlags sampledTransfer =
				VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
			const VkFormatFeatureFlags sampledStorage =
				VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
			if (!FormatFeatures(device.GetPhysicalDevice(), VK_FORMAT_R8G8B8A8_UNORM, sampledTransfer) ||
				!FormatFeatures(device.GetPhysicalDevice(), VK_FORMAT_R16G16_SINT, sampledStorage) ||
				!FormatFeatures(device.GetPhysicalDevice(), VK_FORMAT_R16G16_SFLOAT, sampledStorage) ||
				!FormatFeatures(device.GetPhysicalDevice(), VK_FORMAT_R32_SFLOAT, sampledTransfer) ||
				!FormatFeatures(device.GetPhysicalDevice(), VK_FORMAT_R8_UNORM, sampledStorage) ||
				!FormatFeatures(device.GetPhysicalDevice(), VK_FORMAT_R32_UINT,
					VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT))
			{
				error = "The Vulkan device lacks required FSR 3 approximation image-format features.";
				return false;
			}
			const VkImageUsageFlags colorUsage =
				VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			const VkImageUsageFlags storageUsage =
				VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			const VkExtent2D flowExtent{(extent.width + 7) / 8, (extent.height + 7) / 8};
			if (!createImage(color[0], VK_FORMAT_R8G8B8A8_UNORM, extent, colorUsage) ||
				!createImage(color[1], VK_FORMAT_R8G8B8A8_UNORM, extent, colorUsage) ||
				!createImage(flow, VK_FORMAT_R16G16_SINT, flowExtent, storageUsage) ||
				!createImage(sceneChange, VK_FORMAT_R32_UINT, {3, 1},
					VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
				!createImage(motion, VK_FORMAT_R16G16_SFLOAT, extent, storageUsage) ||
				!createImage(depth, VK_FORMAT_R32_SFLOAT, extent,
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT) ||
				!createImage(reactive, VK_FORMAT_R8_UNORM, extent, storageUsage))
				return false;

			VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
			samplerInfo.magFilter = VK_FILTER_NEAREST;
			samplerInfo.minFilter = VK_FILTER_NEAREST;
			samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerInfo.maxLod = 0.0f;
			if (vkCreateSampler(device.GetDevice(), &samplerInfo, nullptr, &sampler) != VK_SUCCESS)
			{
				error = "Could not create the FSR 3 approximation sampler.";
				return false;
			}

			std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
			for (uint32_t i = 0; i < 3; ++i)
			{
				bindings[i].binding = i;
				bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				bindings[i].descriptorCount = 1;
				bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			}
			for (uint32_t i = 3; i < 5; ++i)
			{
				bindings[i].binding = i;
				bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				bindings[i].descriptorCount = 1;
				bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			}
			VkDescriptorSetLayoutCreateInfo layoutInfo{
				VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
			layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
			layoutInfo.pBindings = bindings.data();
			if (vkCreateDescriptorSetLayout(device.GetDevice(), &layoutInfo, nullptr,
					&descriptorLayout) != VK_SUCCESS)
			{
				error = "Could not create the FSR 3 approximation descriptor layout.";
				return false;
			}
			const VkPushConstantRange pushRange{
				VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants)};
			VkPipelineLayoutCreateInfo pipelineLayoutInfo{
				VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
			pipelineLayoutInfo.setLayoutCount = 1;
			pipelineLayoutInfo.pSetLayouts = &descriptorLayout;
			pipelineLayoutInfo.pushConstantRangeCount = 1;
			pipelineLayoutInfo.pPushConstantRanges = &pushRange;
			if (vkCreatePipelineLayout(device.GetDevice(), &pipelineLayoutInfo, nullptr,
					&pipelineLayout) != VK_SUCCESS)
			{
				error = "Could not create the FSR 3 approximation pipeline layout.";
				return false;
			}
			VkShaderModule shader = g_vulkan_shader_cache ?
				g_vulkan_shader_cache->GetComputeShader(kConversionShader) : VK_NULL_HANDLE;
			if (!shader)
			{
				error = "Could not compile the FSR 3 motion-conversion shader.";
				return false;
			}
			VkComputePipelineCreateInfo pipelineInfo{
				VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
			pipelineInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
			pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
			pipelineInfo.stage.module = shader;
			pipelineInfo.stage.pName = "main";
			pipelineInfo.layout = pipelineLayout;
			const VkResult pipelineResult = vkCreateComputePipelines(device.GetDevice(),
					g_vulkan_shader_cache->GetPipelineCache(), 1, &pipelineInfo, nullptr,
					&pipeline);
			vkDestroyShaderModule(device.GetDevice(), shader, nullptr);
			if (pipelineResult != VK_SUCCESS)
			{
				error = "Could not create the FSR 3 motion-conversion pipeline.";
				return false;
			}
			const std::array<VkDescriptorPoolSize, 2> poolSizes{{
				{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kDescriptorCapacity * 3},
				{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kDescriptorCapacity * 2},
			}};
			VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
			poolInfo.maxSets = kDescriptorCapacity;
			poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
			poolInfo.pPoolSizes = poolSizes.data();
			if (vkCreateDescriptorPool(device.GetDevice(), &poolInfo, nullptr,
					&descriptorPool) != VK_SUCCESS)
			{
				error = "Could not create the FSR 3 approximation descriptor pool.";
				return false;
			}

			if (!kzgsFfxSetVulkanProcs(device.GetVulkanInstance(), device.GetDevice(),
					vkGetInstanceProcAddr, vkGetDeviceProcAddr))
			{
				error = "Required Vulkan procedures for FidelityFX Optical Flow are unavailable.";
				return false;
			}
			const size_t scratchBytes =
				ffxGetScratchMemorySizeVK(device.GetPhysicalDevice(), FFX_OPTICALFLOW_CONTEXT_COUNT);
			if (!scratchBytes)
			{
				error = "FidelityFX returned an invalid Optical Flow scratch-memory size.";
				return false;
			}
			scratch.resize((scratchBytes + sizeof(std::max_align_t) - 1) /
				sizeof(std::max_align_t));
			VkDeviceContext deviceContext{
				device.GetDevice(), device.GetPhysicalDevice(), vkGetDeviceProcAddr};
			FfxErrorCode result = ffxGetInterfaceVK(&backend, ffxGetDeviceVK(&deviceContext),
				scratch.data(), scratch.size() * sizeof(std::max_align_t),
				FFX_OPTICALFLOW_CONTEXT_COUNT);
			if (result != FFX_OK)
			{
				error = "Could not initialize the FidelityFX Optical Flow Vulkan backend.";
				return false;
			}
			FfxOpticalflowContextDescription description{};
			description.backendInterface = backend;
			description.resolution = {extent.width, extent.height};
			result = ffxOpticalflowContextCreate(&opticalFlow, &description);
			if (result != FFX_OK)
			{
				error = "FidelityFX Optical Flow context creation failed.";
				return false;
			}
			opticalFlowCreated = true;
			return true;
		}

		VkDescriptorSet descriptor(uint64_t fence)
		{
			const uint64_t completed = device.GetCompletedFenceCounter();
			for (DescriptorSlot& slot : descriptors)
			{
				if (slot.fence <= completed)
				{
					slot.fence = fence;
					return slot.set;
				}
			}
			if (descriptors.size() >= kDescriptorCapacity)
				return VK_NULL_HANDLE;
			VkDescriptorSetAllocateInfo allocateInfo{
				VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
			allocateInfo.descriptorPool = descriptorPool;
			allocateInfo.descriptorSetCount = 1;
			allocateInfo.pSetLayouts = &descriptorLayout;
			DescriptorSlot slot;
			if (vkAllocateDescriptorSets(device.GetDevice(), &allocateInfo, &slot.set) != VK_SUCCESS)
				return VK_NULL_HANDLE;
			slot.fence = fence;
			descriptors.push_back(slot);
			return slot.set;
		}

		KzgsProviderImage providerImage(const Image& image) const
		{
			KzgsProviderImage result{};
			result.struct_size = sizeof(result);
			result.image = image.image;
			result.view = image.view;
			result.format = image.format;
			result.extent = image.extent;
			result.subresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			result.layout = image.layout;
			result.usage = image.usage;
			result.queue_family = device.GetGraphicsQueueFamilyIndex();
			result.available_stages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			result.available_access = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
			return result;
		}

		bool record(const GSDeviceVK::ExternalCommands& commands,
			const KzgsProviderRect& sourceRect, float jitterX, float jitterY,
			bool reset, TemporalApproximationImages& output)
		{
			error.clear();
			if (pending || sourceRect.left < 0 || sourceRect.top < 0 ||
				sourceRect.right <= sourceRect.left || sourceRect.bottom <= sourceRect.top ||
				static_cast<uint32_t>(sourceRect.right) > commands.source.extent.width ||
				static_cast<uint32_t>(sourceRect.bottom) > commands.source.extent.height ||
				static_cast<uint32_t>(sourceRect.right - sourceRect.left) != extent.width ||
				static_cast<uint32_t>(sourceRect.bottom - sourceRect.top) != extent.height ||
				commands.source.format != VK_FORMAT_R8G8B8A8_UNORM)
			{
				error = "The source image does not match the FSR 3 approximation geometry or format.";
				return false;
			}
			const bool dispatchReset = reset || resetNext || !hasHistory;
			const bool useHistory = hasHistory && !dispatchReset;
			pendingIndex = useHistory ? 1u - committedIndex : 0u;
			Image& current = color[pendingIndex];
			Image& previous = useHistory ? color[committedIndex] : current;
			VkCommandBuffer commandBuffer = commands.command_buffer;

			VkImageMemoryBarrier sourceToTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
			sourceToTransfer.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
			sourceToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			sourceToTransfer.oldLayout = commands.source.layout;
			sourceToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			sourceToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			sourceToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			sourceToTransfer.image = commands.source.image;
			sourceToTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
				&sourceToTransfer);
			barrier(commandBuffer, current, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT |
					VK_ACCESS_MEMORY_WRITE_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
			VkImageBlit blit{};
			blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
			blit.srcOffsets[0] = {sourceRect.left, sourceRect.top, 0};
			blit.srcOffsets[1] = {sourceRect.right, sourceRect.bottom, 1};
			blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
			blit.dstOffsets[1] = {
				static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1};
			vkCmdBlitImage(commandBuffer, commands.source.image,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, current.image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
			std::swap(sourceToTransfer.srcAccessMask, sourceToTransfer.dstAccessMask);
			std::swap(sourceToTransfer.oldLayout, sourceToTransfer.newLayout);
			vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
				&sourceToTransfer);
			barrier(commandBuffer, current, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

			barrier(commandBuffer, depth, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT |
					VK_ACCESS_MEMORY_WRITE_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
			const VkClearColorValue farDepth{{1.0f, 0.0f, 0.0f, 0.0f}};
			const VkImageSubresourceRange fullColor{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			vkCmdClearColorImage(commandBuffer, depth.image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &farDepth, 1, &fullColor);
			barrier(commandBuffer, depth, VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT);

			barrier(commandBuffer, flow, VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT |
					VK_ACCESS_MEMORY_WRITE_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
			barrier(commandBuffer, sceneChange, VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT |
					VK_ACCESS_MEMORY_WRITE_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
			FfxOpticalflowDispatchDescription dispatch{};
			dispatch.commandList = ffxGetCommandListVK(commandBuffer);
			dispatch.color = resource(current, L"KZGS FSR3 normalized color",
				FFX_API_RESOURCE_STATE_COMPUTE_READ, FFX_API_RESOURCE_USAGE_READ_ONLY);
			dispatch.opticalFlowVector = resource(flow, L"KZGS FSR3 optical flow",
				FFX_API_RESOURCE_STATE_UNORDERED_ACCESS, FFX_API_RESOURCE_USAGE_UAV);
			dispatch.opticalFlowSCD = resource(sceneChange, L"KZGS FSR3 scene change",
				FFX_API_RESOURCE_STATE_UNORDERED_ACCESS, FFX_API_RESOURCE_USAGE_UAV);
			dispatch.reset = dispatchReset;
			dispatch.backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
			dispatch.minMaxLuminance = {0.0f, 1.0f};
			const FfxErrorCode opticalFlowResult =
				ffxOpticalflowContextDispatch(&opticalFlow, &dispatch);
			if (opticalFlowResult != FFX_OK)
			{
				error = "FidelityFX Optical Flow dispatch failed.";
				return false;
			}

			barrier(commandBuffer, flow, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
			if (&previous != &current)
				barrier(commandBuffer, previous, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT |
						VK_ACCESS_MEMORY_WRITE_BIT,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
			barrier(commandBuffer, motion, VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT |
					VK_ACCESS_MEMORY_WRITE_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);
			barrier(commandBuffer, reactive, VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT |
					VK_ACCESS_MEMORY_WRITE_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);

			const VkDescriptorSet set = descriptor(commands.fence_counter);
			if (!set)
			{
				error = "FSR 3 approximation descriptors are still in flight.";
				return false;
			}
			const std::array<VkDescriptorImageInfo, 5> imageInfo{{
				{sampler, flow.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
				{sampler, current.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
				{sampler, previous.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
				{VK_NULL_HANDLE, motion.view, VK_IMAGE_LAYOUT_GENERAL},
				{VK_NULL_HANDLE, reactive.view, VK_IMAGE_LAYOUT_GENERAL},
			}};
			std::array<VkWriteDescriptorSet, 5> writes{};
			for (uint32_t i = 0; i < writes.size(); ++i)
			{
				writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[i].dstSet = set;
				writes[i].dstBinding = i;
				writes[i].descriptorCount = 1;
				writes[i].descriptorType = i < 3 ?
					VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER :
					VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				writes[i].pImageInfo = &imageInfo[i];
			}
			vkUpdateDescriptorSets(device.GetDevice(), static_cast<uint32_t>(writes.size()),
				writes.data(), 0, nullptr);
			PushConstants constants;
			constants.size[0] = static_cast<int32_t>(extent.width);
			constants.size[1] = static_cast<int32_t>(extent.height);
			constants.currentJitter[0] = jitterX;
			constants.currentJitter[1] = jitterY;
			constants.previousJitter[0] = previousJitter[0];
			constants.previousJitter[1] = previousJitter[1];
			constants.hasHistory = useHistory ? 1u : 0u;
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
				pipelineLayout, 0, 1, &set, 0, nullptr);
			vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
				0, sizeof(constants), &constants);
			vkCmdDispatch(commandBuffer, (extent.width + 7) / 8, (extent.height + 7) / 8, 1);

			barrier(commandBuffer, motion, VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT);
			barrier(commandBuffer, reactive, VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT);
			barrier(commandBuffer, current, VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT);
			if (&previous != &current)
				barrier(commandBuffer, previous, VK_IMAGE_LAYOUT_GENERAL,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
					VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT);
			barrier(commandBuffer, flow, VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

			output.color = providerImage(current);
			output.depth = providerImage(depth);
			output.motion = providerImage(motion);
			output.reactive = providerImage(reactive);
			output.colorId = current.id;
			output.depthId = depth.id;
			output.motionId = motion.id;
			output.reactiveId = reactive.id;
			pendingJitter[0] = jitterX;
			pendingJitter[1] = jitterY;
			pending = true;
			resetNext = false;
			return true;
		}
	};

	std::unique_ptr<TemporalApproximationVulkan> TemporalApproximationVulkan::Create(
		GSDeviceVK& device, VkExtent2D extent, std::string& error)
	{
		device.WaitForGPUIdle();
		auto impl = std::make_unique<Impl>(device, extent);
		if (!impl->initialize())
		{
			error = impl->error;
			return {};
		}
		return std::unique_ptr<TemporalApproximationVulkan>(
			new TemporalApproximationVulkan(std::move(impl)));
	}

	TemporalApproximationVulkan::TemporalApproximationVulkan(std::unique_ptr<Impl> impl)
		: m_impl(std::move(impl))
	{
	}

	TemporalApproximationVulkan::~TemporalApproximationVulkan() = default;

	bool TemporalApproximationVulkan::Record(const GSDeviceVK::ExternalCommands& commands,
		const KzgsProviderRect& sourceRect, float jitterX, float jitterY,
		bool reset, TemporalApproximationImages& output)
	{
		return m_impl && m_impl->record(commands, sourceRect, jitterX, jitterY, reset, output);
	}

	void TemporalApproximationVulkan::PresentationComplete(bool succeeded)
	{
		if (!m_impl || !m_impl->pending)
			return;
		if (succeeded)
		{
			m_impl->committedIndex = m_impl->pendingIndex;
			m_impl->previousJitter[0] = m_impl->pendingJitter[0];
			m_impl->previousJitter[1] = m_impl->pendingJitter[1];
			m_impl->hasHistory = true;
		}
		else
		{
			m_impl->hasHistory = false;
			m_impl->resetNext = true;
		}
		m_impl->pending = false;
	}

	void TemporalApproximationVulkan::Abandon()
	{
		if (m_impl)
			m_impl->abandoned = true;
	}

	const char* TemporalApproximationVulkan::LastError() const
	{
		return m_impl ? m_impl->error.c_str() : "The FSR 3 approximation context is unavailable.";
	}
}
