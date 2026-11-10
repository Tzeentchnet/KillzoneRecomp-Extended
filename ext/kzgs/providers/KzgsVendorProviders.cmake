# Optional SDKs are supplied by the developer, never fetched or staged.
# SPDX-License-Identifier: GPL-3.0+
include_guard(GLOBAL)

option(KZGS_BUILD_DLSS_PROVIDER "Enable the local, explicitly built DLSS provider target" OFF)
option(KZGS_BUILD_XESS_PROVIDER "Enable the local, explicitly built XeSS provider target" OFF)
set(KZGS_DLSS_SDK_DIR "" CACHE PATH "Local NVIDIA DLSS SDK root")
set(KZGS_XESS_SDK_DIR "" CACHE PATH "Local Intel XeSS SDK root")
set(KZGS_DLSS_SDK_VERSION "" CACHE STRING "Declared local NVIDIA DLSS SDK release (provenance)")
set(KZGS_XESS_SDK_VERSION "" CACHE STRING "Declared local Intel XeSS SDK release (provenance)")

function(_kzgs_vendor_validate_platform vendor)
	if(NOT WIN32 OR NOT MSVC OR
		NOT CMAKE_CXX_COMPILER_ID MATCHES "^(MSVC|Clang)$" OR
		NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR
		NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
		message(FATAL_ERROR
			"${vendor} provider requires Windows x64 with MSVC or clang-cl targeting the MSVC ABI; "
			"got system '${CMAKE_SYSTEM_NAME}', compiler '${CMAKE_CXX_COMPILER_ID}', "
			"architecture '${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}', pointer size '${CMAKE_SIZEOF_VOID_P}'.")
	endif()
endfunction()

function(_kzgs_vendor_require_file vendor root relative)
	if(NOT EXISTS "${root}/${relative}" OR IS_DIRECTORY "${root}/${relative}")
		message(FATAL_ERROR
			"${vendor} SDK is incomplete: missing '${root}/${relative}'. "
			"Set KZGS_${vendor}_SDK_DIR to the root of the complete, locally obtained SDK; "
			"no SDK files are downloaded by this project.")
	endif()
endfunction()

function(_kzgs_vendor_link_probe vendor include_dir library probe)
	include(CheckCXXSourceCompiles)
	set(CMAKE_REQUIRED_INCLUDES "${include_dir};${KZGS_PROVIDER_VULKAN_INCLUDE}")
	set(CMAKE_REQUIRED_LIBRARIES "${library}")
	set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
	set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
	# Rerun when a user changes an SDK in an existing build directory.
	unset(_KZGS_${vendor}_SDK_LINKS CACHE)
	check_cxx_source_compiles("${probe}" _KZGS_${vendor}_SDK_LINKS)
	if(NOT _KZGS_${vendor}_SDK_LINKS)
		message(FATAL_ERROR
			"${vendor} SDK headers/library failed the x64 MSVC-ABI compile/link probe. "
			"Check the SDK release, library architecture, and CMake configure log; "
			"KZGS_${vendor}_SDK_DIR must point to a complete supported SDK.")
	endif()
endfunction()

function(_kzgs_vendor_provenance vendor root version library)
	file(SHA256 "${library}" library_hash)
	message(STATUS "${vendor} local SDK: root='${root}', declared release='${version}'")
	foreach(header IN LISTS ARGN)
		file(SHA256 "${header}" header_hash)
		message(STATUS "${vendor} provenance: header '${header}' SHA256=${header_hash}")
	endforeach()
	message(STATUS "${vendor} provenance: library '${library}' SHA256=${library_hash}")
endfunction()

function(kzgs_add_vendor_providers)
	foreach(vendor IN ITEMS DLSS XESS)
		if(NOT KZGS_BUILD_${vendor}_PROVIDER)
			continue()
		endif()
		_kzgs_vendor_validate_platform("${vendor}")
		set(root "${KZGS_${vendor}_SDK_DIR}")
		if(root STREQUAL "" OR NOT IS_DIRECTORY "${root}")
			message(FATAL_ERROR
				"${vendor} provider is ON but KZGS_${vendor}_SDK_DIR='${root}' is not an SDK directory. "
				"Supply the complete SDK root explicitly or turn KZGS_BUILD_${vendor}_PROVIDER OFF.")
		endif()
		if(NOT EXISTS "${KZGS_PROVIDER_VULKAN_INCLUDE}/vulkan/vulkan.h")
			message(FATAL_ERROR "${vendor} provider needs the project's Vulkan headers at "
				"'${KZGS_PROVIDER_VULKAN_INCLUDE}/vulkan/vulkan.h'.")
		endif()

		if(vendor STREQUAL "DLSS")
			set(include_dir "${root}/include")
			set(library "${root}/lib/Windows_x86_64/x64/nvsdk_ngx_d.lib")
			foreach(file IN ITEMS include/nvsdk_ngx_vk.h include/nvsdk_ngx.h include/nvsdk_ngx_defs.h
					include/nvsdk_ngx_defs_vk.h include/nvsdk_ngx_params.h include/nvsdk_ngx_loader.h
					include/nvsdk_ngx_helpers.h include/nvsdk_ngx_helpers_vk.h
					lib/Windows_x86_64/x64/nvsdk_ngx_d.lib)
				_kzgs_vendor_require_file("${vendor}" "${root}" "${file}")
			endforeach()
			file(STRINGS "${include_dir}/nvsdk_ngx_defs.h" api_line
				REGEX "^#[ \t]*define[ \t]+NVSDK_NGX_VERSION_API_MACRO[ \t]+0x[0-9A-Fa-f]+")
			if(NOT api_line)
				message(FATAL_ERROR "DLSS SDK lacks NVSDK_NGX_VERSION_API_MACRO in include/nvsdk_ngx_defs.h.")
			endif()
			string(REGEX REPLACE ".*NVSDK_NGX_VERSION_API_MACRO[ \t]+(0x[0-9A-Fa-f]+).*" "\\1" api_version "${api_line}")
			math(EXPR api_number "${api_version}")
			if(api_number LESS 21)
				message(FATAL_ERROR "DLSS NGX API ${api_version} is too old; API 0x15 or newer is required.")
			endif()
			message(STATUS "DLSS NGX header API: ${api_version} (reference SDK 310.9.16)")
			set(probe [=[
				#include <vulkan/vulkan.h>
				#include <nvsdk_ngx_vk.h>
				#include <nvsdk_ngx_helpers.h>
				#include <nvsdk_ngx_helpers_vk.h>
				int main() {
					auto volatile a = &NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements;
					auto volatile b = &NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements;
					auto volatile c = &NVSDK_NGX_VULKAN_GetFeatureRequirements;
					auto volatile d = &NVSDK_NGX_VULKAN_Init_with_ProjectID;
					auto volatile e = &NVSDK_NGX_VULKAN_GetCapabilityParameters;
					auto volatile f = &NVSDK_NGX_Parameter_GetI;
					auto volatile g = &NVSDK_NGX_VULKAN_ReleaseFeature;
					auto volatile h = &NVSDK_NGX_VULKAN_DestroyParameters;
					auto volatile i = &NVSDK_NGX_VULKAN_Shutdown1;
					auto volatile j = &NGX_DLSS_GET_OPTIMAL_SETTINGS;
					auto volatile k = &NVSDK_NGX_Create_ImageView_Resource_VK;
					auto volatile l = &NGX_VULKAN_CREATE_DLSS_EXT1;
					auto volatile m = &NGX_VULKAN_EVALUATE_DLSS_EXT;
					NVSDK_NGX_VK_DLSS_Eval_Params params{};
					params.InDepthSubrectBase.X = params.InMVSubrectBase.Y = 0;
					params.InExposureScale = 1.0f;
					return !(a && b && c && d && e && f && g && h && i && j && k && l && m);
				}
			]=])
			set(source "${KZGS_PROVIDER_SOURCE_DIR}/kzgs_vendor_dlss.cpp")
			set(headers "${include_dir}/nvsdk_ngx_defs.h" "${include_dir}/nvsdk_ngx_vk.h"
				"${include_dir}/nvsdk_ngx_helpers_vk.h")
			set(target kzgs_upscaler_dlss)
		else()
			set(include_dir "${root}/inc")
			set(library "${root}/lib/libxess.lib")
			foreach(file IN ITEMS inc/xess/xess.h inc/xess/xess_vk.h lib/libxess.lib)
				_kzgs_vendor_require_file("${vendor}" "${root}" "${file}")
			endforeach()
			set(probe [=[
				#include <xess/xess_vk.h>
				int main() {
					auto volatile a = &xessGetVersion;
					auto volatile b = &xessVKGetRequiredInstanceExtensions;
					auto volatile c = &xessVKGetRequiredDeviceExtensions;
					auto volatile d = &xessVKGetRequiredDeviceFeatures;
					auto volatile e = &xessVKCreateContext;
					auto volatile f = &xessGetOptimalInputResolution;
					auto volatile g = &xessVKInit;
					auto volatile h = &xessSetJitterScale;
					auto volatile i = &xessSetVelocityScale;
					auto volatile j = &xessVKExecute;
					auto volatile k = &xessDestroyContext;
					xess_vk_init_params_t init{};
					xess_vk_execute_params_t execute{};
					init.initFlags = XESS_INIT_FLAG_INVERTED_DEPTH;
					execute.inputDepthBase.x = execute.inputMotionVectorBase.y = 0;
					execute.exposureScale = 1.0f;
					return !(a && b && c && d && e && f && g && h && i && j && k);
				}
			]=])
			set(source "${KZGS_PROVIDER_SOURCE_DIR}/kzgs_vendor_xess.cpp")
			set(headers "${include_dir}/xess/xess.h" "${include_dir}/xess/xess_vk.h")
			set(target kzgs_upscaler_xess)
		endif()

		if("${KZGS_${vendor}_SDK_VERSION}" STREQUAL "")
			message(FATAL_ERROR "${vendor} provider requires KZGS_${vendor}_SDK_VERSION to record the exact "
				"locally obtained SDK release. This is a declared provenance field, not proof of package identity.")
		endif()
		_kzgs_vendor_link_probe("${vendor}" "${include_dir}" "${library}" "${probe}")
		_kzgs_vendor_provenance("${vendor}" "${root}" "${KZGS_${vendor}_SDK_VERSION}" "${library}" ${headers})
		message(STATUS "${vendor} is experimental; real SDK, GPU, and synchronization are not yet qualified.")

		# Deliberately absent from ALL, kzgs, install rules, and ordinary release staging.
		add_library(${target} MODULE EXCLUDE_FROM_ALL
			"${KZGS_PROVIDER_SOURCE_DIR}/kzgs_vendor_provider.cpp" "${source}")
		set_target_properties(${target} PROPERTIES
			PREFIX ""
			MSVC_RUNTIME_LIBRARY MultiThreadedDLL
			RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/local-sdk-providers/$<CONFIG>"
			LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/local-sdk-providers/$<CONFIG>"
			ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/local-sdk-providers/$<CONFIG>")
		target_compile_features(${target} PRIVATE cxx_std_20)
		target_compile_definitions(${target} PRIVATE KZGS_PROVIDER_BUILD VK_NO_PROTOTYPES WIN32_LEAN_AND_MEAN NOMINMAX)
		target_include_directories(${target} PRIVATE "${KZGS_PROVIDER_PUBLIC_INCLUDE}"
			"${KZGS_PROVIDER_SOURCE_DIR}" "${KZGS_PROVIDER_VULKAN_INCLUDE}" "${include_dir}")
		target_link_libraries(${target} PRIVATE "${library}")
	endforeach()
endfunction()
