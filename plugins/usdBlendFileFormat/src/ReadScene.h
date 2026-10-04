// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <blend/BlendFile.h>
#include <blendScene/Scene.h>

namespace blend {

Result<Scene> ReadScene(ByteSource& source);

} // namespace blend
