# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.22)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION" BLEND_VERSION LIMIT_COUNT 1)
project(UsdBlendFileFormatPlugin
    VERSION ${BLEND_VERSION}
    DESCRIPTION "OpenUSD blend file-format plugin"
    LANGUAGES CXX)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/OpenStrataPlugin.cmake")
openstrata_default_build_type()

# Resolved from the runtime OpenStrata activates (toolchain sets CMAKE_PREFIX_PATH).
# Guarded so a parent project (a dual-mode root that `add_subdirectory()`s this
# bundle) can resolve OpenUSD once and have it flow down here.
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/BlendOpenUSD.cmake")

if(NOT TARGET blendFile::blendFile)
    find_package(blendFile CONFIG REQUIRED)
endif()
if(NOT TARGET blendScene::blendScene)
    find_package(blendScene CONFIG REQUIRED)
endif()

add_library(usdBlendAuthoring OBJECT src/AuthorScene.cpp src/ReadScene.cpp)
target_compile_features(usdBlendAuthoring PUBLIC cxx_std_20)
set_target_properties(usdBlendAuthoring PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)
target_include_directories(usdBlendAuthoring PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" ${PXR_INCLUDE_DIRS})
target_link_libraries(usdBlendAuthoring PUBLIC blendScene::blendScene)
openstrata_link_openusd(TARGET usdBlendAuthoring VISIBILITY PUBLIC COMPONENTS tf gf vt sdf usd usdGeom)

set(PLUGIN_NAME UsdBlendFileFormatFileFormat)

add_library(${PLUGIN_NAME} SHARED
    src/ArAssetByteSource.cpp
    src/UsdBlendFileFormatFileFormat.cpp)

set_target_properties(${PLUGIN_NAME} PROPERTIES CXX_EXTENSIONS OFF)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_20)
target_link_libraries(${PLUGIN_NAME} PRIVATE blendFile::blendFile usdBlendAuthoring)
target_include_directories(${PLUGIN_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    ${PXR_INCLUDE_DIRS})

# `ost plugin build` writes this fragment when the bundle co-hosts a compiled
# schema (`schema.usda` + `provides: usd-schema:*`). It adds the generated typed
# API sources to this same shared library and defines the generated `*_EXPORTS`
# macro so the schema symbols are exported from the plugin binary. Plain
# file-format bundles have no fragment and stay unchanged.
if(DEFINED OPENSTRATA_SCHEMA_SOURCES_FILE AND EXISTS "${OPENSTRATA_SCHEMA_SOURCES_FILE}")
    include("${OPENSTRATA_SCHEMA_SOURCES_FILE}")
endif()

openstrata_link_openusd(
    TARGET ${PLUGIN_NAME}
    COMPONENTS arch tf gf vt ar sdf usd usdGeom)

# The copied helper keeps output/install mechanics identical in standalone and
# add_subdirectory workspace builds. This file still owns the target, OpenUSD
# components, resource path, and fixtures as the bundle's readable policy.
openstrata_configure_plugin(
    TARGET ${PLUGIN_NAME}
    PLUG_INFO_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/usdBlendFileFormat/plugInfo.json.in"
    PLUG_INFO_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/usdBlendFileFormat/plugInfo.json")

openstrata_install_plugin_bundle(
    TARGET ${PLUGIN_NAME}
    RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/usdBlendFileFormat/plugInfo.json"
    RESOURCE_DESTINATION "plugin/resources/usdBlendFileFormat"
    MANIFESTS
        "${CMAKE_CURRENT_SOURCE_DIR}/openstrata.plugin.yaml"
        "${CMAKE_CURRENT_SOURCE_DIR}/openstrata.scaffold.yaml"
    FIXTURE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()
if(BUILD_TESTING)
    add_executable(usdBlendAuthoringTests tests/AuthorSceneTests.cpp)
    target_link_libraries(usdBlendAuthoringTests PRIVATE usdBlendAuthoring)
    add_test(NAME usdBlend.authoring COMMAND usdBlendAuthoringTests
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-transforms/blender-4.5.13/transforms.blend"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-transforms/blender-5.2.2/transforms.blend"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-normals/blender-4.5.13/multi.blend"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-normals/blender-5.2.2/multi.blend"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-scene/blender-4.5.13/scene.blend"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-scene/blender-5.2.2/scene.blend"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/single_cube.blend"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-mesh/blender-3.3.21/mesh.blend"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-normals/blender-3.3.21/auto_smooth.blend"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-normals/blender-3.3.21/auto_angle.blend"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-normals/blender-3.3.21/auto_zero.blend"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-normals/blender-3.3.21/auto_boundary.blend"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-normals/blender-5.2.2/polygon_smooth.blend"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-normals/blender-5.2.2/polygon_split.blend"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-scene/blender-5.2.2/fallbacks.blend")
    add_test(NAME usdBlend.units COMMAND usdBlendAuthoringTests --units
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-units/blender-4.5.13"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-units/blender-5.2.2")
endif()
