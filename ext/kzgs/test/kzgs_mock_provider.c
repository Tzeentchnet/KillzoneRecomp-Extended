// A deliberately small Vulkan blit provider used only by the ABI tests; no vendor SDK.
// SPDX-License-Identifier: GPL-3.0+
#define VK_NO_PROTOTYPES
#define KZGS_PROVIDER_BUILD
#include "kzgs_upscaler_provider.h"
#include "kzgs_mock_control.h"

#include <stdlib.h>
#include <string.h>

static uint32_t mode;
static KzgsMockStats stats;

typedef struct MockContext
{
	PFN_vkCmdBlitImage blit;
	VkQueue queue;
	uint32_t queue_family;
	uint64_t last_token;
	uint64_t last_frame;
	uint64_t last_presentation;
} MockContext;

__declspec(dllexport) void __cdecl kzgs_mock_set_mode(uint32_t value)
{
	mode = value;
}

__declspec(dllexport) void __cdecl kzgs_mock_get_stats(KzgsMockStats* value)
{
	if (value)
		*value = stats;
}

static void copy_output(void* target, uint32_t capacity, const void* value, size_t size)
{
	if (target && capacity >= sizeof(uint32_t))
		memcpy(target, value, capacity < size ? capacity : size);
}

static KzgsProviderResult result(KzgsProviderStatus* out, KzgsProviderResult code,
	KzgsProviderReason reason, int32_t native, const char* message)
{
	const KzgsProviderStatus value = {sizeof(value), code, reason, native, message};
	if (out)
		copy_output(out, out->struct_size, &value, sizeof(value));
	return code;
}

static KzgsProviderResult KZGS_PROVIDER_CALL query_info(KzgsProviderInfo* out, KzgsProviderStatus* status)
{
	KzgsProviderInfo info = {0};
	stats.query_info++;
	if (!out || out->struct_size < sizeof(uint32_t))
		return result(status, KZGS_PROVIDER_ERROR_ARGUMENT, KZGS_PROVIDER_REASON_PROVIDER_FAILURE, 0, "info size");
	info.struct_size = (mode & MOCK_SHORT_INFO) ?
		(uint32_t)offsetof(KzgsProviderInfo, supported_color_encodings) : (uint32_t)sizeof(info);
	info.kind = (mode & MOCK_KIND_XESS) ? KZGS_PROVIDER_KIND_XESS : KZGS_PROVIDER_KIND_DLSS;
	info.name = "in-tree mock";
	info.capabilities = (mode & MOCK_TEMPORAL_COLOR_ONLY) ?
		(KZGS_PROVIDER_CAP_TEMPORAL | ((mode & MOCK_DUAL_CAPABILITY) ? KZGS_PROVIDER_CAP_SPATIAL : 0)) :
		KZGS_PROVIDER_CAP_SPATIAL;
	info.supported_inputs = KZGS_PROVIDER_INPUT_COLOR | KZGS_PROVIDER_INPUT_DEPTH;
	info.required_inputs = KZGS_PROVIDER_INPUT_COLOR | ((mode & MOCK_REQUIRE_DEPTH) ? KZGS_PROVIDER_INPUT_DEPTH : 0);
	info.supported_color_encodings = (mode & MOCK_BAD_COLOR_ENCODING) ?
		KZGS_PROVIDER_COLOR_BIT(KZGS_PROVIDER_COLOR_SRGB) : KZGS_PROVIDER_COLOR_BIT(KZGS_PROVIDER_COLOR_UNKNOWN);
	copy_output(out, out->struct_size, &info, sizeof(info));
	return result(status, KZGS_PROVIDER_OK, KZGS_PROVIDER_REASON_NONE, 0, NULL);
}

static KzgsProviderResult KZGS_PROVIDER_CALL instance_requirements(
	KzgsProviderRequirements* out, KzgsProviderStatus* status)
{
	static const char* missing[] = {"VK_KZGS_mock_missing_instance"};
	static const char* available[] = {VK_KHR_SURFACE_EXTENSION_NAME};
	KzgsProviderRequirements requirements = {0};
	stats.instance_requirements++;
	if (!out || out->struct_size < sizeof(uint32_t))
		return result(status, KZGS_PROVIDER_ERROR_ARGUMENT, KZGS_PROVIDER_REASON_PROVIDER_FAILURE, 0, "requirements size");
	requirements.struct_size = sizeof(requirements);
	if (mode & MOCK_MISSING_INSTANCE_EXTENSION)
	{
		requirements.extension_count = 1;
		requirements.extensions = missing;
	}
	else if (mode & MOCK_VALID_FEATURE)
	{
		requirements.extension_count = 1;
		requirements.extensions = available;
	}
	copy_output(out, out->struct_size, &requirements, sizeof(requirements));
	return result(status, KZGS_PROVIDER_OK, KZGS_PROVIDER_REASON_NONE, 0, NULL);
}

static KzgsProviderResult KZGS_PROVIDER_CALL device_requirements(const KzgsProviderDeviceQuery* query,
	KzgsProviderRequirements* out, KzgsProviderStatus* status)
{
	static const char* missing[] = {"VK_KZGS_mock_missing_device"};
	static const char* available_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
	static VkBool32 bits[4];
	static VkBool32 variable_bits[2];
	static struct FutureFeatureBlock
	{
		KzgsProviderFeatureBlock feature;
		uint64_t future[2];
	} features[2];
	KzgsProviderRequirements requirements = {0};
	stats.device_requirements++;
	if (!KZGS_PROVIDER_HAS_FIELD(query, KzgsProviderDeviceQuery, get_instance_proc_addr) ||
		!query->instance || !query->physical_device || !query->get_instance_proc_addr ||
		!out || out->struct_size < sizeof(uint32_t))
		return result(status, KZGS_PROVIDER_ERROR_ARGUMENT, KZGS_PROVIDER_REASON_PROVIDER_FAILURE, 0, "device query");
	requirements.struct_size = sizeof(requirements);
	if (mode & MOCK_MISSING_DEVICE_EXTENSION)
	{
		requirements.extension_count = 1;
		requirements.extensions = missing;
	}
	if (mode & (MOCK_BAD_FEATURE | MOCK_VALID_FEATURE | MOCK_MISALIGNED_FEATURE_POINTER |
		MOCK_MISALIGNED_FEATURE_STRIDE | MOCK_MISALIGNED_FEATURE_BITS | MOCK_MUTABLE_WITHOUT_EXTENSION))
	{
		features[0].feature.struct_size = sizeof(features[0]);
		features[0].feature.s_type = (mode & MOCK_BAD_FEATURE) ? VK_STRUCTURE_TYPE_MAX_ENUM :
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES;
		features[0].feature.feature_count = 4;
		features[0].feature.required = bits;
		features[1].feature.struct_size = sizeof(features[1]);
		features[1].feature.s_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES;
		features[1].feature.feature_count = 2;
		features[1].feature.required = variable_bits;
		requirements.feature_block_count = (mode & MOCK_BAD_FEATURE) ? 1 : 2;
		requirements.feature_blocks = &features[0].feature;
		requirements.feature_block_stride = sizeof(features[0]);
		if (mode & MOCK_MISALIGNED_FEATURE_POINTER)
			requirements.feature_blocks = (const KzgsProviderFeatureBlock*)((const unsigned char*)requirements.feature_blocks + 1);
		if (mode & MOCK_MISALIGNED_FEATURE_STRIDE)
			requirements.feature_block_stride--;
		if (mode & MOCK_MISALIGNED_FEATURE_BITS)
			features[0].feature.required = (const VkBool32*)((const unsigned char*)bits + 1);
		if (mode & MOCK_MUTABLE_WITHOUT_EXTENSION)
		{
			features[0].feature.s_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT;
			features[0].feature.feature_count = 1;
			requirements.feature_block_count = 1;
		}
	}
	if (mode & MOCK_VALID_FEATURE)
	{
		PFN_vkEnumerateDeviceExtensionProperties enumerate = (PFN_vkEnumerateDeviceExtensionProperties)
			query->get_instance_proc_addr(query->instance, "vkEnumerateDeviceExtensionProperties");
		PFN_vkGetPhysicalDeviceFeatures get_features = (PFN_vkGetPhysicalDeviceFeatures)
			query->get_instance_proc_addr(query->instance, "vkGetPhysicalDeviceFeatures");
		PFN_vkGetPhysicalDeviceFeatures2 get_features2 = (PFN_vkGetPhysicalDeviceFeatures2)
			query->get_instance_proc_addr(query->instance, "vkGetPhysicalDeviceFeatures2");
		VkPhysicalDeviceFeatures available = {0};
		VkPhysicalDevice16BitStorageFeatures storage = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES};
		VkPhysicalDeviceFeatures2 features2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
		uint32_t extension_count = 0;
		available_extensions[0] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
		stats.extra_extension_requested = 0;
		if (enumerate && enumerate(query->physical_device, NULL, &extension_count, NULL) == VK_SUCCESS && extension_count)
		{
			VkExtensionProperties* extensions = (VkExtensionProperties*)calloc(extension_count, sizeof(*extensions));
			if (extensions && enumerate(query->physical_device, NULL, &extension_count, extensions) == VK_SUCCESS)
			{
				uint32_t i;
				for (i = 0; i < extension_count; i++)
				{
					if (strcmp(extensions[i].extensionName, VK_KHR_8BIT_STORAGE_EXTENSION_NAME) == 0)
					{
						available_extensions[0] = VK_KHR_8BIT_STORAGE_EXTENSION_NAME;
						stats.extra_extension_requested = 1;
						break;
					}
				}
			}
			free(extensions);
		}
		requirements.extension_count = 1;
		requirements.extensions = available_extensions;
		if (get_features)
			get_features(query->physical_device, &available);
		features2.pNext = &storage;
		if (get_features2)
			get_features2(query->physical_device, &features2);
		bits[0] = storage.storageBuffer16BitAccess;
		requirements.core_features.robustBufferAccess = available.robustBufferAccess;
	}
	copy_output(out, out->struct_size, &requirements, sizeof(requirements));
	return result(status, KZGS_PROVIDER_OK, KZGS_PROVIDER_REASON_NONE, 0, NULL);
}

static KzgsProviderResult KZGS_PROVIDER_CALL create_context(
	const KzgsProviderCreateInfo* info, void** out, KzgsProviderStatus* status)
{
	MockContext* context;
	stats.creates++;
	if (!KZGS_PROVIDER_HAS_FIELD(info, KzgsProviderCreateInfo, sharpening) || !out ||
		!info->instance || !info->physical_device || !info->device || !info->queue ||
		!info->get_instance_proc_addr || !info->get_device_proc_addr ||
		info->source_format != VK_FORMAT_R8G8B8A8_UNORM ||
		info->destination_format != VK_FORMAT_R8G8B8A8_UNORM || !info->destination_extent.width ||
		!info->destination_extent.height || info->color_encoding != KZGS_PROVIDER_COLOR_UNKNOWN)
		return result(status, KZGS_PROVIDER_ERROR_ARGUMENT, KZGS_PROVIDER_REASON_UNSUPPORTED_FORMAT, 0, "create info");
	context = (MockContext*)calloc(1, sizeof(*context));
	if (!context)
		return result(status, KZGS_PROVIDER_ERROR_CREATE, KZGS_PROVIDER_REASON_OUT_OF_MEMORY, 0, "allocation");
	context->blit = (PFN_vkCmdBlitImage)info->get_device_proc_addr(info->device, "vkCmdBlitImage");
	context->queue = info->queue;
	context->queue_family = info->queue_family;
	*out = context;
	if (mode & MOCK_CREATE_DEVICE_LOST)
		return result(status, KZGS_PROVIDER_ERROR_DEVICE_LOST, KZGS_PROVIDER_REASON_DEVICE_LOST, -4, "mock create device loss");
	if (!context->blit || (mode & MOCK_CREATE_FAILURE))
		return result(status, KZGS_PROVIDER_ERROR_CREATE, KZGS_PROVIDER_REASON_PROVIDER_FAILURE, -101, "mock create failure");
	return result(status, KZGS_PROVIDER_OK, KZGS_PROVIDER_REASON_NONE, 0, NULL);
}

static KzgsProviderResult KZGS_PROVIDER_CALL evaluate(
	void* opaque, const KzgsProviderFrame* frame, KzgsProviderStatus* status)
{
	MockContext* context = (MockContext*)opaque;
	VkImageBlit blit = {0};
	stats.evaluates++;
	if (!context || !KZGS_PROVIDER_HAS_FIELD(frame, KzgsProviderFrame, camera) ||
		!KZGS_PROVIDER_HAS_FIELD(frame->sync, KzgsProviderSync, completed_token) ||
		!KZGS_PROVIDER_HAS_FIELD(frame->color, KzgsProviderImage, available_access) ||
		!KZGS_PROVIDER_HAS_FIELD(frame->output, KzgsProviderImage, available_access) ||
		!frame->sync->command_buffer || !frame->sync->queue || !frame->color->image || !frame->color->view ||
		!frame->output->image || !frame->output->view || frame->color->image == frame->output->image ||
		frame->sync->queue != context->queue || frame->sync->submission_token <= frame->sync->completed_token ||
		frame->frame_id <= context->last_frame || frame->presentation_id <= context->last_presentation ||
		frame->color->layout != VK_IMAGE_LAYOUT_GENERAL || frame->output->layout != VK_IMAGE_LAYOUT_GENERAL ||
		frame->color->format != VK_FORMAT_R8G8B8A8_UNORM || frame->output->format != VK_FORMAT_R8G8B8A8_UNORM ||
		!(frame->color->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ||
		!(frame->output->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
		frame->color->subresource.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT ||
		frame->output->subresource.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT ||
		frame->color->available_stages != VK_PIPELINE_STAGE_ALL_COMMANDS_BIT ||
		frame->output->available_stages != VK_PIPELINE_STAGE_ALL_COMMANDS_BIT ||
		(frame->color->available_access & (VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT)) !=
			(VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT) ||
		(frame->output->available_access & (VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT)) !=
			(VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT) ||
		frame->sync->queue_family != context->queue_family || frame->color->queue_family != context->queue_family ||
		frame->output->queue_family != context->queue_family || frame->valid_inputs != KZGS_PROVIDER_INPUT_COLOR ||
		frame->depth || frame->motion || frame->reactive_mask || frame->composition_mask || frame->camera ||
		frame->color_encoding != KZGS_PROVIDER_COLOR_UNKNOWN || frame->source_rect.left < 0 ||
		frame->source_rect.top < 0 || frame->source_rect.right <= frame->source_rect.left ||
		frame->source_rect.bottom <= frame->source_rect.top ||
		(uint32_t)frame->source_rect.right > frame->color->extent.width ||
		(uint32_t)frame->source_rect.bottom > frame->color->extent.height ||
		frame->destination_rect.left != 0 || frame->destination_rect.top != 0 ||
		frame->destination_rect.right <= 0 || frame->destination_rect.bottom <= 0 ||
		(uint32_t)frame->destination_rect.right > frame->output->extent.width ||
		(uint32_t)frame->destination_rect.bottom > frame->output->extent.height)
		return result(status, KZGS_PROVIDER_ERROR_ARGUMENT, KZGS_PROVIDER_REASON_PROVIDER_FAILURE, 0, "frame contract");
	context->last_token = frame->sync->submission_token;
	context->last_frame = frame->frame_id;
	context->last_presentation = frame->presentation_id;
	stats.last_frame_id = frame->frame_id;
	stats.last_presentation_id = frame->presentation_id;
	stats.last_submission_token = frame->sync->submission_token;
	stats.last_completed_token = frame->sync->completed_token;
	stats.last_valid_inputs = frame->valid_inputs;
	stats.last_output_width = frame->output->extent.width;
	stats.last_output_height = frame->output->extent.height;
	stats.last_sharpening = frame->sharpening;
	blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blit.srcSubresource.layerCount = 1;
	blit.srcOffsets[0].x = frame->source_rect.left;
	blit.srcOffsets[0].y = frame->source_rect.top;
	blit.srcOffsets[1].x = frame->source_rect.right;
	blit.srcOffsets[1].y = frame->source_rect.bottom;
	blit.srcOffsets[1].z = 1;
	blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blit.dstSubresource.layerCount = 1;
	blit.dstOffsets[0].x = frame->destination_rect.left;
	blit.dstOffsets[0].y = frame->destination_rect.top;
	blit.dstOffsets[1].x = frame->destination_rect.right;
	blit.dstOffsets[1].y = frame->destination_rect.bottom;
	blit.dstOffsets[1].z = 1;
	context->blit(frame->sync->command_buffer, frame->color->image, frame->color->layout,
		frame->output->image, frame->output->layout, 1, &blit, VK_FILTER_NEAREST);
	if (mode & MOCK_DEVICE_LOST)
		return result(status, KZGS_PROVIDER_ERROR_DEVICE_LOST, KZGS_PROVIDER_REASON_DEVICE_LOST, -4, "mock device loss");
	if (mode & MOCK_EVALUATE_FAILURE)
		return result(status, KZGS_PROVIDER_ERROR_EVALUATE, KZGS_PROVIDER_REASON_PROVIDER_FAILURE, -202, "mock dispatch failure");
	return result(status, KZGS_PROVIDER_OK, KZGS_PROVIDER_REASON_NONE, 0, NULL);
}

static KzgsProviderResult KZGS_PROVIDER_CALL reset(
	void* opaque, const KzgsProviderResetInfo* info, KzgsProviderStatus* status)
{
	MockContext* context = (MockContext*)opaque;
	stats.resets++;
	if (!context || !KZGS_PROVIDER_HAS_FIELD(info, KzgsProviderResetInfo, completed_token))
		return result(status, KZGS_PROVIDER_ERROR_ARGUMENT, KZGS_PROVIDER_REASON_PROVIDER_FAILURE, 0, "reset info");
	if (context->last_token > info->completed_token)
		stats.unsafe_destroys++;
	stats.last_reset_reason = info->reason;
	if (mode & MOCK_RESET_DEVICE_LOST)
		return result(status, KZGS_PROVIDER_ERROR_DEVICE_LOST, KZGS_PROVIDER_REASON_DEVICE_LOST, -4, "mock reset device loss");
	if (mode & MOCK_RESET_FAILURE)
		return result(status, KZGS_PROVIDER_ERROR_RESET, KZGS_PROVIDER_REASON_PROVIDER_FAILURE, -303, "mock reset failure");
	return result(status, KZGS_PROVIDER_OK, KZGS_PROVIDER_REASON_NONE, 0, NULL);
}

static void KZGS_PROVIDER_CALL destroy(void* opaque, const KzgsProviderDestroyInfo* info)
{
	MockContext* context = (MockContext*)opaque;
	stats.destroys++;
	if (!context || !KZGS_PROVIDER_HAS_FIELD(info, KzgsProviderDestroyInfo, completed_token))
	{
		stats.unsafe_destroys++;
		return;
	}
	stats.last_destroy_reason = info->reason;
	if (info->reason == KZGS_PROVIDER_DESTROY_COMPLETED && context->last_token > info->completed_token)
		stats.unsafe_destroys++;
	free(context);
}

KZGS_PROVIDER_EXPORT KzgsProviderResult KZGS_PROVIDER_CALL kzgs_upscaler_get_api(
	uint32_t requested_version, KzgsProviderApi* out, KzgsProviderStatus* status)
{
	KzgsProviderApi api = {0};
	if ((mode & MOCK_VERSION_MISMATCH) || KZGS_PROVIDER_VERSION_MAJOR(requested_version) != 1)
		return result(status, KZGS_PROVIDER_ERROR_VERSION, KZGS_PROVIDER_REASON_VERSION, -1, "mock ABI mismatch");
	if (!out || out->struct_size < sizeof(uint32_t))
		return result(status, KZGS_PROVIDER_ERROR_ARGUMENT, KZGS_PROVIDER_REASON_PROVIDER_FAILURE, 0, "API size");
	api.struct_size = (mode & MOCK_SHORT_API) ?
		(uint32_t)offsetof(KzgsProviderApi, reset) : (uint32_t)sizeof(api);
	api.abi_version = KZGS_PROVIDER_ABI_VERSION;
	api.query_info = query_info;
	api.instance_requirements = instance_requirements;
	api.device_requirements = device_requirements;
	api.create = create_context;
	api.evaluate = evaluate;
	api.reset = reset;
	api.destroy = destroy;
	copy_output(out, out->struct_size, &api, sizeof(api));
	return result(status, KZGS_PROVIDER_OK, KZGS_PROVIDER_REASON_NONE, 0, NULL);
}
