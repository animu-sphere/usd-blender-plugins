#include "blend/BlendFile.h"

#include "zstd.h"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>

namespace blend {
namespace {
Diagnostic Fatal(const char* code, const char* message, std::uint64_t offset) {
    return Diagnostic{code, Severity::Fatal, message, offset, std::nullopt, {}, false};
}

Result<std::size_t> ReadCompressedHeader(ByteSource& source, std::span<std::byte> bytes) {
    std::unique_ptr<ZSTD_DStream, decltype(&ZSTD_freeDStream)> decoder(ZSTD_createDStream(), ZSTD_freeDStream);
    if (!decoder || ZSTD_isError(ZSTD_DCtx_setParameter(decoder.get(), ZSTD_d_windowLogMax, 23))) {
        return Result<std::size_t>(Fatal("BLEND_COMPRESSION_DECODER", "Could not initialize the decoder", 0));
    }
    constexpr std::uint64_t inputLimit = 1024 * 1024;
    std::array<std::byte, 4096> chunk{};
    ZSTD_inBuffer input{chunk.data(), 0, 0};
    ZSTD_outBuffer output{bytes.data(), 12, 0};
    std::uint64_t offset = 0;
    std::size_t remaining = 1;
    bool drain = false;
    while (output.pos < output.size) {
        if (input.pos == input.size && !drain) {
            if (offset == source.Size()) {
                if (remaining == 0) {
                    return Result<std::size_t>(output.pos);
                }
                return Result<std::size_t>(Fatal("BLEND_COMPRESSION_TRUNCATED", "Incomplete compressed header", offset));
            }
            if (offset == inputLimit) {
                return Result<std::size_t>(Fatal("BLEND_COMPRESSION_INPUT_LIMIT", "Compressed header exceeds the input budget", offset));
            }
            const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
                chunk.size(), std::min(source.Size() - offset, inputLimit - offset)));
            if (!source.Read(offset, std::span(chunk).first(count))) {
                return Result<std::size_t>(Fatal("BLEND_COMPRESSION_READ_FAILED", "Could not read compressed bytes", offset));
            }
            offset += count;
            input.size = count;
            input.pos = 0;
        }
        const auto previousInput = input.pos;
        const auto previousOutput = output.pos;
        drain = false;
        remaining = ZSTD_decompressStream(decoder.get(), &output, &input);
        if (ZSTD_isError(remaining)) {
            const auto code = ZSTD_getErrorCode(remaining) == ZSTD_error_frameParameter_windowTooLarge
                ? "BLEND_COMPRESSION_WINDOW_LIMIT" : "BLEND_COMPRESSION_INVALID";
            return Result<std::size_t>(Fatal(code, "Invalid stream or decoder window exceeds 8 MiB", offset - input.size + input.pos));
        }
        if (output.pos == 12 && output.size == 12 && bytes[7] != std::byte{'_'} && bytes[7] != std::byte{'-'}) {
            output.size = bytes.size();
            drain = remaining != 0;
        }
        if (input.pos == previousInput && output.pos == previousOutput) {
            if (input.pos == input.size) {
                continue;
            }
            return Result<std::size_t>(Fatal("BLEND_COMPRESSION_INVALID", "Compressed header decoder made no progress", offset - input.size + input.pos));
        }
    }
    return Result<std::size_t>(output.pos);
}
}

bool MemoryByteSource::Read(std::uint64_t offset, std::span<std::byte> destination) {
    if (offset > Size() || destination.size() > Size() - offset) {
        return false;
    }
    if (destination.empty()) {
        return true;
    }
    std::copy_n(bytes_.begin() + static_cast<std::size_t>(offset), destination.size(), destination.begin());
    return true;
}

FileByteSource::FileByteSource(const std::filesystem::path& path)
    : file_(path, std::ios::binary | std::ios::ate) {
    if (file_) {
        const auto end = file_.tellg();
        if (end >= 0) {
            size_ = static_cast<std::uint64_t>(end);
        } else {
            file_.close();
        }
    }
}

bool FileByteSource::Read(std::uint64_t offset, std::span<std::byte> destination) {
    if (!IsOpen() || offset > size_ || destination.size() > size_ - offset ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
        destination.size() > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) {
        return false;
    }
    if (destination.empty()) {
        return true;
    }
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(offset));
    if (!file_) {
        return false;
    }
    file_.read(reinterpret_cast<char*>(destination.data()), static_cast<std::streamsize>(destination.size()));
    return file_.gcount() == static_cast<std::streamsize>(destination.size());
}

std::string Header::SourceVersion() const {
    return std::to_string(version / 100) + "." + std::to_string(version % 100);
}

Result<Header> ReadHeader(ByteSource& source) {
    const auto fail = [](const char* code, const char* message, std::uint64_t offset) {
        return Result<Header>(Fatal(code, message, offset));
    };
    std::array<std::byte, 17> bytes{};
    auto count = static_cast<std::size_t>(std::min<std::uint64_t>(source.Size(), 12));
    if (!source.Read(0, std::span(bytes).first(count))) {
        return fail("BLEND_HEADER_READ_FAILED", "Could not read the header", 0);
    }
    constexpr std::array zstdMagic{std::byte{0x28}, std::byte{0xb5}, std::byte{0x2f}, std::byte{0xfd}};
    const bool compressed = count >= zstdMagic.size() && std::equal(zstdMagic.begin(), zstdMagic.end(), bytes.begin());
    if (compressed) {
        const auto result = ReadCompressedHeader(source, bytes);
        if (!result.HasValue()) {
            return Result<Header>(result.GetError());
        }
        count = result.GetValue();
    }
    if (count < 12) {
        return fail("BLEND_HEADER_TRUNCATED", "Expected at least twelve header bytes", count);
    }
    constexpr std::string_view magic = "BLENDER";
    for (std::size_t index = 0; index < magic.size(); ++index) {
        if (bytes[index] != static_cast<std::byte>(magic[index])) {
            return fail("BLEND_HEADER_MAGIC", "Invalid BLENDER signature", index);
        }
    }
    const auto pointer = std::to_integer<char>(bytes[7]);
    const bool modern = pointer >= '0' && pointer <= '9';
    if (modern) {
        if (bytes[7] != std::byte{'1'} || bytes[8] != std::byte{'7'}) {
            return fail("BLEND_HEADER_SIZE", "Unsupported header size", 7);
        }
        if (bytes[9] != std::byte{'-'} || bytes[10] != std::byte{'0'} || bytes[11] != std::byte{'1'}) {
            return fail("BLEND_HEADER_FORMAT_VERSION", "Unsupported container format", 9);
        }
        if (!compressed) {
            if (source.Size() < bytes.size()) {
                return fail("BLEND_HEADER_TRUNCATED", "Expected a seventeen-byte header", source.Size());
            }
            if (!source.Read(12, std::span(bytes).subspan(12))) {
                return fail("BLEND_HEADER_READ_FAILED", "Could not read the extended header", 12);
            }
        } else if (count < bytes.size()) {
            return fail("BLEND_HEADER_TRUNCATED", "Expected a seventeen-byte header", count);
        }
        if (bytes[12] != std::byte{'v'}) {
            return fail("BLEND_HEADER_ENDIANNESS", "Extended headers require little endian", 12);
        }
        std::uint16_t version = 0;
        for (std::size_t index = 13; index < bytes.size(); ++index) {
            const auto digit = std::to_integer<char>(bytes[index]);
            if (digit < '0' || digit > '9') {
                return fail("BLEND_HEADER_VERSION", "Version must contain four ASCII digits", index);
            }
            version = static_cast<std::uint16_t>(version * 10 + digit - '0');
        }
        return Result<Header>(Header{8, ByteOrder::Little, version, BlendContainerVersion::Blender5, 17});
    }
    if (pointer != '_' && pointer != '-') {
        return fail("BLEND_HEADER_POINTER_SIZE", "Unsupported legacy pointer-size marker", 7);
    }
    const auto endian = std::to_integer<char>(bytes[8]);
    if (endian != 'v' && endian != 'V') {
        return fail("BLEND_HEADER_ENDIANNESS", "Invalid byte-order marker", 8);
    }
    std::uint16_t version = 0;
    for (std::size_t index = 9; index < 12; ++index) {
        const auto digit = std::to_integer<char>(bytes[index]);
        if (digit < '0' || digit > '9') {
            return fail("BLEND_HEADER_VERSION", "Version must contain three ASCII digits", index);
        }
        version = static_cast<std::uint16_t>(version * 10 + digit - '0');
    }
    return Result<Header>(Header{static_cast<std::uint8_t>(pointer == '_' ? 4 : 8),
        endian == 'v' ? ByteOrder::Little : ByteOrder::Big, version});
}

}