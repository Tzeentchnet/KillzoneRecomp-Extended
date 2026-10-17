set(KZVU_PORTABLE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(KZVU_ROOT "${KZVU_PORTABLE_ROOT}/..")
set(KZVU_PS2X "${KZVU_ROOT}/../PS2Recomp/ps2xRuntime")

add_library(kzvu_interpreter_core STATIC
    "${KZVU_PS2X}/src/lib/vu/ps2_vu1_core.cpp"
    "${KZVU_PS2X}/src/lib/vu/ps2_vu1_upper.cpp"
    "${KZVU_PS2X}/src/lib/vu/ps2_vu1_lower.cpp"
)
target_compile_features(kzvu_interpreter_core PUBLIC cxx_std_20)
target_include_directories(kzvu_interpreter_core
    PUBLIC "${KZVU_PS2X}/include"
    PRIVATE "${KZVU_PS2X}/src/lib/vu"
)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64"
   OR CMAKE_OSX_ARCHITECTURES MATCHES "(^|;)arm64($|;)")
    include(FetchContent)
    FetchContent_Declare(
        sse2neon
        GIT_REPOSITORY https://github.com/DLTcollab/sse2neon.git
        GIT_TAG 92f6de174717aef09033ad21568d5bb9e5470404
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(sse2neon)
    target_compile_definitions(kzvu_interpreter_core PUBLIC USE_SSE2NEON)
    target_include_directories(kzvu_interpreter_core PUBLIC "${sse2neon_SOURCE_DIR}")
endif()

if(BUILD_TESTING)
    add_executable(kzvu_interpreter_conformance
        "${KZVU_PORTABLE_ROOT}/test/interpreter_conformance.cpp"
        "${KZVU_ROOT}/test/ps2recomp_stubs.cpp"
    )
    target_compile_features(kzvu_interpreter_conformance PRIVATE cxx_std_20)
    target_include_directories(kzvu_interpreter_conformance PRIVATE "${KZVU_ROOT}/test")
    target_link_libraries(kzvu_interpreter_conformance PRIVATE kzvu_interpreter_core)
    if(MSVC)
        target_compile_options(kzvu_interpreter_conformance PRIVATE /utf-8 /W3 /wd4244 /wd4267 /wd4305)
    else()
        target_compile_options(kzvu_interpreter_conformance PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME kzvu_interpreter_conformance COMMAND kzvu_interpreter_conformance)
    set_tests_properties(kzvu_interpreter_conformance PROPERTIES
        LABELS "iso-free;component;vu;portable"
        ENVIRONMENT "KZ_ISO_FREE=1")
endif()
