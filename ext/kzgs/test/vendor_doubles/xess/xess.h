// Independently authored declarations for test doubles, not an Intel SDK.
// SPDX-License-Identifier: GPL-3.0+
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xess_fake_context* xess_context_handle_t;
typedef enum xess_result_t
{
	XESS_RESULT_SUCCESS = 0,
	XESS_RESULT_ERROR_UNSUPPORTED_DEVICE = -1,
	XESS_RESULT_ERROR_UNSUPPORTED_DRIVER = -2,
	XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY = -5,
	XESS_RESULT_ERROR_DEVICE = -6
} xess_result_t;
typedef struct xess_version_t { uint16_t major, minor, patch, reserved; } xess_version_t;
typedef struct xess_2d_t { uint32_t x, y; } xess_2d_t;
typedef xess_2d_t xess_coord_t;
typedef enum xess_quality_settings_t
{
	XESS_QUALITY_SETTING_ULTRA_PERFORMANCE = 100,
	XESS_QUALITY_SETTING_PERFORMANCE = 101,
	XESS_QUALITY_SETTING_BALANCED = 102,
	XESS_QUALITY_SETTING_QUALITY = 103,
	XESS_QUALITY_SETTING_ULTRA_QUALITY = 104,
	XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS = 105,
	XESS_QUALITY_SETTING_AA = 106
} xess_quality_settings_t;
enum
{
	XESS_INIT_FLAG_HIGH_RES_MV = 1 << 0,
	XESS_INIT_FLAG_INVERTED_DEPTH = 1 << 1,
	XESS_INIT_FLAG_USE_NDC_VELOCITY = 1 << 4,
	XESS_INIT_FLAG_LDR_INPUT_COLOR = 1 << 6,
	XESS_INIT_FLAG_JITTERED_MV = 1 << 7,
	XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE = 1 << 8
};

xess_result_t xessGetVersion(xess_version_t*);
xess_result_t xessGetOptimalInputResolution(xess_context_handle_t, const xess_2d_t*, xess_quality_settings_t,
	xess_2d_t*, xess_2d_t*, xess_2d_t*);
xess_result_t xessSetJitterScale(xess_context_handle_t, float, float);
xess_result_t xessSetVelocityScale(xess_context_handle_t, float, float);
xess_result_t xessDestroyContext(xess_context_handle_t);

#ifdef __cplusplus
}
#endif
