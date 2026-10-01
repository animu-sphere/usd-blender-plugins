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
      "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xb8\x1f\x72\xdc"
      "\x70\x8a\x11\x80\x00\x00";
  const auto deflatedHeader = Parse(std::string_view(deflated, sizeof(deflated) - 1));
  Require(deflatedHeader.HasValue() && deflatedHeader.GetValue().version == 502 &&
              deflatedHeader.GetValue().headerSize == 17,
      "High-ratio gzip header rejected");
}

class ShortSource final : public blend::ByteSource {
public:
    std::uint64_t Size() const override { return 12; }
    bool Read(std::uint64_t, std::span<std::byte>) override { return false; }
};
}

int main(int argumentCount, char** arguments) {
    try {
        Require(argumentCount == 3, "Expected temporary and Blender fixture paths");
        CheckZlibDecoder();
        CheckGzipHeaders();
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
        std::cout << "Header and byte-source tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}