// SPDX-License-Identifier: Apache-2.0
#include "AuthorScene.h"
#include "ReadScene.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

void Require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <class Value>
const Value& Take(const blend::Result<Value>& result) {
  Require(result.HasValue(), result.HasValue() ? "" : result.GetError().code + ": " + result.GetError().message);
  return result.GetValue();
}

std::string Text(const blend::Scene& scene, bool metadataOnly = false) {
  const auto layer = blend::AuthorScene(scene, metadataOnly);
  std::string text;
  Require(Take(layer)->ExportToString(&text), "Imported Scene serializes");
  return text;
}

void CompareDiagnostic(const blend::Diagnostic& actual, const blend::Diagnostic& expected) {
  Require(actual.code == expected.code && actual.severity == expected.severity &&
              actual.message == expected.message && actual.byteOffset == expected.byteOffset &&
              actual.blockIndex == expected.blockIndex && actual.datablock == expected.datablock &&
              actual.recoverable == expected.recoverable,
      "Compression preserves decoded-file diagnostic context");
}

void ExpectFailure(blend::ByteSource& source, const blend::CompressionLimits& limits, const std::string& code) {
  const auto result = blend::ReadScene(source, limits);
  Require(!result.HasValue(), "Importer rejects invalid compressed input: " + code);
  const auto& error = result.GetError();
  Require(error.code == code && error.severity == blend::Severity::Fatal &&
              !error.recoverable && error.byteOffset.has_value(),
      "Importer retains fatal compression code and byte context: " + code);
}

class OversizedSource final : public blend::ByteSource {
public:
  std::uint64_t Size() const override {
    return blend::DefaultSceneCompressionLimits.maxInputBytes + 1;
  }
  bool Read(std::uint64_t offset, std::span<std::byte> destination) override {
    constexpr std::array magic{std::byte{0x28}, std::byte{0xb5}, std::byte{0x2f}, std::byte{0xfd}};
    Require(offset == 0 && destination.size() == magic.size(), "Input limit rejects before reading compressed payload");
    std::copy(magic.begin(), magic.end(), destination.begin());
    return true;
  }
};

class PayloadReadFailure final : public blend::ByteSource {
public:
  explicit PayloadReadFailure(blend::ByteSource& source) : source_(source) {
  }
  std::uint64_t Size() const override {
    return source_.Size();
  }
  bool Read(std::uint64_t offset, std::span<std::byte> destination) override {
    return offset == 0 && destination.size() <= 4 && source_.Read(offset, destination);
  }

private:
  blend::ByteSource& source_;
};

void CheckCompressed(const std::filesystem::path& path) {
  const auto& defaults = blend::DefaultSceneCompressionLimits;
  Require(defaults.maxInputBytes == 268435456 && defaults.maxOutputBytes == 536870912 &&
              defaults.maxExpansionRatio == 4096 && defaults.maxWindowLog == 23,
      "Importer defaults match the accepted four-field policy");
  blend::FileByteSource source(path);
  const auto bytes = blend::ReadFileBytes(source, defaults);
  const auto& decoded = Take(bytes);
  blend::MemoryByteSource memory(decoded);
  const auto plain = blend::ReadScene(memory);
  const auto compressed = blend::ReadScene(source);
  const auto& scene = Take(compressed);
  Require(Text(scene) == Text(Take(plain)) && Text(scene, true) == Text(Take(plain), true),
      "Blender-written Zstandard imports match full and metadata uncompressed stages");
  Require(compressed.Diagnostics().size() == plain.Diagnostics().size(), "Warning count is unchanged");
  for (std::size_t index = 0; index < compressed.Diagnostics().size(); ++index) {
    CompareDiagnostic(compressed.Diagnostics()[index], plain.Diagnostics()[index]);
  }
  Require(Text(scene) == Text(Take(blend::ReadScene(source))), "Repeated compressed imports are deterministic");

  const auto ratio = decoded.size() / source.Size() + (decoded.size() % source.Size() != 0);
  blend::CompressionLimits exact{source.Size(), decoded.size(), ratio, 19};
  Require(Text(Take(blend::ReadScene(source, exact))) == Text(scene), "Exact corpus budgets succeed");
  auto limits = exact;
  --limits.maxInputBytes;
  ExpectFailure(source, limits, "BLEND_COMPRESSION_INPUT_LIMIT");
  limits = exact;
  --limits.maxOutputBytes;
  ExpectFailure(source, limits, "BLEND_COMPRESSION_OUTPUT_LIMIT");
  limits = exact;
  --limits.maxExpansionRatio;
  ExpectFailure(source, limits, "BLEND_COMPRESSION_RATIO_LIMIT");
  limits = exact;
  --limits.maxWindowLog;
  ExpectFailure(source, limits, "BLEND_COMPRESSION_WINDOW_LIMIT");
  ExpectFailure(source, {}, "BLEND_COMPRESSION_LIMITS");
  OversizedSource oversized;
  ExpectFailure(oversized, defaults, "BLEND_COMPRESSION_INPUT_LIMIT");
  PayloadReadFailure unreadable(source);
  ExpectFailure(unreadable, defaults, "BLEND_COMPRESSION_READ_FAILED");

  blend::MemoryByteSource malformed(decoded);
  auto corrupt = decoded;
  const auto blocks = blend::ReadBlocks(malformed, decoded.size() / 20 + 1);
  const auto& records = Take(blocks);
  const auto dna = std::find_if(records.begin(), records.end(),
      [](const auto& block) { return block.code == std::array<char, 4>{'D', 'N', 'A', '1'}; });
  Require(dna != records.end(), "Decoded corpus has DNA1");
  corrupt.at(static_cast<std::size_t>(dna->offset)) = std::byte{0};
  blend::MemoryByteSource invalid(corrupt);
  const auto failure = blend::ReadScene(invalid);
  Require(!failure.HasValue() && failure.GetError().code == "BLEND_DNA_SECTION" &&
              failure.GetError().byteOffset == dna->offset,
      "DNA errors use decoded-file offsets");
}

void CheckUncompressed(const std::filesystem::path& path) {
  blend::FileByteSource source(path);
  const auto plain = blend::ReadScene(source);
  const auto constrained = blend::ReadScene(source, {1, 1, 1, 10});
  Require(Text(Take(plain)) == Text(Take(constrained)), "Compressed policy does not cap uncompressed inputs");
}

} // namespace

int main(int argc, char** argv) {
  try {
    Require(argc == 3, "Expected Blender-written compressed corpus and uncompressed cube");
    CheckCompressed(argv[1]);
    CheckUncompressed(argv[2]);
    std::cout << "Compressed importer equivalence, diagnostics and exact limit boundaries passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
