include_guard(GLOBAL)

set(KZ_REQUIRED_NINJA_VERSION "1.13.2")

function(_kz_preflight_fail)
    string(JOIN "" _kz_detail ${ARGV})
    message(FATAL_ERROR "BASE-03 preflight: ${_kz_detail}")
endfunction()

function(_kz_configure_ninja)
    if(DEFINED ENV{KZ_NINJA} AND NOT "$ENV{KZ_NINJA}" STREQUAL "")
        file(TO_CMAKE_PATH "$ENV{KZ_NINJA}" _kz_ninja)
        if(IS_DIRECTORY "${_kz_ninja}")
            set(_kz_ninja "${_kz_ninja}/ninja.exe")
        endif()
        if(NOT EXISTS "${_kz_ninja}")
            _kz_preflight_fail("KZ_NINJA does not identify ninja.exe: ${_kz_ninja}")
        endif()
    else()
        find_program(_kz_ninja NAMES ninja.exe ninja NO_CACHE)
    endif()

    if(NOT _kz_ninja)
        _kz_preflight_fail(
            "Ninja ${KZ_REQUIRED_NINJA_VERSION} was not found. Install that version and add it to PATH, "
            "or set KZ_NINJA to ninja.exe.")
    endif()

    execute_process(
        COMMAND "${_kz_ninja}" --version
        RESULT_VARIABLE _kz_ninja_result
        OUTPUT_VARIABLE _kz_ninja_version
        ERROR_VARIABLE _kz_ninja_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE)
    if(NOT _kz_ninja_result EQUAL 0)
        _kz_preflight_fail("Could not run ${_kz_ninja}: ${_kz_ninja_error}")
    endif()
    string(REPLACE "." "\\." _kz_ninja_version_pattern "${KZ_REQUIRED_NINJA_VERSION}")
    if(NOT _kz_ninja_version MATCHES "^${_kz_ninja_version_pattern}([.-]|$)")
        _kz_preflight_fail(
            "Ninja ${KZ_REQUIRED_NINJA_VERSION} is required, but ${_kz_ninja_version} was found at ${_kz_ninja}.")
    endif()

    set(CMAKE_MAKE_PROGRAM "${_kz_ninja}" CACHE FILEPATH "Pinned Ninja executable" FORCE)
    message(STATUS "BASE-03 preflight: Ninja ${_kz_ninja_version} (${_kz_ninja})")
endfunction()

function(_kz_require_msvc_environment)
    if(KZ_TOOLCHAIN STREQUAL "clang-cl")
        set(_kz_preset "win-clang-rel")
    else()
        set(_kz_preset "win-msvc-rel")
    endif()
    if(NOT DEFINED ENV{VCToolsInstallDir}
       OR "$ENV{VCToolsInstallDir}" STREQUAL ""
       OR NOT IS_DIRECTORY "$ENV{VCToolsInstallDir}")
        _kz_preflight_fail(
            "the MSVC Build Tools environment is not active. Install the x64 C++ workload, then use the VS Code "
            "task for ${_kz_preset} or invoke tools\\scripts\\vsenv.bat cmake --preset ${_kz_preset}.")
    endif()
    if(NOT DEFINED ENV{VSCMD_ARG_TGT_ARCH} OR NOT "$ENV{VSCMD_ARG_TGT_ARCH}" STREQUAL "x64")
        _kz_preflight_fail("the active MSVC Build Tools environment must target x64.")
    endif()
    if(NOT DEFINED ENV{WindowsSdkDir}
       OR "$ENV{WindowsSdkDir}" STREQUAL ""
       OR NOT IS_DIRECTORY "$ENV{WindowsSdkDir}")
        _kz_preflight_fail("the Windows SDK is missing from the active MSVC Build Tools environment.")
    endif()
endfunction()

function(_kz_configure_msvc)
    _kz_require_msvc_environment()
    find_program(_kz_cl NAMES cl.exe cl NO_CACHE)
    if(NOT _kz_cl)
        _kz_preflight_fail("cl.exe is missing from the active MSVC Build Tools x64 environment.")
    endif()
    set(CMAKE_C_COMPILER "${_kz_cl}" CACHE FILEPATH "MSVC C compiler" FORCE)
    set(CMAKE_CXX_COMPILER "${_kz_cl}" CACHE FILEPATH "MSVC C++ compiler" FORCE)
endfunction()

function(_kz_configure_clang_cl)
    _kz_require_msvc_environment()
    if(NOT DEFINED ENV{KZ_LLVM_BIN} OR "$ENV{KZ_LLVM_BIN}" STREQUAL "")
        _kz_preflight_fail("set KZ_LLVM_BIN to the LLVM bin directory for the win-clang-rel preset.")
    endif()

    file(TO_CMAKE_PATH "$ENV{KZ_LLVM_BIN}" _kz_llvm_bin)
    foreach(_kz_tool IN ITEMS clang-cl.exe lld-link.exe llvm-lib.exe)
        if(NOT EXISTS "${_kz_llvm_bin}/${_kz_tool}")
            _kz_preflight_fail("${_kz_tool} was not found under KZ_LLVM_BIN (${_kz_llvm_bin}).")
        endif()
    endforeach()

    set(CMAKE_C_COMPILER "${_kz_llvm_bin}/clang-cl.exe" CACHE FILEPATH "clang-cl C compiler" FORCE)
    set(CMAKE_CXX_COMPILER "${_kz_llvm_bin}/clang-cl.exe" CACHE FILEPATH "clang-cl C++ compiler" FORCE)
    set(CMAKE_LINKER "${_kz_llvm_bin}/lld-link.exe" CACHE FILEPATH "LLVM linker" FORCE)
    set(CMAKE_AR "${_kz_llvm_bin}/llvm-lib.exe" CACHE FILEPATH "LLVM librarian" FORCE)
endfunction()

function(_kz_verify_dependencies)
    find_program(_kz_git NAMES git.exe git NO_CACHE)
    if(NOT _kz_git)
        _kz_preflight_fail("Git is required to verify and populate locked dependencies.")
    endif()

    find_package(Python3 3.11 QUIET COMPONENTS Interpreter)
    if(NOT Python3_Interpreter_FOUND)
        _kz_preflight_fail("Python 3.11 or newer is required to run tools\\scripts\\verify_deps.py.")
    endif()

    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/scripts/verify_deps.py" --strict
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE _kz_verify_result
        OUTPUT_VARIABLE _kz_verify_output
        ERROR_VARIABLE _kz_verify_error)
    if(NOT _kz_verify_result EQUAL 0)
        _kz_preflight_fail(
            "locked dependency verification failed.\n${_kz_verify_output}${_kz_verify_error}\n"
            "Provision only the revisions and archives in config\\dependencies.lock.json, then rerun the preset.")
    endif()
    message(STATUS "BASE-03 preflight: locked dependencies verified")
endfunction()

function(_kz_verify_generated_sources)
    get_filename_component(_kz_generated_dir "${KZ_GENERATED_DIR}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    set(KZ_GENERATED_DIR "${_kz_generated_dir}" CACHE PATH "PS2Recomp output for SCUS_974.02" FORCE)

    set(_kz_missing_generated "")
    foreach(_kz_header IN ITEMS ps2_recompiled_functions.h ps2_recompiled_stubs.h)
        if(NOT EXISTS "${KZ_GENERATED_DIR}/${_kz_header}")
            list(APPEND _kz_missing_generated "${_kz_header}")
        endif()
    endforeach()
    file(GLOB _kz_generated_cpp LIST_DIRECTORIES FALSE "${KZ_GENERATED_DIR}/*.cpp")
    if(NOT _kz_generated_cpp)
        list(APPEND _kz_missing_generated "*.cpp")
    endif()

    if(_kz_missing_generated)
        list(JOIN _kz_missing_generated ", " _kz_missing_list)
        _kz_preflight_fail(
            "generated game code is incomplete at ${KZ_GENERATED_DIR} (missing: ${_kz_missing_list}). "
            "Run tools\\scripts\\regen.py with an explicit --elf input, or set KZ_GENERATED_DIR to an existing "
            "private generated tree.")
    endif()
    message(STATUS "BASE-03 preflight: generated source tree found at ${KZ_GENERATED_DIR}")
endfunction()

function(kz_preflight_before_project)
    if(CMAKE_VERSION VERSION_LESS 3.24)
        _kz_preflight_fail("CMake 3.24 or newer is required; found ${CMAKE_VERSION}.")
    endif()
    if(NOT CMAKE_HOST_WIN32)
        _kz_preflight_fail("the checked presets currently support Windows hosts only.")
    endif()
    if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
        _kz_preflight_fail("the checked presets require the Ninja generator; found ${CMAKE_GENERATOR}.")
    endif()

    if(NOT KZ_TOOLCHAIN)
        if(CMAKE_CXX_COMPILER MATCHES "(^|[/\\\\])clang-cl(\\.exe)?$")
            set(KZ_TOOLCHAIN "clang-cl" CACHE STRING "Checked Windows compiler family" FORCE)
        else()
            set(KZ_TOOLCHAIN "msvc" CACHE STRING "Checked Windows compiler family" FORCE)
        endif()
    endif()
    set_property(CACHE KZ_TOOLCHAIN PROPERTY STRINGS msvc clang-cl)

    if(KZ_TOOLCHAIN STREQUAL "msvc")
        _kz_configure_msvc()
    elseif(KZ_TOOLCHAIN STREQUAL "clang-cl")
        _kz_configure_clang_cl()
    else()
        _kz_preflight_fail("KZ_TOOLCHAIN must be msvc or clang-cl; found ${KZ_TOOLCHAIN}.")
    endif()

    _kz_configure_ninja()
    _kz_verify_dependencies()
    _kz_verify_generated_sources()
endfunction()

function(kz_preflight_after_project)
    if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        _kz_preflight_fail("the configured target must be Windows x64.")
    endif()
    if(KZ_TOOLCHAIN STREQUAL "msvc")
        if(NOT MSVC OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            _kz_preflight_fail("win-msvc-rel selected ${CMAKE_CXX_COMPILER_ID}; cl.exe is required.")
        endif()
    elseif(NOT MSVC OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        _kz_preflight_fail("win-clang-rel selected ${CMAKE_CXX_COMPILER_ID}; clang-cl is required.")
    endif()
    message(STATUS
        "BASE-03 preflight: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}, Windows x64")
endfunction()
