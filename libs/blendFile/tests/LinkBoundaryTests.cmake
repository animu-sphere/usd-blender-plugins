cmake_minimum_required(VERSION 3.22)
if(NOT DEFINED LIBRARY_NAME)
    set(LIBRARY_NAME blendFile)
endif()
if(NOT DEFINED BOUNDARY_LABEL)
    set(BOUNDARY_LABEL reader)
endif()
set(forbidden_sibling blendScene.lib)
if(LIBRARY_NAME STREQUAL "blendScene")
    set(forbidden_sibling blendHost.lib)
endif()
file(MAKE_DIRECTORY "${TEST_DIR}")
set(target_file "${TEST_DIR}/target.json")
foreach(dependency IN ITEMS "kernel32.lib" "libm.a" "usd_ms.lib" "-lpxr_usd"
    "blender.lib" "blendFile.lib" "${forbidden_sibling}" "unknown.lib")
    file(WRITE "${target_file}"
        "{\"link\":{\"commandFragments\":[{\"role\":\"libraries\",\"fragment\":\"${LIBRARY_NAME}.lib ${dependency}\"}]}}")
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DTARGET_FILE=${target_file}"
        "-DLIBRARY_NAME=${LIBRARY_NAME}" "-DBOUNDARY_LABEL=${BOUNDARY_LABEL}"
        "-DALLOWED_LIBRARIES=${ALLOWED_LIBRARIES}"
        -P "${CMAKE_CURRENT_LIST_DIR}/LinkBoundary.cmake"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(dependency STREQUAL "kernel32.lib" OR dependency STREQUAL "libm.a"
            OR dependency STREQUAL "${LIBRARY_NAME}.lib"
            OR (dependency STREQUAL "blendFile.lib" AND "blendFile" IN_LIST ALLOWED_LIBRARIES))
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "Allowed dependency rejected: ${dependency}: ${output}${error}")
        endif()
    elseif(result EQUAL 0 OR NOT error MATCHES "Forbidden generated ${BOUNDARY_LABEL} link dependency")
        message(FATAL_ERROR "Forbidden dependency not diagnosed: ${dependency}: ${output}${error}")
    endif()
endforeach()
file(WRITE "${target_file}" "{\"link\":{\"commandFragments\":[]}}")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DTARGET_FILE=${target_file}"
    "-DLIBRARY_NAME=${LIBRARY_NAME}" "-DBOUNDARY_LABEL=${BOUNDARY_LABEL}"
    -P "${CMAKE_CURRENT_LIST_DIR}/LinkBoundary.cmake"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT error MATCHES "does not contain ${LIBRARY_NAME}")
    message(FATAL_ERROR "Missing reader dependency not diagnosed: ${output}${error}")
endif()