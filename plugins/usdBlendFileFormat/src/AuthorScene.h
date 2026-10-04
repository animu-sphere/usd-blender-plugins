// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <blendScene/Scene.h>
#include <blend/BlendFile.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/stage.h>

namespace blend {

Result<pxr::UsdStageRefPtr> CreateAssetStage(
    std::string_view sourceVersion, std::optional<std::string_view> sourceScene = {});
Result<pxr::SdfLayerRefPtr> AuthorScene(const Scene& scene);

} // namespace blend
