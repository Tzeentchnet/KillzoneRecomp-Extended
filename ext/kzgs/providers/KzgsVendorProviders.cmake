# Optional SDKs are supplied by the developer, never fetched or staged.
# SPDX-License-Identifier: GPL-3.0+
include_guard(GLOBAL)

option(KZGS_BUILD_DLSS_PROVIDER "Enable the local, explicitly built DLSS provider target" OFF)
option(KZGS_BUILD_XESS_PROVIDER "Enable the local, explicitly built XeSS provider target" OFF)
option(KZGS_BUILD_FSR3_PROVIDER "Build the source-based FidelityFX FSR 3 provider" ON)
set(KZGS_DLSS_SDK_DIR "" CACHE PATH "Local NVIDIA DLSS SDK root")
set(KZGS_XESS_SDK_DIR "" CACHE PATH "Local Intel XeSS SDK root")
set(KZGS_FIDELITYFX_SDK_DIR "${KZGS_ROOT}/deps/FidelityFX-SDK-2.4.0"
	CACHE PATH "Provisioned FidelityFX SDK v2.4.0 source root")
set(KZGS_FIDELITYFX_SHADER_DIR "${KZGS_FIDELITYFX_SDK_DIR}/generated/vk"
	CACHE PATH "Generated FidelityFX Vulkan shader permutation headers")
set(KZGS_MINIZ_DIR "${KZGS_ROOT}/deps/miniz-3.1.2"
	CACHE PATH "Provisioned miniz source used by FidelityFX shader decompression")
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

	if(NOT KZGS_BUILD_FSR3_PROVIDER)
		return()
	endif()
	_kzgs_vendor_validate_platform("FSR3")
	set(root "${KZGS_FIDELITYFX_SDK_DIR}")
	set(required_files
		Kits/FidelityFX/backend/vk/ffx_vk.cpp
		Kits/FidelityFX/backend/vk/ffx_vk.h
		Kits/FidelityFX/upscalers/fsr3/include/ffx_fsr3upscaler.h
		Kits/FidelityFX/upscalers/fsr3/internal/ffx_fsr3upscaler.cpp
		Kits/FidelityFX/upscalers/fsr3/internal/ffx_fsr3upscaler_shaderblobs.cpp
		Kits/FidelityFX/framegeneration/fsr3/include/ffx_opticalflow.h
		Kits/FidelityFX/framegeneration/fsr3/internal/ffx_opticalflow.cpp
		Kits/FidelityFX/framegeneration/fsr3/internal/ffx_opticalflow_shaderblobs.cpp
		Kits/FidelityFX/docs/license.md
		3rdpartynotice.md)
	foreach(file IN LISTS required_files)
		if(NOT EXISTS "${root}/${file}" OR IS_DIRECTORY "${root}/${file}")
			message(FATAL_ERROR
				"FSR3 provider requires the pinned FidelityFX SDK v2.4.0 source at '${root}', "
				"but '${file}' is missing. Run "
				"'powershell -ExecutionPolicy Bypass -File tools\\scripts\\provision_fidelityfx.ps1' "
				"from the repository root, or set KZGS_BUILD_FSR3_PROVIDER=OFF.")
		endif()
	endforeach()
	set(required_build_files
		"${KZGS_FIDELITYFX_SHADER_DIR}/ffx_fsr3upscaler_accumulate_pass_permutations.h"
		"${KZGS_FIDELITYFX_SHADER_DIR}/ffx_opticalflow_prepare_luma_pass_permutations.h"
		"${KZGS_MINIZ_DIR}/miniz.h"
		"${KZGS_MINIZ_DIR}/miniz.c"
		"${KZGS_MINIZ_DIR}/miniz_tinfl.c")
	foreach(file IN LISTS required_build_files)
		if(NOT EXISTS "${file}" OR IS_DIRECTORY "${file}")
			message(FATAL_ERROR
				"FSR3 provider requires generated shaders and locked miniz sources, but '${file}' is missing. Run "
				"'powershell -ExecutionPolicy Bypass -File tools\\scripts\\provision_fidelityfx.ps1' "
				"from the repository root, or set KZGS_BUILD_FSR3_PROVIDER=OFF.")
		endif()
	endforeach()

	set(ffx "${root}/Kits/FidelityFX")
	set(ffx_patched_dir "${CMAKE_CURRENT_BINARY_DIR}/kzgs_fidelityfx_patched")
	file(MAKE_DIRECTORY "${ffx_patched_dir}")
	file(WRITE "${ffx_patched_dir}/git_hash_branch.h" "#pragma once\n")
	set(ffx_direct_sources)
	foreach(ffx_direct_source
		"${ffx}/upscalers/fsr3/internal/ffx_fsr3upscaler.cpp"
		"${ffx}/framegeneration/fsr3/internal/ffx_opticalflow.cpp")
		get_filename_component(ffx_direct_name "${ffx_direct_source}" NAME)
		file(READ "${ffx_direct_source}" ffx_direct_contents)
		set(ffx_backend_include "#include \"../../../api/internal/ffx_backends.h\"")
		string(FIND "${ffx_direct_contents}" "${ffx_backend_include}" ffx_backend_include_index)
		if(ffx_backend_include_index EQUAL -1)
			message(FATAL_ERROR "Failed to isolate the direct FidelityFX API source '${ffx_direct_source}'.")
		endif()
		string(REPLACE "${ffx_backend_include}" "#include \"kzgs_ffx_direct_backend.h\""
			ffx_direct_contents "${ffx_direct_contents}")
		string(REPLACE "#include <cmath>" "#include <cmath>\n#include <cstdlib>"
			ffx_direct_contents "${ffx_direct_contents}")
		if(ffx_direct_name STREQUAL "ffx_fsr3upscaler.cpp")
			set(ffx_signature
				"static FfxErrorCode fsr3upscalerCreate(FfxFsr3UpscalerContext_Private* context, const FfxFsr3UpscalerContextDescription* contextDescription)")
			set(ffx_guarded_signature
				"static FfxErrorCode fsr3upscalerRelease(FfxFsr3UpscalerContext_Private* context);\n\nstruct KzgsFfxFsr3CreateGuard\n{\n    FfxFsr3UpscalerContext_Private* context;\n    ~KzgsFfxFsr3CreateGuard() { if (context) fsr3upscalerRelease(context); }\n};\n\n${ffx_signature}")
			set(ffx_backend_success
				"    FFX_RETURN_ON_ERROR(errorCode == FFX_OK, errorCode);\n\n    // call out for device caps.")
			set(ffx_guarded_backend_success
				"    FFX_RETURN_ON_ERROR(errorCode == FFX_OK, errorCode);\n    KzgsFfxFsr3CreateGuard createGuard{ context };\n\n    // call out for device caps.")
			set(ffx_create_success
				"    return FFX_OK;\n}\n\nstatic FfxErrorCode fsr3upscalerRelease")
			set(ffx_guarded_create_success
				"#if defined(KZGS_FFX_CONTEXT_FAILURE_TESTING)\n    if (std::getenv(\"KZGS_TEST_FFX_FAIL_FSR3_CONTEXT_CREATE\"))\n        return FFX_ERROR_BACKEND_API_ERROR;\n#endif\n    createGuard.context = nullptr;\n    return FFX_OK;\n}\n\nstatic FfxErrorCode fsr3upscalerRelease")
		elseif(ffx_direct_name STREQUAL "ffx_opticalflow.cpp")
			set(ffx_signature
				"static FfxErrorCode opticalflowCreate(FfxOpticalflowContext_Private* context, const FfxOpticalflowContextDescription* contextDescription)")
			set(ffx_guarded_signature
				"static FfxErrorCode opticalflowRelease(FfxOpticalflowContext_Private* context);\n\nstruct KzgsFfxOpticalFlowCreateGuard\n{\n    FfxOpticalflowContext_Private* context;\n    ~KzgsFfxOpticalFlowCreateGuard() { if (context) opticalflowRelease(context); }\n};\n\n${ffx_signature}")
			set(ffx_backend_success
				"    FFX_RETURN_ON_ERROR(errorCode == FFX_OK, errorCode);\n\n    errorCode = context->contextDescription.backendInterface.fpGetDeviceCapabilities")
			set(ffx_guarded_backend_success
				"    FFX_RETURN_ON_ERROR(errorCode == FFX_OK, errorCode);\n    KzgsFfxOpticalFlowCreateGuard createGuard{ context };\n\n    errorCode = context->contextDescription.backendInterface.fpGetDeviceCapabilities")
			set(ffx_create_success
				"    return FFX_OK;\n}\n\nstatic FfxErrorCode opticalflowRelease")
			set(ffx_guarded_create_success
				"#if defined(KZGS_FFX_CONTEXT_FAILURE_TESTING)\n    if (std::getenv(\"KZGS_TEST_FFX_FAIL_OPTICAL_FLOW_CONTEXT_CREATE\"))\n        return FFX_ERROR_BACKEND_API_ERROR;\n#endif\n    createGuard.context = nullptr;\n    return FFX_OK;\n}\n\nstatic FfxErrorCode opticalflowRelease")
		else()
			message(FATAL_ERROR "Unexpected direct FidelityFX source '${ffx_direct_name}'.")
		endif()
		foreach(ffx_patch_pair IN ITEMS signature backend_success create_success)
			set(ffx_patch_from_name "ffx_${ffx_patch_pair}")
			set(ffx_patch_to_name "ffx_guarded_${ffx_patch_pair}")
			set(ffx_patch_from "${${ffx_patch_from_name}}")
			set(ffx_patch_to "${${ffx_patch_to_name}}")
			string(FIND "${ffx_direct_contents}" "${ffx_patch_from}" ffx_patch_index)
			if(ffx_patch_index EQUAL -1)
				message(FATAL_ERROR
					"Failed to apply the '${ffx_patch_pair}' failure-cleanup patch to '${ffx_direct_source}'.")
			endif()
			string(REPLACE "${ffx_patch_from}" "${ffx_patch_to}"
				ffx_direct_contents "${ffx_direct_contents}")
		endforeach()
		set(ffx_patched_source "${ffx_patched_dir}/${ffx_direct_name}")
		file(WRITE "${ffx_patched_source}" "${ffx_direct_contents}")
		list(APPEND ffx_direct_sources "${ffx_patched_source}")
	endforeach()

	set(miniz_export_dir "${CMAKE_CURRENT_BINARY_DIR}/kzgs_miniz")
	file(MAKE_DIRECTORY "${miniz_export_dir}")
	file(WRITE "${miniz_export_dir}/miniz_export.h" "#pragma once\n#define MINIZ_EXPORT\n")

	set(ffx_sources
		"${ffx}/api/internal/ffx_assert.cpp"
		"${ffx}/api/internal/ffx_message.cpp"
		"${ffx}/api/internal/ffx_object_management.cpp"
		"${ffx}/backend/vk/ffx_vk.cpp"
		${ffx_direct_sources}
		"${ffx}/upscalers/fsr3/internal/ffx_fsr3upscaler_shaderblobs.cpp"
		"${ffx}/framegeneration/fsr3/internal/ffx_opticalflow_shaderblobs.cpp"
		"${KZGS_MINIZ_DIR}/miniz.c"
		"${KZGS_MINIZ_DIR}/miniz_tinfl.c"
		"${KZGS_PROVIDER_SOURCE_DIR}/kzgs_ffx_vk_loader.cpp")
	add_library(kzgs_fidelityfx STATIC ${ffx_sources})
	set_target_properties(kzgs_fidelityfx PROPERTIES
		POSITION_INDEPENDENT_CODE ON
		MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
	target_compile_features(kzgs_fidelityfx PUBLIC cxx_std_20)
	target_compile_definitions(kzgs_fidelityfx PRIVATE
		FFX_BACKEND_VK
		FFX_DEBUG
		FFX_FSR3UPSCALER
		FFX_OPTICALFLOW
		MINIZ_NO_ARCHIVE_APIS
		MINIZ_NO_DEFLATE_APIS
		MINIZ_NO_STDIO
		WIN32_LEAN_AND_MEAN
		NOMINMAX)
	if(KZGS_BUILD_TEST)
		target_compile_definitions(kzgs_fidelityfx PRIVATE KZGS_FFX_CONTEXT_FAILURE_TESTING)
	endif()
	target_compile_options(kzgs_fidelityfx PRIVATE /W0 /utf-8 /permissive- /Zc:__cplusplus)
	set_source_files_properties("${ffx}/backend/vk/ffx_vk.cpp" PROPERTIES
		COMPILE_OPTIONS "/FI${KZGS_PROVIDER_SOURCE_DIR}/kzgs_ffx_vk_redirect.h")
	target_include_directories(kzgs_fidelityfx PUBLIC
		"${KZGS_PROVIDER_SOURCE_DIR}"
		"${KZGS_PROVIDER_VULKAN_INCLUDE}"
		"${ffx}/api/include"
		"${ffx}/api/internal"
		"${ffx}/backend/vk"
		"${ffx}/upscalers/fsr3/include"
		"${ffx}/upscalers/fsr3/internal"
		"${ffx}/framegeneration/fsr3/include"
		"${ffx}/framegeneration/fsr3/internal"
		"${KZGS_FIDELITYFX_SHADER_DIR}"
		"${KZGS_MINIZ_DIR}"
		"${miniz_export_dir}")

	add_library(kzgs_upscaler_fsr3 MODULE
		"${KZGS_PROVIDER_SOURCE_DIR}/kzgs_vendor_provider.cpp"
		"${KZGS_PROVIDER_SOURCE_DIR}/kzgs_vendor_fsr3.cpp")
	set_target_properties(kzgs_upscaler_fsr3 PROPERTIES
		PREFIX ""
		MSVC_RUNTIME_LIBRARY MultiThreadedDLL
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/upscalers/$<CONFIG>"
		LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/upscalers/$<CONFIG>"
		ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/upscalers/$<CONFIG>")
	target_compile_features(kzgs_upscaler_fsr3 PRIVATE cxx_std_20)
	target_compile_definitions(kzgs_upscaler_fsr3 PRIVATE
		KZGS_PROVIDER_BUILD VK_NO_PROTOTYPES WIN32_LEAN_AND_MEAN NOMINMAX)
	target_include_directories(kzgs_upscaler_fsr3 PRIVATE
		"${KZGS_PROVIDER_PUBLIC_INCLUDE}" "${KZGS_PROVIDER_SOURCE_DIR}" "${KZGS_PROVIDER_VULKAN_INCLUDE}")
	target_link_libraries(kzgs_upscaler_fsr3 PRIVATE kzgs_fidelityfx)
	set(KZGS_MINIZ_DIR "${KZGS_MINIZ_DIR}" PARENT_SCOPE)
	message(STATUS "FSR3 provider: FidelityFX SDK v2.4.0 source at '${root}'")
endfunction()
