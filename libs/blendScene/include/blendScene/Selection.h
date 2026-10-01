#pragma once

#include <blend/BlendFile.h>
#include <blendScene/Scene.h>

namespace blend {

struct SelectedScene {
  std::uint32_t blockIndex;
  SceneMetadata metadata;
};

Result<SelectedScene> SelectScene(std::span<const std::byte> bytes,
    std::span<const BlendBlock> blocks, const DnaSchema& schema,
    const Header& header);

struct SceneTraversalLimits {
  std::uint32_t maxVisited = 0;
  std::uint32_t maxDepth = 0;
};

struct SelectedObject {
  std::uint32_t blockIndex;
  std::string sourceName;
};

struct SelectedSceneObjects {
  SelectedScene scene;
  std::vector<SelectedObject> objects;
};

Result<SelectedSceneObjects> SelectSceneObjects(std::span<const std::byte> bytes,
    std::span<const BlendBlock> blocks, const DnaSchema& schema,
    const Header& header, const SceneTraversalLimits& limits);
}