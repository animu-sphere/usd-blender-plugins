#include "blend/BlendFile.h"

#include "zlib.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void CheckPointerMap() {
  std::vector<blend::BlendBlock> blocks{
      {{'D', 'A', 'T', 'A'}, 4, 0xfffffffffffffff0ULL, 0, 1, 12},
      {{'D', 'A', 'T', 'A'}, 4, 0x1234, 0, 1, 40},
      {{'E', 'N', 'D', 'B'}, 0, 0, 0, 0, 68},
      {{'R', 'E', 'N', 'D'}, 4, 0x1234, 0, 0, 72},
      {{'G', 'L', 'O', 'B'}, 4, 0x1234, 0, 0, 100},
      {{'D', 'N', 'A', '1'}, 4, 0x1234, 0, 0, 128}};
  const auto result = blend::BuildPointerMap(blocks);
  Require(result.HasValue(), "Pointer map rejected unique addresses");
  const auto map = result.GetValue();
  blocks.clear();
  for (const auto [address, index] : {std::pair{0xfffffffffffffff0ULL, 0U}, std::pair{0x1234ULL, 1U}}) {
    const auto resolved = map.Resolve(address);
    Require(resolved.HasValue() && resolved.GetValue() == index && resolved.Diagnostics().empty(), "Pointer resolution differs");
  }
  const auto null = map.Resolve(0);
  Require(null.HasValue() && !null.GetValue() && null.Diagnostics().empty(), "Null pointer must not warn");
  const auto missing = map.Resolve(0x1235);
  Require(missing.HasValue() && !missing.GetValue() && missing.Diagnostics().size() == 1 &&
              missing.Diagnostics()[0].code == "BLEND_POINTER_UNRESOLVED" &&
              missing.Diagnostics()[0].severity == blend::Severity::Warning && missing.Diagnostics()[0].recoverable,
      "Unresolved pointer must warn and become null");
  blocks = {{{'D', 'A', 'T', 'A'}, 4, 7, 0, 1, 12}, {{'D', 'A', 'T', 'A'}, 4, 7, 0, 1, 40}};
  const auto duplicate = blend::BuildPointerMap(blocks);
  Require(!duplicate.HasValue() && duplicate.GetError().code == "BLEND_POINTER_DUPLICATE" &&
              duplicate.GetError().byteOffset == 40 && duplicate.GetError().blockIndex == 1,
      "Duplicate address must fail with block context");
  blocks[0].oldAddress = blocks[1].oldAddress = 0;
  Require(blend::BuildPointerMap(blocks).HasValue() && blend::BuildPointerMap({}).HasValue(), "Zero addresses or empty map rejected");
}

void CheckZlibDecoder() {
  Require(std::string_view(zlibVersion()) == "1.3.2", "Wrong vendored zlib version");
  std::array<Bytef, 25> compressed{
      0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x03,
      0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x07, 0x00, 0x86, 0xa6, 0x10,
      0x36, 0x05, 0x00, 0x00, 0x00};
  std::array<char, 5> output{};
  z_stream stream{};
  stream.next_in = compressed.data();
  stream.avail_in = static_cast<uInt>(compressed.size());
  stream.next_out = reinterpret_cast<Bytef*>(output.data());
  stream.avail_out = static_cast<uInt>(output.size());
  Require(inflateInit2(&stream, 15 + 16) == Z_OK, "Vendored gzip decoder initialization failed");
  const auto status = inflate(&stream, Z_FINISH);
  const auto cleanup = inflateEnd(&stream);
  Require(status == Z_STREAM_END && cleanup == Z_OK && stream.total_in == compressed.size() &&
              stream.total_out == output.size() && std::string_view(output.data(), output.size()) == "hello",
      "Vendored gzip decoder smoke test failed");
}

blend::Result<blend::Header> Parse(std::string_view text) {
    blend::MemoryByteSource source(std::as_bytes(std::span(text.data(), text.size())));
    return blend::ReadHeader(source);
}

void ExpectError(std::string_view text, std::string_view code) {
    const auto result = Parse(text);
    Require(!result.HasValue(), "Expected failure");
    Require(result.GetError().code == code, "Unexpected diagnostic code");
    Require(result.GetError().severity == blend::Severity::Fatal && !result.GetError().recoverable,
        "Expected fatal non-recoverable diagnostic");
}

std::string RawZstdFrame(std::string_view text) {
    Require(text.size() <= 255, "Test frame is too large");
    std::string frame{"\x28\xb5\x2f\xfd\x20", 5};
    frame.push_back(static_cast<char>(text.size()));
    const auto block = (text.size() << 3) | 1;
    frame.push_back(static_cast<char>(block & 255));
    frame.push_back(static_cast<char>((block >> 8) & 255));
    frame.push_back(static_cast<char>((block >> 16) & 255));
    frame.append(text);
    return frame;
}

std::string GzipFrame(std::string_view text) {
  Require(text.size() <= 65535, "Test gzip member is too large");
  std::string frame{"\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\xff\x01", 11};
  const auto length = static_cast<std::uint32_t>(text.size());
  const auto inverseLength = length ^ 0xffff;
  frame.push_back(static_cast<char>(length & 255));
  frame.push_back(static_cast<char>((length >> 8) & 255));
  frame.push_back(static_cast<char>(inverseLength & 255));
  frame.push_back(static_cast<char>((inverseLength >> 8) & 255));
  frame.append(text);
  const auto checksum = static_cast<std::uint32_t>(crc32(0, reinterpret_cast<const Bytef*>(text.data()), length));
  for (const auto value : {checksum, length}) {
    for (std::size_t index = 0; index < 4; ++index) {
      frame.push_back(static_cast<char>((value >> (8 * index)) & 255));
    }
  }
  return frame;
}

constexpr blend::CompressionLimits fileLimits{1024 * 1024, 1024 * 1024, 2048, 23};

blend::Result<std::vector<std::byte>> ParseFile(std::string_view text, blend::CompressionLimits limits = fileLimits) {
  blend::MemoryByteSource source(std::as_bytes(std::span(text.data(), text.size())));
  return blend::ReadFileBytes(source, limits);
}

void ExpectFileError(std::string_view text, std::string_view code, blend::CompressionLimits limits = fileLimits) {
  const auto result = ParseFile(text, limits);
  Require(!result.HasValue(), "Expected full-file failure");
  Require(result.GetError().code == code, "Unexpected full-file diagnostic code");
  Require(result.GetError().severity == blend::Severity::Fatal && !result.GetError().recoverable &&
              result.GetError().byteOffset.has_value(),
      "Expected fatal full-file diagnostic with byte offset");
}

class FailingFileSource final : public blend::ByteSource {
public:
  FailingFileSource(std::string_view bytes, std::uint64_t readableBytes)
      : source_(std::as_bytes(std::span(bytes.data(), bytes.size()))), readableBytes_(readableBytes) {
  }
  std::uint64_t Size() const override {
    return source_.Size();
  }
  bool Read(std::uint64_t offset, std::span<std::byte> destination) override {
    return offset <= readableBytes_ && destination.size() <= readableBytes_ - offset && source_.Read(offset, destination);
  }

private:
  blend::MemoryByteSource source_;
  std::uint64_t readableBytes_;
};

void CheckFileBytes() {
  const std::string modern = "BLENDER17-01v0502";
  std::string payload = modern;
  for (std::size_t index = 0; index < 12288; ++index) {
    payload.push_back(static_cast<char>(index % 256));
  }
  std::string zstd;
  for (std::size_t offset = 0; offset < payload.size(); offset += 255) {
    zstd += RawZstdFrame(std::string_view(payload).substr(offset, 255));
  }
  const auto gzip = GzipFrame(payload);
  for (const auto& encoded : {payload, gzip, zstd}) {
    const auto decoded = ParseFile(encoded);
    Require(decoded.HasValue(), "Full-file bytes rejected");
    const auto expected = std::as_bytes(std::span(payload.data(), payload.size()));
    Require(std::equal(decoded.GetValue().begin(), decoded.GetValue().end(), expected.begin(), expected.end()),
        "Decoded bytes differ from the original");
    auto exact = fileLimits;
    exact.maxInputBytes = encoded.size();
    exact.maxOutputBytes = payload.size();
    Require(ParseFile(encoded, exact).HasValue(), "Exact full-file byte limits rejected");
    --exact.maxInputBytes;
    ExpectFileError(encoded, "BLEND_COMPRESSION_INPUT_LIMIT", exact);
    exact.maxInputBytes = encoded.size();
    --exact.maxOutputBytes;
    ExpectFileError(encoded, "BLEND_COMPRESSION_OUTPUT_LIMIT", exact);
    for (const std::uint64_t readable : {0, 4096}) {
      FailingFileSource failed(encoded, readable);
      const auto result = blend::ReadFileBytes(failed, fileLimits);
      Require(!result.HasValue() && result.GetError().code == "BLEND_COMPRESSION_READ_FAILED" &&
                  result.GetError().byteOffset == readable,
          "Full-file source read failure accepted or misplaced");
    }
  }
  for (const char pointer : {'_', '-'}) {
    for (const char endian : {'v', 'V'}) {
      std::string legacy = "BLENDER-v405";
      legacy[7] = pointer;
      legacy[8] = endian;
      for (const auto& encoded : {legacy, GzipFrame(legacy), RawZstdFrame(legacy)}) {
        Require(ParseFile(encoded).HasValue(), "Full-file legacy header rejected");
      }
    }
  }
  for (std::size_t split = 0; split <= modern.size(); ++split) {
    Require(ParseFile(GzipFrame(std::string_view(modern).substr(0, split)) +
                      GzipFrame(std::string_view(modern).substr(split)))
                .HasValue(),
        "Full-file concatenated gzip members rejected");
    Require(ParseFile(RawZstdFrame(std::string_view(modern).substr(0, split)) +
                      RawZstdFrame(std::string_view(modern).substr(split)))
                .HasValue(),
        "Full-file concatenated Zstandard frames rejected");
  }
  const auto shortGzip = GzipFrame(modern);
  const auto shortZstd = RawZstdFrame(modern);
  for (const auto& encoded : {shortGzip, shortZstd}) {
    const auto signatureSize = encoded == shortGzip ? 2u : 4u;
    for (std::size_t length = signatureSize; length < encoded.size(); ++length) {
      ExpectFileError(std::string_view(encoded).substr(0, length), "BLEND_COMPRESSION_TRUNCATED");
    }
    Require(!ParseFile(encoded + "junk").HasValue(), "Trailing garbage accepted");
    auto unlimitedRatio = fileLimits;
    unlimitedRatio.maxExpansionRatio = std::numeric_limits<std::uint64_t>::max();
    Require(ParseFile(encoded, unlimitedRatio).HasValue(), "Ratio multiplication overflow rejected valid bytes");
  }
  for (const std::size_t trailerOffset : {8, 4}) {
    auto corrupt = shortGzip;
    corrupt[corrupt.size() - trailerOffset] ^= 1;
    ExpectFileError(corrupt, "BLEND_COMPRESSION_INVALID");
  }
  auto checksummedEmpty = RawZstdFrame({});
  checksummedEmpty[4] = '\x24';
  checksummedEmpty.append("\x99\xe9\xd8\x51", 4);
  Require(ParseFile(shortZstd + checksummedEmpty).HasValue(), "Zstandard checksum rejected");
  checksummedEmpty.back() ^= 1;
  ExpectFileError(shortZstd + checksummedEmpty, "BLEND_COMPRESSION_INVALID");
  auto oversizedWindow = std::string("\x28\xb5\x2f\xfd\x00\x70", 6) + shortZstd.substr(6);
  ExpectFileError(oversizedWindow, "BLEND_COMPRESSION_WINDOW_LIMIT");
  auto rle = std::string("\x28\xb5\x2f\xfd\x60\x11\x7f\x88\x00\x00", 10) + modern;
  rle.append("\x03\x00\x04\x00", 4);
  const auto inflated = ParseFile(rle);
  Require(inflated.HasValue() && inflated.GetValue().size() == 32785, "Zstandard RLE bytes rejected");
  Require(std::all_of(inflated.GetValue().begin() + modern.size(), inflated.GetValue().end(),
              [](std::byte value) { return value == std::byte{0}; }),
      "Zstandard buffered output was lost");
  auto bombLimits = fileLimits;
  bombLimits.maxExpansionRatio = 1;
  ExpectFileError(rle, "BLEND_COMPRESSION_RATIO_LIMIT", bombLimits);
  bombLimits.maxExpansionRatio = fileLimits.maxExpansionRatio;
  bombLimits.maxOutputBytes = 32784;
  ExpectFileError(rle, "BLEND_COMPRESSION_OUTPUT_LIMIT", bombLimits);
  bombLimits.maxOutputBytes = 32785;
  Require(ParseFile(rle, bombLimits).HasValue(), "Exact Zstandard output budget rejected");
  ExpectFileError(modern, "BLEND_COMPRESSION_LIMITS", {});
  for (const auto invalidWindow : {9u, 31u}) {
    auto limits = fileLimits;
    limits.maxWindowLog = invalidWindow;
    ExpectFileError(shortZstd, "BLEND_COMPRESSION_LIMITS", limits);
  }
  ExpectFileError(GzipFrame("INVALID-v405"), "BLEND_HEADER_MAGIC");
  ExpectFileError(GzipFrame("BLENDER17-02v0502"), "BLEND_HEADER_FORMAT_VERSION");
  ExpectFileError(GzipFrame({}), "BLEND_HEADER_TRUNCATED");
  for (const auto field : {0, 1, 2}) {
    auto limits = fileLimits;
    if (field == 0) {
      limits.maxInputBytes = 0;
    } else if (field == 1) {
      limits.maxOutputBytes = 0;
    } else {
      limits.maxExpansionRatio = 0;
    }
    ExpectFileError(shortGzip, "BLEND_COMPRESSION_LIMITS", limits);
  }
  auto ratioExactFrame = rle;
  ratioExactFrame[5] = '\x1e';
  ratioExactFrame[27] = '\x6b';
  auto ratioExact = fileLimits;
  ratioExact.maxExpansionRatio = 1058;
  Require(ParseFile(ratioExactFrame, ratioExact).HasValue(), "Exact expansion ratio rejected");
  ratioExactFrame[5] = '\x1f';
  ratioExactFrame[27] = '\x73';
  ExpectFileError(ratioExactFrame, "BLEND_COMPRESSION_RATIO_LIMIT", ratioExact);
  auto metadata = gzip;
  metadata[3] = '\x08';
  metadata.insert(10, std::string(8192, 'a') + '\0');
  Require(ParseFile(metadata).HasValue(), "Full-file gzip metadata across chunks rejected");
}

void CheckFileFixture(const std::filesystem::path& path, bool compressed,
    std::uint16_t version = 502, std::uint8_t headerSize = 17) {
  constexpr blend::CompressionLimits limits{64 * 1024 * 1024, 64 * 1024 * 1024, 2048, 23};
  blend::FileByteSource source(path);
  Require(source.IsOpen(), "Full-file fixture could not be opened");
  const auto decoded = blend::ReadFileBytes(source, limits);
  if (!decoded.HasValue()) {
    std::cerr << decoded.GetError().code << ": " << decoded.GetError().message << '\n';
  }
  Require(decoded.HasValue(), "Full-file fixture rejected");
  blend::MemoryByteSource memory(decoded.GetValue());
  const auto header = blend::ReadHeader(memory);
  Require(header.HasValue() && header.GetValue().version == version && header.GetValue().headerSize == headerSize,
      "Decoded Blender fixture has the wrong header");
  const auto containerVersion = headerSize == 12 ? blend::BlendContainerVersion::Legacy : blend::BlendContainerVersion::Blender5;
  Require(header.GetValue().containerVersion == containerVersion && header.GetValue().pointerSize == 8 &&
              header.GetValue().byteOrder == blend::ByteOrder::Little,
      "Decoded Blender fixture has the wrong layout");
  if (!compressed) {
    std::vector<std::byte> original(static_cast<std::size_t>(source.Size()));
    Require(source.Read(0, original) && original == decoded.GetValue(), "Uncompressed file bytes changed");
  }
  const auto& bytes = decoded.GetValue();
  const auto blocks = blend::ReadBlocks(memory, 10000);
  Require(blocks.HasValue(), "SDNA fixture block enumeration failed");
  const auto pointers = blend::BuildPointerMap(blocks.GetValue());
  if (!pointers.HasValue()) {
    std::cerr << pointers.GetError().code << ": " << pointers.GetError().message << '\n';
  }
  Require(pointers.HasValue(), "Real-file pointer map rejected");
  for (std::size_t index = 0; index < blocks.GetValue().size(); ++index) {
    const auto& candidate = blocks.GetValue()[index];
    const bool id = candidate.code[0] >= 'A' && candidate.code[0] <= 'Z' && candidate.code[1] >= 'A' && candidate.code[1] <= 'Z' &&
                    candidate.code[2] == '\0' && candidate.code[3] == '\0';
    if (!id && candidate.code != std::array{'D', 'A', 'T', 'A'}) {
      continue;
    }
    const auto address = candidate.oldAddress;
    const auto resolved = pointers.GetValue().Resolve(address);
    Require(resolved.HasValue() && resolved.Diagnostics().empty() &&
                (address == 0 ? !resolved.GetValue() : resolved.GetValue() == index),
        "Real-file old address did not resolve to its block");
  }
  std::size_t dnaCount = 0;
  for (const auto& block : blocks.GetValue()) {
    if (block.code != std::array<char, 4>{'D', 'N', 'A', '1'}) {
      continue;
    }
    ++dnaCount;
    const auto dna = blend::ReadDna(std::span(bytes).subspan(static_cast<std::size_t>(block.offset),
                                        static_cast<std::size_t>(block.length)),
        header.GetValue());
    if (!dna.HasValue()) {
      std::cerr << dna.GetError().code << ": " << dna.GetError().message << '\n';
    }
    Require(dna.HasValue(), "Real-file SDNA rejected");
    const auto& schema = dna.GetValue();
    Require(!schema.structs.empty(), "Real-file SDNA has no structures");
    for (const auto& structure : schema.structs) {
      const auto& type = schema.types[structure.typeIndex];
      Require(schema.FindStruct(type.name) == &structure, "SDNA structure lookup failed");
      std::uint64_t end = 0;
      for (const auto& member : structure.members) {
        Require(member.offset == end && member.size <= type.length - end, "SDNA member range invalid");
        Require(structure.FindMember(member.baseName) == &member, "SDNA member lookup failed");
        end += member.size;
      }
      Require(end == type.length, "SDNA structure length differs from TLEN");
    }
    Require(schema.FindStruct("Scene") != nullptr && schema.FindStruct("Object") != nullptr,
        "Real-file SDNA is missing core structures");
    const auto datablocks = blend::ListDatablocks(bytes, blocks.GetValue(), schema);
    if (!datablocks.HasValue()) {
      std::cerr << datablocks.GetError().code << ": " << datablocks.GetError().message << '\n';
    }
    Require(datablocks.HasValue(), "Real-file ID enumeration rejected");
    std::size_t expectedIds = 0;
    for (const auto& candidate : blocks.GetValue()) {
      if (candidate.code[0] >= 'A' && candidate.code[0] <= 'Z' && candidate.code[1] >= 'A' && candidate.code[1] <= 'Z' &&
          candidate.code[2] == '\0' && candidate.code[3] == '\0') {
        ++expectedIds;
      }
    }
    Require(datablocks.GetValue().size() == expectedIds && expectedIds > 0, "Not every real-file ID block was listed");
    Require(std::any_of(datablocks.GetValue().begin(), datablocks.GetValue().end(), [](const blend::RawDatablock& value) {
      return value.typeName == "Scene" && value.name == "SCScene";
    }),
        "Known Scene type and name were not preserved");
    if (path.filename() == "empty.blend") {
      Require(expectedIds == 1, "Generated empty scene must contain only its Scene ID");
    }
    for (const auto& value : datablocks.GetValue()) {
      const auto& candidate = blocks.GetValue()[value.blockIndex];
      Require(value.oldAddress == candidate.oldAddress && !value.typeName.empty() && value.name.size() >= 2 &&
                  value.typeName == schema.types[schema.structs[candidate.sdnaIndex].typeIndex].name,
          "Real-file raw datablock identity differs");
    }
    if (path.filename() != "empty.blend") {
      Require(std::any_of(datablocks.GetValue().begin(), datablocks.GetValue().end(), [&](const blend::RawDatablock& value) {
        return blocks.GetValue()[value.blockIndex].code == std::array{'S', 'N', '\0', '\0'} &&
               value.typeName == "bScreen" && value.name.starts_with("SR");
      }),
          "Screen block code and stored name prefix distinction was lost");
    }
  }
  Require(dnaCount == 1, "Fixture must contain exactly one DNA1 block");
  const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  for (const bool gzip : {false, true}) {
    std::string encoded;
    const std::size_t chunkSize = gzip ? 4096 : 255;
    for (std::size_t offset = 0; offset < text.size(); offset += chunkSize) {
      const auto chunk = text.substr(offset, chunkSize);
      encoded += gzip ? GzipFrame(chunk) : RawZstdFrame(chunk);
    }
    const auto roundTrip = ParseFile(encoded, limits);
    Require(roundTrip.HasValue() && roundTrip.GetValue() == bytes, "Compressed Blender fixture bytes differ");
  }
}

void AppendInteger(std::string& bytes, std::uint64_t value, std::size_t width, bool little) {
  for (std::size_t index = 0; index < width; ++index) {
    const auto shift = little ? index : width - 1 - index;
    bytes.push_back(static_cast<char>((value >> (8 * shift)) & 255));
  }
}

std::string DnaFixture(bool little, std::uint8_t pointerSize, std::string_view arrayName = "values[2][3]",
    std::string_view pointerName = "**links[2]", std::string_view callbackName = "(*callback)()") {
  std::string bytes = "SDNANAME";
  AppendInteger(bytes, 4, 4, little);
  for (const auto name : {std::string_view("value"), arrayName, pointerName, callbackName}) {
    bytes.append(name);
    bytes.push_back('\0');
  }
  bytes.resize((bytes.size() + 3) / 4 * 4, '\0');
  bytes += "TYPE";
  AppendInteger(bytes, 5, 4, little);
  for (const auto name : {"void", "int", "float", "Sample", "Empty"}) {
    bytes += name;
    bytes.push_back('\0');
  }
  bytes.resize((bytes.size() + 3) / 4 * 4, '\0');
  bytes += "TLEN";
  for (const auto length : {0, 4, 4, 28 + 3 * pointerSize, 0}) {
    AppendInteger(bytes, length, 2, little);
  }
  bytes.resize((bytes.size() + 3) / 4 * 4, '\0');
  bytes += "STRC";
  AppendInteger(bytes, 2, 4, little);
  for (const auto value : {3, 4, 1, 0, 2, 1, 0, 2, 0, 3, 4, 0}) {
    AppendInteger(bytes, value, 2, little);
  }
  return bytes;
}

blend::Result<blend::DnaSchema> ParseDna(std::string_view text, bool little, std::uint8_t pointerSize) {
  const blend::Header header{pointerSize, little ? blend::ByteOrder::Little : blend::ByteOrder::Big, 405};
  return blend::ReadDna(std::as_bytes(std::span(text.data(), text.size())), header);
}

void ExpectDnaError(std::string_view text, bool little, std::uint8_t pointerSize, std::string_view code) {
  const auto result = ParseDna(text, little, pointerSize);
  Require(!result.HasValue(), "Expected SDNA failure");
  if (result.GetError().code != code) {
    std::cerr << "Expected " << code << ", got " << result.GetError().code << ": " << result.GetError().message << '\n';
  }
  Require(result.GetError().code == code, "Unexpected SDNA diagnostic code");
  Require(result.GetError().severity == blend::Severity::Fatal && !result.GetError().recoverable &&
              result.GetError().byteOffset && *result.GetError().byteOffset <= text.size(),
      "Wrong SDNA diagnostic severity or payload-relative location");
}

void CheckDatablocks() {
  for (const bool little : {false, true}) {
    for (const std::uint8_t pointerSize : {4, 8}) {
      std::string dnaBytes = "SDNANAME";
      AppendInteger(dnaBytes, 4, 4, little);
      for (const auto name : {"*next", "name[8]", "id", "value"}) {
        dnaBytes += name;
        dnaBytes.push_back('\0');
      }
      dnaBytes.resize((dnaBytes.size() + 3) / 4 * 4, '\0');
      dnaBytes += "TYPE";
      AppendInteger(dnaBytes, 4, 4, little);
      for (const auto type : {"char", "int", "ID", "Object"}) {
        dnaBytes += type;
        dnaBytes.push_back('\0');
      }
      dnaBytes.resize((dnaBytes.size() + 3) / 4 * 4, '\0');
      dnaBytes += "TLEN";
      for (const auto length : {1, 4, pointerSize + 8, pointerSize + 12}) {
        AppendInteger(dnaBytes, length, 2, little);
      }
      dnaBytes += "STRC";
      AppendInteger(dnaBytes, 2, 4, little);
      for (const auto value : {2, 2, 2, 0, 0, 1, 3, 2, 2, 2, 1, 3}) {
        AppendInteger(dnaBytes, value, 2, little);
      }
      const auto dna = ParseDna(dnaBytes, little, pointerSize);
      Require(dna.HasValue(), "Synthetic ID SDNA rejected");
      auto schema = dna.GetValue();
      std::string payload;
      AppendInteger(payload, 0, pointerSize, little);
      payload.append("OBCube\0\0", 8);
      AppendInteger(payload, 42, 4, little);
      std::vector<blend::BlendBlock> blocks{
          {{'O', 'B', '\0', '\0'}, payload.size(), 0x1234, 1, 1, 0},
          {{'D', 'A', 'T', 'A'}, 0, 0, 0xffffffff, 0, payload.size()}};
      const auto list = [&] {
        return blend::ListDatablocks(std::as_bytes(std::span(payload.data(), payload.size())), blocks, schema);
      };
      const auto result = list();
      Require(result.HasValue() && result.GetValue() == std::vector<blend::RawDatablock>{{0, 0x1234, "Object", "OBCube"}},
          "Synthetic datablock type, name or identity differs");
      const auto originalPayload = payload;
      payload = payload.substr(payload.size() - 4) + payload.substr(0, payload.size() - 4);
      std::swap(schema.structs[1].members[0], schema.structs[1].members[1]);
      schema.structs[1].members[0].offset = 0;
      schema.structs[1].members[1].offset = 4;
      const auto shifted = list();
      Require(shifted.HasValue() && shifted.GetValue() == result.GetValue(), "Embedded ID offset was hard-coded");
      schema = dna.GetValue();
      payload = originalPayload;
      payload[pointerSize + 2] = static_cast<char>(0x80);
      const auto rawName = list();
      Require(rawName.HasValue() && static_cast<unsigned char>(rawName.GetValue()[0].name[2]) == 0x80 &&
                  result.GetValue()[0].name == "OBCube",
          "Raw name bytes were normalized or result borrowed payload storage");
      payload = originalPayload;
      const auto expect = [&](std::string_view code) {
        const auto invalid = list();
        Require(!invalid.HasValue() && invalid.GetError().code == code && invalid.GetError().blockIndex == 0 &&
                    invalid.GetError().byteOffset == blocks[0].offset && invalid.GetError().severity == blend::Severity::Fatal &&
                    !invalid.GetError().recoverable,
            "Malformed datablock did not fail with expected context");
      };
      blocks[0].sdnaIndex = 2;
      expect("BLEND_DNA_INDEX");
      blocks[0].sdnaIndex = 1;
      schema.structs[1].typeIndex = 4;
      expect("BLEND_DNA_INDEX");
      schema.structs[1].typeIndex = 3;
      for (const auto count : {0ULL, 2ULL, std::numeric_limits<unsigned long long>::max()}) {
        blocks[0].count = count;
        expect("BLEND_DNA_SIZE");
      }
      blocks[0].count = 1;
      --blocks[0].length;
      expect("BLEND_DNA_SIZE");
      blocks[0].length += 2;
      expect("BLEND_BLOCK_SIZE");
      --blocks[0].length;
      blocks[0].offset = std::numeric_limits<std::uint64_t>::max();
      expect("BLEND_BLOCK_SIZE");
      blocks[0].offset = 0;
      schema.structs[1].members[0].pointerLevel = 1;
      expect("BLEND_DNA_MEMBER");
      schema.structs[1].members[0].pointerLevel = 0;
      schema.structs[1].members[0].offset = payload.size();
      expect("BLEND_DNA_SIZE");
      schema.structs[1].members[0].offset = 0;
      schema.structs[0].members[1].typeIndex = 1;
      expect("BLEND_DNA_MEMBER");
      schema.structs[0].members[1].typeIndex = 0;
      schema.structs[0].members[1].offset = payload.size();
      expect("BLEND_DNA_SIZE");
      schema.structs[0].members[1].offset = pointerSize;
      payload[pointerSize] = '\0';
      expect("BLEND_DNA_NAME");
      payload[pointerSize] = 'O';
      payload[pointerSize + 6] = payload[pointerSize + 7] = 'X';
      expect("BLEND_DNA_NAME");
      payload.resize(pointerSize + 8);
      payload[pointerSize + 6] = payload[pointerSize + 7] = '\0';
      payload[pointerSize] = 'S';
      payload[pointerSize + 1] = 'R';
      blocks[0].length = payload.size();
      blocks[0].code = {'S', 'N', '\0', '\0'};
      blocks[0].sdnaIndex = 0;
      const auto screen = list();
      Require(screen.HasValue() && screen.GetValue()[0].name == "SRCube", "Different block code and ID name prefix rejected");
      const auto direct = list();
      Require(direct.HasValue() && direct.GetValue()[0].typeName == "ID", "Direct ID structure rejected");
      blocks.clear();
      Require(list().HasValue() && list().GetValue().empty(), "Empty datablock list rejected");
    }
  }
}

void CheckDna() {
  for (const bool little : {false, true}) {
    for (const std::uint8_t pointerSize : {4, 8}) {
      const auto bytes = DnaFixture(little, pointerSize);
      const auto dna = ParseDna(bytes, little, pointerSize);
      Require(dna.HasValue(), "Synthetic SDNA rejected");
      const auto& schema = dna.GetValue();
      Require(schema.names.size() == 4 && schema.types.size() == 5 && schema.structs.size() == 2,
          "SDNA table counts differ");
      const auto* sample = schema.FindStruct("Sample");
      const auto* empty = schema.FindStruct("Empty");
      Require(sample != nullptr && sample->members.size() == 4 && empty != nullptr && empty->members.empty(),
          "SDNA structure lookup failed");
      Require(schema.FindStruct("Missing") == nullptr && sample->FindMember("missing") == nullptr,
          "Absent SDNA name must return nullptr");
      const auto* value = sample->FindMember("value");
      const auto* array = sample->FindMember("values");
      const auto* links = sample->FindMember("links");
      const auto* callback = sample->FindMember("callback");
      Require(value && value->offset == 0 && value->size == 4 && value->pointerLevel == 0 && value->typeIndex == 1,
          "SDNA scalar layout differs");
      Require(array && array->offset == 4 && array->size == 24 && array->pointerLevel == 0 &&
                  array->arrayDimensions == std::vector<std::uint64_t>{2, 3},
          "SDNA multidimensional array layout differs");
      Require(links && links->offset == 28 && links->size == 2 * pointerSize && links->pointerLevel == 2 &&
                  links->arrayDimensions == std::vector<std::uint64_t>{2},
          "SDNA pointer array layout differs");
      Require(callback && callback->offset == 28 + 2 * pointerSize && callback->size == pointerSize &&
                  callback->pointerLevel == 1 && callback->arrayDimensions.empty(),
          "SDNA function pointer layout differs");
      for (std::size_t length = 0; length < bytes.size(); ++length) {
        const auto prefix = ParseDna(std::string_view(bytes).substr(0, length), little, pointerSize);
        Require(!prefix.HasValue() && prefix.GetError().code.starts_with("BLEND_DNA_") &&
                    prefix.GetError().severity == blend::Severity::Fatal && !prefix.GetError().recoverable &&
                    prefix.GetError().byteOffset && *prefix.GetError().byteOffset <= length,
            "Truncated SDNA did not fail safely");
      }
      auto replaceInteger = [&](std::size_t offset, std::uint64_t value, std::size_t width, std::string_view code) {
        auto corrupt = bytes;
        std::string encoded;
        AppendInteger(encoded, value, width, little);
        corrupt.replace(offset, width, encoded);
        ExpectDnaError(corrupt, little, pointerSize, code);
      };
      const auto strc = bytes.find("STRC");
      const auto tlen = bytes.find("TLEN");
      for (const auto tag : {std::size_t{0}, std::size_t{4}, bytes.find("TYPE"), tlen, strc}) {
        auto corrupt = bytes;
        corrupt[tag] = 'X';
        ExpectDnaError(corrupt, little, pointerSize, "BLEND_DNA_SECTION");
      }
      replaceInteger(8, 0xffffffff, 4, "BLEND_DNA_COUNT");
      replaceInteger(bytes.find("TYPE") + 4, 0xffffffff, 4, "BLEND_DNA_COUNT");
      replaceInteger(strc + 4, 0xffffffff, 4, "BLEND_DNA_COUNT");
      replaceInteger(strc + 8, 5, 2, "BLEND_DNA_INDEX");
      replaceInteger(strc + 10, 0xffff, 2, "BLEND_DNA_COUNT");
      replaceInteger(strc + 12, 5, 2, "BLEND_DNA_INDEX");
      replaceInteger(strc + 14, 4, 2, "BLEND_DNA_INDEX");
      replaceInteger(strc + 18, 0, 2, "BLEND_DNA_DUPLICATE");
      replaceInteger(strc + 28, 3, 2, "BLEND_DNA_DUPLICATE");
      replaceInteger(tlen + 10, 27 + 3 * pointerSize, 2, "BLEND_DNA_SIZE");
      replaceInteger(tlen + 10, 29 + 3 * pointerSize, 2, "BLEND_DNA_SIZE");
      replaceInteger(tlen + 6, 0, 2, "BLEND_DNA_SIZE");
      auto duplicateType = bytes;
      duplicateType.replace(duplicateType.find("float"), 5, "Empty");
      ExpectDnaError(duplicateType, little, pointerSize, "BLEND_DNA_DUPLICATE");
      ExpectDnaError(bytes + "x", little, pointerSize, "BLEND_DNA_TRAILING");
      ExpectDnaError(bytes, little, 0, "BLEND_DNA_LAYOUT");
      for (const auto name : {"", "2values", "values[]", "values[0]", "values[-1]", "values[2", "values[2]junk"}) {
        ExpectDnaError(DnaFixture(little, pointerSize, name), little, pointerSize, "BLEND_DNA_NAME");
      }
      for (const auto name : {"values[18446744073709551616]", "values[18446744073709551615]", "values[2147483648][2147483648]"}) {
        ExpectDnaError(DnaFixture(little, pointerSize, name), little, pointerSize, "BLEND_DNA_SIZE");
      }
      ExpectDnaError(DnaFixture(little, pointerSize, "values[2][3]", "*"), little, pointerSize, "BLEND_DNA_NAME");
      for (const auto name : {"(callback)()", "(*callback)", "(*callback)(int)", "(*callback", "(*callback)()junk"}) {
        ExpectDnaError(DnaFixture(little, pointerSize, "values[2][3]", "**links[2]", name), little, pointerSize, "BLEND_DNA_NAME");
      }
      const auto functionArray = ParseDna(DnaFixture(little, pointerSize, "values[2][3]", "**links[2]", "(*callback[1])()"),
          little, pointerSize);
      Require(functionArray.HasValue() && functionArray.GetValue().FindStruct("Sample")->FindMember("callback")->arrayDimensions ==
                                              std::vector<std::uint64_t>{1},
          "SDNA function pointer array rejected");
    }
  }
  const auto bytes = DnaFixture(true, 8);
  const blend::Header invalid{8, static_cast<blend::ByteOrder>(99), 405};
  const auto result = blend::ReadDna(std::as_bytes(std::span(bytes.data(), bytes.size())), invalid);
  Require(!result.HasValue() && result.GetError().code == "BLEND_DNA_LAYOUT", "Invalid SDNA byte order accepted");
}

std::string BlockHeader(std::string_view code, bool modern, std::size_t pointerSize, bool little,
    std::uint64_t length = 0, std::uint64_t oldAddress = 0, std::uint64_t sdna = 0, std::uint64_t count = 0) {
  Require(code.size() <= 4, "Test block code too long");
  std::string bytes(code);
  bytes.resize(4, '\0');
  AppendInteger(bytes, modern ? sdna : length, 4, little);
  AppendInteger(bytes, oldAddress, pointerSize, little);
  AppendInteger(bytes, modern ? length : sdna, modern ? 8 : 4, little);
  AppendInteger(bytes, count, modern ? 8 : 4, little);
  return bytes;
}

blend::Result<std::vector<blend::BlendBlock>> ParseBlocks(std::string_view text, std::uint64_t limit = 10000) {
  blend::MemoryByteSource source(std::as_bytes(std::span(text.data(), text.size())));
  return blend::ReadBlocks(source, limit);
}

class SparseBlockSource final : public blend::ByteSource {
public:
  static constexpr auto length = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  std::uint64_t Size() const override {
    return prefix_.size() + length + end_.size();
  }
  bool Read(std::uint64_t offset, std::span<std::byte> destination) override {
    if (offset <= prefix_.size() && destination.size() <= prefix_.size() - offset) {
      blend::MemoryByteSource prefix(std::as_bytes(std::span(prefix_.data(), prefix_.size())));
      return prefix.Read(offset, destination);
    }
    const auto endOffset = Size() - end_.size();
    if (offset >= endOffset) {
      blend::MemoryByteSource end(std::as_bytes(std::span(end_.data(), end_.size())));
      return end.Read(offset - endOffset, destination);
    }
    return false;
  }

private:
  std::string prefix_ = "BLENDER17-01v0502" + BlockHeader("DATA", true, 8, true, length,
                                                  std::numeric_limits<std::uint64_t>::max(), std::numeric_limits<std::int32_t>::max(), length);
  std::string end_ = BlockHeader("ENDB", true, 8, true);
};

void ExpectBlockError(std::string_view text, std::string_view code, std::uint64_t offset,
    std::uint32_t index = 0, std::uint64_t limit = 10000) {
  const auto result = ParseBlocks(text, limit);
  Require(!result.HasValue(), "Expected block failure");
  Require(result.GetError().code == code, "Unexpected block diagnostic code");
  Require(result.GetError().byteOffset == offset && result.GetError().blockIndex == index &&
              result.GetError().severity == blend::Severity::Fatal && !result.GetError().recoverable,
      "Wrong block diagnostic location or severity");
}

void CheckBlocks() {
  for (const bool modern : {false, true}) {
    for (const std::size_t pointerSize : {4, 8}) {
      for (const bool little : {false, true}) {
        if (modern && (pointerSize != 8 || !little)) {
          continue;
        }
        std::string header = modern ? "BLENDER17-01v0502" : "BLENDER-v405";
        if (!modern) {
          header[7] = pointerSize == 4 ? '_' : '-';
          header[8] = little ? 'v' : 'V';
        }
        const auto make = [&](std::string_view code, std::uint64_t length = 0,
                              std::uint64_t old = 0, std::uint64_t sdna = 0, std::uint64_t count = 0) {
          return BlockHeader(code, modern, pointerSize, little, length, old, sdna, count);
        };
        const auto end = make("ENDB");
        const auto old = pointerSize == 4 ? 0xfedcba98ULL : 0xfedcba9876543210ULL;
        const auto first = make("SC", 3, old, 23, 2) + "abc";
        const auto data = make("DATA", 1, 42, 7, 1) + "x";
        const auto file = header + first + data + end;
        const auto parsed = ParseBlocks(file, 3);
        Require(parsed.HasValue() && parsed.GetValue().size() == 3 && parsed.Diagnostics().empty(),
            "Valid block layout rejected");
        const auto& blocks = parsed.GetValue();
        Require(blocks[0].code == std::array{'S', 'C', '\0', '\0'} && blocks[0].length == 3 &&
                    blocks[0].oldAddress == old && blocks[0].sdnaIndex == 23 && blocks[0].count == 2 &&
                    blocks[0].offset == header.size() + end.size(),
            "Block normalization failed");
        Require(blocks[1].offset == header.size() + first.size() + end.size() && blocks[1].length == 1 &&
                    blocks[1].oldAddress == 42 && blocks[1].sdnaIndex == 7 && blocks[1].count == 1 &&
                    blocks[2].offset == file.size(),
            "Unaligned block payload offset changed");
        Require(ParseBlocks(header + end, 1).HasValue(), "Empty container rejected");
        ExpectBlockError(file, "BLEND_BLOCK_COUNT_LIMIT", header.size() + first.size() + data.size(), 2, 2);
        ExpectBlockError(header, "BLEND_BLOCK_MISSING_ENDB", header.size());
        ExpectBlockError(header + first, "BLEND_BLOCK_MISSING_ENDB", header.size() + first.size(), 1);
        for (std::size_t length = 1; length < end.size(); ++length) {
          ExpectBlockError(header + end.substr(0, length), "BLEND_BLOCK_TRUNCATED", header.size());
        }
        for (const std::size_t boundary : {header.size(), header.size() + first.size(), header.size() + first.size() + data.size()}) {
          for (std::size_t length = boundary; length < boundary + end.size(); ++length) {
            Require(!ParseBlocks(std::string_view(file).substr(0, length)).HasValue(), "Truncated block boundary accepted");
          }
        }
        const auto lengthPosition = header.size() + (modern ? 16 : 4);
        const auto sdnaPosition = header.size() + (modern ? 4 : 8 + pointerSize);
        const auto countPosition = header.size() + (modern ? 24 : 12 + pointerSize);
        const auto negative = modern ? 0x8000000000000000ULL : 0x80000000ULL;
        ExpectBlockError(header + make("DATA", negative), "BLEND_BLOCK_NEGATIVE_LENGTH", lengthPosition);
        ExpectBlockError(header + make("DATA", 0, 0, 0x80000000ULL), "BLEND_BLOCK_NEGATIVE_SDNA", sdnaPosition);
        ExpectBlockError(header + make("DATA", 0, 0, 0, negative), "BLEND_BLOCK_NEGATIVE_COUNT", countPosition);
        ExpectBlockError(header + make("DATA", negative - 1), "BLEND_BLOCK_SIZE", lengthPosition);
        ExpectBlockError(header + make("DATA", 1), "BLEND_BLOCK_SIZE", lengthPosition);
        ExpectBlockError(header + make("ENDB", 1) + "x", "BLEND_BLOCK_ENDB", header.size());
        ExpectBlockError(header + end + "x", "BLEND_BLOCK_TRAILING", header.size() + end.size());
        ExpectBlockError(header + end + end, "BLEND_BLOCK_TRAILING", header.size() + end.size());
        const auto unknown = ParseBlocks(header + make("????", 3) + "abc" + end);
        Require(unknown.HasValue() && unknown.GetValue().size() == 2 && unknown.Diagnostics().size() == 1,
            "Unknown code prevented block enumeration");
        const auto& diagnostic = unknown.Diagnostics()[0];
        Require(diagnostic.code == "BLEND_BLOCK_UNKNOWN_CODE" && diagnostic.severity == blend::Severity::Unsupported &&
                    diagnostic.recoverable && diagnostic.byteOffset == header.size() && diagnostic.blockIndex == 0,
            "Wrong unknown block diagnostic");
        for (const auto boundary : {header.size(), header.size() + first.size()}) {
          FailingFileSource failed(file, boundary);
          const auto result = blend::ReadBlocks(failed, 3);
          Require(!result.HasValue() && result.GetError().code == "BLEND_BLOCK_READ_FAILED" &&
                      result.GetError().byteOffset == boundary,
              "Block read failure accepted or misplaced");
        }
        for (const auto& encoded : {GzipFrame(file), RawZstdFrame(file)}) {
          ExpectBlockError(encoded, "BLEND_BLOCK_COMPRESSED", 0);
          const auto decoded = ParseFile(encoded);
          Require(decoded.HasValue(), "Compressed container could not be decoded");
          blend::MemoryByteSource source(decoded.GetValue());
          const auto result = blend::ReadBlocks(source, 3);
          Require(result.HasValue() && result.GetValue() == blocks, "Compression changed normalized blocks");
        }
      }
    }
  }
  ExpectBlockError("BLENDER17-01v0502", "BLEND_BLOCK_LIMITS", 0, 0, 0);
  ExpectBlockError("BLENDER17-01v0502", "BLEND_BLOCK_LIMITS", 0, 0, 0x100000000ULL);
  Require(!ParseBlocks("INVALID-v405").HasValue() && !ParseBlocks("BLENDER").HasValue(), "Invalid container header accepted");
  SparseBlockSource sparse;
  const auto large = blend::ReadBlocks(sparse, 2);
  Require(large.HasValue() && large.GetValue().size() == 2 && large.GetValue()[0].length == SparseBlockSource::length &&
              large.GetValue()[0].count == SparseBlockSource::length &&
              large.GetValue()[0].oldAddress == std::numeric_limits<std::uint64_t>::max() &&
              large.GetValue()[0].sdnaIndex == std::numeric_limits<std::int32_t>::max() &&
              large.GetValue()[1].offset == sparse.Size(),
      "64-bit block fields or offsets were truncated, or enumeration read the payload");
  const auto lts = ParseBlocks("BLENDER17-01v0405" + BlockHeader("ENDB", true, 8, true));
  Require(lts.HasValue() && lts.GetValue().size() == 1, "Format-1 block layout incorrectly restricted to Blender 5");
}

std::vector<blend::BlendBlock> CheckBlockFixture(const std::filesystem::path& path, bool empty) {
  blend::FileByteSource source(path);
  constexpr blend::CompressionLimits limits{64 * 1024 * 1024, 64 * 1024 * 1024, 2048, 23};
  const auto bytes = blend::ReadFileBytes(source, limits);
  Require(bytes.HasValue(), "Block fixture could not be decoded");
  blend::MemoryByteSource decoded(bytes.GetValue());
  const auto parsed = blend::ReadBlocks(decoded, 100000);
  if (!parsed.HasValue()) {
    std::cerr << parsed.GetError().code << ": " << parsed.GetError().message << '\n';
  }
  Require(parsed.HasValue(), "Blender-written block fixture rejected");
  const auto& blocks = parsed.GetValue();
  Require(!blocks.empty() && blocks.back().code == std::array{'E', 'N', 'D', 'B'} &&
              blocks.back().offset == bytes.GetValue().size(),
      "Blender fixture has no terminal ENDB");
  const auto dna = std::find_if(blocks.begin(), blocks.end(), [](const blend::BlendBlock& block) {
    return block.code == std::array{'D', 'N', 'A', '1'};
  });
  Require(dna != blocks.end(), "Blender fixture has no DNA1");
  if (empty) {
    Require(blocks.size() == 266 && dna->offset == 53954 && dna->length == 134572 &&
                blocks.back().offset == 188558,
        "Known empty-scene block positions changed");
    Require(parsed.Diagnostics().empty(), "Known empty-scene codes reported as unsupported");
  }
  return blocks;
}

std::set<std::array<char, 4>> BlockKinds(const std::vector<blend::BlendBlock>& blocks) {
  std::set<std::array<char, 4>> kinds;
  for (const auto& block : blocks) {
    kinds.insert(block.code);
  }
  return kinds;
}

class CompressedReadFailure final : public blend::ByteSource {
public:
  explicit CompressedReadFailure(std::string prefix = RawZstdFrame("BLENDER17-01v0502"))
      : prefix_(std::move(prefix)) {
  }
    std::uint64_t Size() const override { return 32; }
    bool Read(std::uint64_t offset, std::span<std::byte> destination) override {
        if (offset != 0 || destination.size() != 12) {
            return false;
        }
        std::copy_n(reinterpret_cast<const std::byte*>(prefix_.data()), destination.size(), destination.begin());
        return true;
    }

  private:
    std::string prefix_;
};

void CheckGzipHeaders() {
  const std::string modern = "BLENDER17-01v0502";
  const auto frame = GzipFrame(modern);
  const auto modernHeader = Parse(frame);
  Require(modernHeader.HasValue() && modernHeader.GetValue().version == 502 &&
              modernHeader.GetValue().containerVersion == blend::BlendContainerVersion::Blender5 &&
              modernHeader.GetValue().headerSize == 17 && modernHeader.GetValue().pointerSize == 8 &&
              modernHeader.GetValue().byteOrder == blend::ByteOrder::Little,
      "Gzip modern header rejected");
  const auto ltsHeader = Parse(GzipFrame("BLENDER17-01v0405"));
  Require(ltsHeader.HasValue() && ltsHeader.GetValue().version == 405 && ltsHeader.GetValue().headerSize == 17,
      "Gzip format-1 LTS header rejected");
  for (const char pointer : {'_', '-'}) {
    for (const char endian : {'v', 'V'}) {
      std::string legacy = "BLENDER-v405";
      legacy[7] = pointer;
      legacy[8] = endian;
      const auto header = Parse(GzipFrame(legacy));
      Require(header.HasValue() && header.GetValue().version == 405 && header.GetValue().headerSize == 12 &&
                  header.GetValue().containerVersion == blend::BlendContainerVersion::Legacy &&
                  header.GetValue().pointerSize == (pointer == '_' ? 4 : 8) &&
                  header.GetValue().byteOrder == (endian == 'v' ? blend::ByteOrder::Little : blend::ByteOrder::Big),
          "Gzip legacy layout rejected");
    }
  }
  for (std::size_t length = 2; length < frame.size() - 8; ++length) {
    ExpectError(std::string_view(frame).substr(0, length), "BLEND_COMPRESSION_TRUNCATED");
  }
  for (std::size_t length = 0; length < modern.size(); ++length) {
    ExpectError(GzipFrame(std::string_view(modern).substr(0, length)), "BLEND_HEADER_TRUNCATED");
  }
  for (std::size_t split = 0; split <= modern.size(); ++split) {
    const auto header = Parse(GzipFrame(std::string_view(modern).substr(0, split)) +
                              GzipFrame(std::string_view(modern).substr(split)));
    Require(header.HasValue() && header.GetValue().version == 502 && header.GetValue().headerSize == 17,
        "Concatenated gzip members rejected");
  }
  ExpectError(GzipFrame("INVALID-v405"), "BLEND_HEADER_MAGIC");
  ExpectError(GzipFrame("BLENDER18-01v0502"), "BLEND_HEADER_SIZE");
  ExpectError(GzipFrame("BLENDER17-02v0502"), "BLEND_HEADER_FORMAT_VERSION");
  ExpectError(GzipFrame("BLENDER17-01V0502"), "BLEND_HEADER_ENDIANNESS");
  ExpectError(GzipFrame("BLENDER17-01v05x2"), "BLEND_HEADER_VERSION");
  auto corrupt = frame;
  corrupt[2] = '\x09';
  ExpectError(corrupt, "BLEND_COMPRESSION_INVALID");
  corrupt = frame;
  corrupt[3] = static_cast<char>(0xe0);
  ExpectError(corrupt, "BLEND_COMPRESSION_INVALID");
  corrupt = frame;
  corrupt[10] = '\x07';
  ExpectError(corrupt, "BLEND_COMPRESSION_INVALID");
  for (const std::size_t trailerOffset : {8, 4}) {
    auto firstMember = GzipFrame("BLEND");
    firstMember[firstMember.size() - trailerOffset] ^= 1;
    ExpectError(firstMember + GzipFrame("ER17-01v0502"), "BLEND_COMPRESSION_INVALID");
  }
  for (const char flag : {'\x08', '\x10'}) {
    auto metadata = frame;
    metadata[3] = flag;
    metadata.insert(10, std::string(8192, 'a') + '\0');
    const auto header = Parse(metadata);
    Require(header.HasValue() && header.GetValue().version == 502, "Gzip metadata across input chunks rejected");
  }
  auto excessiveMetadata = frame;
  excessiveMetadata[3] = '\x08';
  excessiveMetadata.insert(10, std::string(1024 * 1024, 'a') + '\0');
  ExpectError(excessiveMetadata, "BLEND_COMPRESSION_INPUT_LIMIT");
  std::string emptyMembers;
  const auto emptyMember = GzipFrame({});
  while (emptyMembers.size() <= 1024 * 1024) {
    emptyMembers += emptyMember;
  }
  ExpectError(emptyMembers, "BLEND_COMPRESSION_INPUT_LIMIT");
  CompressedReadFailure failedSource(frame);
  const auto failedRead = blend::ReadHeader(failedSource);
  Require(!failedRead.HasValue() && failedRead.GetError().code == "BLEND_COMPRESSION_READ_FAILED",
      "Gzip source read failure accepted");
  const auto trailerless = Parse(std::string_view(frame).substr(0, frame.size() - 8));
  Require(trailerless.HasValue() && trailerless.GetValue().version == 502, "Gzip probe unexpectedly requires the trailer");
  auto badTrailer = frame;
  badTrailer[badTrailer.size() - 8] ^= 1;
  Require(Parse(badTrailer).HasValue(), "Gzip probe unexpectedly validates the trailing checksum");
  constexpr char deflated[] =
      "\x1f\x8b\x08\x00\x00\x00\x00\x00\x02\x0a\xed\xc1\xb1\x0d\x00\x10"
      "\x10\x00\xc0\x89\x24\x4f\x22\x7a\xa1\x13\x85\x0d\x2c\x61\x7e\x8b"
      "\xdc\x5d\x5f\x73\x8f\x79\x72\x4b\x91\x5f\xd4\x28\x17\x00\x00\x00"
      "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
      "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xb8\x1f\x72\xdc"
      "\x70\x8a\x11\x80\x00\x00";
  const auto deflatedHeader = Parse(std::string_view(deflated, sizeof(deflated) - 1));
  Require(deflatedHeader.HasValue() && deflatedHeader.GetValue().version == 502 &&
              deflatedHeader.GetValue().headerSize == 17,
      "High-ratio gzip header rejected");
  const auto deflatedBytes = std::string_view(deflated, sizeof(deflated) - 1);
  const auto inflated = ParseFile(deflatedBytes);
  if (!inflated.HasValue()) {
    std::cerr << inflated.GetError().code << ": " << inflated.GetError().message << '\n';
  }
  Require(inflated.HasValue() && inflated.GetValue().size() == 32785, "Full-file DEFLATE output was lost");
  auto limits = fileLimits;
  limits.maxExpansionRatio = 1;
  ExpectFileError(deflatedBytes, "BLEND_COMPRESSION_RATIO_LIMIT", limits);
  limits.maxExpansionRatio = fileLimits.maxExpansionRatio;
  limits.maxOutputBytes = 32784;
  ExpectFileError(deflatedBytes, "BLEND_COMPRESSION_OUTPUT_LIMIT", limits);
  limits.maxOutputBytes = 32785;
  Require(ParseFile(deflatedBytes, limits).HasValue(), "Exact DEFLATE output budget rejected");
}

class ShortSource final : public blend::ByteSource {
public:
    std::uint64_t Size() const override { return 12; }
    bool Read(std::uint64_t, std::span<std::byte>) override { return false; }
};
}

int main(int argumentCount, char** arguments) {
    try {
      Require(argumentCount == 5, "Expected temporary, compressed, uncompressed and legacy Blender fixture paths");
      CheckZlibDecoder();
      CheckGzipHeaders();
      CheckFileBytes();
      CheckDna();
      CheckPointerMap();
      CheckDatablocks();
      CheckFileFixture(arguments[2], true);
      CheckFileFixture(arguments[3], false);
      CheckFileFixture(arguments[4], false, 405, 12);
      CheckBlocks();
      const auto modernBlocks = CheckBlockFixture(arguments[2], false);
      CheckBlockFixture(arguments[3], true);
      const auto legacyBlocks = CheckBlockFixture(arguments[4], false);
      Require(legacyBlocks.size() == 1240 && legacyBlocks.back().offset == 504759,
          "Known legacy-scene block count or end changed");
      const auto legacyDna = std::find_if(legacyBlocks.begin(), legacyBlocks.end(), [](const blend::BlendBlock& block) {
        return block.code == std::array{'D', 'N', 'A', '1'};
      });
      Require(legacyDna != legacyBlocks.end() && legacyDna->offset == 372491 && legacyDna->length == 132244,
          "Known legacy-scene DNA1 positions changed");
      const auto modernKinds = BlockKinds(modernBlocks);
      const auto legacyKinds = BlockKinds(legacyBlocks);
      Require(legacyKinds.size() == 20 && legacyKinds == modernKinds,
          "Blender 4.5 and 5.x corpus block kinds differ");
      blend::FileByteSource realFile(arguments[2]);
      const auto realHeader = blend::ReadHeader(realFile);
      if (!realHeader.HasValue()) {
        std::cerr << realHeader.GetError().code << ": " << realHeader.GetError().message << '\n';
      }
        Require(realHeader.HasValue(), "Blender-written compressed header rejected");
        Require(realHeader.GetValue().version == 502, "Wrong Blender fixture version");
        Require(realHeader.GetValue().containerVersion == blend::BlendContainerVersion::Blender5 &&
            realHeader.GetValue().headerSize == 17 && realHeader.GetValue().pointerSize == 8 &&
            realHeader.GetValue().byteOrder == blend::ByteOrder::Little, "Wrong Blender fixture layout");
        const std::string modern = "BLENDER17-01v0502";
        const auto modernHeader = Parse(modern);
        Require(modernHeader.HasValue() && modernHeader.GetValue().version == 502 &&
            modernHeader.GetValue().SourceVersion() == "5.2" &&
            modernHeader.GetValue().containerVersion == blend::BlendContainerVersion::Blender5 &&
            modernHeader.GetValue().headerSize == 17, "Uncompressed modern header rejected");
        for (const bool compressed : {false, true}) {
          const auto modernLtsHeader = Parse(compressed ? RawZstdFrame("BLENDER17-01v0405") : std::string("BLENDER17-01v0405"));
          Require(modernLtsHeader.HasValue() && modernLtsHeader.GetValue().version == 405 &&
                      modernLtsHeader.GetValue().SourceVersion() == "4.5" &&
                      modernLtsHeader.GetValue().containerVersion == blend::BlendContainerVersion::Blender5 &&
                      modernLtsHeader.GetValue().headerSize == 17 && modernLtsHeader.GetValue().pointerSize == 8 &&
                      modernLtsHeader.GetValue().byteOrder == blend::ByteOrder::Little,
              "Format-1 header incorrectly restricted to Blender 5");
        }
        for (std::size_t length = 0; length < modern.size(); ++length) {
            ExpectError(std::string_view(modern).substr(0, length), "BLEND_HEADER_TRUNCATED");
        }
        ExpectError("BLENDER18-01v0502", "BLEND_HEADER_SIZE");
        ExpectError("BLENDER17_01v0502", "BLEND_HEADER_FORMAT_VERSION");
        ExpectError("BLENDER17-02v0502", "BLEND_HEADER_FORMAT_VERSION");
        ExpectError("BLENDER17-01V0502", "BLEND_HEADER_ENDIANNESS");
        for (std::size_t index = 13; index < modern.size(); ++index) {
            auto malformed = modern;
            malformed[index] = 'x';
            ExpectError(malformed, "BLEND_HEADER_VERSION");
        }
        const auto compressedModern = RawZstdFrame(modern);
        const auto compressedHeader = Parse(compressedModern);
        Require(compressedHeader.HasValue() && compressedHeader.GetValue().version == 502 &&
            compressedHeader.GetValue().headerSize == 17, "Raw Zstandard modern header rejected");
        const auto compressedLegacy = Parse(RawZstdFrame("BLENDER-v405"));
        Require(compressedLegacy.HasValue() && compressedLegacy.GetValue().version == 405 &&
            compressedLegacy.GetValue().containerVersion == blend::BlendContainerVersion::Legacy &&
            compressedLegacy.GetValue().headerSize == 12, "Compressed legacy header rejected");
        for (std::size_t length = 4; length < compressedModern.size(); ++length) {
            ExpectError(std::string_view(compressedModern).substr(0, length), "BLEND_COMPRESSION_TRUNCATED");
        }
        for (std::size_t length = 0; length < modern.size(); ++length) {
            ExpectError(RawZstdFrame(std::string_view(modern).substr(0, length)), "BLEND_HEADER_TRUNCATED");
        }
        ExpectError(RawZstdFrame("INVALID-v405"), "BLEND_HEADER_MAGIC");
        ExpectError(RawZstdFrame("BLENDER17-01V0502"), "BLEND_HEADER_ENDIANNESS");
        for (std::size_t split = 0; split <= modern.size(); ++split) {
            const auto splitHeader = Parse(RawZstdFrame(std::string_view(modern).substr(0, split)) +
                RawZstdFrame(std::string_view(modern).substr(split)));
            Require(splitHeader.HasValue() && splitHeader.GetValue().version == 502, "Concatenated frames rejected");
        }
        auto corrupt = compressedModern;
        corrupt[6] = static_cast<char>((modern.size() << 3) | 7);
        ExpectError(corrupt, "BLEND_COMPRESSION_INVALID");
        std::string oversizedWindow{"\x28\xb5\x2f\xfd\x00\x70", 6};
        oversizedWindow.append(compressedModern.substr(6));
        ExpectError(oversizedWindow, "BLEND_COMPRESSION_WINDOW_LIMIT");
        std::string emptyFrames;
        const auto emptyFrame = RawZstdFrame({});
        while (emptyFrames.size() <= 1024 * 1024) {
            emptyFrames += emptyFrame;
        }
        ExpectError(emptyFrames, "BLEND_COMPRESSION_INPUT_LIMIT");
        CompressedReadFailure failedCompressedSource;
        const auto failedCompressedRead = blend::ReadHeader(failedCompressedSource);
        Require(!failedCompressedRead.HasValue() &&
            failedCompressedRead.GetError().code == "BLEND_COMPRESSION_READ_FAILED", "Compressed short read accepted");
        for (const char pointer : {'_', '-'}) {
          for (const char endian : {'v', 'V'}) {
            for (const std::uint16_t version : {299, 300, 405}) {
              std::string text = "BLENDER-v" + std::to_string(version);
              text[7] = pointer;
              text[8] = endian;
              const auto result = Parse(text);
              Require(result.HasValue(), "Valid structural header rejected by a compatibility policy");
              Require(result.GetValue().pointerSize == (pointer == '_' ? 4 : 8), "Wrong pointer size");
              Require(result.GetValue().byteOrder == (endian == 'v' ? blend::ByteOrder::Little : blend::ByteOrder::Big),
                  "Wrong endianness");
              Require(result.GetValue().version == version &&
                          result.GetValue().containerVersion == blend::BlendContainerVersion::Legacy &&
                          result.GetValue().headerSize == 12,
                  "Wrong legacy version or layout");
              if (version == 405) {
                Require(result.GetValue().SourceVersion() == "4.5", "Wrong source version");
              }
            }
          }
        }
        const std::string valid = "BLENDER-v405";
        for (std::size_t length = 0; length < valid.size(); ++length) {
            ExpectError(std::string_view(valid).substr(0, length), "BLEND_HEADER_TRUNCATED");
        }
        ExpectError("INVALID-v405", "BLEND_HEADER_MAGIC");
        ExpectError("BLENDER?v405", "BLEND_HEADER_POINTER_SIZE");
        ExpectError("BLENDER-?405", "BLEND_HEADER_ENDIANNESS");
        for (std::size_t index = 9; index < 12; ++index) {
            auto malformed = valid;
            malformed[index] = 'x';
            ExpectError(malformed, "BLEND_HEADER_VERSION");
        }
        ShortSource shortSource;
        const auto shortRead = blend::ReadHeader(shortSource);
        Require(!shortRead.HasValue() && shortRead.GetError().code == "BLEND_HEADER_READ_FAILED", "Short read accepted");
        blend::MemoryByteSource source(std::as_bytes(std::span(valid.data(), valid.size())));
        std::array<std::byte, 1> destination{};
        Require(!source.Read(12, destination), "Over-read accepted");
        Require(!source.Read(std::numeric_limits<std::uint64_t>::max(), destination), "Overflow accepted");
        Require(source.Read(12, {}), "Empty read at end rejected");
        Require(!source.Read(13, {}), "Empty read past end accepted");
        blend::MemoryByteSource emptySource({});
        Require(emptySource.Read(0, {}), "Empty source read rejected");
        const std::filesystem::path fixture(arguments[1]);
        {
            std::ofstream output(fixture, std::ios::binary);
            output.write(valid.data(), static_cast<std::streamsize>(valid.size()));
            Require(static_cast<bool>(output), "Could not write temporary fixture");
        }
        {
            blend::FileByteSource file(fixture);
            Require(file.IsOpen() && file.Size() == 12, "File source size mismatch");
            const auto header = blend::ReadHeader(file);
            Require(header.HasValue() && header.GetValue().version == 405, "File header rejected");
            Require(file.Read(11, destination) && destination[0] == std::byte{'5'}, "Random file read failed");
            Require(!file.Read(12, destination), "File over-read accepted");
            Require(file.Read(12, {}), "File empty read at end rejected");
            Require(!file.Read(std::numeric_limits<std::uint64_t>::max(), destination), "File overflow accepted");
        }
        std::filesystem::remove(fixture);
        blend::FileByteSource missing("this-file-does-not-exist.blend");
        Require(!missing.IsOpen() && !missing.Read(0, destination), "Missing file accepted");
        std::cout << "Header, compression, block and byte-source tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}