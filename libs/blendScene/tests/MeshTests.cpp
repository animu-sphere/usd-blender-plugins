#include <blendScene/Decode.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

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
    Type("short", 2);
    Type("int8_t", 1);
    Type("int64_t", 8);
    Type("CustomDataExternal", 0);
    Type("Key", 0);
    Structure("vec3f", {{"float", "x"}, {"float", "y"}, {"float", "z"}});
    Structure("vec2f", {{"float", "x"}, {"float", "y"}});
    Structure("MIntProperty", {{"int", "i"}});
    Structure("CustomDataLayer", {{"int", "type"}, {"int", "flag"}, {"int", "active_rnd"},
                                     {"char", "name", false, 32}, {"void", "data", true}});
    Structure("CustomData", {{"CustomDataLayer", "layers", true},
                                {"int", "totlayer"}, {"CustomDataExternal", "external", true}});
    Structure("Attribute", {{"char", "name", true}, {"short", "data_type"},
                               {"int8_t", "domain"}, {"int8_t", "storage_type"}, {"void", "data", true}});
    Structure("AttributeArray", {{"void", "data", true}, {"int64_t", "size"}, {"int8_t", "is_single"}});
    Structure("AttributeStorage", {{"Attribute", "dna_attributes", true}, {"int", "dna_attributes_num"}});
    const auto meshStruct = Structure("Mesh", {{"ID", "id"}, {"int", "totvert"},
                                                  {"int", "totpoly"}, {"int", "totloop"}, {"int", "poly_offset_indices", true},
                                                  {"AttributeStorage", "attribute_storage"}, {"CustomData", "vdata"},
                                                  {"CustomData", "edata"}, {"CustomData", "pdata"}, {"CustomData", "ldata"},
                                                  {"char", "default_uv_map_attribute", true}, {"Key", "key", true}});
    mesh = static_cast<std::uint32_t>(blocks.size() - 2);
    blocks[mesh].sdnaIndex = meshStruct;
    blocks[mesh].offset = bytes.size();
    blocks[mesh].length = schema.types[schema.structs[meshStruct].typeIndex].length;
    bytes.resize(bytes.size() + static_cast<std::size_t>(blocks[mesh].length));
    Name(mesh, 0, "MEShared");
    Set(mesh, "Mesh", "totvert", 4);
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
      attributeRecords = Data("Attribute", 5, 0);
      const auto storage = Member("Mesh", "attribute_storage").offset;
      Bits(mesh, storage + Member("AttributeStorage", "dna_attributes").offset, blocks[attributeRecords].oldAddress, header.pointerSize);
      Bits(mesh, storage + Member("AttributeStorage", "dna_attributes_num").offset, 5, 4);
      Attribute(0, "position", 7, 0, points, 4);
      Attribute(1, ".corner_vert", 3, 3, corners, 6);
      Attribute(2, "sharp_face", 0, 2, sharp, 2);
      Attribute(3, "First", 6, 3, uv, 6);
      Attribute(4, "Second", 6, 3, secondUv, 6);
      Set(mesh, "Mesh", "default_uv_map_attribute", blocks[Text("Second")].oldAddress);
    } else {
      const auto vertexLayers = Data("CustomDataLayer", 1, 0);
      Domain("vdata", vertexLayers, 1);
      Layer(vertexLayers, 0, "position", 48, points);
      const auto faceLayers = Data("CustomDataLayer", 1, 0);
      Domain("pdata", faceLayers, 1);
      Layer(faceLayers, 0, "sharp_face", 50, sharp);
      attributeRecords = Data("CustomDataLayer", 3, 0);
      Domain("ldata", attributeRecords, 3);
      Layer(attributeRecords, 0, ".corner_vert", 11, corners);
      Layer(attributeRecords, 1, "First", 49, uv);
      Layer(attributeRecords, 2, "Second", 49, secondUv);
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
    Bits(mesh, offset + Member("CustomData", "layers").offset, blocks[layers].oldAddress, header.pointerSize);
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

  std::vector<std::byte> bytes;
  std::vector<blend::BlendBlock> blocks;
  blend::DnaSchema schema;
  blend::Header header;
  bool modern;
  std::uint32_t mesh, points, corners, offsets, sharp, uv, attributeRecords;
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

} // namespace

void CheckNativeMeshes(const std::vector<std::byte>& bytes,
    const std::vector<blend::BlendBlock>& blocks, const blend::DnaSchema& schema,
    const blend::Header& header) {
  for (const auto modern : {false, true}) {
    const Fixture fixture(bytes, blocks, schema, header, modern);
    const auto decoded = Take(fixture.Decode());
    Require(decoded.objects.size() == 2 && decoded.meshes.size() == 1 &&
                decoded.objects[0].mesh == 0 && decoded.objects[1].mesh == 0 &&
                decoded.objects[1].parent == 0 && decoded.objects[0].hiddenForRender,
        "Mesh objects share owning data indices while preserving saved membership, parenting and visibility");
    const auto& mesh = decoded.meshes[0];
    const std::vector<blend::Vector3> expectedPoints = {{0, 0, 0}, {2, 0, 0}, {2, 0, -3}, {0, 0, -3}};
    Require(mesh.sourceName == "Shared" && mesh.points == expectedPoints &&
                mesh.faceVertexCounts == std::vector<std::int32_t>{3, 3} &&
                mesh.faceVertexIndices == std::vector<std::int32_t>{0, 1, 2, 0, 2, 3} &&
                mesh.cornerNormals == std::vector<blend::Vector3>(6, {0, 1, 0}),
        "Both source forms decode exact points, right-handed topology and face-varying flat normals");
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
    for (const auto member : {"totvert", "totpoly", "totloop"}) {
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
    changed.Failure("BLEND_MESH_NORMALS_UNSUPPORTED", changed.sharp);
    changed.Bits(changed.sharp, 0, 2, 1);
    changed.Failure("BLEND_MESH_STORAGE_INVALID", changed.sharp);
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
      changed.Failure("BLEND_MESH_STORAGE_UNSUPPORTED", changed.arrays[0]);
      changed = fixture;
      changed.Set(changed.attributeRecords, "Attribute", "storage_type", 1);
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
    for (const auto member : {"totvert", "totpoly", "totloop", "poly_offset_indices", "default_uv_map_attribute"}) {
      changed.Set(changed.mesh, "Mesh", member, 0);
    }
    const auto storageOffset = changed.Member("Mesh", "attribute_storage").offset;
    changed.Bits(changed.mesh, storageOffset + changed.Member("AttributeStorage", "dna_attributes").offset, 0, header.pointerSize);
    changed.Bits(changed.mesh, storageOffset + changed.Member("AttributeStorage", "dna_attributes_num").offset, 0, 4);
    for (const auto domain : {"vdata", "pdata", "ldata"}) {
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
