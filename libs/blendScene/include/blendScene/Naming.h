#pragma once

#include <blend/BlendFile.h>
#include <blendScene/Scene.h>

namespace blend {

bool SourceNameLess(std::string_view left, std::string_view right);
Result<std::string> NameForDisplay(std::string_view sourceName);
Result<std::vector<std::string>> ObjectIdentifiers(const Scene& scene);
Result<std::vector<std::string>> UvIdentifiers(const Mesh& mesh);

} // namespace blend
