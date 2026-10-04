#pragma once

#include <blend/BlendFile.h>
#include <blendScene/Scene.h>

namespace blend {

Result<std::string> NameForDisplay(std::string_view sourceName);
Result<std::vector<std::string>> ObjectIdentifiers(const Scene& scene);

} // namespace blend
