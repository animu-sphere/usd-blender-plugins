#include <blend/BlendFile.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <exception>
#include <iostream>
#include <limits>
#include <map>
#include <new>
#include <span>
#include <string_view>

namespace {

struct Options {
  std::filesystem::path path;
  blend::CompressionLimits limits;
  bool blocks = false;
  bool dna = false;
  bool objects = false;
  bool help = false;
  unsigned limitMask = 0;
};

void Usage() {
  std::cout << "Usage: blend_inspect FILE [--blocks] [--dna] [--objects]\n"
               "       [--max-input-bytes N --max-output-bytes N\n"
               "        --max-expansion-ratio N --max-window-log N]\n"
               "       blend_inspect --help\n"
               "Compressed files require all four limits; window log must be 10..30.\n"
               "Uncompressed files use their file size as the byte limit unless limits are supplied.\n";
}

bool ParseOptions(int argc, char** argv, Options& options) {
  bool positional = false;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (!positional && argument == "--") {
      positional = true;
    } else if (!positional && argument == "--help") {
      options.help = true;
    } else if (!positional && argument == "--blocks") {
      options.blocks = true;
    } else if (!positional && argument == "--dna") {
      options.dna = true;
    } else if (!positional && argument == "--objects") {
      options.objects = true;
    } else if (!positional && argument.starts_with("--max-")) {
      unsigned mask = 0;
      if (argument == "--max-input-bytes") {
        mask = 1;
      } else if (argument == "--max-output-bytes") {
        mask = 2;
      } else if (argument == "--max-expansion-ratio") {
        mask = 4;
      } else if (argument == "--max-window-log") {
        mask = 8;
      }
      if (mask == 0 || (options.limitMask & mask) != 0 || index + 1 == argc) {
        return false;
      }
      const std::string_view text(argv[++index]);
      std::uint64_t value = 0;
      const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
      if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value == 0) {
        return false;
      }
      switch (mask) {
      case 1:
        options.limits.maxInputBytes = value;
        break;
      case 2:
        options.limits.maxOutputBytes = value;
        break;
      case 4:
        options.limits.maxExpansionRatio = value;
        break;
      case 8:
        if (value < 10 || value > 30) {
          return false;
        }
        options.limits.maxWindowLog = static_cast<std::uint32_t>(value);
        break;
      }
      options.limitMask |= mask;
    } else if (!positional && argument.starts_with('-')) {
      return false;
    } else if (options.path.empty()) {
      options.path = std::filesystem::u8path(argument);
    } else {
      return false;
    }
  }
  return options.help || (!options.path.empty() && (options.limitMask == 0 || options.limitMask == 15));
}

void Quoted(std::ostream& output, std::string_view text) {
  constexpr std::string_view hex = "0123456789abcdef";
  output << '"';
  for (const unsigned char character : text) {
    if (character == '"' || character == '\\') {
      output << '\\' << static_cast<char>(character);
    } else if (character >= 32 && character < 127) {
      output << static_cast<char>(character);
    } else {
      output << "\\x" << hex[character >> 4] << hex[character & 15];
    }
  }
  output << '"';
}

void Report(const blend::Diagnostic& diagnostic, std::uint64_t offsetBase = 0,
    std::string_view byteSpace = "decoded byte") {
  const char* severity = diagnostic.severity == blend::Severity::Fatal ? "fatal" : diagnostic.severity == blend::Severity::Warning ? "warning"
                                                                                                                                   : "unsupported";
  std::cerr << diagnostic.code << " [" << severity << "]: " << diagnostic.message;
  if (diagnostic.byteOffset) {
    std::cerr << " (" << byteSpace << ' ' << offsetBase + *diagnostic.byteOffset << ')';
  }
  if (diagnostic.blockIndex) {
    std::cerr << " (block " << *diagnostic.blockIndex << ')';
  }
  if (!diagnostic.datablock.empty()) {
    std::cerr << " (datablock ";
    Quoted(std::cerr, diagnostic.datablock);
    std::cerr << ')';
  }
  std::cerr << '\n';
}

template <class Value>
bool Check(const blend::Result<Value>& result, std::uint64_t offsetBase = 0,
    std::string_view byteSpace = "decoded byte") {
  if (!result.HasValue()) {
    Report(result.GetError(), offsetBase, byteSpace);
    return false;
  }
  for (const auto& diagnostic : result.Diagnostics()) {
    Report(diagnostic, offsetBase, byteSpace);
  }
  return true;
}

int Inspect(Options options) {
  blend::FileByteSource source(options.path);
  if (!source.IsOpen()) {
    std::cerr << "BLEND_IO_OPEN [fatal]: Cannot open input file\n";
    return 1;
  }
  if (options.limitMask == 0) {
    std::array<std::byte, 7> signature{};
    const auto prefixSize = static_cast<std::size_t>(std::min<std::uint64_t>(source.Size(), signature.size()));
    constexpr std::array gzipMagic{std::byte{0x1f}, std::byte{0x8b}};
    constexpr std::array zstdMagic{std::byte{0x28}, std::byte{0xb5}, std::byte{0x2f}, std::byte{0xfd}};
    if (source.Read(0, std::span(signature).first(prefixSize)) &&
        ((prefixSize >= gzipMagic.size() && std::equal(gzipMagic.begin(), gzipMagic.end(), signature.begin())) ||
            (prefixSize >= zstdMagic.size() && std::equal(zstdMagic.begin(), zstdMagic.end(), signature.begin())))) {
      std::cerr << "BLEND_COMPRESSION_LIMITS [fatal]: Compressed input requires all four --max-* options\n";
      return 1;
    }
    options.limits = {std::max<std::uint64_t>(source.Size(), 1), std::max<std::uint64_t>(source.Size(), 1), 1, 10};
  }
  const auto decoded = blend::ReadFileBytes(source, options.limits);
  if (!Check(decoded, 0, "byte")) {
    return 1;
  }
  const auto& bytes = decoded.GetValue();
  blend::MemoryByteSource memory(bytes);
  const auto header = blend::ReadHeader(memory);
  if (!Check(header)) {
    return 1;
  }
  const auto blocks = blend::ReadBlocks(memory,
      std::min<std::uint64_t>(bytes.size() / 20 + 1, std::numeric_limits<std::uint32_t>::max()));
  if (!Check(blocks)) {
    return 1;
  }
  const auto pointers = blend::BuildPointerMap(blocks.GetValue());
  if (!Check(pointers)) {
    return 1;
  }
  const blend::BlendBlock* dnaBlock = nullptr;
  for (const auto& block : blocks.GetValue()) {
    if (block.code == std::array{'D', 'N', 'A', '1'}) {
      if (dnaBlock) {
        std::cerr << "BLEND_DNA_BLOCK [fatal]: Multiple DNA1 blocks\n";
        return 1;
      }
      dnaBlock = &block;
    }
  }
  if (!dnaBlock) {
    std::cerr << "BLEND_DNA_BLOCK [fatal]: Missing DNA1 block\n";
    return 1;
  }
  const auto dna = blend::ReadDna(std::span(bytes).subspan(static_cast<std::size_t>(dnaBlock->offset),
                                      static_cast<std::size_t>(dnaBlock->length)),
      header.GetValue());
  if (!Check(dna, dnaBlock->offset)) {
    return 1;
  }
  const auto& schema = dna.GetValue();
  const auto datablocks = blend::ListDatablocks(bytes, blocks.GetValue(), schema);
  if (!Check(datablocks)) {
    return 1;
  }
  std::map<std::string, std::size_t> counts;
  for (const auto& datablock : datablocks.GetValue()) {
    ++counts[datablock.typeName];
  }
  std::cout << "Blender version: " << header.GetValue().SourceVersion() << " (" << header.GetValue().version << ")\n"
            << "Container: " << (header.GetValue().containerVersion == blend::BlendContainerVersion::Legacy ? "legacy" : "format-1")
            << ", pointers: " << unsigned(header.GetValue().pointerSize) * 8
            << ", byte order: " << (header.GetValue().byteOrder == blend::ByteOrder::Little ? "little" : "big") << '\n'
            << "Decoded bytes: " << bytes.size() << "\nBlocks: " << blocks.GetValue().size()
            << "\nSDNA: " << schema.names.size() << " names, " << schema.types.size() << " types, "
            << schema.structs.size() << " structs\nDatablocks: " << datablocks.GetValue().size() << '\n'
            << "Objects: " << counts["Object"] << ", Meshes: " << counts["Mesh"] << ", Scenes: " << counts["Scene"] << '\n';
  for (const auto& [type, count] : counts) {
    if (count != 0) {
      std::cout << "  ";
      Quoted(std::cout, type);
      std::cout << ": " << count << '\n';
    }
  }
  if (options.blocks) {
    std::cout << "\nBlock list (payload offsets in decoded bytes):\n";
    for (std::size_t index = 0; index < blocks.GetValue().size(); ++index) {
      const auto& block = blocks.GetValue()[index];
      const auto end = std::find(block.code.begin(), block.code.end(), '\0');
      std::cout << index << ' ';
      Quoted(std::cout, std::string_view(block.code.data(), static_cast<std::size_t>(end - block.code.begin())));
      std::cout << " offset=" << block.offset << " length=" << block.length << " old=0x" << std::hex << block.oldAddress
                << std::dec << " sdna=" << block.sdnaIndex << " count=" << block.count << '\n';
    }
  }
  if (options.dna) {
    std::cout << "\nSDNA structures:\n";
    for (std::size_t index = 0; index < schema.structs.size(); ++index) {
      const auto& structure = schema.structs[index];
      const auto& type = schema.types[structure.typeIndex];
      std::cout << index << ' ';
      Quoted(std::cout, type.name);
      std::cout << " size=" << type.length << '\n';
      for (const auto& member : structure.members) {
        std::cout << "  ";
        Quoted(std::cout, schema.types[member.typeIndex].name);
        std::cout << ' ';
        Quoted(std::cout, schema.names[member.nameIndex]);
        std::cout << " offset=" << member.offset << " size=" << member.size << '\n';
      }
    }
  }
  if (options.objects) {
    std::cout << "\nRaw Object datablocks:\n";
    for (const auto& datablock : datablocks.GetValue()) {
      if (datablock.typeName == "Object") {
        std::cout << "block=" << datablock.blockIndex << " old=0x" << std::hex << datablock.oldAddress << std::dec << " name=";
        Quoted(std::cout, datablock.name);
        std::cout << '\n';
      }
    }
  }
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  try {
    Options options;
    if (!ParseOptions(argc, argv, options)) {
      std::cerr << "BLEND_INSPECT_USAGE: Invalid arguments; use --help\n";
      return 2;
    }
    if (options.help) {
      Usage();
      return 0;
    }
    return Inspect(std::move(options));
  } catch (const std::bad_alloc&) {
    std::cerr << "BLEND_INSPECT_MEMORY [fatal]: Allocation failed\n";
  } catch (const std::exception& error) {
    std::cerr << "BLEND_INSPECT_ERROR [fatal]: " << error.what() << '\n';
  }
  return 1;
}