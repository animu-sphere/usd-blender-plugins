#include "blend/BlendFile.h"

#include "zlib.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
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

void CheckFileFixture(const std::filesystem::path& path, bool compressed) {
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
  Require(header.HasValue() && header.GetValue().version == 502 && header.GetValue().headerSize == 17,
      "Decoded Blender fixture has the wrong header");
  if (!compressed) {
    std::vector<std::byte> original(static_cast<std::size_t>(source.Size()));
    Require(source.Read(0, original) && original == decoded.GetValue(), "Uncompressed file bytes changed");
  }
  const auto& bytes = decoded.GetValue();
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
      Require(argumentCount == 4, "Expected temporary, compressed and uncompressed Blender fixture paths");
      CheckZlibDecoder();
      CheckGzipHeaders();
      CheckFileBytes();
      CheckFileFixture(arguments[2], true);
      CheckFileFixture(arguments[3], false);
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
        std::cout << "Header, compression and byte-source tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}