// Test-only declarations for the small XeSS API surface exercised by the fake below.
// This is not an Intel SDK header or a functional SDK.
// SPDX-License-Identifier: GPL-3.0+
#pragma once
#include <vulkan/vulkan.h>
#include <stdint.h>

typedef enum xess_result_t
{
	XESS_RESULT_SUCCESS = 0,
	XESS_RESULT_ERROR_UNSUPPORTED_DEVICE = -1,
	XESS_RESULT_ERROR_UNSUPPORTED_DRIVER = -2,
	XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY = -5
} xess_result_t;

typedef struct xess_version_t
{
	uint16_t major, minor, patch, reserved;
} xess_version_t;

extern "C" xess_result_t xessGetVersion(xess_version_t*);
extern "C" xess_result_t xessVKGetRequiredInstanceExtensions(uint32_t*, const char* const**, uint32_t*);
extern "C" xess_result_t xessVKGetRequiredDeviceExtensions(VkInstance, VkPhysicalDevice, uint32_t*, const char* const**);
extern "C" xess_result_t xessVKGetRequiredDeviceFeatures(VkInstance, VkPhysicalDevice, void**);
