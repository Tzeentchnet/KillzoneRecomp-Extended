# Build-configuration tests: all SDK contents created here are disposable test doubles.
# SPDX-License-Identifier: GPL-3.0+
if(NOT KZGS_SOURCE OR NOT KZGS_TEST_WORK OR NOT KZGS_FAKE_LIBRARY OR NOT KZGS_PARTIAL_LIBRARY OR
	NOT KZGS_NEGOTIATION_LIBRARY OR NOT KZGS_GENERATOR)
	message(FATAL_ERROR "Vendor configuration test paths are missing.")
endif()
if(EXISTS "${KZGS_TEST_WORK}")
	if(NOT EXISTS "${KZGS_TEST_WORK}/.kzgs-owned-test-directory")
		message(FATAL_ERROR "Refusing to overwrite an unrelated directory: ${KZGS_TEST_WORK}")
	endif()
	file(REMOVE_RECURSE "${KZGS_TEST_WORK}")
endif()
file(MAKE_DIRECTORY "${KZGS_TEST_WORK}")
file(WRITE "${KZGS_TEST_WORK}/.kzgs-owned-test-directory" "Generated test fixtures only.\n")

function(run_case name expect_success expected_text)
	execute_process(
		COMMAND "${CMAKE_COMMAND}" -S "${KZGS_SOURCE}/ext/kzgs/test/vendor_config" -B "${KZGS_TEST_WORK}/${name}"
			-G "${KZGS_GENERATOR}" "-DKZGS_SOURCE=${KZGS_SOURCE}"
			"-DCMAKE_MAKE_PROGRAM=${KZGS_MAKE_PROGRAM}" "-DCMAKE_CXX_COMPILER=${KZGS_CXX_COMPILER}" ${ARGN}
		RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
	set(combined "${output}\n${error}")
	if(expect_success AND NOT result EQUAL 0)
		message(FATAL_ERROR "${name}: expected success, got ${result}\n${combined}")
	elseif(NOT expect_success AND result EQUAL 0)
		message(FATAL_ERROR "${name}: expected failure\n${combined}")
	endif()
	if(NOT expected_text STREQUAL "" AND NOT combined MATCHES "${expected_text}")
		message(FATAL_ERROR "${name}: missing diagnostic '${expected_text}'\n${combined}")
	endif()
	message(STATUS "${name}: PASS")
endfunction()

set(absent "${KZGS_TEST_WORK}/absent-sdk")
run_case(default_off TRUE "" "-DKZGS_DLSS_SDK_DIR=${absent}" "-DKZGS_XESS_SDK_DIR=${absent}")
run_case(off_on_unsupported_platform TRUE "" "-DKZGS_TEST_BAD_ARCH=ON"
	"-DKZGS_TEST_BAD_TOOLCHAIN=ON" "-DKZGS_DLSS_SDK_DIR=${absent}")
foreach(vendor IN ITEMS DLSS XESS)
	run_case("${vendor}_invalid_root" FALSE "KZGS_${vendor}_SDK_DIR"
		"-DKZGS_BUILD_${vendor}_PROVIDER=ON" "-DKZGS_${vendor}_SDK_DIR=${absent}")
	run_case("${vendor}_unsupported_arch" FALSE "requires Windows x64"
		"-DKZGS_BUILD_${vendor}_PROVIDER=ON" "-DKZGS_TEST_BAD_ARCH=ON")
	run_case("${vendor}_unsupported_pointer_size" FALSE "requires Windows x64"
		"-DKZGS_BUILD_${vendor}_PROVIDER=ON" "-DKZGS_TEST_BAD_POINTER_SIZE=ON")
	run_case("${vendor}_unsupported_toolchain" FALSE "requires Windows x64"
		"-DKZGS_BUILD_${vendor}_PROVIDER=ON" "-DKZGS_TEST_BAD_TOOLCHAIN=ON")
endforeach()

set(dlss "${KZGS_TEST_WORK}/fake-dlss")
set(xess "${KZGS_TEST_WORK}/fake-xess")
file(MAKE_DIRECTORY "${dlss}/include" "${dlss}/lib/Windows_x86_64/x64" "${xess}/inc/xess" "${xess}/lib")
foreach(header IN ITEMS nvsdk_ngx_vk.h nvsdk_ngx.h nvsdk_ngx_defs_vk.h nvsdk_ngx_params.h
		nvsdk_ngx_loader.h nvsdk_ngx_helpers.h nvsdk_ngx_helpers_vk.h)
	file(WRITE "${dlss}/include/${header}" "// Fake configure-test fixture, not an SDK.\n")
endforeach()
file(WRITE "${dlss}/include/nvsdk_ngx_defs.h" "#define NVSDK_NGX_VERSION_API_MACRO 0x0000015\n")
file(WRITE "${xess}/inc/xess/xess.h" "// Fake configure-test fixture, not an SDK.\n")
file(WRITE "${xess}/inc/xess/xess_vk.h" "// Fake configure-test fixture, not an SDK.\n")
run_case(DLSS_partial FALSE "nvsdk_ngx_d.lib"
	"-DKZGS_BUILD_DLSS_PROVIDER=ON" "-DKZGS_DLSS_SDK_DIR=${dlss}")
run_case(XESS_partial FALSE "libxess.lib"
	"-DKZGS_BUILD_XESS_PROVIDER=ON" "-DKZGS_XESS_SDK_DIR=${xess}")

# A self-authored link-probe double lets us test the target graph without claiming a real SDK build.
file(COPY_FILE "${KZGS_FAKE_LIBRARY}" "${dlss}/lib/Windows_x86_64/x64/nvsdk_ngx_d.lib")
file(COPY_FILE "${KZGS_FAKE_LIBRARY}" "${xess}/lib/libxess.lib")
run_case(DLSS_missing_version FALSE "KZGS_DLSS_SDK_VERSION"
	"-DKZGS_BUILD_DLSS_PROVIDER=ON" "-DKZGS_DLSS_SDK_DIR=${dlss}")
run_case(XESS_missing_version FALSE "KZGS_XESS_SDK_VERSION"
	"-DKZGS_BUILD_XESS_PROVIDER=ON" "-DKZGS_XESS_SDK_DIR=${xess}")
file(WRITE "${dlss}/include/nvsdk_ngx_defs.h" "#define NVSDK_NGX_VERSION_API_MACRO 0x0000014\n")
run_case(DLSS_old_api FALSE "too old"
	"-DKZGS_BUILD_DLSS_PROVIDER=ON" "-DKZGS_DLSS_SDK_DIR=${dlss}" "-DKZGS_DLSS_SDK_VERSION=test-double")
file(WRITE "${dlss}/include/nvsdk_ngx_defs.h" "#define NVSDK_NGX_VERSION_API_MACRO 0x0000015\n")
file(WRITE "${dlss}/include/nvsdk_ngx_vk.h" "#include <nvsdk_ngx_helpers.h>\n")
file(COPY_FILE "${KZGS_SOURCE}/ext/kzgs/test/vendor_doubles/nvsdk_ngx_helpers.h" "${dlss}/include/nvsdk_ngx_helpers.h")
file(COPY_FILE "${KZGS_SOURCE}/ext/kzgs/test/vendor_doubles/nvsdk_ngx_helpers_vk.h" "${dlss}/include/nvsdk_ngx_helpers_vk.h")
file(COPY_FILE "${KZGS_SOURCE}/ext/kzgs/test/vendor_doubles/xess/xess.h" "${xess}/inc/xess/xess.h")
file(COPY_FILE "${KZGS_SOURCE}/ext/kzgs/test/vendor_doubles/xess/xess_vk.h" "${xess}/inc/xess/xess_vk.h")
file(COPY_FILE "${KZGS_PARTIAL_LIBRARY}" "${dlss}/lib/Windows_x86_64/x64/nvsdk_ngx_d.lib")
run_case(DLSS_missing_symbols FALSE "compile/link probe"
	"-DKZGS_LINK_PROBE_CASE=ON" "-DKZGS_BUILD_DLSS_PROVIDER=ON" "-DKZGS_DLSS_SDK_DIR=${dlss}"
	"-DKZGS_DLSS_SDK_VERSION=test-double")
file(COPY_FILE "${KZGS_PARTIAL_LIBRARY}" "${xess}/lib/libxess.lib")
run_case(XESS_missing_symbols FALSE "compile/link probe"
	"-DKZGS_LINK_PROBE_CASE=ON" "-DKZGS_BUILD_XESS_PROVIDER=ON" "-DKZGS_XESS_SDK_DIR=${xess}"
	"-DKZGS_XESS_SDK_VERSION=test-double")
file(COPY_FILE "${KZGS_NEGOTIATION_LIBRARY}" "${xess}/lib/libxess.lib")
run_case(XESS_missing_lifecycle_symbols FALSE "compile/link probe"
	"-DKZGS_LINK_PROBE_CASE=ON" "-DKZGS_BUILD_XESS_PROVIDER=ON" "-DKZGS_XESS_SDK_DIR=${xess}"
	"-DKZGS_XESS_SDK_VERSION=test-double")
file(WRITE "${dlss}/lib/Windows_x86_64/x64/nvsdk_ngx_d.lib" "This is intentionally not an x64 library.\n")
run_case(DLSS_invalid_library FALSE "compile/link probe"
	"-DKZGS_LINK_PROBE_CASE=ON" "-DKZGS_BUILD_DLSS_PROVIDER=ON" "-DKZGS_DLSS_SDK_DIR=${dlss}"
	"-DKZGS_DLSS_SDK_VERSION=test-double")
file(COPY_FILE "${KZGS_FAKE_LIBRARY}" "${dlss}/lib/Windows_x86_64/x64/nvsdk_ngx_d.lib")
file(WRITE "${xess}/lib/libxess.lib" "This is intentionally not an x64 library.\n")
run_case(XESS_invalid_library FALSE "compile/link probe"
	"-DKZGS_LINK_PROBE_CASE=ON" "-DKZGS_BUILD_XESS_PROVIDER=ON" "-DKZGS_XESS_SDK_DIR=${xess}"
	"-DKZGS_XESS_SDK_VERSION=test-double")
file(COPY_FILE "${KZGS_FAKE_LIBRARY}" "${xess}/lib/libxess.lib")
foreach(vendor IN ITEMS DLSS XESS)
	string(TOLOWER "${vendor}" vendor_lower)
	run_case("fake_${vendor_lower}_only" TRUE "declared release='test-double'"
		"-DKZGS_FAKE_COMPLETE_CASE=ON" "-DKZGS_BUILD_${vendor}_PROVIDER=ON"
		"-DKZGS_${vendor}_SDK_DIR=${${vendor_lower}}" "-DKZGS_${vendor}_SDK_VERSION=test-double")
	execute_process(COMMAND "${CMAKE_COMMAND}" --build "${KZGS_TEST_WORK}/fake_${vendor_lower}_only"
		--target "kzgs_upscaler_${vendor_lower}"
		RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
	if(NOT build_result EQUAL 0)
		message(FATAL_ERROR "Independent ${vendor} test-double target failed:\n${build_output}\n${build_error}")
	endif()
	message(STATUS "fake_${vendor_lower}_independent_target: PASS (test double only)")
endforeach()
run_case(fake_complete TRUE "declared release='test-double'"
	"-DKZGS_FAKE_COMPLETE_CASE=ON" "-DKZGS_BUILD_DLSS_PROVIDER=ON" "-DKZGS_BUILD_XESS_PROVIDER=ON"
	"-DKZGS_DLSS_SDK_DIR=${dlss}" "-DKZGS_XESS_SDK_DIR=${xess}"
	"-DKZGS_DLSS_SDK_VERSION=test-double" "-DKZGS_XESS_SDK_VERSION=test-double")
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${KZGS_TEST_WORK}/fake_complete" --target all
	RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
	message(FATAL_ERROR "Default build must exclude both fake provider targets:\n${build_output}\n${build_error}")
endif()
file(GLOB_RECURSE leaked LIST_DIRECTORIES FALSE
	"${KZGS_TEST_WORK}/fake_complete/local-sdk-providers/*.dll"
	"${KZGS_TEST_WORK}/fake_complete/local-sdk-providers/*.lib"
	"${KZGS_TEST_WORK}/fake_complete/local-sdk-providers/*.pdb")
if(leaked)
	message(FATAL_ERROR "Default build produced provider artifacts: ${leaked}")
endif()
message(STATUS "fake_complete_default_build: PASS (test doubles only)")
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${KZGS_TEST_WORK}/fake_complete"
	--target kzgs_upscaler_dlss kzgs_upscaler_xess
	RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
	message(FATAL_ERROR "Explicit test-double DLL targets must compile and link:\n${build_output}\n${build_error}")
endif()
file(GLOB_RECURSE provider_dlls LIST_DIRECTORIES FALSE
	"${KZGS_TEST_WORK}/fake_complete/local-sdk-providers/kzgs_upscaler_dlss.dll"
	"${KZGS_TEST_WORK}/fake_complete/local-sdk-providers/kzgs_upscaler_xess.dll")
list(LENGTH provider_dlls provider_count)
if(NOT provider_count EQUAL 2)
	message(FATAL_ERROR "Expected two isolated test-double provider DLLs; got: ${provider_dlls}")
endif()
file(GLOB root_dlls "${KZGS_TEST_WORK}/fake_complete/kzgs_upscaler_*.dll")
if(root_dlls)
	message(FATAL_ERROR "Explicit providers leaked into the ordinary build root: ${root_dlls}")
endif()
message(STATUS "fake_explicit_dll_targets: PASS (test doubles only; not a real SDK build)")
file(REMOVE_RECURSE "${KZGS_TEST_WORK}")
