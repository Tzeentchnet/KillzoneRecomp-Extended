add_library(kz_host_core STATIC
    src/core.cpp
)

if(WIN32)
    target_sources(kz_host_core PRIVATE
        src/core_win32.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../src/kz_crash.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../src/kz_profiler.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../src/kz_sampler.cpp"
    )
    target_link_libraries(kz_host_core PRIVATE dbghelp winmm)
elseif(APPLE)
    enable_language(OBJCXX)
    find_library(KZ_COCOA_FRAMEWORK Cocoa REQUIRED)
    target_sources(kz_host_core PRIVATE src/core_macos.mm)
    target_link_libraries(kz_host_core PRIVATE "${KZ_COCOA_FRAMEWORK}")
    target_compile_options(kz_host_core PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
else()
    target_sources(kz_host_core PRIVATE src/core_unsupported.cpp)
endif()

target_compile_features(kz_host_core PUBLIC cxx_std_20)
target_include_directories(kz_host_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_definitions(kz_host_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)

# Feature detection must run on the oldest supported x64 baseline, not on code
# which already assumes the features it is trying to detect.
if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(kz_host_core PRIVATE /clang:-march=x86-64)
elseif(MSVC)
    target_compile_options(kz_host_core PRIVATE /arch:SSE2)
endif()

if(BUILD_TESTING)
    add_executable(kz_host_core_test test/core_test.cpp)
    target_link_libraries(kz_host_core_test PRIVATE kz_host_core)
    target_compile_features(kz_host_core_test PRIVATE cxx_std_20)
    target_compile_definitions(kz_host_core_test PRIVATE _CRT_SECURE_NO_WARNINGS)
    add_test(NAME kz_host_core_component COMMAND kz_host_core_test)
    set_tests_properties(kz_host_core_component PROPERTIES
        LABELS "iso-free;component;host"
        ENVIRONMENT "KZ_ISO_FREE=1")

    if(WIN32 OR APPLE)
        add_executable(kz_host_log_test test/log_test.cpp)
        target_link_libraries(kz_host_log_test PRIVATE kz_host_core)
        target_compile_features(kz_host_log_test PRIVATE cxx_std_20)
        add_test(NAME kz_host_log_component COMMAND kz_host_log_test)
        set_tests_properties(kz_host_log_component PROPERTIES
            LABELS "iso-free;component;host;logging"
            ENVIRONMENT "KZ_ISO_FREE=1"
            TIMEOUT 30)
    endif()

    if(TARGET SDL3::SDL3)
        add_executable(kz_host_window_smoke
            test/window_smoke.cpp
            "${CMAKE_CURRENT_SOURCE_DIR}/../src/kz_sdl_host.cpp"
        )
        target_link_libraries(kz_host_window_smoke PRIVATE kz_host_core SDL3::SDL3)
        target_compile_features(kz_host_window_smoke PRIVATE cxx_std_20)
        target_include_directories(kz_host_window_smoke PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../src")

        add_executable(kz_host_audio_smoke test/audio_smoke.cpp)
        target_link_libraries(kz_host_audio_smoke PRIVATE kz_host_core SDL3::SDL3)
        target_compile_features(kz_host_audio_smoke PRIVATE cxx_std_20)

        if(WIN32)
            foreach(_kz_host_sdl_target IN ITEMS kz_host_window_smoke kz_host_audio_smoke)
                add_custom_command(TARGET "${_kz_host_sdl_target}" POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_FILE:SDL3::SDL3>
                        $<TARGET_FILE_DIR:${_kz_host_sdl_target}>)
            endforeach()
        endif()

        add_test(NAME kz_host_window_smoke COMMAND kz_host_window_smoke)
        set_tests_properties(kz_host_window_smoke PROPERTIES
            LABELS "iso-free;smoke;host;window"
            ENVIRONMENT "KZ_ISO_FREE=1"
            TIMEOUT 30)

        set(_kz_host_audio_environment "KZ_ISO_FREE=1")
        if(WIN32)
            list(APPEND _kz_host_audio_environment "SDL_AUDIODRIVER=dummy")
        endif()
        add_test(NAME kz_host_audio_smoke COMMAND kz_host_audio_smoke)
        set_tests_properties(kz_host_audio_smoke PROPERTIES
            LABELS "iso-free;smoke;host;audio"
            ENVIRONMENT "${_kz_host_audio_environment}"
            TIMEOUT 30)
    endif()
endif()
