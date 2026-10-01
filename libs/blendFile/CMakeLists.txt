cmake_minimum_required(VERSION 3.22)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION" BLEND_VERSION LIMIT_COUNT 1)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(blendFile VERSION ${BLEND_VERSION} LANGUAGES CXX)
    include(CTest)
endif()
add_library(blendFile STATIC src/BlendFile.cpp)
add_library(blendFile::blendFile ALIAS blendFile)
target_compile_features(blendFile PUBLIC cxx_std_20)
set_target_properties(blendFile PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)
target_include_directories(blendFile PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
if(MSVC)
    target_compile_options(blendFile PUBLIC /utf-8)
    target_compile_definitions(blendFile PUBLIC NOMINMAX)
endif()
include(CMakePackageConfigHelpers)
install(TARGETS blendFile EXPORT blendFileTargets ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
install(EXPORT blendFileTargets NAMESPACE blendFile:: DESTINATION lib/cmake/blendFile)
configure_package_config_file(cmake/blendFileConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/blendFileConfig.cmake
    INSTALL_DESTINATION lib/cmake/blendFile)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/blendFileConfigVersion.cmake
    VERSION ${BLEND_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/blendFileConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/blendFileConfigVersion.cmake DESTINATION lib/cmake/blendFile)
if(BUILD_TESTING)
    add_executable(blendFileTests tests/BlendFileTests.cpp)
    target_link_libraries(blendFileTests PRIVATE blendFile::blendFile)
    if(MSVC)
        target_sources(blendFileTests PRIVATE tests/utf8.manifest)
    endif()
    add_test(NAME blendFile.header COMMAND blendFileTests
        "${CMAKE_CURRENT_BINARY_DIR}/header-test.blend")
    add_test(NAME blendFile.boundary COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/Boundary.cmake)
    get_target_property(BLEND_LINKS blendFile LINK_LIBRARIES)
    if(BLEND_LINKS)
        message(FATAL_ERROR "blendFile must have no external link dependencies in Phase 0")
    endif()
endif()