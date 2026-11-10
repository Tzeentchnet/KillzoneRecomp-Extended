// Test-only declarations for a fake NGX Vulkan implementation, not an NVIDIA SDK header.
// SPDX-License-Identifier: GPL-3.0+
#pragma once
#include "nvsdk_ngx_helpers.h"

struct NVSDK_NGX_Resource_VK
{
	VkImageView view;
	VkImage image;
	VkImageSubresourceRange subresource;
	VkFormat format;
	unsigned int width, height;
	bool writable;
};
struct NVSDK_NGX_VK_Feature_Eval_Params { NVSDK_NGX_Resource_VK* pInColor; NVSDK_NGX_Resource_VK* pInOutput; float InSharpness; };
struct NVSDK_NGX_VK_DLSS_Eval_Params
{
	NVSDK_NGX_VK_Feature_Eval_Params Feature;
	NVSDK_NGX_Resource_VK* pInDepth;
	NVSDK_NGX_Resource_VK* pInMotionVectors;
	float InJitterOffsetX, InJitterOffsetY;
	NVSDK_NGX_Dimensions InRenderSubrectDimensions;
	int InReset;
	float InMVScaleX, InMVScaleY;
	NVSDK_NGX_Coordinates InColorSubrectBase, InDepthSubrectBase, InMVSubrectBase;
	float InPreExposure, InExposureScale;
};
extern "C" NVSDK_NGX_Resource_VK NVSDK_NGX_Create_ImageView_Resource_VK(
	VkImageView, VkImage, VkImageSubresourceRange, VkFormat, unsigned int, unsigned int, bool);
extern "C" NVSDK_NGX_Result NGX_VULKAN_CREATE_DLSS_EXT1(VkDevice, VkCommandBuffer,
	unsigned int, unsigned int, NVSDK_NGX_Handle**, NVSDK_NGX_Parameter*, NVSDK_NGX_DLSS_Create_Params*);
extern "C" NVSDK_NGX_Result NGX_VULKAN_EVALUATE_DLSS_EXT(VkCommandBuffer, NVSDK_NGX_Handle*,
	NVSDK_NGX_Parameter*, NVSDK_NGX_VK_DLSS_Eval_Params*);
