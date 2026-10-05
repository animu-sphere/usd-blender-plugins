cmake_minimum_required(VERSION 3.22)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION" BLEND_VERSION LIMIT_COUNT 1)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(blendScene VERSION ${BLEND_VERSION} LANGUAGES CXX)
    include(CTest)
endif()
if(NOT TARGET blendFile::blendFile)
    find_package(blendFile CONFIG REQUIRED)
endif()
add_library(blendScene STATIC src/Scene.cpp src/Selection.cpp src/Collections.cpp src/Decode.cpp src/Mesh.cpp src/Material.cpp src/ScenePointers.cpp src/Naming.cpp)
add_library(blendScene::blendScene ALIAS blendScene)
target_link_libraries(blendScene PUBLIC blendFile::blendFile)
target_compile_features(blendScene PUBLIC cxx_std_20)
set_target_properties(blendScene PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)
target_include_directories(blendScene PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
if(MSVC)
    target_compile_options(blendScene PUBLIC /utf-8)
    target_compile_definitions(blendScene PUBLIC NOMINMAX)
endif()
include(CMakePackageConfigHelpers)
install(TARGETS blendScene EXPORT blendSceneTargets ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
install(EXPORT blendSceneTargets NAMESPACE blendScene:: DESTINATION lib/cmake/blendScene)
configure_package_config_file(cmake/blendSceneConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/blendSceneConfig.cmake
    INSTALL_DESTINATION lib/cmake/blendScene)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/blendSceneConfigVersion.cmake
    VERSION ${BLEND_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/blendSceneConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/blendSceneConfigVersion.cmake DESTINATION lib/cmake/blendScene)
if(BUILD_TESTING)
    file(WRITE "${CMAKE_BINARY_DIR}/.cmake/api/v1/query/client-blendScene/codemodel-v2" "")
    add_executable(blendSceneTests tests/SceneTests.cpp tests/MeshTests.cpp)
    target_link_libraries(blendSceneTests PRIVATE blendScene::blendScene)
    add_executable(blendSceneTransformTests tests/TransformTests.cpp)
    target_link_libraries(blendSceneTransformTests PRIVATE blendScene::blendScene)
    add_executable(blendSceneFixtureTests tests/SceneFixtureTests.cpp)
    target_link_libraries(blendSceneFixtureTests PRIVATE blendScene::blendScene)
    add_executable(blendSceneNormalTests tests/NormalTests.cpp)
    target_link_libraries(blendSceneNormalTests PRIVATE blendScene::blendScene)
    add_executable(blendSceneMeshBoundaryTests tests/MeshBoundaryTests.cpp)
    target_link_libraries(blendSceneMeshBoundaryTests PRIVATE blendScene::blendScene)
    add_executable(blendSceneNamingTests tests/NamingTests.cpp)
    target_link_libraries(blendSceneNamingTests PRIVATE blendScene::blendScene)
    add_executable(blendSceneInstanceTests tests/InstanceTests.cpp)
    target_link_libraries(blendSceneInstanceTests PRIVATE blendScene::blendScene)
    add_executable(blendSceneMaterialTests tests/MaterialTests.cpp)
    target_link_libraries(blendSceneMaterialTests PRIVATE blendScene::blendScene)
    add_test(NAME blendScene.materials COMMAND blendSceneMaterialTests
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-materials/blender-5.2.2/materials.blend)
    add_test(NAME blendScene.instances COMMAND blendSceneInstanceTests
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-scene/blender-5.2.2/instances.blend)
    add_test(NAME blendScene.naming COMMAND blendSceneNamingTests)
    add_test(NAME blendScene.meshBoundaries COMMAND blendSceneMeshBoundaryTests
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-normals/blender-4.5.13
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-normals/blender-5.2.2)
    add_test(NAME blendScene.legacyMeshStorage COMMAND blendSceneMeshBoundaryTests --legacy
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-mesh/blender-3.3.21/mesh.blend)
    add_test(NAME blendScene.normals COMMAND blendSceneNormalTests
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-normals/blender-4.5.13
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-normals/blender-5.2.2)
    add_test(NAME blendScene.polygonNormals COMMAND blendSceneNormalTests --polygons
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-normals/blender-5.2.2)
    add_test(NAME blendScene.legacyNormals COMMAND blendSceneNormalTests --legacy
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-normals/blender-3.3.21)
    add_test(NAME blendScene.transforms COMMAND blendSceneTransformTests
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-transforms/blender-4.5.13/transforms.blend
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-transforms/blender-5.2.2/transforms.blend)
    add_test(NAME blendScene.sceneFixture COMMAND blendSceneFixtureTests
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-scene/blender-4.5.13/scene.blend
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-scene/blender-5.2.2/scene.blend)
    add_test(NAME blendScene.objectFallbacks COMMAND blendSceneFixtureTests --fallbacks
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-scene/blender-5.2.2/fallbacks.blend)
    add_test(NAME blendScene.sourceEvaluation COMMAND blendSceneFixtureTests --evaluation
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-scene/blender-5.2.2/evaluation.blend)
    add_test(NAME blendScene.meshFixture COMMAND blendSceneFixtureTests --mesh-domains
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-mesh/blender-3.3.21/mesh.blend
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-mesh/blender-4.5.13/mesh.blend
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/native-mesh/blender-5.2.2/mesh.blend)
    add_test(NAME blendScene.ir COMMAND blendSceneTests
        ${CMAKE_CURRENT_SOURCE_DIR}/../../plugins/usdBlendFileFormat/tests/fixtures/empty.blend
        ${CMAKE_CURRENT_SOURCE_DIR}/../../plugins/usdBlendFileFormat/tests/corpus/blender-4.5.13/Untitled.blend
        ${CMAKE_CURRENT_SOURCE_DIR}/../../plugins/usdBlendFileFormat/tests/corpus/blender-5.2.2/Untitled.blend)
    add_test(NAME blendScene.boundary COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/Boundary.cmake)
    add_test(NAME blendScene.linkMetadata COMMAND ${CMAKE_COMMAND}
        -S ${CMAKE_SOURCE_DIR} -B ${CMAKE_BINARY_DIR})
    set_tests_properties(blendScene.linkMetadata PROPERTIES
        FIXTURES_SETUP blendSceneLinkMetadata RUN_SERIAL TRUE)
    add_test(NAME blendScene.linkBoundary COMMAND ${CMAKE_COMMAND}
        -DBUILD_DIR=${CMAKE_BINARY_DIR} -DCONFIG=$<CONFIG>
        -DLIBRARY_NAME=blendScene -DBOUNDARY_LABEL=scene
        -DALLOWED_LIBRARIES=blendFile
        -P ${CMAKE_CURRENT_SOURCE_DIR}/../blendFile/tests/LinkBoundary.cmake)
    set_tests_properties(blendScene.linkBoundary PROPERTIES FIXTURES_REQUIRED blendSceneLinkMetadata)
    add_test(NAME blendScene.linkBoundaryChecks COMMAND ${CMAKE_COMMAND}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/link-boundary-tests
        -DLIBRARY_NAME=blendScene -DBOUNDARY_LABEL=scene
        -DALLOWED_LIBRARIES=blendFile
        -P ${CMAKE_CURRENT_SOURCE_DIR}/../blendFile/tests/LinkBoundaryTests.cmake)
    get_target_property(BLEND_SCENE_LINKS blendScene LINK_LIBRARIES)
    if(NOT BLEND_SCENE_LINKS STREQUAL "blendFile::blendFile")
        message(FATAL_ERROR "The native scene library may link only blendFile")
    endif()
endif()