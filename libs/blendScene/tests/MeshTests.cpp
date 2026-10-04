#include <blendScene/Decode.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <tuple>

namespace {

void Require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <class Value>
Value Take(const blend::Result<Value>& result) {
  if (!result.HasValue()) {
    throw std::runtime_error(result.GetError().code + ": " + result.GetError().message);
  }
  return result.GetValue();
}

bool SameMesh(const blend::Mesh& left, const blend::Mesh& right) {
  if (left.sourceName != right.sourceName || left.points != right.points ||
      left.faceVertexCounts != right.faceVertexCounts ||
      left.faceVertexIndices != right.faceVertexIndices ||
      left.cornerNormals != right.cornerNormals || left.uvMaps.size() != right.uvMaps.size()) {
    return false;
  }
  for (std::size_t uv = 0; uv < left.uvMaps.size(); ++uv) {
    if (left.uvMaps[uv].sourceName != right.uvMaps[uv].sourceName ||
        left.uvMaps[uv].values != right.uvMaps[uv].values ||
        left.uvMaps[uv].indices != right.uvMaps[uv].indices ||
        left.uvMaps[uv].activeRender != right.uvMaps[uv].activeRender) {
      return false;
    }
  }
  return true;
}

void Store(std::vector<std::byte>& bytes, std::uint64_t offset,
    std::uint64_t value, std::size_t width, blend::ByteOrder order) {
  for (std::size_t byte = 0; byte < width; ++byte) {
    const auto shift = order == blend::ByteOrder::Little ? byte : width - 1 - byte;
    bytes[static_cast<std::size_t>(offset) + byte] = static_cast<std::byte>((value >> (8 * shift)) & 255);
  }
}

struct Field {
  std::string type;
  std::string name;
  bool pointer = false;
  std::uint64_t elements = 0;
};

class Fixture {
public:
  Fixture(const std::vector<std::byte>& input,
      const std::vector<blend::BlendBlock>& records, const blend::DnaSchema& dna,
      blend::Header layout, bool newStorage)
      : bytes(input), blocks(records), schema(dna), header(layout), modern(newStorage) {
    header.version = modern ? 502 : 405;
    Structure("raw_data", {});
    Type("int", 4);
    Type("uint", 4);
    Type("short", 2);
    Type("int8_t", 1);
    Type("int64_t", 8);
    Type("uchar", 1);
    Type("CustomDataExternal", 0);
    Type("Key", 0);
    Structure("vec3f", {{"float", "x"}, {"float", "y"}, {"float", "z"}});
    Structure("vec2f", {{"float", "x"}, {"float", "y"}});
    Structure("MLoopUV", {{"float", "uv", false, 2}, {"int", "flag"}});
    Structure("MVert", {{"char", "flag"}, {"float", "co", false, 3}, {"short", "no", false, 3}});
    Structure("MLoop", {{"short", "pad"}, {"uint", "v"}, {"uint", "e"}});
    Structure("MPoly", {{"short", "mat_nr"}, {"int", "loopstart"}, {"int", "totloop"}, {"char", "flag"}});
    Structure("MEdge", {{"uint", "v1"}, {"uint", "v2"}, {"short", "flag"}});
    Structure("vec2s", {{"short", "x"}, {"short", "y"}});
    Structure("MIntProperty", {{"int", "i"}});
    Structure("MBoolProperty", {{"uchar", "b"}});
    Structure("CustomDataLayer", {{"int", "type"}, {"int", "flag"}, {"int", "active_rnd"},
                                     {"char", "name", false, 32}, {"void", "data", true}});
    Structure("CustomData", {{"CustomDataLayer", "layers", true},
                                {"int", "totlayer"}, {"CustomDataExternal", "external", true}});
    Structure("Attribute", {{"char", "name", true}, {"short", "data_type"},
                               {"int8_t", "domain"}, {"int8_t", "storage_type"}, {"void", "data", true}});
    Structure("AttributeArray", {{"void", "data", true}, {"int64_t", "size"}, {"int8_t", "is_single"}});
    Structure("AttributeSingle", {{"void", "data", true}});
    Structure("AttributeStorage", {{"Attribute", "dna_attributes", true}, {"int", "dna_attributes_num"}});
    const auto meshStruct = Structure("Mesh", {{"ID", "id"}, {"int", "totvert"}, {"int", "totedge"},
                                                  {"int", "totpoly"}, {"int", "totloop"}, {"int", "poly_offset_indices", true},
                                                  {"AttributeStorage", "attribute_storage"}, {"CustomData", "vdata"},
                                                  {"CustomData", "edata"}, {"CustomData", "pdata"}, {"CustomData", "ldata"},
                                                  {"char", "default_uv_map_attribute", true}, {"Key", "key", true},
                                                  {"MVert", "mvert", true}, {"MLoop", "mloop", true},
                                                  {"MPoly", "mpoly", true}, {"MEdge", "medge", true}});
    mesh = static_cast<std::uint32_t>(blocks.size() - 2);
    blocks[mesh].sdnaIndex = meshStruct;
    blocks[mesh].offset = bytes.size();
    blocks[mesh].length = schema.types[schema.structs[meshStruct].typeIndex].length;
    bytes.resize(bytes.size() + static_cast<std::size_t>(blocks[mesh].length));
    Name(mesh, 0, "MEShared");
    Set(mesh, "Mesh", "totvert", 4);
    Set(mesh, "Mesh", "totedge", 5);
    Set(mesh, "Mesh", "totpoly", 2);
    Set(mesh, "Mesh", "totloop", 6);
    for (const auto object : {4, 5}) {
      Set(object, "Object", "type", 1);
      Set(object, "Object", "data", blocks[mesh].oldAddress);
    }
    points = Data(modern ? "raw_data" : "vec3f", modern ? 1 : 4, 48);
    const std::array<blend::Vector3, 4> positions = {{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}};
    for (std::size_t vertex = 0; vertex < positions.size(); ++vertex) {
      for (std::size_t axis = 0; axis < 3; ++axis) {
        Float(points, (vertex * 3 + axis) * 4, static_cast<float>(positions[vertex][axis]));
      }
    }
    corners = Data(modern ? "raw_data" : "MIntProperty", modern ? 1 : 6, 24);
    constexpr std::array<int, 6> topology = {0, 1, 2, 0, 2, 3};
    for (std::size_t corner = 0; corner < topology.size(); ++corner) {
      Bits(corners, corner * 4, topology[corner], 4);
    }
    offsets = Data("raw_data", 1, 12);
    Bits(offsets, 4, 3, 4);
    Bits(offsets, 8, 6, 4);
    Set(mesh, "Mesh", "poly_offset_indices", blocks[offsets].oldAddress);
    sharp = Data("raw_data", 1, 2);
    Bits(sharp, 0, 1, 1);
    Bits(sharp, 1, 1, 1);
    cornerEdges = Data(modern ? "raw_data" : "MIntProperty", modern ? 1 : 6, 24);
    constexpr std::array<int, 6> edgeIndices = {0, 1, 2, 2, 3, 4};
    for (std::size_t corner = 0; corner < edgeIndices.size(); ++corner) {
      Bits(cornerEdges, corner * 4, edgeIndices[corner], 4);
    }
    sharpEdges = Data("raw_data", 1, 5);
    uv = Data(modern ? "raw_data" : "vec2f", modern ? 1 : 6, 48);
    constexpr std::array<blend::Vector2, 6> texcoords = {{{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}}};
    for (std::size_t corner = 0; corner < texcoords.size(); ++corner) {
      for (std::size_t axis = 0; axis < 2; ++axis) {
        Float(uv, (corner * 2 + axis) * 4, static_cast<float>(texcoords[corner][axis]));
      }
    }
    const auto secondUv = Data(modern ? "raw_data" : "vec2f", modern ? 1 : 6, 48);
    for (std::size_t corner = 0; corner < 6; ++corner) {
      Float(secondUv, corner * 8, 1);
    }
    if (modern) {
      attributeRecords = Data("Attribute", 7, 0);
      const auto storage = Member("Mesh", "attribute_storage").offset;
      Bits(mesh, storage + Member("AttributeStorage", "dna_attributes").offset, blocks[attributeRecords].oldAddress, header.pointerSize);
      Bits(mesh, storage + Member("AttributeStorage", "dna_attributes_num").offset, 7, 4);
      Attribute(0, "position", 7, 0, points, 4);
      Attribute(1, ".corner_vert", 3, 3, corners, 6);
      Attribute(2, "sharp_face", 0, 2, sharp, 2);
      Attribute(3, "First", 6, 3, uv, 6);
      Attribute(4, "Second", 6, 3, secondUv, 6);
      Attribute(5, ".corner_edge", 3, 3, cornerEdges, 6);
      Attribute(6, "sharp_edge", 0, 1, sharpEdges, 5);
      Set(mesh, "Mesh", "default_uv_map_attribute", blocks[Text("Second")].oldAddress);
    } else {
      const auto vertexLayers = Data("CustomDataLayer", 1, 0);
      Domain("vdata", vertexLayers, 1);
      Layer(vertexLayers, 0, "position", 48, points);
      const auto faceLayers = Data("CustomDataLayer", 1, 0);
      Domain("pdata", faceLayers, 1);
      Layer(faceLayers, 0, "sharp_face", 50, sharp);
      const auto edgeLayers = Data("CustomDataLayer", 1, 0);
      Domain("edata", edgeLayers, 1);
      Layer(edgeLayers, 0, "sharp_edge", 50, sharpEdges);
      attributeRecords = Data("CustomDataLayer", 4, 0);
      Domain("ldata", attributeRecords, 4);
      Layer(attributeRecords, 0, ".corner_vert", 11, corners);
      Layer(attributeRecords, 1, "First", 49, uv);
      Layer(attributeRecords, 2, "Second", 49, secondUv);
      Layer(attributeRecords, 3, ".corner_edge", 11, cornerEdges);
      for (std::size_t layer = 1; layer < 3; ++layer) {
        Bits(attributeRecords, layer * Size("CustomDataLayer") + Member("CustomDataLayer", "active_rnd").offset, 1, 4);
      }
    }
  }

  const blend::DnaMember& Member(std::string_view type, std::string_view member) const {
    return *schema.FindStruct(type)->FindMember(member);
  }

  void Bits(std::uint32_t index, std::uint64_t offset, std::uint64_t value, std::size_t width) {
    Store(bytes, blocks[index].offset + offset, value, width, header.byteOrder);
  }

  void Float(std::uint32_t index, std::uint64_t offset, float value) {
    Bits(index, offset, std::bit_cast<std::uint32_t>(value), 4);
  }

  void Set(std::uint32_t index, std::string_view type, std::string_view member, std::uint64_t value) {
    const auto& field = Member(type, member);
    Bits(index, field.offset, value, static_cast<std::size_t>(field.size));
  }

  blend::Result<blend::Scene> Decode() const {
    return blend::DecodeScene(bytes, blocks, schema, header, {64, 8});
  }

  std::uint32_t AddPackedNormals(std::array<std::int16_t, 2> pair) {
    const auto values = Data(modern ? "raw_data" : "vec2s", modern ? 1 : 6, 24);
    for (std::size_t corner = 0; corner < 6; ++corner) {
      for (std::size_t axis = 0; axis < 2; ++axis) {
        Bits(values, corner * 4 + axis * 2, static_cast<std::uint16_t>(pair[axis]), 2);
      }
    }
    const auto old = attributeRecords;
    const auto count = modern ? 7u : 4u;
    const auto type = modern ? "Attribute" : "CustomDataLayer";
    attributeRecords = Data(type, count + 1, 0);
    std::copy_n(bytes.begin() + static_cast<std::size_t>(blocks[old].offset),
        static_cast<std::size_t>(blocks[old].length),
        bytes.begin() + static_cast<std::size_t>(blocks[attributeRecords].offset));
    if (modern) {
      const auto storage = Member("Mesh", "attribute_storage").offset;
      Bits(mesh, storage + Member("AttributeStorage", "dna_attributes").offset,
          blocks[attributeRecords].oldAddress, header.pointerSize);
      Bits(mesh, storage + Member("AttributeStorage", "dna_attributes_num").offset, count + 1, 4);
      Attribute(count, "custom_normal", 2, 3, values, 6);
    } else {
      Domain("ldata", attributeRecords, count + 1);
      Layer(attributeRecords, count, "", 41, values);
    }
    return values;
  }

  void Failure(std::string_view code, std::uint32_t index) const {
    const auto result = Decode();
    if (result.HasValue() || result.GetError().code != code || result.GetError().blockIndex != index) {
      throw std::runtime_error("Mesh expected " + std::string(code) + " at " + std::to_string(index) +
                               (result.HasValue() ? ", got Scene" : ", got " + result.GetError().code + " at " + (result.GetError().blockIndex ? std::to_string(*result.GetError().blockIndex) : "none")));
    }
    Require(result.GetError().byteOffset == blocks[index].offset &&
                result.GetError().severity == blend::Severity::Fatal && !result.GetError().recoverable,
        "Mesh semantic failures carry exact fatal payload context");
  }

  std::uint32_t Text(std::string_view text) {
    const auto index = Data("raw_data", 1, text.size() + 1);
    Name(index, 0, text);
    return index;
  }

  std::uint32_t Data(std::string_view type, std::uint64_t count, std::uint64_t length) {
    const auto structure = schema.FindStruct(type);
    const auto dna = static_cast<std::uint32_t>(structure - schema.structs.data());
    if (length == 0) {
      length = schema.types[structure->typeIndex].length * count;
    }
    const auto index = static_cast<std::uint32_t>(blocks.size());
    blocks.push_back({{'D', 'A', 'T', 'A'}, length, 100000 + 100 * index, dna, count, bytes.size()});
    bytes.resize(bytes.size() + static_cast<std::size_t>(length));
    return index;
  }

  void Name(std::uint32_t index, std::uint64_t offset, std::string_view name) {
    for (std::size_t byte = 0; byte < name.size(); ++byte) {
      Bits(index, offset + byte, static_cast<unsigned char>(name[byte]), 1);
    }
  }

  std::uint16_t Size(std::string_view type) const {
    return schema.types[schema.FindStruct(type)->typeIndex].length;
  }

  void Domain(std::string_view name, std::uint32_t layers, std::uint32_t count) {
    const auto offset = Member("Mesh", name).offset;
    Bits(mesh, offset + Member("CustomData", "layers").offset, count == 0 ? 0 : blocks[layers].oldAddress, header.pointerSize);
    Bits(mesh, offset + Member("CustomData", "totlayer").offset, count, 4);
  }

  void Layer(std::uint32_t records, std::uint32_t element, std::string_view name, int type, std::uint32_t values) {
    const auto offset = element * Size("CustomDataLayer");
    Bits(records, offset + Member("CustomDataLayer", "type").offset, type, 4);
    Name(records, offset + Member("CustomDataLayer", "name").offset, name);
    Bits(records, offset + Member("CustomDataLayer", "data").offset, blocks[values].oldAddress, header.pointerSize);
  }

  void Attribute(std::uint32_t element, std::string_view name, int type, int domain, std::uint32_t values, int count) {
    const auto offset = element * Size("Attribute");
    const auto nameIndex = Text(name);
    const auto array = Data("AttributeArray", 1, 0);
    arrays.push_back(array);
    Set(array, "AttributeArray", "data", blocks[values].oldAddress);
    Set(array, "AttributeArray", "size", count);
    Bits(attributeRecords, offset + Member("Attribute", "name").offset, blocks[nameIndex].oldAddress, header.pointerSize);
    Bits(attributeRecords, offset + Member("Attribute", "data_type").offset, type, 2);
    Bits(attributeRecords, offset + Member("Attribute", "domain").offset, domain, 1);
    Bits(attributeRecords, offset + Member("Attribute", "data").offset, blocks[array].oldAddress, header.pointerSize);
  }

  std::uint32_t Constant(std::uint32_t element, std::string_view type,
      std::uint64_t stride, bool separate) {
    const auto source = Take(blend::ViewDnaBlock(bytes, blocks, schema, header, arrays[element]));
    const auto address = Take(Take(source.Member("data")).Pointer());
    const auto values = *Take(Take(blend::BuildPointerMap(blocks)).Resolve(address));
    const auto offset = blocks[values].offset;
    const auto data = Data(type, 1, stride);
    std::copy_n(bytes.begin() + static_cast<std::size_t>(offset), static_cast<std::size_t>(stride),
        bytes.begin() + static_cast<std::size_t>(blocks[data].offset));
    if (separate) {
      const auto single = Data("AttributeSingle", 1, 0);
      Set(single, "AttributeSingle", "data", blocks[data].oldAddress);
      Bits(attributeRecords, element * Size("Attribute") + Member("Attribute", "storage_type").offset, 1, 1);
      Bits(attributeRecords, element * Size("Attribute") + Member("Attribute", "data").offset,
          blocks[single].oldAddress, header.pointerSize);
    } else {
      Set(arrays[element], "AttributeArray", "is_single", 1);
      Set(arrays[element], "AttributeArray", "data", blocks[data].oldAddress);
    }
    return data;
  }

  std::uint32_t LegacyUv(std::uint32_t element) {
    const auto source = Take(blend::ViewDnaBlock(bytes, blocks, schema, header, attributeRecords, element));
    const auto address = Take(Take(source.Member("data")).Pointer());
    const auto old = *Take(Take(blend::BuildPointerMap(blocks)).Resolve(address));
    const auto values = Data("MLoopUV", 6, 0);
    for (std::size_t corner = 0; corner < 6; ++corner) {
      const auto offset = corner * Size("MLoopUV");
      for (std::size_t axis = 0; axis < 2; ++axis) {
        const auto value = Take(Take(Take(blend::ViewDnaBlock(bytes, blocks, schema, header, old, corner))
                                         .Member(axis == 0 ? "x" : "y"))
                .FloatingPoint());
        Float(values, offset + Member("MLoopUV", "uv").offset + axis * 4, static_cast<float>(value));
      }
      Bits(values, offset + Member("MLoopUV", "flag").offset, corner % 2 == 0 ? 0 : 0xffffffffu, 4);
    }
    const auto offset = element * Size("CustomDataLayer");
    Bits(attributeRecords, offset + Member("CustomDataLayer", "type").offset, 16, 4);
    Bits(attributeRecords, offset + Member("CustomDataLayer", "data").offset, blocks[values].oldAddress, header.pointerSize);
    return values;
  }

  void EmptyPolygons() {
    for (const auto member : {"totpoly", "totloop", "poly_offset_indices"}) {
      Set(mesh, "Mesh", member, 0);
    }
    for (const auto element : {1, 3, 4, 5}) {
      Set(arrays[element], "AttributeArray", "size", 0);
      Set(arrays[element], "AttributeArray", "data", 0);
    }
  }

  void LegacyGeometry(bool clearCore = true) {
    legacyPoints = Data("MVert", 4, 0);
    legacyLoops = Data("MLoop", 6, 0);
    legacyPolygons = Data("MPoly", 2, 0);
    legacyEdges = Data("MEdge", 5, 0);
    Set(mesh, "Mesh", "mvert", blocks[legacyPoints].oldAddress);
    Set(mesh, "Mesh", "mloop", blocks[legacyLoops].oldAddress);
    Set(mesh, "Mesh", "mpoly", blocks[legacyPolygons].oldAddress);
    Set(mesh, "Mesh", "medge", blocks[legacyEdges].oldAddress);
    for (std::size_t vertex = 0; vertex < 4; ++vertex) {
      for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto offset = blocks[points].offset + (vertex * 3 + axis) * 4;
        std::copy_n(bytes.begin() + static_cast<std::size_t>(offset), 4,
            bytes.begin() + static_cast<std::size_t>(blocks[legacyPoints].offset +
                                                     vertex * Size("MVert") + Member("MVert", "co").offset + axis * 4));
      }
    }
    constexpr std::array<int, 6> vertices = {0, 1, 2, 0, 2, 3};
    constexpr std::array<int, 6> edges = {0, 1, 2, 2, 3, 4};
    for (std::size_t corner = 0; corner < 6; ++corner) {
      Bits(legacyLoops, corner * Size("MLoop") + Member("MLoop", "v").offset, vertices[corner], 4);
      Bits(legacyLoops, corner * Size("MLoop") + Member("MLoop", "e").offset, edges[corner], 4);
    }
    for (std::size_t face = 0; face < 2; ++face) {
      Bits(legacyPolygons, face * Size("MPoly") + Member("MPoly", "loopstart").offset, face * 3, 4);
      Bits(legacyPolygons, face * Size("MPoly") + Member("MPoly", "totloop").offset, 3, 4);
    }
    if (clearCore) {
      Require(!modern, "Legacy geometry fixture retains CustomData UV layers");
      Set(mesh, "Mesh", "poly_offset_indices", 0);
      for (const auto domain : {"vdata", "edata", "pdata"}) {
        Domain(domain, 0, 0);
      }
      const auto old = attributeRecords;
      attributeRecords = Data("CustomDataLayer", 2, 0);
      std::copy_n(bytes.begin() + static_cast<std::size_t>(blocks[old].offset + Size("CustomDataLayer")),
          2 * Size("CustomDataLayer"), bytes.begin() + static_cast<std::size_t>(blocks[attributeRecords].offset));
      Domain("ldata", attributeRecords, 2);
    }
  }

  std::vector<std::byte> bytes;
  std::vector<blend::BlendBlock> blocks;
  blend::DnaSchema schema;
  blend::Header header;
  bool modern;
  std::uint32_t mesh, points, corners, offsets, sharp, uv, attributeRecords, cornerEdges, sharpEdges;
  std::uint32_t legacyPoints = 0, legacyLoops = 0, legacyPolygons = 0, legacyEdges = 0;
  std::vector<std::uint32_t> arrays;

private:
  std::uint16_t Type(std::string_view name, std::uint16_t length) {
    const auto found = std::find_if(schema.types.begin(), schema.types.end(),
        [&](const auto& type) { return type.name == name; });
    if (found != schema.types.end()) {
      return static_cast<std::uint16_t>(found - schema.types.begin());
    }
    schema.types.push_back({std::string(name), length});
    return static_cast<std::uint16_t>(schema.types.size() - 1);
  }

  std::uint32_t Structure(std::string_view name, const std::vector<Field>& fields) {
    const auto type = Type(name, 0);
    blend::DnaStruct structure{type, {}};
    std::uint64_t offset = 0;
    for (const auto& field : fields) {
      const auto memberType = Type(field.type, 0);
      const auto size = field.pointer ? header.pointerSize
                                      : schema.types[memberType].length * (field.elements == 0 ? 1 : field.elements);
      structure.members.push_back({memberType, 0, field.name, field.pointer ? 1u : 0u,
          field.elements == 0 ? std::vector<std::uint64_t>{} : std::vector<std::uint64_t>{field.elements},
          offset, size});
      offset += size;
    }
    schema.types[type].length = static_cast<std::uint16_t>(offset);
    const auto found = std::find_if(schema.structs.begin(), schema.structs.end(),
        [&](const auto& record) { return record.typeIndex == type; });
    if (found != schema.structs.end()) {
      const auto index = static_cast<std::uint32_t>(found - schema.structs.begin());
      *found = std::move(structure);
      return index;
    }
    schema.structs.push_back(std::move(structure));
    return static_cast<std::uint32_t>(schema.structs.size() - 1);
  }
};

void CheckLegacyGeometry(const Fixture& fixture) {
  const auto expected = Take(fixture.Decode()).meshes[0];
  auto legacy = fixture;
  legacy.LegacyGeometry();
  const auto result = legacy.Decode();
  Require(result.HasValue() && result.Diagnostics().empty() &&
              result.GetValue().objects[0].mesh == 0 && result.GetValue().objects[1].mesh == 0 &&
              SameMesh(expected, result.GetValue().meshes[0]),
      "SDNA legacy arrays preserve shared owning points, topology, flat normals and indexed UVs");
  auto loopUv = fixture;
  loopUv.LegacyUv(1);
  loopUv.LegacyUv(2);
  loopUv.LegacyGeometry();
  Require(SameMesh(expected, Take(loopUv.Decode()).meshes[0]),
      "Legacy geometry composes with SDNA MLoopUV maps");
  auto packed = legacy;
  const auto values = packed.Data("vec2s", 6, 0);
  const auto oldLayers = packed.attributeRecords;
  packed.attributeRecords = packed.Data("CustomDataLayer", 3, 0);
  std::copy_n(packed.bytes.begin() + static_cast<std::size_t>(packed.blocks[oldLayers].offset),
      2 * packed.Size("CustomDataLayer"),
      packed.bytes.begin() + static_cast<std::size_t>(packed.blocks[packed.attributeRecords].offset));
  packed.Layer(packed.attributeRecords, 2, "", 41, values);
  packed.Domain("ldata", packed.attributeRecords, 3);
  Require(SameMesh(expected, Take(packed.Decode()).meshes[0]),
      "Legacy geometry composes with automatic packed custom normals");
  auto changed = legacy;
  std::reverse(changed.blocks.begin(), changed.blocks.end());
  Require(SameMesh(expected, Take(changed.Decode()).meshes[0]) &&
              SameMesh(expected, Take(legacy.Decode()).meshes[0]),
      "Legacy arrays retain exact repeated and reversed-block results");
  changed = legacy;
  changed.header.version = 502;
  Require(SameMesh(expected, Take(changed.Decode()).meshes[0]),
      "Legacy array selection uses storage shape, not a version-based fallback");
  for (const float scale : {1.0f, 0.01f, 0.001f, 10.0f}) {
    changed = legacy;
    changed.Float(1, changed.Member("Scene", "unit").offset, scale);
    const auto mesh = Take(changed.Decode()).meshes[0];
    for (std::size_t vertex = 0; vertex < 4; ++vertex) {
      for (std::size_t axis = 0; axis < 3; ++axis) {
        Require(mesh.points[vertex][axis] == expected.points[vertex][axis] * scale,
            "Legacy positions are normalized exactly once");
      }
    }
    Require(mesh.cornerNormals == expected.cornerNormals && mesh.uvMaps[0].values == expected.uvMaps[0].values,
        "Legacy normals and UVs are independent of source units");
  }
  changed = legacy;
  auto owned = Take(changed.Decode());
  changed.bytes.clear();
  changed.blocks.clear();
  changed.schema = {};
  Require(SameMesh(expected, owned.meshes[0]), "Legacy results retain no borrowed arrays");

  auto modernSmooth = fixture;
  modernSmooth.Float(modernSmooth.points, 11 * 4, 3);
  modernSmooth.Bits(modernSmooth.sharp, 0, 0, 1);
  modernSmooth.Bits(modernSmooth.sharp, 1, 0, 1);
  auto legacySmooth = legacy;
  legacySmooth.Float(legacySmooth.legacyPoints,
      3 * legacySmooth.Size("MVert") + legacySmooth.Member("MVert", "co").offset + 8, 3);
  for (std::size_t face = 0; face < 2; ++face) {
    legacySmooth.Bits(legacySmooth.legacyPolygons,
        face * legacySmooth.Size("MPoly") + legacySmooth.Member("MPoly", "flag").offset, 1, 1);
  }
  Require(SameMesh(Take(modernSmooth.Decode()).meshes[0], Take(legacySmooth.Decode()).meshes[0]),
      "Legacy smooth polygon flags reuse angle-weighted normal construction");
  modernSmooth.Bits(modernSmooth.sharpEdges, 2, 1, 1);
  legacySmooth.Bits(legacySmooth.legacyEdges,
      2 * legacySmooth.Size("MEdge") + legacySmooth.Member("MEdge", "flag").offset, 512, 2);
  Require(SameMesh(Take(modernSmooth.Decode()).meshes[0], Take(legacySmooth.Decode()).meshes[0]),
      "Legacy sharp-edge flags and corner edge indices reuse split normal fans");
  modernSmooth.Bits(modernSmooth.sharp, 1, 1, 1);
  legacySmooth.Bits(legacySmooth.legacyPolygons,
      legacySmooth.Size("MPoly") + legacySmooth.Member("MPoly", "flag").offset, 0, 1);
  Require(SameMesh(Take(modernSmooth.Decode()).meshes[0], Take(legacySmooth.Decode()).meshes[0]),
      "Legacy mixed flat/smooth polygons preserve normal boundaries");
  for (const auto invalid : {5u, 0xffffffffu}) {
    changed = legacySmooth;
    changed.Set(changed.legacyLoops, "MLoop", "e", invalid);
    changed.Failure("BLEND_MESH_TOPOLOGY_INVALID", changed.legacyLoops);
  }
  changed = legacySmooth;
  changed.Set(changed.legacyLoops, "MLoop", "e", 2);
  changed.Failure("BLEND_MESH_TOPOLOGY_INVALID", changed.legacyLoops);

  for (const auto [member, block] : {
           std::pair{"mvert", legacy.legacyPoints}, {"mloop", legacy.legacyLoops},
           {"mpoly", legacy.legacyPolygons}, {"medge", legacy.legacyEdges}}) {
    for (const auto invalid : {std::uint64_t{0}, std::uint64_t{999}, legacy.blocks[block].oldAddress + 1}) {
      changed = legacy;
      changed.Set(changed.mesh, "Mesh", member, invalid);
      changed.Failure("BLEND_MESH_REFERENCE_INVALID", changed.mesh);
    }
    changed = legacy;
    ++changed.blocks[block].count;
    changed.Failure("BLEND_MESH_STORAGE_INVALID", block);
    changed = legacy;
    --changed.blocks[block].length;
    changed.Failure("BLEND_DNA_SIZE", block);
    changed = legacy;
    changed.blocks[block].sdnaIndex = changed.blocks[changed.points].sdnaIndex;
    changed.blocks[block].length = changed.blocks[block].count * changed.Size("vec3f");
    changed.bytes.resize(changed.bytes.size() + 48);
    changed.Failure("BLEND_MESH_STORAGE_INVALID", block);
    changed = legacy;
    changed.blocks[block].code = {'M', 'E', 0, 0};
    changed.Failure("BLEND_MESH_REFERENCE_INVALID", block);
    changed = legacy;
    changed.blocks[block].offset = changed.bytes.size();
    changed.Failure("BLEND_BLOCK_SIZE", block);
  }
  for (const auto invalid : {4u, 0xffffffffu}) {
    changed = legacy;
    changed.Set(changed.legacyLoops, "MLoop", "v", invalid);
    changed.Failure("BLEND_MESH_TOPOLOGY_INVALID", changed.legacyLoops);
  }
  for (const auto [element, member, value] : {
           std::tuple{0u, "loopstart", 1u}, {0u, "loopstart", 0xffffffffu},
           {1u, "loopstart", 2u}, {1u, "loopstart", 4u}, {0u, "totloop", 2u},
           {0u, "totloop", 7u}, {0u, "totloop", 0xffffffffu}, {1u, "totloop", 4u}}) {
    changed = legacy;
    changed.Bits(changed.legacyPolygons,
        element * changed.Size("MPoly") + changed.Member("MPoly", member).offset, value, 4);
    changed.Failure("BLEND_MESH_TOPOLOGY_INVALID", changed.legacyPolygons);
  }
  changed = legacy;
  changed.Set(changed.mesh, "Mesh", "totloop", 7);
  changed.blocks[changed.legacyLoops].count = 7;
  changed.blocks[changed.legacyLoops].length = 7 * changed.Size("MLoop");
  changed.Failure("BLEND_MESH_TOPOLOGY_INVALID", changed.legacyPolygons);
  changed = legacy;
  changed.Set(changed.mesh, "Mesh", "poly_offset_indices", changed.blocks[changed.offsets].oldAddress);
  changed.Failure("BLEND_MESH_STORAGE_UNSUPPORTED", changed.mesh);
  for (const auto invalid : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
    changed = legacy;
    changed.Float(changed.legacyPoints, changed.Member("MVert", "co").offset, invalid);
    changed.Failure("BLEND_MESH_VALUE_INVALID", changed.legacyPoints);
  }
  for (const auto [type, member, block] : {
           std::tuple{"MVert", "co", legacy.legacyPoints}, {"MLoop", "v", legacy.legacyLoops},
           {"MPoly", "flag", legacy.legacyPolygons}, {"MEdge", "flag", legacy.legacyEdges}}) {
    changed = legacy;
    auto& field = *std::find_if(changed.schema.structs.begin(), changed.schema.structs.end(),
        [&](const auto& record) { return changed.schema.types[record.typeIndex].name == type; });
    auto& value = *std::find_if(field.members.begin(), field.members.end(),
        [&](const auto& record) { return record.baseName == member; });
    value.arrayDimensions = member == std::string_view{"co"} ? std::vector<std::uint64_t>{1, 3}
                                                             : std::vector<std::uint64_t>{1};
    changed.Failure("BLEND_MESH_STORAGE_INVALID", block);
  }

  changed = legacy;
  changed.Set(changed.mesh, "Mesh", "totpoly", 0);
  changed.Set(changed.mesh, "Mesh", "totloop", 0);
  changed.Set(changed.mesh, "Mesh", "mloop", 0);
  changed.Set(changed.mesh, "Mesh", "mpoly", 0);
  changed.Domain("ldata", 0, 0);
  const auto loose = changed.Decode();
  Require(loose.HasValue() && loose.GetValue().meshes[0].points == expected.points &&
              loose.GetValue().meshes[0].faceVertexCounts.empty() &&
              loose.GetValue().meshes[0].cornerNormals.empty() &&
              loose.Diagnostics().size() == 1 && loose.Diagnostics()[0].code == "BLEND_MESH_EMPTY" &&
              loose.Diagnostics()[0].blockIndex == changed.mesh && loose.Diagnostics()[0].recoverable,
      "Polygon-free legacy Meshes retain loose points and one contextual warning");
  auto invalidEmpty = changed;
  invalidEmpty.Set(invalidEmpty.mesh, "Mesh", "mloop", invalidEmpty.blocks[invalidEmpty.legacyLoops].oldAddress);
  invalidEmpty.Failure("BLEND_MESH_STORAGE_INVALID", invalidEmpty.mesh);
  changed.Set(changed.mesh, "Mesh", "totvert", 0);
  changed.Failure("BLEND_MESH_STORAGE_INVALID", changed.mesh);
}

void CheckLegacyUvs(const Fixture& fixture) {
  const auto expected = Take(fixture.Decode()).meshes[0];
  for (const auto maps : {1, 2, 3}) {
    auto changed = fixture;
    for (std::uint32_t element = 1; element <= 2; ++element) {
      if ((maps & (1 << (element - 1))) != 0) {
        changed.LegacyUv(element);
      }
    }
    const auto decoded = Take(changed.Decode());
    Require(decoded.meshes.size() == 1 && decoded.objects[0].mesh == decoded.objects[1].mesh &&
                SameMesh(expected, decoded.meshes[0]) &&
                SameMesh(decoded.meshes[0], Take(changed.Decode()).meshes[0]),
        "MLoopUV and float2 maps share indexed UV semantics, render selection and owning shared geometry");
    for (const float scale : {0.01f, 10.0f}) {
      auto scaled = changed;
      scaled.Float(1, scaled.Member("Scene", "unit").offset, scale);
      const auto mesh = Take(scaled.Decode()).meshes[0];
      for (std::size_t uv = 0; uv < mesh.uvMaps.size(); ++uv) {
        Require(mesh.uvMaps[uv].values == expected.uvMaps[uv].values &&
                    mesh.uvMaps[uv].indices == expected.uvMaps[uv].indices &&
                    mesh.uvMaps[uv].activeRender == expected.uvMaps[uv].activeRender,
            "Legacy UV coordinates and selection are independent of distance units");
      }
    }
    std::reverse(changed.blocks.begin(), changed.blocks.end());
    Require(SameMesh(expected, Take(changed.Decode()).meshes[0]),
        "Legacy UV values do not depend on block enumeration");
  }
  auto legacy = fixture;
  const auto values = legacy.LegacyUv(1);
  legacy.LegacyUv(2);
  auto changed = legacy;
  changed.Set(changed.mesh, "Mesh", "default_uv_map_attribute", changed.blocks[changed.Text("First")].oldAddress);
  const auto selected = Take(changed.Decode()).meshes[0];
  Require(selected.uvMaps[0].activeRender && !selected.uvMaps[1].activeRender,
      "A saved render-map name overrides the legacy active render index");
  changed = legacy;
  constexpr std::array<blend::Vector2, 6> coordinates = {{{0, -0.0}, {-0.0, 0}, {-0.25, 1.5},
      {2, -2}, {-0.25, 1.5}, {2, -2}}};
  for (std::size_t corner = 0; corner < coordinates.size(); ++corner) {
    for (std::size_t axis = 0; axis < 2; ++axis) {
      changed.Float(values, corner * changed.Size("MLoopUV") + axis * 4, static_cast<float>(coordinates[corner][axis]));
    }
  }
  const auto indexed = Take(changed.Decode()).meshes[0].uvMaps[0];
  Require(indexed.values == std::vector<blend::Vector2>{{0, 0}, {-0.25, 1.5}, {2, -2}} &&
              indexed.indices == std::vector<std::int32_t>{0, 0, 1, 2, 1, 2},
      "Legacy UVs retain out-of-range coordinates and deduplicate equal signed-zero pairs");
  for (const auto invalid : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
    for (std::size_t axis = 0; axis < 2; ++axis) {
      changed = legacy;
      changed.Float(values, 5 * changed.Size("MLoopUV") + axis * 4, invalid);
      changed.Failure("BLEND_MESH_VALUE_INVALID", values);
    }
  }
  for (const auto count : {5u, 7u}) {
    changed = legacy;
    changed.blocks[values].count = count;
    changed.Failure("BLEND_MESH_STORAGE_INVALID", values);
  }
  for (const auto delta : {-1, 1}) {
    changed = legacy;
    changed.blocks[values].length = static_cast<std::uint64_t>(static_cast<std::int64_t>(changed.blocks[values].length) + delta);
    changed.Failure("BLEND_DNA_SIZE", values);
  }
  changed = legacy;
  changed.blocks[values].sdnaIndex = changed.blocks[changed.points].sdnaIndex;
  changed.Failure("BLEND_MESH_STORAGE_INVALID", values);
  for (const auto invalid : {std::uint64_t{0}, std::uint64_t{999}, legacy.blocks[values].oldAddress + 1}) {
    changed = legacy;
    changed.Bits(changed.attributeRecords, changed.Size("CustomDataLayer") + changed.Member("CustomDataLayer", "data").offset,
        invalid, changed.header.pointerSize);
    changed.Failure("BLEND_MESH_REFERENCE_INVALID", changed.attributeRecords);
  }
  changed = legacy;
  changed.blocks[values].offset = changed.bytes.size();
  changed.Failure("BLEND_BLOCK_SIZE", values);
  changed = legacy;
  changed.blocks[values].sdnaIndex = static_cast<std::uint32_t>(changed.schema.structs.size());
  changed.Failure("BLEND_DNA_INDEX", values);
  for (const auto shape : {0, 1, 2, 3}) {
    changed = legacy;
    auto& members = changed.schema.structs[changed.blocks[values].sdnaIndex].members;
    auto& member = *std::find_if(members.begin(), members.end(),
        [](const auto& field) { return field.baseName == "uv"; });
    if (shape == 0) {
      member.pointerLevel = 1;
      member.arrayDimensions.clear();
      member.size = changed.header.pointerSize;
    } else if (shape == 1) {
      member.arrayDimensions.clear();
      member.size = 4;
    } else if (shape == 2) {
      member.arrayDimensions = {1, 2};
    } else {
      member.typeIndex = changed.Member("MLoopUV", "flag").typeIndex;
    }
    changed.Failure("BLEND_MESH_STORAGE_INVALID", values);
  }
  changed = fixture;
  auto& members = changed.schema.structs[static_cast<std::size_t>(
                                             changed.schema.FindStruct("MLoopUV") - changed.schema.structs.data())]
                      .members;
  members[0].offset = 4;
  members[1].offset = 0;
  changed.LegacyUv(1);
  changed.LegacyUv(2);
  Require(SameMesh(expected, Take(changed.Decode()).meshes[0]),
      "Legacy UV decoding follows SDNA member offsets rather than a host struct layout");
  changed = legacy;
  const auto layerOffset = changed.Size("CustomDataLayer");
  changed.Bits(changed.attributeRecords, layerOffset + changed.Member("CustomDataLayer", "flag").offset, 1, 4);
  changed.Failure("BLEND_MESH_STORAGE_UNSUPPORTED", changed.attributeRecords);
  changed = legacy;
  changed.Bits(changed.attributeRecords, layerOffset + changed.Member("CustomDataLayer", "active_rnd").offset, 0, 4);
  changed.Failure("BLEND_MESH_STORAGE_INVALID", changed.attributeRecords);
  changed = legacy;
  for (std::uint32_t element = 1; element <= 2; ++element) {
    changed.Bits(changed.attributeRecords, element * changed.Size("CustomDataLayer") + changed.Member("CustomDataLayer", "active_rnd").offset,
        2, 4);
  }
  changed.Failure("BLEND_MESH_STORAGE_INVALID", changed.mesh);
  changed = legacy;
  const auto nameOffset = 2 * changed.Size("CustomDataLayer") + changed.Member("CustomDataLayer", "name").offset;
  changed.Name(changed.attributeRecords, nameOffset, "First");
  changed.Bits(changed.attributeRecords, nameOffset + 5, 0, 1);
  changed.Failure("BLEND_MESH_STORAGE_INVALID", changed.attributeRecords);
  changed = legacy;
  const auto vertexData = Take(Take(blend::ViewDnaBlock(changed.bytes, changed.blocks, changed.schema, changed.header, changed.mesh)).Member("vdata"));
  const auto vertexAddress = Take(Take(vertexData.Member("layers")).Pointer());
  const auto vertexLayers = *Take(Take(blend::BuildPointerMap(changed.blocks)).Resolve(vertexAddress));
  changed.Set(vertexLayers, "CustomDataLayer", "type", 16);
  changed.Failure("BLEND_MESH_STORAGE_UNSUPPORTED", vertexLayers);
  changed = legacy;
  for (const auto member : {"totpoly", "totloop", "poly_offset_indices"}) {
    changed.Set(changed.mesh, "Mesh", member, 0);
  }
  for (std::uint32_t element = 0; element < 4; ++element) {
    changed.Bits(changed.attributeRecords, element * changed.Size("CustomDataLayer") + changed.Member("CustomDataLayer", "data").offset,
        0, changed.header.pointerSize);
  }
  const auto empty = changed.Decode();
  Require(empty.HasValue() && empty.Diagnostics().size() == 1 &&
              empty.Diagnostics()[0].code == "BLEND_MESH_EMPTY" &&
              empty.GetValue().meshes[0].uvMaps.size() == 2 &&
              empty.GetValue().meshes[0].uvMaps[0].values.empty() &&
              empty.GetValue().meshes[0].uvMaps[0].indices.empty() &&
              empty.GetValue().meshes[0].uvMaps[1].activeRender,
      "Empty corner domains retain named legacy UV maps and the render selector without dereferencing null data");
  changed.Bits(changed.attributeRecords, layerOffset + changed.Member("CustomDataLayer", "data").offset,
      changed.blocks[values].oldAddress, changed.header.pointerSize);
  changed.Failure("BLEND_MESH_STORAGE_INVALID", values);
  auto owned = Take(legacy.Decode());
  legacy.bytes.clear();
  legacy.blocks.clear();
  legacy.schema = {};
  Require(SameMesh(expected, owned.meshes[0]), "Legacy UV results own their names, values and indices");
}

void CheckEmptyArrays(const Fixture& fixture) {
  for (const auto element : {0u, 3u}) {
    auto empty = fixture;
    empty.EmptyPolygons();
    if (element == 0) {
      empty.Set(empty.mesh, "Mesh", "totvert", 0);
      empty.Set(empty.arrays[element], "AttributeArray", "size", 0);
    }
    for (const auto address : {std::uint64_t{0}, std::uint64_t{999}, fixture.blocks[fixture.uv].oldAddress + 1}) {
      auto changed = empty;
      changed.Set(changed.arrays[element], "AttributeArray", "data", address);
      const auto result = changed.Decode();
      const auto scene = Take(result);
      const auto& mesh = scene.meshes[0];
      Require(result.Diagnostics().size() == 1 && result.Diagnostics()[0].code == "BLEND_MESH_EMPTY" &&
                  result.Diagnostics()[0].blockIndex == changed.mesh && result.Diagnostics()[0].recoverable &&
                  mesh.points.size() == (element == 0 ? 0 : 4) && mesh.uvMaps.size() == 2 &&
                  mesh.uvMaps[0].values.empty() && mesh.uvMaps[0].indices.empty() &&
                  mesh.uvMaps[1].values.empty() && mesh.uvMaps[1].indices.empty() && mesh.uvMaps[1].activeRender,
          "Zero-domain dense arrays do not follow null, absent or interior saved data keys");
      auto malformed = changed;
      malformed.Set(malformed.arrays[element], "AttributeArray", "size", 1);
      malformed.Failure("BLEND_MESH_STORAGE_INVALID", malformed.arrays[element]);
      malformed = changed;
      malformed.Set(malformed.arrays[element], "AttributeArray", "is_single", 2);
      malformed.Failure("BLEND_MESH_STORAGE_INVALID", malformed.arrays[element]);
      malformed = changed;
      malformed.Set(malformed.arrays[element], "AttributeArray", "is_single", 1);
      malformed.Failure("BLEND_MESH_REFERENCE_INVALID", malformed.arrays[element]);
      malformed = fixture;
      malformed.Set(malformed.arrays[element], "AttributeArray", "data", address);
      malformed.Failure("BLEND_MESH_REFERENCE_INVALID", malformed.arrays[element]);
    }
  }
}

void CheckConstantMeshes(const Fixture& fixture) {
  for (const bool separate : {false, true}) {
    for (const bool raw : {false, true}) {
      auto changed = fixture;
      const auto uv = changed.Constant(3, raw ? "raw_data" : "vec2f", 8, separate);
      changed.Float(uv, 0, 0.25f);
      changed.Float(uv, 4, -0.75f);
      const auto scene = Take(changed.Decode());
      const auto mesh = scene.meshes[0];
      Require(mesh.uvMaps[0].values == std::vector<blend::Vector2>{{0.25, -0.75}} &&
                  mesh.uvMaps[0].indices == std::vector<std::int32_t>(6, 0) &&
                  !mesh.uvMaps[0].activeRender && mesh.uvMaps[1].activeRender,
          "Both constant storage forms expand raw/structured UVs without changing render-map selection");
      auto reordered = changed;
      std::reverse(reordered.blocks.begin(), reordered.blocks.end());
      Require(SameMesh(mesh, Take(reordered.Decode()).meshes[0]) &&
                  SameMesh(mesh, Take(changed.Decode()).meshes[0]),
          "Constant decoding is deterministic across repeated/reversed block reads");
      changed.Float(1, changed.Member("Scene", "unit").offset, 10);
      Require(Take(changed.Decode()).meshes[0].uvMaps[0].values == mesh.uvMaps[0].values,
          "Constant UV values are not unit-scaled");
      changed.Float(uv, 0, std::numeric_limits<float>::infinity());
      changed.Failure("BLEND_MESH_VALUE_INVALID", uv);
      changed = fixture;
      changed.EmptyPolygons();
      const auto points = changed.Constant(0, raw ? "raw_data" : "vec3f", 12, separate);
      changed.Float(points, 0, 2);
      changed.Float(points, 4, 3);
      changed.Float(points, 8, 4);
      for (const float scale : {0.01f, 1.0f, 10.0f}) {
        changed.Float(1, changed.Member("Scene", "unit").offset, scale);
        const auto result = changed.Decode();
        Require(result.HasValue() && result.Diagnostics().size() == 1 &&
                    result.Diagnostics()[0].code == "BLEND_MESH_EMPTY" &&
                    result.GetValue().meshes[0].points ==
                        std::vector<blend::Vector3>(4, {2 * scale, 4 * scale, -3 * scale}),
            "Constant positions expand into owning points with one basis/unit conversion");
      }
      changed.Float(points, 0, std::numeric_limits<float>::quiet_NaN());
      changed.Failure("BLEND_MESH_VALUE_INVALID", points);
      for (const auto element : {2u, 6u}) {
        for (const auto value : {0u, 1u}) {
          changed = fixture;
          changed.Bits(changed.sharp, 0, 0, 1);
          changed.Bits(changed.sharp, 1, 0, 1);
          changed.Float(changed.points, 11 * 4, 4);
          const auto values = element == 2 ? changed.sharp : changed.sharpEdges;
          const auto count = element == 2 ? 2u : 5u;
          for (std::size_t item = 0; item < count; ++item) {
            changed.Bits(values, item, value, 1);
          }
          const auto expected = Take(changed.Decode()).meshes[0];
          const auto data = changed.Constant(element, raw ? "raw_data" : "MBoolProperty", 1, separate);
          Require(SameMesh(expected, Take(changed.Decode()).meshes[0]),
              "Constant face/edge booleans preserve flat/smooth/split normal domains");
          changed.Bits(data, 0, 2, 1);
          changed.Failure("BLEND_MESH_STORAGE_INVALID", data);
        }
      }
      for (const auto pair : {std::array<std::int16_t, 2>{0, 0}, {16384, 32767}, {-32768, -32768}}) {
        changed = fixture;
        changed.AddPackedNormals(pair);
        const auto expected = Take(changed.Decode()).meshes[0];
        const auto packed = changed.Constant(7, raw ? "raw_data" : "vec2s", 4, separate);
        Require(SameMesh(expected, Take(changed.Decode()).meshes[0]),
            "Constant packed normals preserve automatic values and the signed-short range");
        --changed.blocks[packed].length;
        changed.Failure("BLEND_MESH_STORAGE_INVALID", packed);
      }
      changed = fixture;
      const auto corners = changed.Constant(1, raw ? "raw_data" : "MIntProperty", 4, separate);
      changed.Bits(corners, 0, 4, 4);
      changed.Failure("BLEND_MESH_TOPOLOGY_INVALID", corners);
      changed.Bits(corners, 0, 0, 4);
      changed.Failure("BLEND_MESH_NORMALS_INVALID", changed.mesh);
      changed = fixture;
      changed.EmptyPolygons();
      const auto emptyUv = changed.Data(raw ? "raw_data" : "vec2f", 1, 8);
      changed.Set(changed.arrays[3], "AttributeArray", "data", changed.blocks[emptyUv].oldAddress);
      const auto emptyData = changed.Constant(3, raw ? "raw_data" : "vec2f", 8, separate);
      const auto empty = Take(changed.Decode()).meshes[0];
      Require(empty.uvMaps[0].values.empty() && empty.uvMaps[0].indices.empty(),
          "A constant with an empty domain retains its one stored value without publishing elements");
      changed.blocks[emptyData].length = 0;
      changed.Failure("BLEND_MESH_STORAGE_INVALID", emptyData);
      changed = fixture;
      const auto data = changed.Constant(3, raw ? "raw_data" : "vec2f", 8, separate);
      changed.bytes.resize(changed.bytes.size() + 8);
      for (const auto length : {0u, 7u, 16u}) {
        auto malformed = changed;
        malformed.blocks[data].length = length;
        malformed.Failure("BLEND_MESH_STORAGE_INVALID", data);
      }
      auto malformed = changed;
      malformed.blocks[data].count = 2;
      malformed.Failure("BLEND_MESH_STORAGE_INVALID", data);
      const auto attribute = Take(blend::ViewDnaBlock(changed.bytes, changed.blocks,
          changed.schema, changed.header, changed.attributeRecords, 3));
      const auto wrapper = *Take(Take(blend::BuildPointerMap(changed.blocks))
              .Resolve(Take(Take(attribute.Member("data")).Pointer())));
      for (const auto address : {std::uint64_t{0}, std::uint64_t{999}, changed.blocks[data].oldAddress + 1}) {
        malformed = changed;
        malformed.Set(wrapper, separate ? "AttributeSingle" : "AttributeArray", "data", address);
        malformed.Failure("BLEND_MESH_REFERENCE_INVALID", wrapper);
      }
      malformed = changed;
      malformed.blocks[wrapper].count = 2;
      malformed.Failure("BLEND_MESH_STORAGE_INVALID", wrapper);
      malformed = changed;
      malformed.blocks[wrapper].sdnaIndex = changed.blocks[data].sdnaIndex;
      malformed.blocks[wrapper].length = changed.blocks[data].length;
      malformed.Failure(raw ? "BLEND_DNA_SIZE" : "BLEND_MESH_STORAGE_INVALID", wrapper);
      if (!separate) {
        for (const auto size : {0u, 5u, 7u, std::numeric_limits<unsigned>::max()}) {
          malformed = changed;
          malformed.Set(wrapper, "AttributeArray", "size", size);
          malformed.Failure("BLEND_MESH_STORAGE_INVALID", wrapper);
        }
        for (const auto flag : {2u, 255u}) {
          malformed = changed;
          malformed.Set(wrapper, "AttributeArray", "is_single", flag);
          malformed.Failure("BLEND_MESH_STORAGE_INVALID", wrapper);
        }
      }
    }
  }
}

} // namespace

void CheckNativeMeshes(const std::vector<std::byte>& bytes,
    const std::vector<blend::BlendBlock>& blocks, const blend::DnaSchema& schema,
    const blend::Header& header) {
  for (const auto modern : {false, true}) {
    const Fixture fixture(bytes, blocks, schema, header, modern);
    const auto decoded = Take(fixture.Decode());
    Require(decoded.objects.size() == 2 && decoded.meshes.size() == 1 &&
                decoded.objects[0].mesh == 0 && decoded.objects[1].mesh == 0 &&
                decoded.objects[0].identifier == "One" && decoded.objects[1].identifier == "Two" &&
                decoded.objects[1].parent == 0 && decoded.objects[0].hiddenForRender,
        "Mesh objects share owning data indices while preserving saved membership, parenting and visibility");
    auto named = fixture;
    named.Name(5, 0, "OBmesh");
    named.Bits(5, 6, 0, 1);
    const auto reserved = Take(named.Decode());
    Require(reserved.objects[1].sourceName == "mesh" && reserved.objects[1].identifier == "mesh_1" &&
                reserved.objects[1].parent == 0 && reserved.objects[1].mesh == 0 &&
                SameMesh(decoded.meshes[0], reserved.meshes[0]),
        "Native Mesh parents reserve the data child name without changing shared geometry");
    const auto& mesh = decoded.meshes[0];
    if (modern) {
      CheckEmptyArrays(fixture);
      CheckConstantMeshes(fixture);
    } else {
      CheckLegacyUvs(fixture);
      CheckLegacyGeometry(fixture);
    }
    auto shadowed = fixture;
    shadowed.LegacyGeometry(false);
    for (const auto member : {"mvert", "mloop", "mpoly", "medge"}) {
      shadowed.Set(shadowed.mesh, "Mesh", member, 999);
    }
    Require(SameMesh(mesh, Take(shadowed.Decode()).meshes[0]),
        "Modern geometry does not interpret stale legacy pointers");
    shadowed.Float(shadowed.points, 0, std::numeric_limits<float>::infinity());
    shadowed.Failure("BLEND_MESH_VALUE_INVALID", shadowed.points);
    shadowed = fixture;
    shadowed.LegacyGeometry(false);
    shadowed.Set(shadowed.mesh, "Mesh", "poly_offset_indices", 0);
    if (modern) {
      for (const auto element : {0u, 1u}) {
        shadowed.Bits(shadowed.attributeRecords,
            element * shadowed.Size("Attribute") + shadowed.Member("Attribute", "name").offset,
            shadowed.blocks[shadowed.Text(element == 0 ? "other_position" : "other_corner")].oldAddress, header.pointerSize);
      }
    } else {
      shadowed.Domain("vdata", 0, 0);
    }
    shadowed.Failure("BLEND_MESH_STORAGE_UNSUPPORTED", shadowed.mesh);
    const std::vector<blend::Vector3> expectedPoints = {{0, 0, 0}, {2, 0, 0}, {2, 0, -3}, {0, 0, -3}};
    Require(mesh.sourceName == "Shared" && mesh.points == expectedPoints &&
                mesh.faceVertexCounts == std::vector<std::int32_t>{3, 3} &&
                mesh.faceVertexIndices == std::vector<std::int32_t>{0, 1, 2, 0, 2, 3} &&
                mesh.cornerNormals == std::vector<blend::Vector3>(6, {0, 1, 0}),
        "Both source forms decode exact points, right-handed topology and face-varying flat normals");
    auto custom = fixture;
    const auto packed = custom.AddPackedNormals({0, 0});
    Require(Take(custom.Decode()).meshes[0].cornerNormals == mesh.cornerNormals,
        "Automatic packed normals preserve flat face normals across all layouts");
    for (const auto pair : {std::array<std::int16_t, 2>{32767, 0}, {-21845, 32767}, {-32768, -32768}}) {
      auto changed = custom;
      for (std::size_t corner = 0; corner < 6; ++corner) {
        for (std::size_t axis = 0; axis < 2; ++axis) {
          changed.Bits(packed, corner * 4 + axis * 2, static_cast<std::uint16_t>(pair[axis]), 2);
        }
      }
      const auto normals = Take(changed.Decode()).meshes[0].cornerNormals;
      const auto alpha = pair[0] >= 0 ? pair[0] / 32767.0 * std::numbers::pi / 2
                                      : 2 * std::numbers::pi + pair[0] / 32767.0 * (1.5 * std::numbers::pi);
      const auto reference = std::atan2(3.0, 2.0);
      const auto beta = pair[1] >= 0 ? pair[1] / 32767.0 * reference
                                     : 2 * std::numbers::pi + pair[1] / 32767.0 * (2 * std::numbers::pi - reference);
      const auto expected = blend::ToUsdBasis(
          blend::Vector3{std::sin(alpha) * std::cos(beta), std::sin(alpha) * std::sin(beta), std::cos(alpha)});
      for (std::size_t axis = 0; axis < 3; ++axis) {
        Require(std::abs(normals[0][axis] - expected[axis]) < 2e-5,
            "Signed raw/structured short pairs reconstruct source-space angles");
      }
      for (const auto scale : {0.01f, 10.0f}) {
        auto scaled = changed;
        scaled.Float(1, scaled.Member("Scene", "unit").offset, scale);
        Require(Take(scaled.Decode()).meshes[0].cornerNormals == normals,
            "Custom packed normals are independent of scene unit scale");
      }
    }
    auto malformedCustom = custom;
    --malformedCustom.blocks[packed].length;
    malformedCustom.Failure("BLEND_MESH_STORAGE_INVALID", packed);
    Require(mesh.uvMaps.size() == 2 && mesh.uvMaps[0].sourceName == "First" &&
                mesh.uvMaps[0].values == std::vector<blend::Vector2>{{0, 0}, {1, 0}, {1, 1}, {0, 1}} &&
                mesh.uvMaps[0].indices == std::vector<std::int32_t>{0, 1, 2, 0, 2, 3} &&
                !mesh.uvMaps[0].activeRender && mesh.uvMaps[1].sourceName == "Second" &&
                mesh.uvMaps[1].values == std::vector<blend::Vector2>{{1, 0}} &&
                mesh.uvMaps[1].indices == std::vector<std::int32_t>(6, 0) && mesh.uvMaps[1].activeRender,
        "UV values are unflipped, deterministically indexed and retain the saved render map");
    const auto repeated = Take(fixture.Decode());
    Require(SameMesh(mesh, repeated.meshes[0]), "Repeated decoding retains identical Mesh arrays");
    auto changed = fixture;
    std::reverse(changed.blocks.begin(), changed.blocks.end());
    Require(SameMesh(mesh, Take(changed.Decode()).meshes[0]), "Block enumeration does not change decoded Mesh values");
    for (const float scale : {1.0f, 0.01f, 0.001f, 10.0f}) {
      changed = fixture;
      changed.Float(1, changed.Member("Scene", "unit").offset, scale);
      const auto converted = Take(changed.Decode());
      for (std::size_t vertex = 0; vertex < mesh.points.size(); ++vertex) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
          Require(converted.meshes[0].points[vertex][axis] == mesh.points[vertex][axis] * scale,
              "Mesh distances are normalized once into meters");
        }
      }
      Require(converted.meshes[0].cornerNormals == mesh.cornerNormals &&
                  converted.meshes[0].uvMaps[0].values == mesh.uvMaps[0].values,
          "Normals and UVs are independent of the source distance scale");
    }
    changed = fixture;
    changed.Set(4, "Object", "type", 0);
    changed.Set(4, "Object", "data", 0);
    Require(!Take(changed.Decode()).objects[0].mesh && Take(changed.Decode()).objects[1].mesh == 0,
        "Empty and Mesh objects coexist without changing Empty behavior");
    changed = fixture;
    const auto modifierOffset = changed.Member("Object", "modifiers").offset;
    changed.Bits(4, modifierOffset, 999, header.pointerSize);
    changed.Set(changed.mesh, "Mesh", "key", 999);
    const auto sourceOnly = changed.Decode();
    Require(sourceOnly.HasValue() && sourceOnly.Diagnostics().size() == 2 &&
                sourceOnly.Diagnostics()[0].code == "BLEND_SCENE_EVALUATION_UNAPPLIED" &&
                sourceOnly.Diagnostics()[1].code == "BLEND_MESH_EVALUATION_UNAPPLIED" &&
                SameMesh(sourceOnly.GetValue().meshes[0], mesh),
        "Modifiers and shape keys are reported but neither followed nor applied");
    for (const auto member : {"totvert", "totedge", "totpoly", "totloop"}) {
      changed = fixture;
      changed.Set(changed.mesh, "Mesh", member, std::numeric_limits<std::uint32_t>::max());
      changed.Failure("BLEND_MESH_STORAGE_INVALID", changed.mesh);
    }
    for (const auto invalid : {std::uint64_t{0}, std::uint64_t{999}, fixture.blocks[fixture.offsets].oldAddress + 1}) {
      changed = fixture;
      changed.Set(changed.mesh, "Mesh", "poly_offset_indices", invalid);
      changed.Failure("BLEND_MESH_REFERENCE_INVALID", changed.mesh);
    }
    for (const auto [offset, value] : {std::pair{0, 1}, {4, 2}, {4, 7}, {4, -1}, {8, 5}}) {
      changed = fixture;
      changed.Bits(changed.offsets, offset, static_cast<std::uint32_t>(value), 4);
      changed.Failure("BLEND_MESH_TOPOLOGY_INVALID", changed.offsets);
    }
    for (const auto vertex : {4, -1}) {
      changed = fixture;
      changed.Bits(changed.corners, 0, static_cast<std::uint32_t>(vertex), 4);
      changed.Failure("BLEND_MESH_TOPOLOGY_INVALID", changed.corners);
    }
    for (const auto values : {fixture.points, fixture.uv}) {
      for (const auto invalid : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        changed = fixture;
        changed.Float(values, 0, invalid);
        changed.Failure("BLEND_MESH_VALUE_INVALID", values);
      }
      changed = fixture;
      --changed.blocks[values].length;
      changed.Failure("BLEND_MESH_STORAGE_INVALID", values);
      changed = fixture;
      changed.blocks[values].offset = changed.bytes.size();
      changed.Failure("BLEND_BLOCK_SIZE", values);
      changed = fixture;
      changed.blocks[values].sdnaIndex = static_cast<std::uint32_t>(changed.schema.structs.size());
      changed.Failure("BLEND_DNA_INDEX", values);
    }
    changed = fixture;
    changed.Set(changed.mesh, "Mesh", "totvert", std::numeric_limits<std::int32_t>::max());
    changed.Failure("BLEND_MESH_STORAGE_INVALID", modern ? changed.arrays[0] : changed.points);
    changed = fixture;
    changed.blocks[changed.points].code = {'M', 'E', 0, 0};
    changed.Failure("BLEND_MESH_REFERENCE_INVALID", changed.points);
    changed = fixture;
    ++changed.blocks[changed.points].count;
    changed.Failure("BLEND_MESH_STORAGE_INVALID", changed.points);
    changed = fixture;
    changed.blocks[changed.offsets].count = 2;
    changed.Failure("BLEND_MESH_STORAGE_INVALID", changed.offsets);
    changed = fixture;
    changed.Bits(changed.sharp, 0, 0, 1);
    Require(Take(changed.Decode()).meshes[0].cornerNormals == mesh.cornerNormals,
        "Mixed flat/smooth coplanar faces retain the same corner normals");
    changed.Bits(changed.sharp, 0, 2, 1);
    changed.Failure("BLEND_MESH_STORAGE_INVALID", changed.sharp);
    changed = fixture;
    changed.Bits(changed.sharpEdges, 0, 2, 1);
    changed.Failure("BLEND_MESH_STORAGE_INVALID", changed.sharpEdges);
    changed = fixture;
    changed.Bits(changed.sharp, 0, 0, 1);
    for (const auto invalid : {std::int32_t{-1}, std::int32_t{5}}) {
      auto invalidEdge = changed;
      invalidEdge.Bits(invalidEdge.cornerEdges, 0, static_cast<std::uint32_t>(invalid), 4);
      invalidEdge.Failure("BLEND_MESH_TOPOLOGY_INVALID", invalidEdge.cornerEdges);
    }
    changed.Bits(changed.cornerEdges, 0, 2, 4);
    changed.Failure("BLEND_MESH_TOPOLOGY_INVALID", changed.cornerEdges);
    changed = fixture;
    changed.Bits(changed.sharp, 0, 0, 1);
    changed.Bits(changed.sharp, 1, 0, 1);
    changed.Float(changed.points, 11 * 4, 3);
    const auto smooth = Take(changed.Decode()).meshes[0];
    const blend::Vector3 faceNormal = {3 / std::sqrt(17.0), 2 / std::sqrt(17.0), 2 / std::sqrt(17.0)};
    const auto angle0 = std::atan2(3.0, 2.0);
    const auto angle1 = std::acos(9 / std::sqrt(13.0 * 18.0));
    blend::Vector3 expected = {faceNormal[0] * angle1, angle0 + faceNormal[1] * angle1,
        faceNormal[2] * angle1};
    const auto length = std::hypot(expected[0], expected[1], expected[2]);
    for (auto& component : expected) {
      component /= length;
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
      Require(std::abs(smooth.cornerNormals[0][axis] - expected[axis]) < 1e-12 &&
                  smooth.cornerNormals[0] == smooth.cornerNormals[3],
          "Smooth point normals weight unit face normals by corner angle, not polygon area");
    }
    Require(smooth.cornerNormals[2] == smooth.cornerNormals[4],
        "An entirely smooth mesh shares point normals across incident faces");
    auto noSharpFace = changed;
    if (modern) {
      noSharpFace.Bits(noSharpFace.attributeRecords,
          2 * noSharpFace.Size("Attribute") + noSharpFace.Member("Attribute", "name").offset,
          noSharpFace.blocks[noSharpFace.Text("other_face")].oldAddress, header.pointerSize);
    } else {
      const auto domain = Take(Take(blend::ViewDnaBlock(noSharpFace.bytes, noSharpFace.blocks,
                                        noSharpFace.schema, noSharpFace.header, noSharpFace.mesh))
              .Member("pdata"));
      const auto index = *Take(Take(blend::BuildPointerMap(noSharpFace.blocks))
              .Resolve(Take(Take(domain.Member("layers")).Pointer())));
      noSharpFace.Name(index, noSharpFace.Member("CustomDataLayer", "name").offset, "other_face");
    }
    Require(Take(noSharpFace.Decode()).meshes[0].cornerNormals == smooth.cornerNormals,
        "Absent sharp_face storage means every polygon is smooth");
    auto cancelled = changed;
    cancelled.Float(cancelled.points, 9 * 4, 2);
    cancelled.Float(cancelled.points, 10 * 4, 0);
    cancelled.Float(cancelled.points, 11 * 4, 0);
    cancelled.Failure("BLEND_MESH_NORMALS_INVALID", cancelled.mesh);
    for (const float scale : {0.01f, 10.0f}) {
      auto scaled = changed;
      scaled.Float(1, scaled.Member("Scene", "unit").offset, scale);
      Require(Take(scaled.Decode()).meshes[0].cornerNormals == smooth.cornerNormals,
          "Smooth normals are not unit-scaled");
    }
    changed.Bits(changed.sharpEdges, 2, 1, 1);
    const auto split = Take(changed.Decode()).meshes[0];
    for (std::size_t corner = 0; corner < 6; ++corner) {
      const auto expectedNormal = corner < 3 ? blend::Vector3{0, 1, 0} : faceNormal;
      for (std::size_t axis = 0; axis < 3; ++axis) {
        Require(std::abs(split.cornerNormals[corner][axis] - expectedNormal[axis]) < 1e-12,
            "A sharp shared edge splits both endpoint corner fans");
      }
    }
    changed.blocks[changed.cornerEdges].length -= 4;
    changed.Failure("BLEND_MESH_STORAGE_INVALID", changed.cornerEdges);
    changed = fixture;
    changed.Bits(changed.sharp, 0, 0, 1);
    if (modern) {
      changed.Bits(changed.attributeRecords,
          5 * changed.Size("Attribute") + changed.Member("Attribute", "name").offset,
          changed.blocks[changed.Text(".unused_edge")].oldAddress, header.pointerSize);
    } else {
      changed.Name(changed.attributeRecords,
          3 * changed.Size("CustomDataLayer") + changed.Member("CustomDataLayer", "name").offset,
          ".unused_edge");
    }
    changed.Failure("BLEND_MESH_STORAGE_UNSUPPORTED", changed.mesh);
    changed = fixture;
    for (std::size_t coordinate = 0; coordinate < 12; ++coordinate) {
      changed.Float(changed.points, coordinate * 4, 0);
    }
    changed.Failure("BLEND_MESH_NORMALS_INVALID", changed.mesh);
    changed = fixture;
    changed.header.version = 404;
    changed.Failure("BLEND_MESH_STORAGE_UNSUPPORTED", changed.mesh);
    changed = fixture;
    changed.Set(changed.mesh, "Mesh", "default_uv_map_attribute", changed.blocks[changed.Text("Absent")].oldAddress);
    changed.Failure("BLEND_MESH_STORAGE_INVALID", changed.mesh);
    if (modern) {
      changed = fixture;
      changed.Set(changed.arrays[0], "AttributeArray", "size", 5);
      changed.Failure("BLEND_MESH_STORAGE_INVALID", changed.arrays[0]);
      changed = fixture;
      changed.Set(changed.arrays[0], "AttributeArray", "is_single", 1);
      changed.Failure("BLEND_MESH_STORAGE_INVALID", changed.points);
      changed = fixture;
      changed.Set(changed.attributeRecords, "Attribute", "storage_type", 2);
      changed.Failure("BLEND_MESH_STORAGE_UNSUPPORTED", changed.attributeRecords);
      changed = fixture;
      changed.Set(changed.attributeRecords, "Attribute", "domain", 3);
      changed.Failure("BLEND_MESH_STORAGE_UNSUPPORTED", changed.attributeRecords);
      changed = fixture;
      changed.Set(changed.arrays[0], "AttributeArray", "data", 999);
      changed.Failure("BLEND_MESH_REFERENCE_INVALID", changed.arrays[0]);
      changed = fixture;
      changed.Set(changed.attributeRecords, "Attribute", "name", changed.blocks[changed.Text("custom_normal")].oldAddress);
      changed.Failure("BLEND_MESH_NORMALS_UNSUPPORTED", changed.attributeRecords);
      changed = fixture;
      changed.Set(changed.attributeRecords, "Attribute", "name", 999);
      changed.Failure("BLEND_MESH_REFERENCE_INVALID", changed.attributeRecords);
      changed = fixture;
      const auto offset = changed.Size("Attribute") + changed.Member("Attribute", "name").offset;
      changed.Bits(changed.attributeRecords, offset, changed.blocks[changed.Text("position")].oldAddress, header.pointerSize);
      changed.Failure("BLEND_MESH_STORAGE_UNSUPPORTED", changed.attributeRecords);
      changed = fixture;
      const auto uvOffset = 4 * changed.Size("Attribute") + changed.Member("Attribute", "name").offset;
      changed.Bits(changed.attributeRecords, uvOffset, changed.blocks[changed.Text("First")].oldAddress, header.pointerSize);
      changed.Failure("BLEND_MESH_STORAGE_INVALID", changed.attributeRecords);
    } else {
      changed = fixture;
      const auto layers = Take(Take(blend::ViewDnaBlock(changed.bytes, changed.blocks, changed.schema, changed.header, changed.mesh)).Member("vdata"));
      const auto pointer = Take(Take(layers.Member("layers")).Pointer());
      const auto index = *Take(Take(blend::BuildPointerMap(changed.blocks)).Resolve(pointer));
      changed.Set(index, "CustomDataLayer", "flag", 8);
      changed.Failure("BLEND_MESH_STORAGE_UNSUPPORTED", index);
      changed = fixture;
      changed.Set(changed.attributeRecords, "CustomDataLayer", "type", 41);
      changed.Failure("BLEND_MESH_NORMALS_UNSUPPORTED", changed.attributeRecords);
      changed = fixture;
      changed.Set(changed.attributeRecords, "CustomDataLayer", "type", 16);
      changed.Failure("BLEND_MESH_STORAGE_UNSUPPORTED", changed.attributeRecords);
      changed = fixture;
      changed.Bits(changed.attributeRecords, changed.Size("CustomDataLayer") + changed.Member("CustomDataLayer", "active_rnd").offset, 99, 4);
      changed.Failure("BLEND_MESH_STORAGE_INVALID", changed.attributeRecords);
      changed = fixture;
      const auto nameOffset = changed.Member("CustomDataLayer", "name").offset;
      for (std::uint64_t byte = 0; byte < 32; ++byte) {
        changed.Bits(changed.attributeRecords, nameOffset + byte, 'x', 1);
      }
      changed.Failure("BLEND_MESH_STORAGE_INVALID", changed.attributeRecords);
    }
    changed = fixture;
    for (const auto member : {"totvert", "totedge", "totpoly", "totloop", "poly_offset_indices", "default_uv_map_attribute"}) {
      changed.Set(changed.mesh, "Mesh", member, 0);
    }
    const auto storageOffset = changed.Member("Mesh", "attribute_storage").offset;
    changed.Bits(changed.mesh, storageOffset + changed.Member("AttributeStorage", "dna_attributes").offset, 0, header.pointerSize);
    changed.Bits(changed.mesh, storageOffset + changed.Member("AttributeStorage", "dna_attributes_num").offset, 0, 4);
    for (const auto domain : {"vdata", "edata", "pdata", "ldata"}) {
      const auto offset = changed.Member("Mesh", domain).offset;
      changed.Bits(changed.mesh, offset + changed.Member("CustomData", "layers").offset, 0, header.pointerSize);
      changed.Bits(changed.mesh, offset + changed.Member("CustomData", "totlayer").offset, 0, 4);
    }
    const auto empty = changed.Decode();
    Require(empty.HasValue() && empty.GetValue().meshes.size() == 1 &&
                empty.GetValue().meshes[0].points.empty() && empty.GetValue().meshes[0].faceVertexCounts.empty() &&
                empty.GetValue().meshes[0].cornerNormals.empty() && empty.GetValue().meshes[0].uvMaps.empty() &&
                empty.Diagnostics().size() == 1 && empty.Diagnostics()[0].code == "BLEND_MESH_EMPTY" &&
                empty.Diagnostics()[0].blockIndex == changed.mesh && empty.Diagnostics()[0].recoverable,
        "Empty Mesh data remains owning, shared and explicitly diagnosed once");
    changed = fixture;
    auto owned = Take(changed.Decode());
    changed.bytes.clear();
    changed.blocks.clear();
    changed.schema = {};
    Require(SameMesh(owned.meshes[0], mesh), "Mesh and UV results retain no borrowed storage");
  }
}

void CheckCorpusMesh(const std::vector<std::byte>& bytes,
    const std::vector<blend::BlendBlock>& blocks, const blend::DnaSchema& schema,
    const blend::Header& header, const blend::SelectedSceneObjects& selected) {
  auto input = bytes;
  const auto* objectStruct = schema.FindStruct("Object");
  for (const auto& object : selected.objects) {
    const auto offset = blocks[object.blockIndex].offset;
    Store(input, offset + objectStruct->FindMember("rotmode")->offset, 1, 2, header.byteOrder);
    if (object.sourceName != "Cube") {
      Store(input, offset + objectStruct->FindMember("type")->offset, 0, 2, header.byteOrder);
      Store(input, offset + objectStruct->FindMember("data")->offset, 0, header.pointerSize, header.byteOrder);
    }
  }
  const auto result = blend::DecodeScene(input, blocks, schema, header, {10000, 64});
  const auto decoded = Take(result);
  Require(result.Diagnostics().empty() && decoded.objects.size() == 3 && decoded.meshes.size() == 1,
      "Both real corpus Mesh payloads decode without changing their stored Mesh bytes");
  const auto cube = std::find_if(decoded.objects.begin(), decoded.objects.end(),
      [](const auto& object) { return object.sourceName == "Cube"; });
  Require(cube != decoded.objects.end() && cube->mesh == 0, "Real Cube references the owning Mesh IR");
  const auto& mesh = decoded.meshes[0];
  std::vector<blend::Vector3> points = {{1, 1, 1}, {1, 1, -1}, {1, -1, 1}, {1, -1, -1},
      {-1, 1, 1}, {-1, 1, -1}, {-1, -1, 1}, {-1, -1, -1}};
  const blend::UnitConversion units(decoded.metadata.sourceUnitScale);
  for (auto& point : points) {
    point = units.Position(point);
  }
  Require(mesh.sourceName == "Cube" && mesh.points == points &&
              mesh.faceVertexCounts == std::vector<std::int32_t>(6, 4) &&
              mesh.faceVertexIndices == std::vector<std::int32_t>{0, 4, 6, 2, 3, 2, 6, 7,
                                            7, 6, 4, 5, 5, 1, 3, 7, 1, 0, 2, 3, 5, 4, 0, 1} &&
              mesh.cornerNormals.size() == 24 &&
              mesh.uvMaps.size() == 1 && mesh.uvMaps[0].sourceName == "UVMap" &&
              mesh.uvMaps[0].activeRender && mesh.uvMaps[0].indices.size() == 24,
      "4.5 CustomData and 5.2 AttributeArray cube geometry have pinned source values");
  constexpr std::array<blend::Vector3, 6> normals = {{{0, 1, 0}, {0, 0, 1}, {-1, 0, 0},
      {0, -1, 0}, {1, 0, 0}, {0, 0, -1}}};
  for (std::size_t corner = 0; corner < mesh.cornerNormals.size(); ++corner) {
    Require(mesh.cornerNormals[corner] == normals[corner / 4],
        "All real cube corners have the correct converted outward face normal");
  }
  const auto& uv = mesh.uvMaps[0];
  Require(uv.values[uv.indices[0]] == blend::Vector2{0.625, 0.5} &&
              uv.values[uv.indices[1]] == blend::Vector2{0.875, 0.5} &&
              uv.values[uv.indices[2]] == blend::Vector2{0.875, 0.75} &&
              uv.values[uv.indices[3]] == blend::Vector2{0.625, 0.75},
      "Real cube UVs retain exact unflipped source coordinates");
  const auto repeated = Take(blend::DecodeScene(input, blocks, schema, header, {10000, 64}));
  Require(SameMesh(mesh, repeated.meshes[0]), "Real corpus repeated Mesh reads are identical");
}
