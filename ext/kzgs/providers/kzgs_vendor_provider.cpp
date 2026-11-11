// Shared, fail-closed ABI and Vulkan resource handling for optional temporal adapters.
// SPDX-License-Identifier: GPL-3.0+
#include "kzgs_vendor_backend.h"
#include "kzgs_vendor_conventions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
extern "C" IMAGE_DOS_HEADER __ImageBase;
#endif

namespace
{
	using namespace kzgs::vendor;
	constexpr KzgsProviderInputs RequiredInputs = KZGS_PROVIDER_INPUT_COLOR | KZGS_PROVIDER_INPUT_DEPTH |
		KZGS_PROVIDER_INPUT_MOTION | KZGS_PROVIDER_INPUT_JITTER | KZGS_PROVIDER_INPUT_CAMERA;
	constexpr KzgsProviderInputs Fsr3RequiredInputs = RequiredInputs | KZGS_PROVIDER_INPUT_FRAME_TIME;
	constexpr KzgsProviderInputs Fsr3OptionalInputs =
		KZGS_PROVIDER_INPUT_REACTIVE_MASK | KZGS_PROVIDER_INPUT_COMPOSITION_MASK;

	struct Context
	{
		KzgsProviderCreateInfo info{};
		PFN_vkCmdPipelineBarrier barrier = nullptr;
		void* vendor = nullptr;
		std::thread::id creationThread;
		uint64_t lastToken = 0;
		uint64_t lastFrame = 0;
		uint64_t lastPresentation = 0;
		bool hasFrame = false;
		bool reset = true;
	};

	// ABI callbacks are serialized by the host. Storage remains valid until the next provider call.
	Requirements requirements;

	struct InitializationInvocation
	{
		InitializationWork work;
		void* data;
		std::thread::id callingThread;
		Result result{};
		bool called = false;
		bool started = false;
		bool repeated = false;
		bool wrongThread = false;
	};

	void KZGS_PROVIDER_CALL initializationWork(void* opaque)
	{
		auto& invocation = *static_cast<InitializationInvocation*>(opaque);
		if (invocation.called)
		{
			invocation.repeated = true;
			return;
		}
		invocation.called = true;
		if (invocation.callingThread != std::this_thread::get_id())
		{
			invocation.wrongThread = true;
			return;
		}
		invocation.started = true;
		try { invocation.result = invocation.work(invocation.data); }
		catch (...)
		{
			invocation.result = failure(KZGS_PROVIDER_ERROR_CREATE, KZGS_PROVIDER_REASON_PROVIDER_FAILURE,
				"SDK initialization threw an exception.");
		}
	}

	void copyOutput(void* out, uint32_t capacity, const void* value, size_t size)
	{
		if (out && capacity >= sizeof(uint32_t))
			std::memcpy(out, value, std::min<size_t>(capacity, size));
	}

	KzgsProviderResult report(KzgsProviderStatus* out, const Result& result = {})
	{
		const KzgsProviderStatus value{sizeof(value), result.code, result.reason, result.native, result.message};
		if (out)
			copyOutput(out, out->struct_size, &value, sizeof(value));
		return result.code;
	}

	Result invalid(const char* message)
	{
		return failure(KZGS_PROVIDER_ERROR_ARGUMENT, KZGS_PROVIDER_REASON_PROVIDER_FAILURE, message);
	}

	Result missing(const char* message)
	{
		return failure(KZGS_PROVIDER_ERROR_INPUT, KZGS_PROVIDER_REASON_MISSING_INPUT, message);
	}

	Result format(const char* message)
	{
		return failure(KZGS_PROVIDER_ERROR_INPUT, KZGS_PROVIDER_REASON_UNSUPPORTED_FORMAT, message);
	}

	bool rectInside(const KzgsProviderRect& rect, VkExtent2D extent)
	{
		return rect.left >= 0 && rect.top >= 0 && rect.right > rect.left && rect.bottom > rect.top &&
			static_cast<uint32_t>(rect.right) <= extent.width && static_cast<uint32_t>(rect.bottom) <= extent.height;
	}

	bool sameRect(const KzgsProviderRect& a, const KzgsProviderRect& b)
	{
		return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
	}

	bool sameExtent(VkExtent2D a, VkExtent2D b)
	{
		return a.width == b.width && a.height == b.height;
	}

	bool validImage(const KzgsProviderImage* image, uint32_t family, VkImageAspectFlags aspect,
		VkImageUsageFlags usage)
	{
		return KZGS_PROVIDER_HAS_FIELD(image, KzgsProviderImage, available_access) &&
			image->image && image->view && image->extent.width && image->extent.height &&
			image->subresource.aspectMask == aspect && image->subresource.baseMipLevel == 0 &&
			image->subresource.levelCount == 1 && image->subresource.baseArrayLayer == 0 &&
			image->subresource.layerCount == 1 && image->queue_family == family &&
			(image->usage & usage) == usage && image->available_stages && image->available_access &&
			(image->layout == VK_IMAGE_LAYOUT_GENERAL || image->layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	bool colorFormat(VkFormat format, KzgsProviderColorEncoding encoding)
	{
		return (encoding == KZGS_PROVIDER_COLOR_LINEAR_SDR && format == VK_FORMAT_R8G8B8A8_UNORM) ||
			(encoding == KZGS_PROVIDER_COLOR_LINEAR_HDR && format == VK_FORMAT_R16G16B16A16_SFLOAT);
	}

	bool depthFormat(VkFormat format)
	{
		// Packed depth/stencil needs separate-layout feature negotiation before a depth-only barrier is legal.
		return format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_D32_SFLOAT;
	}

	bool validCamera(const KzgsProviderCamera* camera, bool scalarOnly)
	{
		if (!KZGS_PROVIDER_HAS_FIELD(camera, KzgsProviderCamera, previous_world_to_view) ||
			!std::isfinite(camera->near_plane) || camera->near_plane <= 0.0f ||
			!std::isfinite(camera->vertical_fov_radians) || camera->vertical_fov_radians <= 0.0f ||
			camera->vertical_fov_radians >= 3.141592654f ||
			(camera->flags & ~(KZGS_PROVIDER_CAMERA_REVERSED_DEPTH | KZGS_PROVIDER_CAMERA_INFINITE_FAR)))
			return false;
		if (camera->flags & KZGS_PROVIDER_CAMERA_INFINITE_FAR)
		{
			if (!(camera->far_plane > camera->near_plane))
				return false;
		}
		else if (!std::isfinite(camera->far_plane) || camera->far_plane <= camera->near_plane)
			return false;
		if (scalarOnly)
		{
			return KZGS_PROVIDER_HAS_FIELD(camera, KzgsProviderCamera, valid_fields) &&
				(camera->valid_fields & KZGS_PROVIDER_CAMERA_VALID_SCALARS) ==
					KZGS_PROVIDER_CAMERA_VALID_SCALARS &&
				!(camera->valid_fields & ~KZGS_PROVIDER_CAMERA_VALID_ALL);
		}
		for (const float* matrix : {camera->view_to_clip, camera->previous_view_to_clip,
			camera->world_to_view, camera->previous_world_to_view})
		{
			bool nonzero = false;
			for (size_t i = 0; i < 16; ++i)
			{
				if (!std::isfinite(matrix[i]))
					return false;
				nonzero |= matrix[i] != 0.0f;
			}
			if (!nonzero)
				return false;
		}
		return true;
	}

	Result validateCreate(const KzgsProviderCreateInfo* info)
	{
		if (!KZGS_PROVIDER_HAS_FIELD(info, KzgsProviderCreateInfo, sharpening) || !info->instance ||
			!info->physical_device || !info->device || !info->queue || !info->get_instance_proc_addr ||
			!info->get_device_proc_addr || !info->source_extent.width || !info->source_extent.height ||
			!info->destination_extent.width || !info->destination_extent.height ||
			info->destination_extent.width > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
			info->destination_extent.height > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
			!rectInside(info->source_rect, info->source_extent) || info->sharpening > 100)
			return invalid("Incomplete Vulkan create information.");
		if (!colorFormat(info->source_format, info->color_encoding) || info->source_format != info->destination_format)
			return format("A matched, known linear SDR or HDR color format is required.");
		const uint32_t width = static_cast<uint32_t>(info->source_rect.right - info->source_rect.left);
		const uint32_t height = static_cast<uint32_t>(info->source_rect.bottom - info->source_rect.top);
		if (width > info->destination_extent.width || height > info->destination_extent.height)
			return format("The source rectangle cannot exceed the output extent.");
		return {};
	}

	Result validateFrame(const Context& context, const KzgsProviderFrame* frame)
	{
		const bool fsr3 = backend().kind == KZGS_PROVIDER_KIND_FSR3;
		const KzgsProviderInputs required = fsr3 ? Fsr3RequiredInputs : RequiredInputs;
		if (!KZGS_PROVIDER_HAS_FIELD(frame, KzgsProviderFrame, camera) ||
			(frame->valid_inputs & required) != required)
			return missing(fsr3 ?
				"Color, depth, motion, jitter, scalar camera, and frame-time inputs are required." :
				"Matched color, depth, motion, jitter, and camera inputs are required.");
		const KzgsProviderInputs approximate = KZGS_PROVIDER_HAS_FIELD(
			frame, KzgsProviderFrame, approximate_inputs) ? frame->approximate_inputs : 0;
		if ((approximate & ~frame->valid_inputs) || (!fsr3 && approximate) ||
			(fsr3 && !KZGS_PROVIDER_HAS_FIELD(frame, KzgsProviderFrame, approximate_inputs)))
			return invalid("Input approximation provenance is invalid for this provider.");
		if (frame->valid_inputs & ~(fsr3 ? Fsr3RequiredInputs | Fsr3OptionalInputs : RequiredInputs))
			return invalid("The frame advertises inputs unsupported by this provider.");
		if (fsr3 && (!std::isfinite(frame->frame_time_ms) || frame->frame_time_ms <= 0.0f))
			return missing("The FSR 3 frame time is invalid.");
		if (!KZGS_PROVIDER_HAS_FIELD(frame->sync, KzgsProviderSync, completed_token) ||
			!frame->sync->command_buffer || frame->sync->queue != context.info.queue ||
			frame->sync->queue_family != context.info.queue_family ||
			frame->sync->submission_token <= frame->sync->completed_token ||
			frame->sync->submission_token < context.lastToken ||
			(context.hasFrame && (frame->frame_id <= context.lastFrame ||
				frame->presentation_id < context.lastPresentation ||
				(frame->presentation_id == context.lastPresentation && !context.reset))))
			return invalid("Invalid command-buffer, completion token, camera, or frame identity.");
		if (!validCamera(frame->camera, fsr3 && (approximate & KZGS_PROVIDER_INPUT_CAMERA)))
			return missing("The matched camera descriptor or depth convention is invalid.");
		const bool approximateDepth = fsr3 && (approximate & KZGS_PROVIDER_INPUT_DEPTH);
		const VkImageAspectFlags depthAspect =
			approximateDepth ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
		if (!validImage(frame->color, context.info.queue_family, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_USAGE_SAMPLED_BIT) ||
			!validImage(frame->depth, context.info.queue_family, depthAspect, VK_IMAGE_USAGE_SAMPLED_BIT) ||
			!validImage(frame->motion, context.info.queue_family, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_USAGE_SAMPLED_BIT) ||
			!validImage(frame->output, context.info.queue_family, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_USAGE_STORAGE_BIT))
			return missing("A complete, queue-owned sampled depth/motion/color and storage output is required.");
		for (const auto& optional : {
				std::pair{KZGS_PROVIDER_INPUT_REACTIVE_MASK, frame->reactive_mask},
				std::pair{KZGS_PROVIDER_INPUT_COMPOSITION_MASK, frame->composition_mask}})
		{
			if ((frame->valid_inputs & optional.first) &&
				(!validImage(optional.second, context.info.queue_family, VK_IMAGE_ASPECT_COLOR_BIT,
					 VK_IMAGE_USAGE_SAMPLED_BIT) ||
				 optional.second->format != VK_FORMAT_R8_UNORM ||
				 !sameExtent(optional.second->extent, context.info.source_extent)))
				return format("An optional temporal mask has an invalid format or extent.");
		}
		if (frame->output->layout != VK_IMAGE_LAYOUT_GENERAL ||
			frame->color->image == frame->output->image || frame->color->image == frame->depth->image ||
			frame->color->image == frame->motion->image || frame->depth->image == frame->motion->image ||
			frame->depth->image == frame->output->image || frame->motion->image == frame->output->image ||
			!sameExtent(frame->color->extent, context.info.source_extent) ||
			!sameExtent(frame->depth->extent, context.info.source_extent) ||
			!sameExtent(frame->motion->extent, context.info.source_extent) ||
			!sameExtent(frame->output->extent, context.info.destination_extent) ||
			frame->color->format != context.info.source_format ||
			frame->output->format != context.info.destination_format ||
			(!approximateDepth && !depthFormat(frame->depth->format)) ||
			(approximateDepth && frame->depth->format != VK_FORMAT_R32_SFLOAT) ||
			frame->motion->format != VK_FORMAT_R16G16_SFLOAT ||
			frame->color_encoding != context.info.color_encoding ||
			frame->sharpening != context.info.sharpening ||
			!sameRect(frame->source_rect, context.info.source_rect) ||
			frame->destination_rect.left != 0 || frame->destination_rect.top != 0 ||
			frame->destination_rect.right != static_cast<int32_t>(context.info.destination_extent.width) ||
			frame->destination_rect.bottom != static_cast<int32_t>(context.info.destination_extent.height))
			return format("Image formats, extents, crop, or output do not match the created context.");
		TemporalConversion conversion{};
		const KzgsProviderDepthConvention depth = KZGS_PROVIDER_HAS_FIELD(&context.info, KzgsProviderCreateInfo, depth_convention) ?
			context.info.depth_convention : KZGS_PROVIDER_DEPTH_UNKNOWN;
		if (!convertTemporal(frame->jitter_x, frame->jitter_y, frame->motion_scale_x, frame->motion_scale_y,
				frame->camera->flags, depth, conversion) || frame->sharpening > 100)
			return missing("Invalid jitter, motion scale, or depth convention.");
		return {};
	}

	void barriers(const Context& context, const KzgsProviderFrame& frame, bool before)
	{
		const KzgsProviderImage* images[] = {
			frame.color, frame.depth, frame.motion, frame.reactive_mask, frame.composition_mask, frame.output};
		std::array<VkImageMemoryBarrier, 6> imageBarriers{};
		VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
		memory.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		memory.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		VkPipelineStageFlags sourceStages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		uint32_t barrierCount = 0;
		for (const KzgsProviderImage* imagePointer : images)
		{
			if (!imagePointer)
				continue;
			const KzgsProviderImage& image = *imagePointer;
			const bool output = imagePointer == frame.output;
			const VkImageLayout sdkLayout = output ? VK_IMAGE_LAYOUT_GENERAL : backend().inputLayout;
			VkImageMemoryBarrier& barrier = imageBarriers[barrierCount++];
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.srcAccessMask = before ? image.available_access :
				(output ? VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT : VK_ACCESS_MEMORY_READ_BIT);
			barrier.dstAccessMask = before ?
				(output ? VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT : VK_ACCESS_MEMORY_READ_BIT) :
				image.available_access;
			barrier.oldLayout = before ? image.layout : sdkLayout;
			barrier.newLayout = before ? sdkLayout : image.layout;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = image.image;
			barrier.subresourceRange = image.subresource;
			if (before)
				sourceStages |= image.available_stages;
		}
		context.barrier(frame.sync->command_buffer, sourceStages,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			0, 1, &memory, 0, nullptr, barrierCount, imageBarriers.data());
	}

	KzgsProviderResult KZGS_PROVIDER_CALL queryInfo(KzgsProviderInfo* out, KzgsProviderStatus* status)
	{
		if (!out || out->struct_size < sizeof(uint32_t))
			return report(status, invalid("Provider info buffer is missing."));
		KzgsProviderInfo info{};
		info.struct_size = sizeof(info);
		info.kind = backend().kind;
		info.name = backend().name;
		info.capabilities = KZGS_PROVIDER_CAP_TEMPORAL;
		if (backend().kind == KZGS_PROVIDER_KIND_FSR3)
		{
			info.capabilities |= KZGS_PROVIDER_CAP_APPROXIMATE_INPUTS;
			info.supported_inputs = Fsr3RequiredInputs | Fsr3OptionalInputs;
			info.required_inputs = Fsr3RequiredInputs;
			info.supported_color_encodings = KZGS_PROVIDER_COLOR_BIT(KZGS_PROVIDER_COLOR_LINEAR_SDR);
		}
		else
		{
			info.supported_inputs = RequiredInputs;
			info.required_inputs = RequiredInputs;
			info.supported_color_encodings = KZGS_PROVIDER_COLOR_BIT(KZGS_PROVIDER_COLOR_LINEAR_SDR) |
				KZGS_PROVIDER_COLOR_BIT(KZGS_PROVIDER_COLOR_LINEAR_HDR);
		}
		copyOutput(out, out->struct_size, &info, sizeof(info));
		return report(status);
	}

	KzgsProviderResult KZGS_PROVIDER_CALL instanceRequirements(KzgsProviderRequirements* out, KzgsProviderStatus* status)
	{
		if (!out || out->struct_size < sizeof(uint32_t))
			return report(status, invalid("Instance requirements buffer is missing."));
		try
		{
			requirements = {};
			const Result result = backend().instanceRequirements(requirements);
			if (result.code != KZGS_PROVIDER_OK)
				return report(status, result);
			const KzgsProviderRequirements value = requirements.describe();
			copyOutput(out, out->struct_size, &value, sizeof(value));
			return report(status);
		}
		catch (...) { return report(status, failure(KZGS_PROVIDER_ERROR_INTERNAL, KZGS_PROVIDER_REASON_OUT_OF_MEMORY, "Instance requirements failed.")); }
	}

	KzgsProviderResult KZGS_PROVIDER_CALL deviceRequirements(const KzgsProviderDeviceQuery* query,
		KzgsProviderRequirements* out, KzgsProviderStatus* status)
	{
		if (!KZGS_PROVIDER_HAS_FIELD(query, KzgsProviderDeviceQuery, get_instance_proc_addr) ||
			!query->instance || !query->physical_device || !query->get_instance_proc_addr ||
			!out || out->struct_size < sizeof(uint32_t))
			return report(status, invalid("Device requirements query is incomplete."));
		try
		{
			requirements = {};
			const Result result = backend().deviceRequirements(*query, requirements);
			if (result.code != KZGS_PROVIDER_OK)
				return report(status, result);
			const KzgsProviderRequirements value = requirements.describe();
			copyOutput(out, out->struct_size, &value, sizeof(value));
			return report(status);
		}
		catch (...) { return report(status, failure(KZGS_PROVIDER_ERROR_INTERNAL, KZGS_PROVIDER_REASON_OUT_OF_MEMORY, "Device requirements failed.")); }
	}

	KzgsProviderResult KZGS_PROVIDER_CALL create(const KzgsProviderCreateInfo* info, void** out, KzgsProviderStatus* status)
	{
		if (!out)
			return report(status, invalid("Context output pointer is missing."));
		*out = nullptr;
		const Result checked = validateCreate(info);
		if (checked.code != KZGS_PROVIDER_OK)
			return report(status, checked);
		std::unique_ptr<Context> context;
		try
		{
			context = std::make_unique<Context>();
			context->creationThread = std::this_thread::get_id();
			std::memcpy(&context->info, info, std::min<size_t>(info->struct_size, sizeof(context->info)));
			context->info.struct_size = static_cast<uint32_t>(std::min<size_t>(info->struct_size, sizeof(context->info)));
			context->barrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(
				info->get_device_proc_addr(info->device, "vkCmdPipelineBarrier"));
			if (!context->barrier)
				return report(status, failure(KZGS_PROVIDER_ERROR_CREATE, KZGS_PROVIDER_REASON_MISSING_FEATURE,
					"vkCmdPipelineBarrier is unavailable."));
			const Result result = backend().create(context->info, &context->vendor);
			context->info.initialization = nullptr;
			if (result.code != KZGS_PROVIDER_OK || !context->vendor)
			{
				if (context->vendor)
					backend().destroy(std::exchange(context->vendor, nullptr),
						result.code == KZGS_PROVIDER_ERROR_DEVICE_LOST ||
							result.code == KZGS_PROVIDER_ERROR_SYNCHRONIZATION_UNRESOLVED);
				return report(status, result.code != KZGS_PROVIDER_OK ? result :
					failure(KZGS_PROVIDER_ERROR_CREATE, KZGS_PROVIDER_REASON_PROVIDER_FAILURE, "SDK context is null."));
			}
			*out = context.release();
			return report(status);
		}
		catch (...)
		{
			if (context && context->vendor)
			{
				try { backend().destroy(context->vendor, false); }
				catch (...) {}
			}
			return report(status, failure(KZGS_PROVIDER_ERROR_CREATE, KZGS_PROVIDER_REASON_OUT_OF_MEMORY, "Context allocation failed."));
		}
	}

	KzgsProviderResult KZGS_PROVIDER_CALL evaluate(void* opaque, const KzgsProviderFrame* frame, KzgsProviderStatus* status)
	{
		auto* context = static_cast<Context*>(opaque);
		if (!context)
			return report(status, invalid("Context is null."));
		if (backend().requireCreationThread && context->creationThread != std::this_thread::get_id())
		{
			context->reset = true;
			return report(status, failure(KZGS_PROVIDER_ERROR_EVALUATE, KZGS_PROVIDER_REASON_PROVIDER_FAILURE,
				"SDK evaluation must run on its initialization thread."));
		}
		const Result checked = validateFrame(*context, frame);
		if (checked.code != KZGS_PROVIDER_OK)
		{
			context->reset = true;
			return report(status, checked);
		}
		context->lastToken = frame->sync->submission_token;
		barriers(*context, *frame, true);
		Result result;
		try { result = backend().evaluate(context->vendor, *frame, context->reset); }
		catch (...) { result = failure(KZGS_PROVIDER_ERROR_EVALUATE, KZGS_PROVIDER_REASON_PROVIDER_FAILURE, "SDK evaluation threw an exception."); }
		barriers(*context, *frame, false);
		context->reset = result.code != KZGS_PROVIDER_OK;
		if (result.code == KZGS_PROVIDER_OK)
		{
			context->hasFrame = true;
			context->lastFrame = frame->frame_id;
			context->lastPresentation = frame->presentation_id;
		}
		return report(status, result);
	}

	KzgsProviderResult KZGS_PROVIDER_CALL reset(void* opaque, const KzgsProviderResetInfo* info, KzgsProviderStatus* status)
	{
		auto* context = static_cast<Context*>(opaque);
		if (!context || !KZGS_PROVIDER_HAS_FIELD(info, KzgsProviderResetInfo, completed_token))
			return report(status, invalid("Reset information is incomplete."));
		context->reset = true;
		return report(status);
	}

	void KZGS_PROVIDER_CALL destroy(void* opaque, const KzgsProviderDestroyInfo* info)
	{
		std::unique_ptr<Context> context(static_cast<Context*>(opaque));
		if (!context)
			return;
		const bool lost = !KZGS_PROVIDER_HAS_FIELD(info, KzgsProviderDestroyInfo, completed_token) ||
			info->reason == KZGS_PROVIDER_DESTROY_DEVICE_LOST || context->lastToken > info->completed_token;
		try { backend().destroy(context->vendor, lost); }
		catch (...) {}
	}
}

namespace kzgs::vendor
{
	bool pinProviderModule()
	{
#ifdef _WIN32
		HMODULE module = nullptr;
		return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
			reinterpret_cast<LPCWSTR>(&__ImageBase), &module) != FALSE;
#else
		return false;
#endif
	}

	Result runInitialization(const KzgsProviderCreateInfo& info, uint32_t requiredGuarantees,
		InitializationWork work, void* workData)
	{
		if (!KZGS_PROVIDER_HAS_FIELD(&info, KzgsProviderCreateInfo, initialization) ||
			!KZGS_PROVIDER_HAS_FIELD(info.initialization, KzgsProviderInitialization, run_exclusive) ||
			!info.initialization->run_exclusive ||
			!(info.initialization->guarantees & KZGS_PROVIDER_INIT_EXCLUSIVE_DEVICE_IDLE))
			return failure(KZGS_PROVIDER_ERROR_CREATE, KZGS_PROVIDER_REASON_SYNCHRONIZATION,
				"Host does not provide exclusive, device-idle SDK initialization.");
		if ((info.initialization->guarantees & requiredGuarantees) != requiredGuarantees)
			return failure(KZGS_PROVIDER_ERROR_CREATE, KZGS_PROVIDER_REASON_UNQUALIFIED,
				"The requested SDK execution and image-layout contract is not qualified.");
		if (!work)
			return failure(KZGS_PROVIDER_ERROR_CREATE, KZGS_PROVIDER_REASON_PROVIDER_FAILURE,
				"SDK initialization callback is missing.");

		InitializationInvocation invocation{work, workData, std::this_thread::get_id()};
		KzgsProviderStatus status{sizeof(status)};
		KzgsProviderResult hostResult = KZGS_PROVIDER_ERROR_CREATE;
		try
		{
			hostResult = info.initialization->run_exclusive(info.initialization->host_data,
				initializationWork, &invocation, &status);
		}
		catch (...) {}
		if (hostResult != KZGS_PROVIDER_OK || invocation.repeated || invocation.wrongThread)
		{
			if (invocation.started)
				return failure(KZGS_PROVIDER_ERROR_SYNCHRONIZATION_UNRESOLVED, KZGS_PROVIDER_REASON_SYNCHRONIZATION,
					"Host could not prove completion after SDK initialization; abandon device work.", status.native_code);
			return failure(KZGS_PROVIDER_ERROR_CREATE, KZGS_PROVIDER_REASON_SYNCHRONIZATION,
				invocation.wrongThread ? "Host invoked SDK initialization on the wrong thread." :
					"Host could not enter exclusive SDK initialization.", status.native_code);
		}
		if (!invocation.called)
			return failure(KZGS_PROVIDER_ERROR_CREATE, KZGS_PROVIDER_REASON_SYNCHRONIZATION,
				"Host did not invoke the synchronous SDK initialization callback.");
		return invocation.result;
	}
}

KZGS_PROVIDER_EXPORT KzgsProviderResult KZGS_PROVIDER_CALL kzgs_upscaler_get_api(
	uint32_t version, KzgsProviderApi* out, KzgsProviderStatus* status)
{
	if (KZGS_PROVIDER_VERSION_MAJOR(version) != KZGS_PROVIDER_VERSION_MAJOR(KZGS_PROVIDER_ABI_VERSION))
		return report(status, failure(KZGS_PROVIDER_ERROR_VERSION, KZGS_PROVIDER_REASON_VERSION, "Unsupported provider ABI major."));
	if (!out || out->struct_size < sizeof(uint32_t))
		return report(status, invalid("Provider API buffer is missing."));
	KzgsProviderApi api{};
	api.struct_size = sizeof(api);
	api.abi_version = KZGS_PROVIDER_VERSION(KZGS_PROVIDER_VERSION_MAJOR(KZGS_PROVIDER_ABI_VERSION),
		std::min(KZGS_PROVIDER_VERSION_MINOR(version), KZGS_PROVIDER_VERSION_MINOR(KZGS_PROVIDER_ABI_VERSION)));
	api.query_info = queryInfo;
	api.instance_requirements = instanceRequirements;
	api.device_requirements = deviceRequirements;
	api.create = create;
	api.evaluate = evaluate;
	api.reset = reset;
	api.destroy = destroy;
	copyOutput(out, out->struct_size, &api, sizeof(api));
	return report(status);
}
