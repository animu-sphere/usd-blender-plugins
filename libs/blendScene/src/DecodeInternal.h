#pragma once

#include <blendScene/Decode.h>

namespace blend::detail {

template <class Value>
Value Take(const Result<Value>& result) {
  if (!result.HasValue()) {
    throw result.GetError();
  }
  return result.GetValue();
}

Mesh DecodeMesh(std::span<const std::byte> bytes,
    std::span<const BlendBlock> blocks, const DnaSchema& schema,
    const Header& header, const PointerMap& pointers, std::uint32_t index,
    const UnitConversion& units, std::vector<Diagnostic>& diagnostics);

} // namespace blend::detail
