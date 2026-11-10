// Test-only, independently authored declarations, not NVIDIA SDK material.
// SPDX-License-Identifier: GPL-3.0+
#pragma once
#include <vulkan/vulkan.h>
#include <stdint.h>

using NVSDK_NGX_Result = uint32_t;
constexpr NVSDK_NGX_Result NVSDK_NGX_Result_Success = 1;
constexpr NVSDK_NGX_Result NVSDK_NGX_Result_FAIL_FeatureNotSupported = 0xbad00001u;
constexpr NVSDK_NGX_Result NVSDK_NGX_Result_FAIL_OutOfDate = 0xbad0000cu;
constexpr NVSDK_NGX_Result NVSDK_NGX_Result_FAIL_OutOfGPUMemory = 0xbad0000du;
constexpr NVSDK_NGX_Result NVSDK_NGX_Result_FAIL_UnsupportedInputFormat = 0xbad00008u;
constexpr NVSDK_NGX_Result NVSDK_NGX_Result_FAIL_UnsupportedFormat = 0xbad0000eu;
constexpr NVSDK_NGX_Result NVSDK_NGX_Result_FAIL_MissingInput = 0xbad0000au;
#define NVSDK_NGX_FAILED(value) ((value) != NVSDK_NGX_Result_Success)

enum NVSDK_NGX_Version { NVSDK_NGX_Version_API = 0x15 };
enum NVSDK_NGX_Feature { NVSDK_NGX_Feature_SuperSampling = 1 };
enum NVSDK_NGX_EngineType { NVSDK_NGX_ENGINE_TYPE_CUSTOM = 0 };
enum NVSDK_NGX_Application_Identifier_Type { NVSDK_NGX_Application_Identifier_Type_Project_Id = 1 };
enum NVSDK_NGX_Feature_Support_Result { NVSDK_NGX_FeatureSupportResult_Supported = 0 };
enum NVSDK_NGX_PerfQuality_Value
{
	NVSDK_NGX_PerfQuality_Value_MaxPerf, NVSDK_NGX_PerfQuality_Value_Balanced,
	NVSDK_NGX_PerfQuality_Value_MaxQuality, NVSDK_NGX_PerfQuality_Value_UltraPerformance,
	NVSDK_NGX_PerfQuality_Value_UltraQuality, NVSDK_NGX_PerfQuality_Value_DLAA
};
enum NVSDK_NGX_DLSS_Feature_Flags
{
	NVSDK_NGX_DLSS_Feature_Flags_IsHDR = 1 << 0,
	NVSDK_NGX_DLSS_Feature_Flags_MVLowRes = 1 << 1,
	NVSDK_NGX_DLSS_Feature_Flags_MVJittered = 1 << 2,
	NVSDK_NGX_DLSS_Feature_Flags_DepthInverted = 1 << 3,
	NVSDK_NGX_DLSS_Feature_Flags_AutoExposure = 1 << 6
};
struct NVSDK_NGX_Parameter;
struct NVSDK_NGX_Handle;
struct NVSDK_NGX_ProjectIdDescription { const char* ProjectId; NVSDK_NGX_EngineType EngineType; const char* EngineVersion; };
struct NVSDK_NGX_Application_Identifier
{
	NVSDK_NGX_Application_Identifier_Type IdentifierType;
	union { NVSDK_NGX_ProjectIdDescription ProjectDesc; unsigned long long ApplicationId; } v;
};
struct NVSDK_NGX_PathListInfo { const wchar_t* const* Path; unsigned int Length; };
struct NVSDK_NGX_FeatureCommonInfo { NVSDK_NGX_PathListInfo PathListInfo; };
struct NVSDK_NGX_FeatureDiscoveryInfo
{
	NVSDK_NGX_Version SDKVersion;
	NVSDK_NGX_Feature FeatureID;
	NVSDK_NGX_Application_Identifier Identifier;
	const wchar_t* ApplicationDataPath;
	const NVSDK_NGX_FeatureCommonInfo* FeatureInfo;
};
struct NVSDK_NGX_FeatureRequirement { NVSDK_NGX_Feature_Support_Result FeatureSupported; };
struct NVSDK_NGX_Feature_Create_Params
{
	unsigned int InWidth, InHeight, InTargetWidth, InTargetHeight;
	NVSDK_NGX_PerfQuality_Value InPerfQualityValue;
};
struct NVSDK_NGX_DLSS_Create_Params { NVSDK_NGX_Feature_Create_Params Feature; int InFeatureCreateFlags; bool InEnableOutputSubrects; };
struct NVSDK_NGX_Coordinates { unsigned int X, Y; };
struct NVSDK_NGX_Dimensions { unsigned int Width, Height; };
#define NVSDK_NGX_Parameter_SuperSampling_Available "SuperSampling.Available"

extern "C" NVSDK_NGX_Result NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(
	const NVSDK_NGX_FeatureDiscoveryInfo*, uint32_t*, VkExtensionProperties**);
extern "C" NVSDK_NGX_Result NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(
	VkInstance, VkPhysicalDevice, const NVSDK_NGX_FeatureDiscoveryInfo*, uint32_t*, VkExtensionProperties**);
extern "C" NVSDK_NGX_Result NVSDK_NGX_VULKAN_GetFeatureRequirements(
	VkInstance, VkPhysicalDevice, const NVSDK_NGX_FeatureDiscoveryInfo*, NVSDK_NGX_FeatureRequirement*);
extern "C" NVSDK_NGX_Result NVSDK_NGX_VULKAN_Init_with_ProjectID(const char*, NVSDK_NGX_EngineType, const char*,
	const wchar_t*, VkInstance, VkPhysicalDevice, VkDevice, PFN_vkGetInstanceProcAddr, PFN_vkGetDeviceProcAddr,
	const NVSDK_NGX_FeatureCommonInfo*, NVSDK_NGX_Version);
extern "C" NVSDK_NGX_Result NVSDK_NGX_VULKAN_GetCapabilityParameters(NVSDK_NGX_Parameter**);
extern "C" NVSDK_NGX_Result NVSDK_NGX_Parameter_GetI(NVSDK_NGX_Parameter*, const char*, int*);
extern "C" NVSDK_NGX_Result NVSDK_NGX_VULKAN_ReleaseFeature(NVSDK_NGX_Handle*);
extern "C" NVSDK_NGX_Result NVSDK_NGX_VULKAN_DestroyParameters(NVSDK_NGX_Parameter*);
extern "C" NVSDK_NGX_Result NVSDK_NGX_VULKAN_Shutdown1(VkDevice);
extern "C" NVSDK_NGX_Result NGX_DLSS_GET_OPTIMAL_SETTINGS(NVSDK_NGX_Parameter*, unsigned int, unsigned int,
	NVSDK_NGX_PerfQuality_Value, unsigned int*, unsigned int*, unsigned int*, unsigned int*,
	unsigned int*, unsigned int*, float*);
