#pragma once

#include <array>
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

struct CompressionLimits {
  std::uint64_t maxInputBytes = 0;
  std::uint64_t maxOutputBytes = 0;
  std::uint64_t maxExpansionRatio = 0;
  std::uint32_t maxWindowLog = 0;
};

Result<std::vector<std::byte>> ReadFileBytes(ByteSource& source, const CompressionLimits& limits);

struct BlendBlock {
  std::array<char, 4> code;
  std::uint64_t length;
  std::uint64_t oldAddress;
  std::uint32_t sdnaIndex;
  std::uint64_t count;
  std::uint64_t offset;
  bool operator==(const BlendBlock&) const = default;
};

Result<std::vector<BlendBlock>> ReadBlocks(ByteSource& source, std::uint64_t maxBlocks);

class PointerMap {
public:
  Result<std::optional<std::uint32_t>> Resolve(std::uint64_t oldAddress) const;

private:
  std::vector<std::pair<std::uint64_t, std::uint32_t>> entries_;
  friend Result<PointerMap> BuildPointerMap(std::span<const BlendBlock> blocks);
};

Result<PointerMap> BuildPointerMap(std::span<const BlendBlock> blocks);

struct DnaType {
  std::string name;
  std::uint16_t length;
};

struct DnaMember {
  std::uint16_t typeIndex;
  std::uint16_t nameIndex;
  std::string baseName;
  std::uint32_t pointerLevel;
  std::vector<std::uint64_t> arrayDimensions;
  std::uint64_t offset;
  std::uint64_t size;
};

struct DnaStruct {
  std::uint16_t typeIndex;
  std::vector<DnaMember> members;
  const DnaMember* FindMember(std::string_view name) const;
};

struct DnaSchema {
  std::vector<std::string> names;
  std::vector<DnaType> types;
  std::vector<DnaStruct> structs;
  const DnaStruct* FindStruct(std::string_view name) const;
};

Result<DnaSchema> ReadDna(std::span<const std::byte> payload, const Header& header);

class DnaValueView {
public:
  const DnaType& Type() const;
  std::span<const std::byte> Bytes() const;
  std::uint32_t PointerLevel() const;
  std::span<const std::uint64_t> ArrayDimensions() const;
  Result<DnaValueView> Member(std::string_view name) const;
  Result<DnaValueView> Element(std::uint64_t index) const;
  Result<std::uint64_t> Pointer() const;
  Result<std::int64_t> SignedInteger() const;
  Result<std::uint64_t> UnsignedInteger() const;
  Result<double> FloatingPoint() const;

private:
  DnaValueView(std::span<const std::byte> bytes, const DnaSchema& schema, const Header& header,
      std::uint16_t typeIndex, std::uint32_t pointerLevel, std::span<const std::uint64_t> dimensions,
      std::uint64_t offset, std::uint32_t blockIndex);
  Diagnostic Error(const char* code, const char* message) const;
  std::uint64_t IntegerBits() const;
  std::span<const std::byte> bytes_;
  const DnaSchema* schema_;
  Header header_;
  std::uint16_t typeIndex_;
  std::uint32_t pointerLevel_;
  std::span<const std::uint64_t> dimensions_;
  std::uint64_t offset_;
  std::uint32_t blockIndex_;
  friend Result<DnaValueView> ViewDnaBlock(std::span<const std::byte> bytes,
      std::span<const BlendBlock> blocks, const DnaSchema& schema, const Header& header,
      std::uint32_t blockIndex, std::uint64_t elementIndex);
};

Result<DnaValueView> ViewDnaBlock(std::span<const std::byte> bytes,
    std::span<const BlendBlock> blocks, const DnaSchema& schema, const Header& header,
    std::uint32_t blockIndex, std::uint64_t elementIndex = 0);

struct RawDatablock {
  std::uint32_t blockIndex;
  std::uint64_t oldAddress;
  std::string typeName;
  std::string name;
  bool operator==(const RawDatablock&) const = default;
};

Result<std::vector<RawDatablock>> ListDatablocks(std::span<const std::byte> bytes,
    std::span<const BlendBlock> blocks, const DnaSchema& schema);
}