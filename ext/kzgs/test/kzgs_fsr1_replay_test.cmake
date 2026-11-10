# SPDX-License-Identifier: GPL-3.0+
set(root "test-state/kzgs/fsr1-replay")
file(MAKE_DIRECTORY "${root}" "${root}/normal" "${root}/burst" "${root}/off" "${root}/debug")
file(REMOVE "${root}/synthetic.gs"
    "${root}/normal/replay_0003.png" "${root}/burst/replay_0003.png" "${root}/off/replay_0003.png"
    "${root}/debug/bb_0003.png" "${root}/debug/present_0003.png")

execute_process(COMMAND "${TEST_EXE}" --write-synthetic-trace "${root}/synthetic.gs"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Synthetic trace failed: ${output}\n${error}")
endif()

function(run_replay mode provider)
    execute_process(COMMAND "${REPLAY_EXE}" "${root}/synthetic.gs" "${root}/${mode}"
        --renderer vulkan --upscale 1 --upscaler "${provider}" --upscaler-sharpening 60
        --cas 30 --bilinear 1 --present 1003x757 --every 3 ${ARGN}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "${mode} replay failed: ${output}\n${error}")
    endif()
    if(provider STREQUAL "fsr1" AND NOT output MATCHES "final presentation upscaler: requested=fsr1 effective=fsr1 reason=active")
        message(FATAL_ERROR "${mode} replay did not use FSR1: ${output}\n${error}")
    endif()
endfunction()

run_replay(normal fsr1)
run_replay(burst fsr1 --backbuffer)
run_replay(off off)
foreach(mode normal burst off)
    file(SHA256 "${root}/${mode}/replay_0003.png" ${mode}_hash)
endforeach()
if(NOT normal_hash STREQUAL burst_hash)
    message(FATAL_ERROR "Presented readback and completed-output burst differ")
endif()
if(normal_hash STREQUAL off_hash)
    message(FATAL_ERROR "FSR1 replay did not differ from disabled presentation")
endif()

if(DEFINED DEBUG_REPLAY_EXE AND NOT DEBUG_REPLAY_EXE STREQUAL "")
    execute_process(COMMAND "${DEBUG_REPLAY_EXE}" "${root}/synthetic.gs" "${root}/debug"
        --renderer vulkan --window --upscale 1 --upscaler fsr1 --upscaler-sharpening 60
        --cas 30 --bilinear 1 --present 1003x757 --every 3 --backbuffer
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result STREQUAL "0" OR NOT output MATCHES "final presentation upscaler: requested=fsr1 effective=fsr1 reason=active")
        message(FATAL_ERROR "Debug replay failed or did not use FSR1: ${output}\n${error}")
    endif()
    file(SHA256 "${root}/debug/bb_0003.png" burst_debug_hash)
    file(SHA256 "${root}/debug/present_0003.png" presented_debug_hash)
    if(NOT burst_debug_hash STREQUAL presented_debug_hash)
        message(FATAL_ERROR "Debug replay burst and presented readback differ")
    endif()
endif()
message(STATUS "FSR1 synthetic replay and completed-output captures: PASS")
