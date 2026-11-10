// Optional Vulkan presentation-upscaler provider ABI. See docs/upscaler-provider-abi.md.
// SPDX-License-Identifier: GPL-3.0+
#ifndef KZGS_UPSCALER_PROVIDER_H
#define KZGS_UPSCALER_PROVIDER_H

#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define KZGS_PROVIDER_CALL __cdecl
#if defined(KZGS_PROVIDER_BUILD)
#define KZGS_PROVIDER_EXPORT __declspec(dllexport)
#else
#define KZGS_PROVIDER_EXPORT
#endif
#else
#define KZGS_PROVIDER_CALL
#define KZGS_PROVIDER_EXPORT __attribute__((visibility("default")))
#endif

#define KZGS_PROVIDER_VERSION(major, minor) (((uint32_t)(major) << 16) | (uint32_t)(minor))
#define KZGS_PROVIDER_ABI_VERSION KZGS_PROVIDER_VERSION(1, 1)
#define KZGS_PROVIDER_VERSION_MAJOR(version) ((uint32_t)(version) >> 16)
#define KZGS_PROVIDER_VERSION_MINOR(version) ((uint32_t)(version) & 0xffffu)
#define KZGS_PROVIDER_FIELD_END(type, field) (offsetof(type, field) + sizeof(((type*)0)->field))
#define KZGS_PROVIDER_HAS_FIELD(value, type, field) \
	((value) != NULL && (value)->struct_size >= KZGS_PROVIDER_FIELD_END(type, field))

typedef uint32_t KzgsProviderResult;
enum
{
	KZGS_PROVIDER_OK = 0,
	KZGS_PROVIDER_ERROR_VERSION = 1,
	KZGS_PROVIDER_ERROR_ARGUMENT = 2,
	KZGS_PROVIDER_ERROR_UNSUPPORTED = 3,
	KZGS_PROVIDER_ERROR_INPUT = 4,
	KZGS_PROVIDER_ERROR_EXTENSIONS = 5,
	KZGS_PROVIDER_ERROR_FEATURES = 6,
	KZGS_PROVIDER_ERROR_CREATE = 7,
	KZGS_PROVIDER_ERROR_EVALUATE = 8,
	KZGS_PROVIDER_ERROR_RESET = 9,
	KZGS_PROVIDER_ERROR_DEVICE_LOST = 10,
	KZGS_PROVIDER_ERROR_INTERNAL = 11
};

typedef uint32_t KzgsProviderKind;
enum
{
	KZGS_PROVIDER_KIND_DLSS = 1,
	KZGS_PROVIDER_KIND_XESS = 2
};

typedef uint32_t KzgsProviderReason;
enum
{
	KZGS_PROVIDER_REASON_NONE = 0,
	KZGS_PROVIDER_REASON_VERSION = 1,
	KZGS_PROVIDER_REASON_UNSUPPORTED_DEVICE = 2,
	KZGS_PROVIDER_REASON_MISSING_EXTENSION = 3,
	KZGS_PROVIDER_REASON_MISSING_FEATURE = 4,
	KZGS_PROVIDER_REASON_MISSING_INPUT = 5,
	KZGS_PROVIDER_REASON_UNSUPPORTED_FORMAT = 6,
	KZGS_PROVIDER_REASON_OUT_OF_MEMORY = 7,
	KZGS_PROVIDER_REASON_DEVICE_LOST = 8,
	KZGS_PROVIDER_REASON_PROVIDER_FAILURE = 9,
	KZGS_PROVIDER_REASON_SYNCHRONIZATION = 10,
	KZGS_PROVIDER_REASON_UNQUALIFIED = 11
};

typedef struct KzgsProviderStatus
{
	uint32_t struct_size;
	KzgsProviderResult result;
	KzgsProviderReason reason;
	int32_t native_code;        /* e.g. VkResult or provider-specific error */
	const char* message;        /* UTF-8, borrowed until the next provider call */
} KzgsProviderStatus;

typedef uint64_t KzgsProviderInputs;
#define KZGS_PROVIDER_INPUT_COLOR            (UINT64_C(1) << 0)
#define KZGS_PROVIDER_INPUT_DEPTH            (UINT64_C(1) << 1)
#define KZGS_PROVIDER_INPUT_MOTION           (UINT64_C(1) << 2)
#define KZGS_PROVIDER_INPUT_JITTER           (UINT64_C(1) << 3)
#define KZGS_PROVIDER_INPUT_REACTIVE_MASK    (UINT64_C(1) << 4)
#define KZGS_PROVIDER_INPUT_COMPOSITION_MASK (UINT64_C(1) << 5)
#define KZGS_PROVIDER_INPUT_CAMERA           (UINT64_C(1) << 6)
#define KZGS_PROVIDER_INPUT_FRAME_TIME       (UINT64_C(1) << 7)

typedef uint32_t KzgsProviderColorEncoding;
enum
{
	KZGS_PROVIDER_COLOR_UNKNOWN = 0,
	KZGS_PROVIDER_COLOR_SRGB = 1,
	KZGS_PROVIDER_COLOR_LINEAR_SDR = 2,
	KZGS_PROVIDER_COLOR_LINEAR_HDR = 3,
	KZGS_PROVIDER_COLOR_PQ = 4
};
#define KZGS_PROVIDER_COLOR_BIT(encoding) (UINT64_C(1) << (encoding))

typedef struct KzgsProviderInfo
{
	uint32_t struct_size;
	KzgsProviderKind kind;
	const char* name;             /* UTF-8, borrowed until unload */
	uint64_t capabilities;        /* KZGS_PROVIDER_CAP_* */
	KzgsProviderInputs supported_inputs;
	KzgsProviderInputs required_inputs;
	uint64_t supported_color_encodings;
} KzgsProviderInfo;

#define KZGS_PROVIDER_CAP_SPATIAL  (UINT64_C(1) << 0)
#define KZGS_PROVIDER_CAP_TEMPORAL (UINT64_C(1) << 1)

/* A feature block names a Vulkan VkPhysicalDevice*Features struct. Values correspond
 * to its VkBool32 members in declaration order, excluding sType/pNext. V1 hosts may
 * reject unrecognized sTypes and must not silently ignore required bits. */
typedef struct KzgsProviderFeatureBlock
{
	uint32_t struct_size;
	VkStructureType s_type;
	uint32_t feature_count;
	const VkBool32* required;
} KzgsProviderFeatureBlock;

typedef struct KzgsProviderRequirements
{
	uint32_t struct_size;
	uint32_t extension_count;
	const char* const* extensions; /* names and feature arrays borrowed until next call */
	VkPhysicalDeviceFeatures core_features;
	uint32_t feature_block_count;
	const KzgsProviderFeatureBlock* feature_blocks;
	uint32_t feature_block_stride; /* byte stride, at least each block's struct_size */
} KzgsProviderRequirements;

typedef struct KzgsProviderDeviceQuery
{
	uint32_t struct_size;
	VkInstance instance;
	VkPhysicalDevice physical_device;
	PFN_vkGetInstanceProcAddr get_instance_proc_addr;
} KzgsProviderDeviceQuery;

/* Rectangles are half-open pixel coordinates; extents describe whole images. */
typedef struct KzgsProviderRect
{
	int32_t left, top, right, bottom;
} KzgsProviderRect;

typedef struct KzgsProviderImage
{
	uint32_t struct_size;
	VkImage image;
	VkImageView view;
	VkFormat format;
	VkExtent2D extent;
	VkImageSubresourceRange subresource;
	VkImageLayout layout;
	VkImageUsageFlags usage;
	uint32_t queue_family;
	VkPipelineStageFlags available_stages;
	VkAccessFlags available_access;
} KzgsProviderImage;

typedef struct KzgsProviderSync
{
	uint32_t struct_size;
	VkCommandBuffer command_buffer;
	VkQueue queue;
	uint32_t queue_family;
	uint64_t submission_token; /* host fence counter, not a VkFence */
	uint64_t completed_token;
} KzgsProviderSync;

typedef struct KzgsProviderCamera
{
	uint32_t struct_size;
	float near_plane;
	float far_plane;
	float vertical_fov_radians;
	float view_to_clip[16];
	float previous_view_to_clip[16];
	uint32_t flags; /* KZGS_PROVIDER_CAMERA_* */
	float world_to_view[16];
	float previous_world_to_view[16];
} KzgsProviderCamera;
#define KZGS_PROVIDER_CAMERA_REVERSED_DEPTH (1u << 0)
#define KZGS_PROVIDER_CAMERA_INFINITE_FAR  (1u << 1)

typedef uint32_t KzgsProviderDepthConvention;
enum
{
	KZGS_PROVIDER_DEPTH_UNKNOWN = 0,
	KZGS_PROVIDER_DEPTH_FORWARD = 1,
	KZGS_PROVIDER_DEPTH_REVERSED = 2
};

/* ABI 1.1: the host may provide this synchronous, exclusive initialization scope.
 * The work callback and its data must not be retained or invoked asynchronously.
 * See the ABI documentation for the required queue, idle, and failure guarantees. */
typedef void (KZGS_PROVIDER_CALL *KzgsProviderInitializationWork)(void* work_data);
typedef KzgsProviderResult (KZGS_PROVIDER_CALL *KzgsProviderRunInitialization)(
	void* host_data, KzgsProviderInitializationWork work, void* work_data, KzgsProviderStatus* status);

#define KZGS_PROVIDER_INIT_EXCLUSIVE_DEVICE_IDLE (1u << 0)
#define KZGS_PROVIDER_INIT_SDK_EXECUTION_QUALIFIED (1u << 1)
#define KZGS_PROVIDER_INIT_NGX_GENERAL_LAYOUT_QUALIFIED (1u << 2)

typedef struct KzgsProviderInitialization
{
	uint32_t struct_size;
	uint32_t guarantees;
	void* host_data;
	KzgsProviderRunInitialization run_exclusive;
} KzgsProviderInitialization;

typedef struct KzgsProviderCreateInfo
{
	uint32_t struct_size;
	VkInstance instance;
	VkPhysicalDevice physical_device;
	VkDevice device;
	VkQueue queue;
	uint32_t queue_family;
	PFN_vkGetInstanceProcAddr get_instance_proc_addr;
	PFN_vkGetDeviceProcAddr get_device_proc_addr;
	VkExtent2D source_extent;
	KzgsProviderRect source_rect;
	VkFormat source_format;
	VkExtent2D destination_extent;
	VkFormat destination_format;
	KzgsProviderColorEncoding color_encoding;
	uint32_t sharpening; /* 0..100 */
	/* ABI 1.1 trailing fields; absent on 1.0 hosts. Borrowed only for create. */
	const KzgsProviderInitialization* initialization;
	KzgsProviderDepthConvention depth_convention;
} KzgsProviderCreateInfo;

typedef struct KzgsProviderFrame
{
	uint32_t struct_size;
	uint64_t frame_id;        /* new source-frame identity, not a VSync count */
	uint64_t presentation_id; /* successful-present sequence candidate */
	KzgsProviderInputs valid_inputs;
	const KzgsProviderSync* sync;    /* borrowed for evaluate; required */
	const KzgsProviderImage* color;  /* borrowed for evaluate; required */
	const KzgsProviderImage* output; /* borrowed for evaluate; required */
	KzgsProviderRect source_rect;
	KzgsProviderRect destination_rect;
	float presentation_rect[4]; /* window coordinates: left, top, right, bottom */
	KzgsProviderColorEncoding color_encoding;
	uint32_t sharpening;
	/* Optional images share the color pixel grid and source_rect; valid_inputs controls presence. */
	const KzgsProviderImage* depth;
	const KzgsProviderImage* motion;
	const KzgsProviderImage* reactive_mask;
	const KzgsProviderImage* composition_mask;
	float jitter_x, jitter_y;   /* source pixels, current frame */
	float motion_scale_x, motion_scale_y;
	float frame_time_ms;
	const KzgsProviderCamera* camera;
} KzgsProviderFrame;

typedef uint32_t KzgsProviderResetReason;
enum
{
	KZGS_PROVIDER_RESET_INITIAL = 0,
	KZGS_PROVIDER_RESET_GAME = 1,
	KZGS_PROVIDER_RESET_RESIZE = 2,
	KZGS_PROVIDER_RESET_DISCONTINUITY = 3,
	KZGS_PROVIDER_RESET_PRESENT_FAILED = 4,
	KZGS_PROVIDER_RESET_CONFIGURATION = 5
};

typedef struct KzgsProviderResetInfo
{
	uint32_t struct_size;
	KzgsProviderResetReason reason;
	uint64_t completed_token;
} KzgsProviderResetInfo;

typedef uint32_t KzgsProviderDestroyReason;
enum
{
	KZGS_PROVIDER_DESTROY_COMPLETED = 0,
	KZGS_PROVIDER_DESTROY_DEVICE_LOST = 1
};

typedef struct KzgsProviderDestroyInfo
{
	uint32_t struct_size;
	KzgsProviderDestroyReason reason;
	uint64_t completed_token;
} KzgsProviderDestroyInfo;

typedef struct KzgsProviderApi
{
	uint32_t struct_size;
	uint32_t abi_version;
	KzgsProviderResult (KZGS_PROVIDER_CALL *query_info)(KzgsProviderInfo*, KzgsProviderStatus*);
	KzgsProviderResult (KZGS_PROVIDER_CALL *instance_requirements)(KzgsProviderRequirements*, KzgsProviderStatus*);
	KzgsProviderResult (KZGS_PROVIDER_CALL *device_requirements)(
		const KzgsProviderDeviceQuery*, KzgsProviderRequirements*, KzgsProviderStatus*);
	KzgsProviderResult (KZGS_PROVIDER_CALL *create)(
		const KzgsProviderCreateInfo*, void** context, KzgsProviderStatus*);
	KzgsProviderResult (KZGS_PROVIDER_CALL *evaluate)(
		void* context, const KzgsProviderFrame*, KzgsProviderStatus*);
	KzgsProviderResult (KZGS_PROVIDER_CALL *reset)(
		void* context, const KzgsProviderResetInfo*, KzgsProviderStatus*);
	void (KZGS_PROVIDER_CALL *destroy)(void* context, const KzgsProviderDestroyInfo*);
} KzgsProviderApi;

typedef KzgsProviderResult (KZGS_PROVIDER_CALL *KzgsProviderGetApi)(
	uint32_t requested_version, KzgsProviderApi* api, KzgsProviderStatus* status);

KZGS_PROVIDER_EXPORT KzgsProviderResult KZGS_PROVIDER_CALL kzgs_upscaler_get_api(
	uint32_t requested_version, KzgsProviderApi* api, KzgsProviderStatus* status);

#ifdef __cplusplus
}
#endif
#endif
