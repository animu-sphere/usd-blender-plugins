#include "blend/BlendFile.h"

#include "zlib.h"
#include "zstd.h"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>

namespace blend {
namespace {
Diagnostic Fatal(const char* code, const char* message, std::uint64_t offset) {
    return Diagnostic{code, Severity::Fatal, message, offset, std::nullopt, {}, false};
}

Result<std::size_t> ReadGzipHeader(ByteSource& source, std::span<std::byte> bytes) {
  z_stream stream{};
  if (inflateInit2(&stream, 15 + 16) != Z_OK) {
    return Result<std::size_t>(Fatal("BLEND_COMPRESSION_DECODER", "Could not initialize the gzip decoder", 0));
  }
  std::unique_ptr<z_stream, decltype(&inflateEnd)> decoder(&stream, inflateEnd);
  constexpr std::uint64_t inputLimit = 1024 * 1024;
  std::array<std::byte, 4096> chunk{};
  std::uint64_t offset = 0;
  std::size_t outputSize = 12;
  std::size_t outputCount = 0;
  int status = Z_OK;
  bool drain = false;
  while (outputCount < outputSize) {
    if (stream.avail_in == 0 && !drain) {
      if (offset == source.Size()) {
        if (status == Z_STREAM_END) {
          return Result<std::size_t>(outputCount);
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
      stream.next_in = reinterpret_cast<Bytef*>(chunk.data());
      stream.avail_in = static_cast<uInt>(count);
    }
    if (status == Z_STREAM_END && inflateReset2(&stream, 15 + 16) != Z_OK) {
      return Result<std::size_t>(Fatal("BLEND_COMPRESSION_DECODER", "Could not reset the gzip decoder", offset - stream.avail_in));
    }
    const auto previousInput = stream.avail_in;
    const auto previousOutput = outputCount;
    stream.next_out = reinterpret_cast<Bytef*>(bytes.data() + outputCount);
    stream.avail_out = static_cast<uInt>(outputSize - outputCount);
    drain = false;
    status = inflate(&stream, Z_BLOCK);
    outputCount = outputSize - stream.avail_out;
    if (status == Z_MEM_ERROR) {
      return Result<std::size_t>(Fatal("BLEND_COMPRESSION_DECODER", "Could not allocate the gzip decoder window", offset - stream.avail_in));
    }
    if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) {
      return Result<std::size_t>(Fatal("BLEND_COMPRESSION_INVALID", "Invalid gzip stream", offset - stream.avail_in));
    }
    if (outputCount == 12 && outputSize == 12 && bytes[7] != std::byte{'_'} && bytes[7] != std::byte{'-'}) {
      outputSize = bytes.size();
      drain = status != Z_STREAM_END;
    }
    if (stream.avail_in == previousInput && outputCount == previousOutput && status != Z_STREAM_END) {
      if (stream.avail_in == 0) {
        continue;
      }
      return Result<std::size_t>(Fatal("BLEND_COMPRESSION_INVALID", "Gzip header decoder made no progress", offset - stream.avail_in));
    }
  }
  return Result<std::size_t>(outputCount);
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

Result<std::vector<std::byte>> ReadFileBytes(ByteSource& source, const CompressionLimits& limits) {
  using BytesResult = Result<std::vector<std::byte>>;
  const auto fail = [](const char* code, const char* message, std::uint64_t offset) {
    return BytesResult(Fatal(code, message, offset));
  };
  if (limits.maxInputBytes == 0 || limits.maxOutputBytes == 0 || limits.maxExpansionRatio == 0 ||
      limits.maxWindowLog < 10 || limits.maxWindowLog > 30) {
    return fail("BLEND_COMPRESSION_LIMITS", "Positive byte and ratio limits and a window log from 10 to 30 are required", 0);
  }
  const auto sourceSize = source.Size();
  if (sourceSize > limits.maxInputBytes) {
    return fail("BLEND_COMPRESSION_INPUT_LIMIT", "Source exceeds the input budget", 0);
  }
  try {
    std::array<std::byte, 4096> inputBytes{};
    const auto prefixSize = static_cast<std::size_t>(std::min<std::uint64_t>(sourceSize, 7));
    if (!source.Read(0, std::span(inputBytes).first(prefixSize))) {
      return fail("BLEND_COMPRESSION_READ_FAILED", "Could not read source bytes", 0);
    }
    constexpr std::array gzipMagic{std::byte{0x1f}, std::byte{0x8b}};
    constexpr std::array zstdMagic{std::byte{0x28}, std::byte{0xb5}, std::byte{0x2f}, std::byte{0xfd}};
    const bool gzip = prefixSize >= gzipMagic.size() && std::equal(gzipMagic.begin(), gzipMagic.end(), inputBytes.begin());
    const bool zstd = prefixSize >= zstdMagic.size() && std::equal(zstdMagic.begin(), zstdMagic.end(), inputBytes.begin());
    const bool compressed = gzip || zstd;
    const auto ratioLimit = !compressed || sourceSize > std::numeric_limits<std::uint64_t>::max() / limits.maxExpansionRatio
                                ? std::numeric_limits<std::uint64_t>::max()
                                : sourceSize * limits.maxExpansionRatio;
    std::vector<std::byte> bytes;
    const auto outputLimit = std::min({limits.maxOutputBytes, ratioLimit, static_cast<std::uint64_t>(bytes.max_size())});
    const auto append = [&](std::span<const std::byte> chunk, std::uint64_t offset) -> std::optional<Diagnostic> {
      if (chunk.size() > outputLimit - bytes.size()) {
        const auto code = ratioLimit < limits.maxOutputBytes && ratioLimit <= bytes.max_size()
                              ? "BLEND_COMPRESSION_RATIO_LIMIT"
                              : "BLEND_COMPRESSION_OUTPUT_LIMIT";
        return Fatal(code, "Decoded bytes exceed the output or expansion ratio budget", offset);
      }
      const auto required = bytes.size() + chunk.size();
      if (required > bytes.capacity()) {
        const auto capacity = bytes.capacity();
        const auto grown = capacity + std::min<std::uint64_t>(capacity, outputLimit - capacity);
        bytes.reserve(static_cast<std::size_t>(std::max<std::uint64_t>(required, grown)));
      }
      bytes.insert(bytes.end(), chunk.begin(), chunk.end());
      return std::nullopt;
    };
    z_stream stream{};
    std::unique_ptr<z_stream, decltype(&inflateEnd)> gzipDecoder(nullptr, inflateEnd);
    std::unique_ptr<ZSTD_DStream, decltype(&ZSTD_freeDStream)> zstdDecoder(nullptr, ZSTD_freeDStream);
    if (gzip) {
      if (inflateInit2(&stream, 15 + 16) != Z_OK) {
        return fail("BLEND_COMPRESSION_DECODER", "Could not initialize the gzip decoder", 0);
      }
      gzipDecoder.reset(&stream);
    } else if (zstd) {
      zstdDecoder.reset(ZSTD_createDStream());
      if (!zstdDecoder || ZSTD_isError(ZSTD_DCtx_setParameter(
                              zstdDecoder.get(), ZSTD_d_windowLogMax, static_cast<int>(limits.maxWindowLog)))) {
        return fail("BLEND_COMPRESSION_DECODER", "Could not initialize the Zstandard decoder", 0);
      }
    }
    std::array<std::byte, 4096> outputBytes{};
    ZSTD_inBuffer input{inputBytes.data(), 0, 0};
    std::uint64_t offset = 0;
    bool complete = false;
    bool drain = false;
    while (true) {
      if (input.pos == input.size && !drain) {
        if (offset == sourceSize) {
          if (compressed && !complete) {
            return fail("BLEND_COMPRESSION_TRUNCATED", "Compressed stream ended before its trailer", offset);
          }
          break;
        }
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(inputBytes.size(), sourceSize - offset));
        if (!source.Read(offset, std::span(inputBytes).first(count))) {
          return fail("BLEND_COMPRESSION_READ_FAILED", "Could not read source bytes", offset);
        }
        offset += count;
        input.size = count;
        input.pos = 0;
      }
      if (!compressed) {
        if (const auto error = append(std::span(inputBytes).first(input.size), offset - input.size)) {
          return BytesResult(*error);
        }
        input.pos = input.size;
        continue;
      }
      const auto previousInput = input.pos;
      const auto budget = outputLimit - bytes.size();
      const auto capacity = static_cast<std::size_t>(budget < outputBytes.size() ? budget + 1 : outputBytes.size());
      std::size_t count = 0;
      if (gzip) {
        if (complete && inflateReset2(&stream, 15 + 16) != Z_OK) {
          return fail("BLEND_COMPRESSION_DECODER", "Could not reset the gzip decoder", offset - input.size + input.pos);
        }
        stream.next_in = reinterpret_cast<Bytef*>(inputBytes.data() + input.pos);
        stream.avail_in = static_cast<uInt>(input.size - input.pos);
        stream.next_out = reinterpret_cast<Bytef*>(outputBytes.data());
        stream.avail_out = static_cast<uInt>(capacity);
        const auto status = inflate(&stream, Z_NO_FLUSH);
        input.pos = input.size - stream.avail_in;
        count = capacity - stream.avail_out;
        if (status == Z_MEM_ERROR) {
          return fail("BLEND_COMPRESSION_DECODER", "Could not allocate the gzip decoder window", offset - input.size + input.pos);
        }
        if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) {
          return fail("BLEND_COMPRESSION_INVALID", "Invalid gzip stream or checksum", offset - input.size + input.pos);
        }
        complete = status == Z_STREAM_END;
      } else {
        ZSTD_outBuffer output{outputBytes.data(), capacity, 0};
        const auto remaining = ZSTD_decompressStream(zstdDecoder.get(), &output, &input);
        if (ZSTD_isError(remaining)) {
          const auto code = ZSTD_getErrorCode(remaining) == ZSTD_error_frameParameter_windowTooLarge
                                ? "BLEND_COMPRESSION_WINDOW_LIMIT"
                                : "BLEND_COMPRESSION_INVALID";
          return fail(code, "Invalid Zstandard stream, checksum or decoder window", offset - input.size + input.pos);
        }
        count = output.pos;
        complete = remaining == 0;
      }
      if (const auto error = append(std::span(outputBytes).first(count), offset - input.size + input.pos)) {
        return BytesResult(*error);
      }
      drain = count == capacity && !complete;
      if (input.pos == previousInput && count == 0) {
        if (input.pos != input.size) {
          return fail("BLEND_COMPRESSION_INVALID", "Decoder made no progress", offset - input.size + input.pos);
        }
        drain = false;
      }
    }
    constexpr std::string_view magic = "BLENDER";
    if (bytes.size() >= magic.size() &&
        !std::equal(magic.begin(), magic.end(), bytes.begin(), [](char character, std::byte value) {
          return static_cast<std::byte>(character) == value;
        })) {
      return fail("BLEND_HEADER_MAGIC", "Decoded bytes do not begin with BLENDER", 0);
    }
    MemoryByteSource decoded(bytes);
    const auto header = ReadHeader(decoded);
    if (!header.HasValue()) {
      return BytesResult(header.GetError());
    }
    return BytesResult(std::move(bytes));
  } catch (const std::bad_alloc&) {
    return fail("BLEND_COMPRESSION_DECODER", "Could not allocate the decoded byte buffer", 0);
  } catch (const std::length_error&) {
    return fail("BLEND_COMPRESSION_OUTPUT_LIMIT", "Decoded byte buffer exceeds the addressable size", 0);
  }
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
    constexpr std::array gzipMagic{std::byte{0x1f}, std::byte{0x8b}};
    const bool gzip = count >= gzipMagic.size() && std::equal(gzipMagic.begin(), gzipMagic.end(), bytes.begin());
    const bool compressed = gzip || (count >= zstdMagic.size() && std::equal(zstdMagic.begin(), zstdMagic.end(), bytes.begin()));
    if (compressed) {
      const auto result = gzip ? ReadGzipHeader(source, bytes) : ReadCompressedHeader(source, bytes);
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