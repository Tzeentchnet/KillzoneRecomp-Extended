// SPDX-License-Identifier: GPL-3.0+
#pragma once
#include <stdint.h>

enum
{
	MOCK_VERSION_MISMATCH = 1u << 0,
	MOCK_MISSING_INSTANCE_EXTENSION = 1u << 1,
	MOCK_MISSING_DEVICE_EXTENSION = 1u << 2,
	MOCK_BAD_FEATURE = 1u << 3,
	MOCK_CREATE_FAILURE = 1u << 4,
	MOCK_EVALUATE_FAILURE = 1u << 5,
	MOCK_RESET_FAILURE = 1u << 6,
	MOCK_REQUIRE_DEPTH = 1u << 7,
	MOCK_VALID_FEATURE = 1u << 8,
	MOCK_DEVICE_LOST = 1u << 9,
	MOCK_BAD_COLOR_ENCODING = 1u << 10,
	MOCK_SHORT_API = 1u << 11,
	MOCK_SHORT_INFO = 1u << 12,
	MOCK_MISALIGNED_FEATURE_POINTER = 1u << 13,
	MOCK_MISALIGNED_FEATURE_STRIDE = 1u << 14,
	MOCK_MISALIGNED_FEATURE_BITS = 1u << 15,
	MOCK_KIND_XESS = 1u << 16,
	MOCK_CREATE_DEVICE_LOST = 1u << 17,
	MOCK_RESET_DEVICE_LOST = 1u << 18,
	MOCK_MUTABLE_WITHOUT_EXTENSION = 1u << 19,
	MOCK_TEMPORAL_COLOR_ONLY = 1u << 20,
	MOCK_DUAL_CAPABILITY = 1u << 21,
	MOCK_CREATE_UNRESOLVED_SYNC = 1u << 22,
	MOCK_ABI_1_0 = 1u << 23,
	MOCK_RESET_UNRESOLVED_SYNC = 1u << 24,
	MOCK_QUERY_FAILURE = 1u << 25,
	MOCK_UNSUPPORTED_DEVICE = 1u << 26,
	MOCK_KIND_FSR3 = 1u << 27,
	MOCK_ABI_1_1 = 1u << 28,
	MOCK_APPROXIMATE_CAPABILITY = 1u << 29
};

typedef struct KzgsMockStats
{
	uint32_t query_info, instance_requirements, device_requirements;
	uint32_t creates, evaluates, resets, destroys, unsafe_destroys;
	uint32_t last_reset_reason, last_destroy_reason;
	uint32_t last_output_width, last_output_height;
	uint32_t extra_extension_requested;
	uint32_t last_sharpening;
	uint64_t last_frame_id, last_presentation_id, last_submission_token, last_completed_token;
	uint64_t last_valid_inputs;
	uint64_t last_approximate_inputs;
	uint32_t negotiated_version;
	uint32_t last_frame_struct_size;
} KzgsMockStats;

typedef void (__cdecl *KzgsMockSetMode)(uint32_t mode);
typedef void (__cdecl *KzgsMockGetStats)(KzgsMockStats* stats);
