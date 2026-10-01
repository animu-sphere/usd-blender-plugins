#include "blend/BlendFile.h"

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

class CompressedReadFailure final : public blend::ByteSource {
public:
    std::uint64_t Size() const override { return 32; }
    bool Read(std::uint64_t offset, std::span<std::byte> destination) override {
        if (offset != 0 || destination.size() != 12) {
            return false;
        }
        const auto prefix = RawZstdFrame("BLENDER17-01v0502");
        std::copy_n(reinterpret_cast<const std::byte*>(prefix.data()), destination.size(), destination.begin());
        return true;
    }
};

class ShortSource final : public blend::ByteSource {
public:
    std::uint64_t Size() const override { return 12; }
    bool Read(std::uint64_t, std::span<std::byte>) override { return false; }
};
}

int main(int argumentCount, char** arguments) {
    try {
        Require(argumentCount == 3, "Expected temporary and Blender fixture paths");
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