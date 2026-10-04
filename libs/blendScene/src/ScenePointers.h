#pragma once

#include <blend/BlendFile.h>

#include <map>

namespace blend::detail {

class ScenePointers {
public:
  Result<std::optional<std::uint32_t>> Resolve(std::uint64_t address,
      std::optional<std::uint32_t> mesh = std::nullopt) const;

private:
  PointerMap global_;
  std::map<std::pair<std::uint64_t, std::uint32_t>, std::uint32_t> owned_;
  friend Result<ScenePointers> BuildScenePointers(std::span<const std::byte>,
      std::span<const BlendBlock>, const DnaSchema&, const Header&);
};

Result<ScenePointers> BuildScenePointers(std::span<const std::byte> bytes,
    std::span<const BlendBlock> blocks, const DnaSchema& schema, const Header& header);

} // namespace blend::detail
