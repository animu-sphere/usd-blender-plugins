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

struct SavedObjectValues {
  std::int16_t type;
  bool hiddenForRender;
  std::int16_t transformFlags;
  std::optional<std::uint32_t> instanceCollectionBlockIndex;
};

struct SelectedObject {
  std::uint32_t blockIndex;
  std::string sourceName;
  std::optional<std::uint32_t> parentBlockIndex;
  std::optional<std::uint32_t> dataBlockIndex;
  std::optional<SavedObjectValues> values;
};

struct SelectedSceneObjects {
  SelectedScene scene;
  std::vector<SelectedObject> objects;
};

Result<SelectedSceneObjects> SelectSceneObjects(std::span<const std::byte> bytes,
    std::span<const BlendBlock> blocks, const DnaSchema& schema,
    const Header& header, const SceneTraversalLimits& limits);

Result<SelectedSceneObjects> SelectSceneObjectValues(std::span<const std::byte> bytes,
    std::span<const BlendBlock> blocks, const DnaSchema& schema,
    const Header& header, const SceneTraversalLimits& limits);
} // namespace blend