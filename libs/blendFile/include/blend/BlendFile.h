#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace blend {

enum class Severity { Fatal, Warning, Unsupported };

struct Diagnostic {
    std::string code;
    Severity severity;
    std::string message;
    std::optional<std::uint64_t> byteOffset;
    std::optional<std::uint32_t> blockIndex;
    std::string datablock;
    bool recoverable;
};

template <class Value> class Result {
public:
    explicit Result(Value value, std::vector<Diagnostic> diagnostics = {})
        : storage_(std::move(value)), diagnostics_(std::move(diagnostics)) {}
    explicit Result(Diagnostic fatal) : storage_(std::move(fatal)) {}
    bool HasValue() const { return std::holds_alternative<Value>(storage_); }
    const Value& GetValue() const { return std::get<Value>(storage_); }
    const Diagnostic& GetError() const { return std::get<Diagnostic>(storage_); }
    const std::vector<Diagnostic>& Diagnostics() const { return diagnostics_; }

private:
    std::variant<Value, Diagnostic> storage_;
    std::vector<Diagnostic> diagnostics_;
};

class ByteSource {
public:
    virtual ~ByteSource() = default;
    virtual std::uint64_t Size() const = 0;
    virtual bool Read(std::uint64_t offset, std::span<std::byte> destination) = 0;
};

class MemoryByteSource final : public ByteSource {
public:
    explicit MemoryByteSource(std::span<const std::byte> bytes) : bytes_(bytes) {}
    std::uint64_t Size() const override { return bytes_.size(); }
    bool Read(std::uint64_t offset, std::span<std::byte> destination) override;

private:
    std::span<const std::byte> bytes_;
};

class FileByteSource final : public ByteSource {
public:
    explicit FileByteSource(const std::filesystem::path& path);
    bool IsOpen() const { return file_.is_open(); }
    std::uint64_t Size() const override { return size_; }
    bool Read(std::uint64_t offset, std::span<std::byte> destination) override;

private:
    std::ifstream file_;
    std::uint64_t size_ = 0;
};

enum class ByteOrder { Little, Big };
enum class BlendContainerVersion { Legacy, Blender5 };

struct Header {
    std::uint8_t pointerSize;
    ByteOrder byteOrder;
    std::uint16_t version;
    BlendContainerVersion containerVersion = BlendContainerVersion::Legacy;
    std::uint8_t headerSize = 12;
    std::string SourceVersion() const;
};

Result<Header> ReadHeader(ByteSource& source);

}