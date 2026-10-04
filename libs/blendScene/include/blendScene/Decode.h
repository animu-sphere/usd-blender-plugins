#pragma once

#include <blendScene/Selection.h>

namespace blend {

Result<Scene> DecodeScene(std::span<const std::byte> bytes,
    std::span<const BlendBlock> blocks, const DnaSchema& schema,
    const Header& header, const SceneTraversalLimits& limits);

} // namespace blend
