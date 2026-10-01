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

}