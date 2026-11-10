// Test-only Vulkan declarations; not an Intel SDK or a functional implementation.
// SPDX-License-Identifier: GPL-3.0+
#pragma once
#include <vulkan/vulkan.h>
#include "xess.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xess_vk_image_view_info
{
	VkImageView imageView;
	VkImage image;
	VkImageSubresourceRange subresourceRange;
	VkFormat format;
	unsigned int width, height;
} xess_vk_image_view_info;

typedef struct xess_vk_init_params_t
{
	xess_2d_t outputResolution;
	xess_quality_settings_t qualitySetting;
	uint32_t initFlags, creationNodeMask, visibleNodeMask;
} xess_vk_init_params_t;

typedef struct xess_vk_execute_params_t
{
	xess_vk_image_view_info colorTexture, velocityTexture, depthTexture, exposureScaleTexture,
		responsivePixelMaskTexture, outputTexture;
	float jitterOffsetX, jitterOffsetY, exposureScale;
	uint32_t resetHistory, inputWidth, inputHeight;
	xess_coord_t inputColorBase, inputMotionVectorBase, inputDepthBase, inputResponsiveMaskBase,
		reserved0, outputColorBase;
} xess_vk_execute_params_t;

xess_result_t xessVKGetRequiredInstanceExtensions(uint32_t*, const char* const**, uint32_t*);
xess_result_t xessVKGetRequiredDeviceExtensions(VkInstance, VkPhysicalDevice, uint32_t*, const char* const**);
xess_result_t xessVKGetRequiredDeviceFeatures(VkInstance, VkPhysicalDevice, void**);
xess_result_t xessVKCreateContext(VkInstance, VkPhysicalDevice, VkDevice, xess_context_handle_t*);
xess_result_t xessVKInit(xess_context_handle_t, const xess_vk_init_params_t*);
xess_result_t xessVKExecute(xess_context_handle_t, VkCommandBuffer, const xess_vk_execute_params_t*);

#ifdef __cplusplus
}
#endif
