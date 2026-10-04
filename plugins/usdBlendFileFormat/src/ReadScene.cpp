// SPDX-License-Identifier: Apache-2.0
#include "ReadScene.h"

#include <blendScene/Decode.h>

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace blend {
namespace {

template <class Value>
const Value& Take(const Result<Value>& result, std::vector<Diagnostic>& diagnostics) {
  if (!result.HasValue()) {
    throw result.GetError();
  }
  diagnostics.insert(diagnostics.end(), result.Diagnostics().begin(), result.Diagnostics().end());
  return result.GetValue();
}

} // namespace

Result<Scene> ReadScene(ByteSource& source) {
  try {
    std::vector<Diagnostic> diagnostics;
    const auto blockResult = ReadBlocks(source,
        std::min<std::uint64_t>(source.Size() / 20 + 1, std::numeric_limits<std::uint32_t>::max()));
    const auto& blocks = Take(blockResult, diagnostics);
    const BlendBlock* dnaBlock = nullptr;
    for (const auto& block : blocks) {
      if (block.code == std::array<char, 4>{'D', 'N', 'A', '1'}) {
        if (dnaBlock) {
          return Result<Scene>(Diagnostic{"BLEND_DNA_BLOCK", Severity::Fatal,
              "Multiple DNA1 blocks", block.offset, {}, {}, false});
        }
        dnaBlock = &block;
      }
    }
    if (!dnaBlock) {
      return Result<Scene>(Diagnostic{"BLEND_DNA_BLOCK", Severity::Fatal,
          "Missing DNA1 block", {}, {}, {}, false});
    }
    // ReadBlocks rejects compressed input before these exact, uncompressed byte budgets are used.
    const auto byteResult = ReadFileBytes(source, {source.Size(), source.Size(), 1, 10});
    const auto& bytes = Take(byteResult, diagnostics);
    MemoryByteSource memory(bytes);
    const auto headerResult = ReadHeader(memory);
    const auto& header = Take(headerResult, diagnostics);
    if (dnaBlock->offset > bytes.size() || dnaBlock->length > bytes.size() - dnaBlock->offset) {
      return Result<Scene>(Diagnostic{"BLEND_BLOCK_SIZE", Severity::Fatal,
          "DNA1 payload exceeds the input bytes", dnaBlock->offset, {}, {}, false});
    }
    const auto dnaResult = ReadDna(std::span<const std::byte>(bytes).subspan(
                                       static_cast<std::size_t>(dnaBlock->offset), static_cast<std::size_t>(dnaBlock->length)),
        header);
    if (!dnaResult.HasValue()) {
      auto error = dnaResult.GetError();
      if (error.byteOffset) {
        *error.byteOffset += dnaBlock->offset;
      }
      return Result<Scene>(std::move(error));
    }
    const auto& schema = Take(dnaResult, diagnostics);
    // Every visited graph node and active depth entry resolves to a distinct saved block.
    const auto graphBound = static_cast<std::uint32_t>(blocks.size());
    const auto sceneResult = DecodeScene(bytes, blocks, schema, header, {graphBound, graphBound});
    const auto& scene = Take(sceneResult, diagnostics);
    return Result<Scene>(scene, std::move(diagnostics));
  } catch (const Diagnostic& error) {
    return Result<Scene>(error);
  } catch (const std::bad_alloc& error) {
    return Result<Scene>(Diagnostic{"BLEND_USD_ALLOCATION", Severity::Fatal,
        error.what(), {}, {}, {}, false});
  } catch (const std::length_error& error) {
    return Result<Scene>(Diagnostic{"BLEND_USD_ALLOCATION", Severity::Fatal,
        error.what(), {}, {}, {}, false});
  }
}

} // namespace blend
