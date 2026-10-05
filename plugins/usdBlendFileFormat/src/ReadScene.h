// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <blend/BlendFile.h>
#include <blendScene/Scene.h>

namespace blend {

inline constexpr CompressionLimits DefaultSceneCompressionLimits{268435456, 536870912, 4096, 23};

Result<Scene> ReadScene(ByteSource& source,
    const CompressionLimits& compressionLimits = DefaultSceneCompressionLimits);

} // namespace blend
