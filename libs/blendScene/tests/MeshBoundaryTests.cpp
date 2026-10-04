#include <blendScene/Decode.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {

void Require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <class Value>
Value Take(const blend::Result<Value>& result) {
  Require(result.HasValue(), result.HasValue() ? "" : result.GetError().code + ": " + result.GetError().message);
  return result.GetValue();
}

struct Fixture {
  explicit Fixture(const std::filesystem::path& path) {
    blend::FileByteSource source(path);
    bytes = Take(blend::ReadFileBytes(source, {4 * 1024 * 1024, 4 * 1024 * 1024, 16, 23}));
    blend::MemoryByteSource memory(bytes);
    header = Take(blend::ReadHeader(memory));
    blocks = Take(blend::ReadBlocks(memory, 10000));
    const auto dna = std::find_if(blocks.begin(), blocks.end(),
        [](const auto& block) { return block.code == std::array<char, 4>{'D', 'N', 'A', '1'}; });
    Require(dna != blocks.end(), "Mesh boundary fixture has DNA1");
    schema = Take(blend::ReadDna(Payload(*dna), header));
  }

  std::span<const std::byte> Payload(const blend::BlendBlock& block) const {
    return std::span<const std::byte>(bytes).subspan(
        static_cast<std::size_t>(block.offset), static_cast<std::size_t>(block.length));
  }

  blend::DnaValueView View(std::uint32_t index, std::uint64_t element = 0) const {
    return Take(blend::ViewDnaBlock(bytes, blocks, schema, header, index, element));
  }

  std::string_view Type(const blend::BlendBlock& block) const {
    return schema.types[schema.structs[block.sdnaIndex].typeIndex].name;
  }

  std::uint32_t Resolve(const blend::PointerMap& map, const blend::DnaValueView& pointer) const {
    const auto index = Take(map.Resolve(Take(pointer.Pointer())));
    Require(index.has_value(), "Mesh boundary storage pointer is nonnull");
    return *index;
  }

  std::vector<std::byte> bytes;
  std::vector<blend::BlendBlock> blocks;
  blend::Header header;
  blend::DnaSchema schema;
};

template <class Value>
void Failure(const blend::Result<Value>& result, const std::string& code,
    const Fixture& fixture, std::uint32_t index) {
  Require(!result.HasValue(), code + " must return no partial result");
  const auto& error = result.GetError();
  Require(error.code == code && error.severity == blend::Severity::Fatal && !error.recoverable &&
              error.blockIndex == index && error.byteOffset == fixture.blocks[index].offset,
      code + " retains exact fatal source context; got " + error.code + " at " +
          (error.blockIndex ? std::to_string(*error.blockIndex) : "none") +
          ", expected " + std::to_string(index));
}

void SkipVectors(std::istream& input, std::size_t count, bool normals) {
  for (std::size_t index = 0; index < count; ++index) {
    blend::Vector3 vector{};
    for (auto& value : vector) {
      Require(static_cast<bool>(input >> value) && std::isfinite(value), "Custom normal oracle has finite vectors");
    }
    if (normals) {
      Require(std::abs(std::hypot(vector[0], vector[1], vector[2]) - 1) <= 2e-6,
          "Saved custom normal oracle has unit-length vectors");
    }
  }
}

void StorePointer(Fixture& fixture, const blend::DnaValueView& view, std::uint64_t address) {
  const auto offset = static_cast<std::size_t>(view.Bytes().data() - fixture.bytes.data());
  for (std::size_t byte = 0; byte < fixture.header.pointerSize; ++byte) {
    const auto shift = fixture.header.byteOrder == blend::ByteOrder::Little ? byte : fixture.header.pointerSize - 1 - byte;
    fixture.bytes[offset + byte] = static_cast<std::byte>((address >> (8 * shift)) & 255);
  }
}

void RejectCollision(const Fixture& fixture) {
  const auto pointers = blend::BuildPointerMap(fixture.blocks);
  Require(!pointers.HasValue() && pointers.GetError().blockIndex.has_value(), "Mutation retains duplicate addresses");
  const auto index = *pointers.GetError().blockIndex;
  Failure(blend::SelectScene(fixture.bytes, fixture.blocks, fixture.schema, fixture.header),
      "BLEND_POINTER_DUPLICATE", fixture, index);
  Failure(blend::SelectSceneObjectValues(fixture.bytes, fixture.blocks, fixture.schema, fixture.header, {10000, 64}),
      "BLEND_POINTER_DUPLICATE", fixture, index);
  Failure(blend::DecodeScene(fixture.bytes, fixture.blocks, fixture.schema, fixture.header, {10000, 64}),
      "BLEND_POINTER_DUPLICATE", fixture, index);
}

void CheckCustom(const std::filesystem::path& path) {
  const Fixture fixture(path);
  const auto map = Take(blend::BuildPointerMap(fixture.blocks));
  const auto selected = Take(blend::SelectSceneObjectValues(
      fixture.bytes, fixture.blocks, fixture.schema, fixture.header, {10000, 64}));
  Require(selected.objects.size() == 1 && selected.objects[0].sourceName == "Custom" &&
              selected.objects[0].dataBlockIndex.has_value(),
      "Custom normal fixture has one selected Mesh");
  const auto mesh = fixture.View(*selected.objects[0].dataBlockIndex);
  const auto corners = Take(Take(mesh.Member("totloop")).SignedInteger());
  const bool modern = fixture.header.version >= 500;
  const auto storage = Take(mesh.Member(modern ? "attribute_storage" : "ldata"));
  const auto records = fixture.Resolve(map, Take(storage.Member(modern ? "dna_attributes" : "layers")));
  const auto count = Take(Take(storage.Member(modern ? "dna_attributes_num" : "totlayer")).SignedInteger());
  std::optional<std::uint32_t> values;
  for (std::int64_t element = 0; element < count; ++element) {
    const auto attribute = fixture.View(records, static_cast<std::uint64_t>(element));
    const auto type = Take(Take(attribute.Member(modern ? "data_type" : "type")).SignedInteger());
    if (type != (modern ? 2 : 41)) {
      continue;
    }
    Require(!values, "Custom normal fixture has exactly one packed layer");
    const auto name = Take(attribute.Member("name"));
    auto nameBytes = name.Bytes();
    if (modern) {
      const auto nameIndex = fixture.Resolve(map, name);
      Require(fixture.Type(fixture.blocks[nameIndex]) == "raw_data" && fixture.blocks[nameIndex].count == 1,
          "Packed attribute name uses raw character storage");
      nameBytes = fixture.Payload(fixture.blocks[nameIndex]);
    } else {
      Require(name.Type().name == "char" && name.Type().length == 1 &&
                  name.PointerLevel() == 0 && name.ArrayDimensions().size() == 1,
          "Packed layer name uses a character array");
    }
    const auto end = std::find(nameBytes.begin(), nameBytes.end(), std::byte{0});
    Require(end != nameBytes.end() &&
                std::string(reinterpret_cast<const char*>(nameBytes.data()), static_cast<std::size_t>(end - nameBytes.begin())) == (modern ? "custom_normal" : ""),
        "Packed corner storage has the observed modern name or unnamed legacy type-41 layer");
    if (modern) {
      Require(Take(Take(attribute.Member("domain")).SignedInteger()) == 3 &&
                  Take(Take(attribute.Member("storage_type")).SignedInteger()) == 0,
          "Modern packed custom normals use dense corner storage");
    } else {
      Require(Take(Take(attribute.Member("flag")).SignedInteger()) == 0,
          "Legacy packed custom normals have no storage flags");
    }
    auto data = fixture.Resolve(map, Take(attribute.Member("data")));
    if (modern) {
      Require(fixture.Type(fixture.blocks[data]) == "AttributeArray" && fixture.blocks[data].count == 1,
          "Packed attribute references one AttributeArray");
      const auto array = fixture.View(data);
      Require(Take(Take(array.Member("size")).SignedInteger()) == corners &&
                  Take(Take(array.Member("is_single")).SignedInteger()) == 0,
          "Packed custom AttributeArray is dense and has one value per corner");
      data = fixture.Resolve(map, Take(array.Member("data")));
    }
    values = data;
  }
  Require(values.has_value() && corners > 0, "Custom normal fixture contains packed normals");
  const auto& block = fixture.blocks[*values];
  const auto raw = fixture.Type(block) == "raw_data";
  Require(block.length == static_cast<std::uint64_t>(corners) * 4 &&
              (raw ? block.count == 1 : fixture.Type(block) == "vec2s" && block.count == static_cast<std::uint64_t>(corners)),
      "Saved packed normals are exact signed-short pairs, not float3 vectors");

  auto oraclePath = path;
  oraclePath.replace_extension(".oracle.txt");
  std::ifstream oracle(oraclePath);
  std::string line;
  Require(static_cast<bool>(std::getline(oracle, line)) && line == "BLEND_NORMALS_ORACLE 2", "Packed normal oracle version");
  Require(static_cast<bool>(std::getline(oracle, line)), "Packed oracle Blender version");
  double scale = 0;
  std::size_t objects = 0, points = 0, faces = 0, loops = 0;
  std::string name;
  Require(static_cast<bool>(oracle >> scale >> objects >> std::quoted(name) >> points >> faces >> loops) &&
              scale == selected.scene.metadata.sourceUnitScale && objects == 1 && name == "Custom" &&
              loops == static_cast<std::size_t>(corners) &&
              points == static_cast<std::size_t>(Take(Take(mesh.Member("totvert")).SignedInteger())) &&
              faces == static_cast<std::size_t>(Take(Take(mesh.Member("totpoly")).SignedInteger())),
      "Packed oracle matches saved Mesh counts and source units");
  SkipVectors(oracle, points, false);
  std::int64_t total = 0;
  for (std::size_t face = 0; face < faces; ++face) {
    std::int32_t count = 0;
    Require(static_cast<bool>(oracle >> count) && count >= 3, "Packed oracle polygon size");
    total += count;
  }
  Require(total == corners, "Packed oracle topology has one normal per corner");
  for (std::size_t corner = 0; corner < loops; ++corner) {
    std::int32_t vertex = -1;
    Require(static_cast<bool>(oracle >> vertex) && vertex >= 0 && static_cast<std::size_t>(vertex) < points,
        "Packed oracle corner vertex is in range");
  }
  SkipVectors(oracle, loops, true);
  std::size_t packed = 0;
  Require(static_cast<bool>(oracle >> name >> packed) && name == "PACKED_CUSTOM_NORMALS" && packed == loops,
      "Packed oracle contains one short pair per corner");
  bool positive = false, negative = false, automatic = false;
  const auto payload = fixture.Payload(block);
  for (std::size_t corner = 0; corner < loops; ++corner) {
    std::array<std::int64_t, 2> pair{};
    for (std::size_t component = 0; component < 2; ++component) {
      std::int32_t expected = 0;
      Require(static_cast<bool>(oracle >> expected), "Packed oracle short pair");
      if (raw) {
        const auto offset = corner * 4 + component * 2;
        const auto low = fixture.header.byteOrder == blend::ByteOrder::Little ? offset : offset + 1;
        const auto high = fixture.header.byteOrder == blend::ByteOrder::Little ? offset + 1 : offset;
        const auto bits = static_cast<std::uint16_t>(std::to_integer<std::uint16_t>(payload[low]) |
                                                     (std::to_integer<std::uint16_t>(payload[high]) << 8));
        pair[component] = std::bit_cast<std::int16_t>(bits);
      } else {
        const auto value = Take(fixture.View(*values, corner).Member(component == 0 ? "x" : "y"));
        Require(value.Type().name == "short" && value.Type().length == 2 &&
                    value.PointerLevel() == 0 && value.ArrayDimensions().empty(),
            "Structured packed normal components are scalar shorts");
        pair[component] = Take(value.SignedInteger());
      }
      Require(pair[component] == expected, "Saved packed pair exactly matches Blender RNA oracle");
      positive = positive || expected > 0;
      negative = negative || expected < 0;
    }
    automatic = automatic || pair == std::array<std::int64_t, 2>{0, 0};
  }
  oracle >> std::ws;
  Require(oracle.eof() && positive && negative && automatic, "Packed normals cover signed data and automatic-zero entries");
  Require(blend::DecodeScene(fixture.bytes, fixture.blocks, fixture.schema, fixture.header, {10000, 64}).HasValue(),
      "Saved packed normal storage decodes into an owning Scene");
  auto reversed = fixture;
  std::reverse(reversed.blocks.begin(), reversed.blocks.end());
  const auto repeated = blend::DecodeScene(reversed.bytes, reversed.blocks, reversed.schema, reversed.header, {10000, 64});
  Require(repeated.HasValue(), "Reversed packed normal storage decodes");
}

void CheckRepeatedAddresses(const std::filesystem::path& path) {
  Fixture fixture(path);
  const auto constant = path.stem() == "constant";
  const auto valueType = constant ? "AttributeSingle" : "AttributeArray";
  std::map<std::uint64_t, std::vector<std::uint32_t>> addresses;
  std::vector<std::uint32_t> meshes;
  for (std::size_t index = 0; index < fixture.blocks.size(); ++index) {
    const auto& block = fixture.blocks[index];
    if (block.code == std::array<char, 4>{'D', 'A', 'T', 'A'} && block.oldAddress != 0) {
      addresses[block.oldAddress].push_back(static_cast<std::uint32_t>(index));
    }
    if (block.code == std::array<char, 4>{'M', 'E', 0, 0}) {
      meshes.push_back(static_cast<std::uint32_t>(index));
      const auto mesh = fixture.View(static_cast<std::uint32_t>(index));
      const auto storage = Take(mesh.Member("attribute_storage"));
      Require(Take(Take(storage.Member("dna_attributes_num")).SignedInteger()) > 0,
          "Repeated-address Mesh uses modern Attribute storage");
    }
  }
  Require(meshes.size() == 2, "Repeated-address fixture contains two independently constructed Mesh datablocks");
  bool differingAttributes = false, differingArrays = false;
  std::size_t constantFaces = 0;
  std::vector<std::array<std::uint32_t, 3>> singles;
  if (constant) {
    for (const auto& block : fixture.blocks) {
      if (block.code != std::array<char, 4>{'D', 'A', 'T', 'A'} || fixture.Type(block) != "Attribute") {
        continue;
      }
      const auto index = static_cast<std::uint32_t>(&block - fixture.blocks.data());
      for (std::uint64_t element = 0; element < block.count; ++element) {
        const auto attribute = fixture.View(index, element);
        const auto nameAddress = Take(Take(attribute.Member("name")).Pointer());
        const auto name = std::find_if(fixture.blocks.begin(), fixture.blocks.end(),
            [&](const auto& entry) { return entry.oldAddress == nameAddress; });
        Require(name != fixture.blocks.end(), "Constant attribute has a saved name");
        const auto text = fixture.Payload(*name);
        if (text.size() != 11 || !std::equal(text.begin(), text.end(),
                reinterpret_cast<const std::byte*>("sharp_face"))) {
          continue;
        }
        Require(Take(Take(attribute.Member("storage_type")).SignedInteger()) == 1 &&
                    Take(Take(attribute.Member("data_type")).SignedInteger()) == 0 &&
                    Take(Take(attribute.Member("domain")).SignedInteger()) == 2,
            "Blender writes constant sharp_face as a face-domain boolean AttributeSingle");
        const auto address = Take(Take(attribute.Member("data")).Pointer());
        const auto single = std::find_if(fixture.blocks.begin(), fixture.blocks.end(),
            [&](const auto& entry) {
              return entry.oldAddress == address && entry.offset > block.offset;
            });
        Require(single != fixture.blocks.end() && fixture.Type(*single) == "AttributeSingle" && single->count == 1,
            "Constant face attribute references one serialized AttributeSingle");
        const auto valueAddress = Take(Take(fixture.View(static_cast<std::uint32_t>(single - fixture.blocks.begin())).Member("data")).Pointer());
        const auto value = std::find_if(fixture.blocks.begin(), fixture.blocks.end(),
            [&](const auto& entry) { return entry.oldAddress == valueAddress; });
        Require(value != fixture.blocks.end() && fixture.Type(*value) == "raw_data" &&
                    value->count == 1 && value->length == 1 && fixture.Payload(*value)[0] == std::byte{1},
            "Constant sharp_face serializes one true byte for two faces");
        singles.push_back({index, static_cast<std::uint32_t>(element),
            static_cast<std::uint32_t>(single - fixture.blocks.begin())});
        ++constantFaces;
      }
    }
    Require(constantFaces == 2, "Both independently constructed Meshes retain constant sharp_face storage");
    const auto first = singles[0][2];
    const auto second = singles[1][2];
    const auto previous = fixture.blocks[second].oldAddress;
    fixture.blocks[second].oldAddress = fixture.blocks[first].oldAddress;
    StorePointer(fixture, Take(fixture.View(singles[1][0], singles[1][1]).Member("data")),
        fixture.blocks[first].oldAddress);
    addresses.erase(previous);
    addresses[fixture.blocks[first].oldAddress].push_back(second);
  }
  for (const auto& [address, indices] : addresses) {
    if (indices.size() < 2) {
      continue;
    }
    const auto& first = fixture.blocks[indices.front()];
    for (const auto index : indices) {
      const auto& other = fixture.blocks[index];
      Require(other.code == first.code && fixture.Type(other) == fixture.Type(first) &&
                  other.count == first.count && other.length == first.length,
          "Repeated saved address has matching block and SDNA shape");
      if (!std::ranges::equal(fixture.Payload(first), fixture.Payload(other))) {
        differingAttributes = differingAttributes || fixture.Type(first) == "Attribute";
        differingArrays = differingArrays || fixture.Type(first) == valueType;
      }
    }
  }
  Require(differingAttributes && differingArrays,
      "Blender-written Attribute and value-storage address collisions are not byte-identical aliases");
  for (const auto code : {std::array<char, 4>{'D', 'A', 'T', 'A'}, {'O', 'B', 0, 0}}) {
    auto changed = fixture;
    changed.blocks[meshes.back()].code = code;
    RejectCollision(changed);
  }
  auto changed = fixture;
  changed.blocks[meshes.back()].oldAddress = changed.blocks[meshes.front()].oldAddress;
  RejectCollision(changed);
  changed = fixture;
  changed.header.version = 405;
  RejectCollision(changed);
  changed = fixture;
  StorePointer(changed, Take(Take(changed.View(meshes.front()).Member("attribute_storage")).Member("dna_attributes")), 0);
  RejectCollision(changed);
  for (const auto& [address, indices] : addresses) {
    if (indices.size() < 2 || fixture.Type(fixture.blocks[indices.front()]) != valueType) {
      continue;
    }
    changed = fixture;
    const auto storage = Take(changed.View(meshes.front()).Member("attribute_storage"));
    const auto recordsAddress = Take(Take(storage.Member("dna_attributes")).Pointer());
    const auto records = addresses.at(recordsAddress).front();
    for (std::uint64_t element = 0; element < changed.blocks[records].count; ++element) {
      const auto data = Take(changed.View(records, element).Member("data"));
      if (Take(data.Pointer()) == address) {
        StorePointer(changed, data, 0);
      }
    }
    RejectCollision(changed);
    if (constant) {
      changed = fixture;
      for (std::uint64_t element = 0; element < changed.blocks[records].count; ++element) {
        const auto record = changed.View(records, element);
        if (Take(Take(record.Member("data")).Pointer()) == address) {
          const auto storage = Take(record.Member("storage_type"));
          const auto offset = static_cast<std::size_t>(storage.Bytes().data() - changed.bytes.data());
          changed.bytes[offset] = std::byte{0};
        }
      }
      RejectCollision(changed);
    }
    changed = fixture;
    const auto raw = changed.schema.FindStruct("raw_data");
    changed.blocks[indices.front()].sdnaIndex = static_cast<std::uint32_t>(raw - changed.schema.structs.data());
    RejectCollision(changed);
    break;
  }
  for (const bool reverse : {false, true}) {
    if (reverse) {
      std::reverse(fixture.blocks.begin(), fixture.blocks.end());
    }
    const auto pointers = blend::BuildPointerMap(fixture.blocks);
    Require(!pointers.HasValue() && pointers.GetError().blockIndex.has_value(), "Repeated addresses fail pointer-map construction");
    const auto index = *pointers.GetError().blockIndex;
    Require(index < fixture.blocks.size() && addresses.at(fixture.blocks[index].oldAddress).size() > 1,
        "Duplicate-pointer diagnostic identifies a collided saved address");
    Failure(pointers, "BLEND_POINTER_DUPLICATE", fixture, index);
    Require(blend::SelectScene(fixture.bytes, fixture.blocks, fixture.schema, fixture.header).HasValue(),
        "Scene selection accepts fixture-proven Mesh-owned Attribute collisions");
    const auto scene = Take(blend::DecodeScene(fixture.bytes, fixture.blocks, fixture.schema, fixture.header, {10000, 64}));
    Require(scene.meshes.size() == 2 && scene.meshes[0].points != scene.meshes[1].points,
        "Mesh-owned collisions resolve differing arrays without conflating source geometry");
  }
}

} // namespace

int main(int argc, char** argv) {
  try {
    Require(argc == 3, "Two Blender-written Mesh boundary fixture directories are required");
    for (const auto directory : {argv[1], argv[2]}) {
      CheckCustom(std::filesystem::path(directory) / "custom.blend");
    }
    CheckRepeatedAddresses(std::filesystem::path(argv[2]) / "multi.blend");
    CheckRepeatedAddresses(std::filesystem::path(argv[2]) / "constant.blend");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
