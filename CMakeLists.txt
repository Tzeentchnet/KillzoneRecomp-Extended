cmake_minimum_required(VERSION 3.24)

option(KZ_MACOS_PHASE0_ONLY "Configure only the macOS Phase 0 host and portable VU gates" OFF)
option(KZ_PORTABLE_VU_ONLY "Configure only the portable VU interpreter conformance gate" OFF)
if(KZ_MACOS_PHASE0_ONLY)
    project(KillzoneRecompMacPhase0 CXX)
    if(NOT APPLE)
        message(FATAL_ERROR "KZ_MACOS_PHASE0_ONLY requires a macOS host")
    endif()
    if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64"
       AND NOT CMAKE_OSX_ARCHITECTURES MATCHES "(^|;)arm64($|;)")
        message(FATAL_ERROR "KZ_MACOS_PHASE0_ONLY requires an Apple Silicon ARM64 target")
    endif()
    enable_language(OBJCXX)
    include(CTest)
    set(KZ_SDL3_SOURCE_DIR
        "${CMAKE_SOURCE_DIR}/ext/sdl3/SDL3-3.4.16-source"
        CACHE PATH "Locked SDL 3.4.16 source tree")
    if(NOT EXISTS "${KZ_SDL3_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "The locked SDL 3.4.16 source tree is missing at ${KZ_SDL3_SOURCE_DIR}. "
            "Provision the sdl3-source record from config/dependencies.lock.json.")
    endif()
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory("${KZ_SDL3_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/sdl3-build" EXCLUDE_FROM_ALL)
    add_subdirectory(host)
    add_subdirectory(ext/kzvu/portable)
    return()
endif()

if(KZ_PORTABLE_VU_ONLY)
    project(KillzoneRecompPortableVu CXX)
    include(CTest)
    add_subdirectory(ext/kzvu/portable)
    return()
endif()

set(KZ_GENERATED_DIR "${CMAKE_SOURCE_DIR}/generated" CACHE PATH "PS2Recomp output for SCUS_974.02")
include(cmake/KzPreflight.cmake)
kz_preflight_before_project()
project(KillzoneRecomp CXX C)
kz_preflight_after_project()
include(CTest)
find_package(SDL3 3.4.16 EXACT CONFIG REQUIRED
    PATHS "${CMAKE_SOURCE_DIR}/ext/sdl3/SDL3-3.4.16/cmake"
    NO_DEFAULT_PATH)
add_subdirectory(host)

# Shipping build is RelWithDebInfo (symbols for crash reports). CMake's default for it is /O2 /Ob1 (no automatic
# inlining) with security cookies and incremental linking, which cost the hot generated code measurably (out-of-line
# memory helpers, __security_check_cookie in profiles). Keep strict float semantics (no /fp:fast: PS2 float
# emulation depends on it). KZ_FAST_FLAGS=OFF restores CMake's defaults.
option(KZ_FAST_FLAGS "Release-grade optimisation flags for RelWithDebInfo" ON)
# Development link: /OPT:NOICF instead of /OPT:ICF. Identical-COMDAT folding over the ~250 generated unity objects is
# most of the killzone.exe link (MSVC, warm cache: 3.8 s -> 1.0 s; the exe grows ~1.5 MB). Compile flags are unchanged,
# so toggling it on an existing build directory only relinks. Leave OFF for builds that get packaged.
option(KZ_DEV_LINK "Faster killzone.exe link for the edit-build-test loop (no identical-COMDAT folding)" OFF)
if(KZ_DEV_LINK)
    set(_kz_icf "/OPT:NOICF")
else()
    set(_kz_icf "/OPT:ICF")
endif()
# clang-cl (opt-in: configure with -DCMAKE_C_COMPILER=<llvm>/clang-cl.exe -DCMAKE_CXX_COMPILER=<llvm>/clang-cl.exe, see
# docs/findings.md "clang-cl build"): same option set, spelled for clang. Float semantics stay strict:
# -ffp-contract=off (no FMA fusing of a*b+c: PS2 float emulation is bit-exact), never /fp:fast. The kz* libraries
# (PCSX2 code) add their own /fp:contract /arch:AVX2 after these, as they do with MSVC.
if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(KZ_CLANG_CL ON)
else()
    set(KZ_CLANG_CL OFF)
endif()
# Optional clang-cl extras (all OFF by default):
#   -DKZ_CLANG_LTO=thin|full   link-time optimisation. Needs llvm-lib as the archiver (MSVC lib.exe does not index
#                              bitcode): also pass -DCMAKE_AR=<llvm>/bin/llvm-lib.exe -DCMAKE_LINKER=<llvm>/bin/lld-link.exe
#   -DKZ_CLANG_PGO_GEN=ON      instrumented build (writes <cwd>/kz_%p.profraw at exit; run a gameplay session,
#                              then llvm-profdata merge -output=kz.profdata *.profraw)
#   -DKZ_CLANG_PGO_USE=<file>  optimise with that .profdata
set(KZ_CLANG_LTO "" CACHE STRING "clang-cl LTO mode: empty, thin or full")
option(KZ_CLANG_PGO_GEN "clang-cl: build an instrumented (profile-generating) binary" OFF)
set(KZ_CLANG_PGO_USE "" CACHE FILEPATH "clang-cl: .profdata to optimise with")
if(KZ_CLANG_CL AND KZ_FAST_FLAGS)
    set(_kz_clang_opt "/Zi /O2 /Oi /Gy /Gw /GS- /clang:-march=x86-64-v3 /clang:-ffp-contract=off /DNDEBUG")
    set(_kz_clang_link "/DEBUG /INCREMENTAL:NO /OPT:REF ${_kz_icf}")
    if(KZ_CLANG_LTO)
        string(APPEND _kz_clang_opt " /clang:-flto=${KZ_CLANG_LTO}")
        string(APPEND _kz_clang_link " /lldltocache:${CMAKE_BINARY_DIR}/lto-cache")
    endif()
    if(KZ_CLANG_PGO_GEN)
        string(APPEND _kz_clang_opt " /clang:-fprofile-instr-generate")
        get_filename_component(_kz_llvm_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
        file(GLOB _kz_prof_rt "${_kz_llvm_bin}/../lib/clang/*/lib/windows/clang_rt.profile-x86_64.lib")
        list(GET _kz_prof_rt 0 _kz_prof_rt)
        string(APPEND _kz_clang_link " \"${_kz_prof_rt}\"")
    elseif(KZ_CLANG_PGO_USE)
        string(APPEND _kz_clang_opt " /clang:-fprofile-instr-use=${KZ_CLANG_PGO_USE} /clang:-Wno-profile-instr-unprofiled /clang:-Wno-profile-instr-out-of-date /clang:-Wno-backend-plugin")
    endif()
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${_kz_clang_opt}")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "${_kz_clang_opt}")
    set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${_kz_clang_link}")
elseif(MSVC AND KZ_FAST_FLAGS)
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/Zi /O2 /Ob3 /Oi /Gy /Gw /GS- /Qspectre- /arch:AVX2 /DNDEBUG")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "/Zi /O2 /Ob3 /Oi /Gy /Gw /GS- /Qspectre- /arch:AVX2 /DNDEBUG")
    set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "/DEBUG /INCREMENTAL:NO /OPT:REF ${_kz_icf}")
endif()

# PS2Recomp runtime + IOP only (recompiler/analyzer are built separately by tools/scripts/build_ps2recomp.bat)
set(PS2X_BUILD_RECOMP OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_ANALYZER OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_STUDIO OFF CACHE BOOL "" FORCE)
set(PS2X_ENABLE_AGRESSIVE_LOGS OFF CACHE BOOL "" FORCE)  # per-call enter/exit trace: ~500MB per 30s
if(BUILD_TESTING)
    set(KZGS_BUILD_TEST ON CACHE BOOL "" FORCE)
    set(KZVU_BUILD_TEST ON CACHE BOOL "" FORCE)
    set(KZIPU_BUILD_TEST ON CACHE BOOL "" FORCE)
    set(KZSPU2_BUILD_TEST ON CACHE BOOL "" FORCE)
    set(KZ_BYOD_ISO "$ENV{KZ_ISO}" CACHE FILEPATH
        "Authorized disc image used only by CTest tests labeled byod")
endif()
add_subdirectory(ext/PS2Recomp)
add_subdirectory(ext/kzgs)   # PCSX2 hardware GS as a library (ext/kzgs/README.md)
add_subdirectory(ext/kzvu)   # PCSX2 microVU (VU1 recompiler) as a library (ext/kzvu/README.md)
add_subdirectory(ext/kzvu/portable) # PS2Recomp VU interpreter without x86 microVU
add_subdirectory(ext/kzipu)  # PCSX2 IPU (MPEG decoder) as a library (ext/kzipu/README.md)
add_subdirectory(ext/kzspu2) # PCSX2 SPU2 (sound) as a library (ext/kzspu2/README.md)

# Generated code lives outside the repo (derived from the game ELF). Point KZ_GENERATED_DIR at it.
file(GLOB KZ_GENERATED_SRC CONFIGURE_DEPENDS "${KZ_GENERATED_DIR}/*.cpp")
list(LENGTH KZ_GENERATED_SRC KZ_GENERATED_COUNT)
message(STATUS "Killzone: ${KZ_GENERATED_COUNT} generated sources")

file(GLOB KZ_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
list(FILTER KZ_SRC EXCLUDE REGEX "/kz_(crash|profiler|sampler)\\.cpp$")

# Our code lives in its own library so its dependencies (SDL3, ImGui, ...) never change the compile flags of the
# ~250 generated-code unity batches (a flag change there means a full rebuild: about 2 minutes with MSVC, 1 with
# clang-cl, on 16 cores).
include(FetchContent)
FetchContent_GetProperties(imgui SOURCE_DIR KZ_IMGUI_DIR)
add_library(kz_port STATIC
    ${KZ_SRC}
    "${KZ_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp"
    "${KZ_IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp")
target_include_directories(kz_port PRIVATE "${CMAKE_SOURCE_DIR}/src" "${KZ_IMGUI_DIR}" "${KZ_IMGUI_DIR}/backends")
target_link_libraries(kz_port PRIVATE kz_host_core ps2_runtime SDL3::SDL3 imgui kzgs kzvu kzipu kzspu2)
target_compile_definitions(kz_port PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
if(KZ_CLANG_CL AND KZ_CLANG_PGO_GEN)
    target_compile_definitions(kz_port PRIVATE KZ_PGO_GEN=1)
endif()

add_executable(killzone
    ${KZ_GENERATED_SRC}
)
target_include_directories(killzone PRIVATE "${KZ_GENERATED_DIR}" "${CMAKE_SOURCE_DIR}/src")
# kz_port holds main() and the game override registration; link it whole so static registrars are kept.
target_link_libraries(killzone PRIVATE ps2_runtime "$<LINK_LIBRARY:WHOLE_ARCHIVE,kz_port>")
set_target_properties(killzone PROPERTIES
    UNITY_BUILD ON
    UNITY_BUILD_BATCH_SIZE 128
)
target_precompile_headers(killzone PRIVATE
    ext/PS2Recomp/ps2xRuntime/include/ps2_runtime_macros.h
    ext/PS2Recomp/ps2xRuntime/include/ps2_runtime.h
    "${KZ_GENERATED_DIR}/ps2_recompiled_functions.h"
    "${KZ_GENERATED_DIR}/ps2_recompiled_stubs.h"
    ext/PS2Recomp/ps2xRuntime/include/ps2_syscalls.h
    ext/PS2Recomp/ps2xRuntime/include/ps2_stubs.h
)
if(MSVC)
    target_compile_options(killzone PRIVATE /bigobj /FS)
endif()
if(KZGS_ADDRESS_SANITIZER)
    target_link_options(killzone PRIVATE /fsanitize=address)
endif()

# Copy FFmpeg runtime DLLs next to killzone.exe (helper from ps2xRuntime).
ps2x_stage_ffmpeg_runtime_dlls(killzone)
kzgs_stage_resources(killzone)
add_custom_command(TARGET killzone POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:killzone>)

# Offline GS trace replay (tools/gs_replay): renders a KZ_GS_TRACE recording through kzgs.
add_executable(kz_gs_replay tools/gs_replay/gs_replay.cpp)
target_link_libraries(kz_gs_replay PRIVATE kzgs SDL3::SDL3)
target_compile_features(kz_gs_replay PRIVATE cxx_std_20)
add_executable(kz_gs_replay_cpu tools/gs_replay/gs_replay_cpu.cpp)
target_link_libraries(kz_gs_replay_cpu PRIVATE ps2_runtime)
target_compile_features(kz_gs_replay_cpu PRIVATE cxx_std_20)

if(BUILD_TESTING)
    set(KZ_TEST_STATE_DIR "${CMAKE_BINARY_DIR}/test-state")
    set(KZ_TEST_CONFIG "${KZ_TEST_STATE_DIR}/missing/killzone.ini")
    set(KZ_TEST_VU0_CAPTURES "${KZ_TEST_STATE_DIR}/empty-vu0-captures")
    file(MAKE_DIRECTORY
        "${KZ_TEST_STATE_DIR}/product/work"
        "${KZ_TEST_STATE_DIR}/kzgs/component"
        "${KZ_TEST_STATE_DIR}/kzgs/d3d11"
        "${KZ_TEST_STATE_DIR}/kzgs/d3d12"
        "${KZ_TEST_STATE_DIR}/kzgs/vulkan"
        "${KZ_TEST_STATE_DIR}/kzipu"
        "${KZ_TEST_VU0_CAPTURES}")
    set(_kz_iso_free_environment
        "KZ_CONFIG=${KZ_TEST_CONFIG}"
        "KZ_ISO_FREE=1"
        "KZ_AUDIO_DEVICE=0"
        "PS2X_HEADLESS=1")

    function(kz_add_iso_free_test test_name target)
        add_test(NAME "${test_name}" COMMAND "$<TARGET_FILE:${target}>" ${ARGN})
        set_tests_properties("${test_name}" PROPERTIES
            LABELS "iso-free"
            ENVIRONMENT "${_kz_iso_free_environment}"
            TIMEOUT 180)
    endfunction()

    kz_add_iso_free_test(kz_product_selftest killzone --selftest)
    set_tests_properties(kz_product_selftest PROPERTIES
        LABELS "iso-free;product"
        WORKING_DIRECTORY "${KZ_TEST_STATE_DIR}/product")

    kz_add_iso_free_test(kz_spu2_component kzspu2_test)
    kz_add_iso_free_test(kz_vu1_component kzvu_test --no-bench)
    kz_add_iso_free_test(kz_vu0_component kzvu0_test "${KZ_TEST_VU0_CAPTURES}")
    kz_add_iso_free_test(kz_gs_component kzgs_test
        --renderer d3d11 --warp --frames 2 --stress-uploads 64
        --out "${KZ_TEST_STATE_DIR}/kzgs/component")
    kz_add_iso_free_test(kzgs_palette_guard kzgs_palette_test)
    kz_add_iso_free_test(kzgs_xbrz_scaler kzgs_xbrz_test)
    set_tests_properties(
        kz_spu2_component
        kz_vu1_component
        kz_vu0_component
        kz_gs_component
        kzgs_palette_guard
        kzgs_xbrz_scaler
        PROPERTIES LABELS "iso-free;component")
    set_tests_properties(kz_gs_component PROPERTIES RESOURCE_LOCK kzgs_gpu)

    kz_add_iso_free_test(kzgs_d3d11_golden kzgs_test
        --renderer d3d11 --warp --golden --frames 2
        --out "${KZ_TEST_STATE_DIR}/kzgs/d3d11")
    kz_add_iso_free_test(kzgs_d3d12_golden kzgs_test
        --renderer d3d12 --warp --golden --frames 2
        --out "${KZ_TEST_STATE_DIR}/kzgs/d3d12")
    kz_add_iso_free_test(kzgs_vulkan_golden kzgs_test
        --renderer vulkan --golden --frames 2
        --out "${KZ_TEST_STATE_DIR}/kzgs/vulkan")
    set_tests_properties(
        kzgs_d3d11_golden
        kzgs_d3d12_golden
        kzgs_vulkan_golden
        PROPERTIES
            LABELS "iso-free;golden;renderer"
            RESOURCE_LOCK kzgs_gpu)

    add_test(NAME kz_ipu_byod
        COMMAND "$<TARGET_FILE:kzipu_test>"
            --iso "${KZ_BYOD_ISO}"
            --out "${KZ_TEST_STATE_DIR}/kzipu"
            --videos 1
            --frames 2
            --es-mb 4)
    set_tests_properties(kz_ipu_byod PROPERTIES
        LABELS "byod;component"
        TIMEOUT 600)
    if(NOT KZ_BYOD_ISO OR NOT EXISTS "${KZ_BYOD_ISO}")
        set_tests_properties(kz_ipu_byod PROPERTIES DISABLED TRUE)
    endif()
endif()
