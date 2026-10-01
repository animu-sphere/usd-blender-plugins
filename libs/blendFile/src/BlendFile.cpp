#include "blend/BlendFile.h"

#include <algorithm>
#include <array>
#include <limits>

namespace blend {

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
        return Result<Header>(Diagnostic{code, Severity::Fatal, message, offset, std::nullopt, {}, false});
    };
    std::array<std::byte, 12> bytes{};
    if (source.Size() < bytes.size()) {
        return fail("BLEND_HEADER_TRUNCATED", "Expected a twelve-byte legacy header", source.Size());
    }
    if (!source.Read(0, bytes)) {
        return fail("BLEND_HEADER_READ_FAILED", "Could not read the header", 0);
    }
    constexpr std::string_view magic = "BLENDER";
    for (std::size_t index = 0; index < magic.size(); ++index) {
        if (bytes[index] != static_cast<std::byte>(magic[index])) {
            return fail("BLEND_HEADER_MAGIC", "Invalid BLENDER signature", index);
        }
    }
    const auto pointer = std::to_integer<char>(bytes[7]);
    if (pointer != '_' && pointer != '-') {
        return fail("BLEND_HEADER_POINTER_SIZE", "Unsupported legacy pointer-size marker", 7);
    }
    const auto endian = std::to_integer<char>(bytes[8]);
    if (endian != 'v' && endian != 'V') {
        return fail("BLEND_HEADER_ENDIANNESS", "Invalid byte-order marker", 8);
    }
    std::uint16_t version = 0;
    for (std::size_t index = 9; index < bytes.size(); ++index) {
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