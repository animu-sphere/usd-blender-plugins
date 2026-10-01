cmake_minimum_required(VERSION 3.22)
file(MAKE_DIRECTORY "${TEST_DIR}")
set(target_file "${TEST_DIR}/target.json")
foreach(dependency IN ITEMS "kernel32.lib" "libm.a" "usd_ms.lib" "-lpxr_usd"
        "blender.lib" "blendScene.lib" "unknown.lib")
    file(WRITE "${target_file}"
        "{\"link\":{\"commandFragments\":[{\"role\":\"libraries\",\"fragment\":\"blendFile.lib ${dependency}\"}]}}")
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DTARGET_FILE=${target_file}"
        -P "${CMAKE_CURRENT_LIST_DIR}/LinkBoundary.cmake"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(dependency STREQUAL "kernel32.lib" OR dependency STREQUAL "libm.a")
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "Allowed dependency rejected: ${dependency}: ${output}${error}")
        endif()
    elseif(result EQUAL 0 OR NOT error MATCHES "Forbidden generated reader link dependency")
        message(FATAL_ERROR "Forbidden dependency not diagnosed: ${dependency}: ${output}${error}")
    endif()
endforeach()
file(WRITE "${target_file}" "{\"link\":{\"commandFragments\":[]}}")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DTARGET_FILE=${target_file}"
    -P "${CMAKE_CURRENT_LIST_DIR}/LinkBoundary.cmake"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT error MATCHES "does not contain blendFile")
    message(FATAL_ERROR "Missing reader dependency not diagnosed: ${output}${error}")
endif()