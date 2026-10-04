#include "DecodeInternal.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <stdexcept>

namespace blend::detail {
namespace {

enum class Kind { Other,
  Integer,
  Boolean,
  Float2,
  Float3 };

struct Attribute {
  std::string name;
  std::int64_t domain;
  Kind kind;
  DnaValueView source;
  std::uint32_t index;
  bool modern;
};

class MeshDecoder {
public:
  MeshDecoder(std::span<const std::byte> bytes,
      std::span<const BlendBlock> blocks, const DnaSchema& schema,
      const Header& header, const PointerMap& pointers, std::uint32_t index,
      const UnitConversion& units, std::vector<Diagnostic>& diagnostics)
      : bytes_(bytes), blocks_(blocks), schema_(schema), header_(header),
        pointers_(pointers), index_(index), units_(units), diagnostics_(diagnostics) {
  }

  Mesh Run() {
    const auto source = Take(ViewDnaBlock(bytes_, blocks_, schema_, header_, index_));
    if (header_.version != 405 && (header_.version < 500 || header_.version >= 600)) {
      Fail("BLEND_MESH_STORAGE_UNSUPPORTED", "Mesh decoding targets 4.5 and 5.x storage", index_);
    }
    Mesh mesh;
    mesh.sourceName = String(Take(Take(source.Member("id")).Member("name")), index_).substr(2);
    const auto vertices = Count(source, "totvert", index_);
    const auto faces = Count(source, "totpoly", index_);
    const auto corners = Count(source, "totloop", index_);
    if ((faces == 0) != (corners == 0)) {
      Fail("BLEND_MESH_TOPOLOGY_INVALID", "Face and corner counts must both be empty or nonempty", index_);
    }
    ReadAttributes(source);
    const auto position = Find("position", 0, Kind::Float3, vertices != 0);
    if (position) {
      const auto values = Values(*position, vertices);
      mesh.points.reserve(vertices);
      for (std::uint32_t vertex = 0; vertex < vertices; ++vertex) {
        const auto point = Vector<3>(values, vertex);
        sourcePoints_.push_back(point);
        try {
          mesh.points.push_back(units_.Position(point));
        } catch (const std::invalid_argument& error) {
          UnitFailure(error);
        } catch (const std::overflow_error& error) {
          UnitFailure(error);
        }
      }
    }
    const auto cornerVertices = Find(".corner_vert", 3, Kind::Integer, corners != 0);
    if (cornerVertices) {
      const auto values = Values(*cornerVertices, corners);
      mesh.faceVertexIndices.reserve(corners);
      for (std::uint32_t corner = 0; corner < corners; ++corner) {
        const auto vertex = Integer(values, corner);
        if (vertex < 0 || static_cast<std::uint32_t>(vertex) >= vertices) {
          Fail("BLEND_MESH_TOPOLOGY_INVALID", "Corner vertex index is outside the source points", values.index);
        }
        mesh.faceVertexIndices.push_back(vertex);
      }
    }
    const auto offsetsAddress = Pointer(source, "poly_offset_indices", "int", index_);
    if (faces != 0 || offsetsAddress != 0) {
      const auto offsets = Array(offsetsAddress, std::uint64_t{faces} + 1, Kind::Integer, index_);
      if (Integer(offsets, 0) != 0) {
        Fail("BLEND_MESH_TOPOLOGY_INVALID", "Face offsets must start at zero", offsets.index);
      }
      std::int32_t previous = 0;
      mesh.faceVertexCounts.reserve(faces);
      for (std::uint32_t face = 0; face < faces; ++face) {
        const auto end = Integer(offsets, std::uint64_t{face} + 1);
        if (end < previous || static_cast<std::uint32_t>(end) > corners || end - previous < 3) {
          Fail("BLEND_MESH_TOPOLOGY_INVALID", "Face offsets must delimit polygons of at least three corners", offsets.index);
        }
        mesh.faceVertexCounts.push_back(end - previous);
        previous = end;
      }
      if (static_cast<std::uint32_t>(previous) != corners) {
        Fail("BLEND_MESH_TOPOLOGY_INVALID", "Final face offset must equal the corner count", offsets.index);
      }
    }
    ReadNormals(mesh, source, faces);
    ReadUvs(mesh, source, corners);
    if (mesh.faceVertexCounts.empty()) {
      diagnostics_.push_back({"BLEND_MESH_EMPTY", Severity::Warning,
          "Source Mesh has no polygons; points and empty topology are retained",
          blocks_[index_].offset, index_, mesh.sourceName, true});
    }
    if (schema_.FindStruct("Mesh")->FindMember("key") &&
        Pointer(source, "key", "Key", index_) != 0) {
      diagnostics_.push_back({"BLEND_MESH_EVALUATION_UNAPPLIED", Severity::Unsupported,
          "Source Mesh positions are used; shape keys are not evaluated",
          blocks_[index_].offset, index_, mesh.sourceName, true});
    }
    return mesh;
  }

private:
  [[noreturn]] void Fail(const char* code, const char* message, std::uint32_t index) const {
    throw Diagnostic{code, Severity::Fatal, message, blocks_[index].offset, index, {}, false};
  }

  [[noreturn]] void UnitFailure(const std::exception& error) const {
    const std::string message = error.what();
    throw Diagnostic{message.substr(0, message.find(':')), Severity::Fatal,
        message, blocks_[index_].offset, index_, {}, false};
  }

  std::int64_t Scalar(const DnaValueView& source, std::string_view member,
      std::string_view type, std::uint16_t width, std::uint32_t index) const {
    const auto value = Take(source.Member(member));
    if (value.Type().name != type || value.Type().length != width ||
        value.PointerLevel() != 0 || !value.ArrayDimensions().empty()) {
      Fail("BLEND_MESH_STORAGE_INVALID", "Mesh scalar has incompatible SDNA storage", index);
    }
    return Take(value.SignedInteger());
  }

  std::uint32_t Count(const DnaValueView& source, std::string_view member,
      std::uint32_t index) const {
    const auto count = Scalar(source, member, "int", 4, index);
    if (count < 0) {
      Fail("BLEND_MESH_STORAGE_INVALID", "Mesh counts must be nonnegative", index);
    }
    return static_cast<std::uint32_t>(count);
  }

  std::uint64_t Pointer(const DnaValueView& source, std::string_view member,
      std::string_view type, std::uint32_t index) const {
    const auto value = Take(source.Member(member));
    if (value.Type().name != type || value.PointerLevel() != 1 ||
        !value.ArrayDimensions().empty()) {
      Fail("BLEND_MESH_STORAGE_INVALID", "Mesh pointer has incompatible SDNA storage", index);
    }
    return Take(value.Pointer());
  }

  std::uint32_t Resolve(std::uint64_t address, std::uint32_t referrer) const {
    const auto index = Take(pointers_.Resolve(address));
    if (!index) {
      Fail("BLEND_MESH_REFERENCE_INVALID", "Mesh storage pointer is null, absent or interior", referrer);
    }
    if (blocks_[*index].code != std::array<char, 4>{'D', 'A', 'T', 'A'}) {
      Fail("BLEND_MESH_REFERENCE_INVALID", "Mesh storage must reference a DATA block", *index);
    }
    return *index;
  }

  DnaValueView Records(std::uint64_t address, std::uint64_t count,
      std::string_view type, std::uint32_t referrer) const {
    const auto index = Resolve(address, referrer);
    if (blocks_[index].count != count) {
      Fail("BLEND_MESH_STORAGE_INVALID", "Mesh storage record count differs from the declared count", index);
    }
    const auto result = Take(ViewDnaBlock(bytes_, blocks_, schema_, header_, index));
    if (result.Type().name != type) {
      Fail("BLEND_MESH_STORAGE_INVALID", "Mesh storage record has an incompatible SDNA type", index);
    }
    return result;
  }

  std::string String(const DnaValueView& value, std::uint32_t index) const {
    const auto end = std::find(value.Bytes().begin(), value.Bytes().end(), std::byte{0});
    if (value.Type().name != "char" || value.Type().length != 1 ||
        value.PointerLevel() != 0 || value.ArrayDimensions().size() != 1 ||
        end == value.Bytes().end()) {
      Fail("BLEND_MESH_STORAGE_INVALID", "Mesh attribute name must be a terminated char array", index);
    }
    return {reinterpret_cast<const char*>(value.Bytes().data()),
        static_cast<std::size_t>(end - value.Bytes().begin())};
  }

  std::span<const std::byte> Payload(std::uint32_t index) const {
    const auto& block = blocks_[index];
    if (block.offset > bytes_.size() || block.length > bytes_.size() - block.offset) {
      Fail("BLEND_BLOCK_SIZE", "Mesh storage block exceeds the supplied bytes", index);
    }
    if (block.sdnaIndex >= schema_.structs.size() ||
        schema_.structs[block.sdnaIndex].typeIndex >= schema_.types.size()) {
      Fail("BLEND_DNA_INDEX", "Mesh storage SDNA index is out of range", index);
    }
    return bytes_.subspan(static_cast<std::size_t>(block.offset), static_cast<std::size_t>(block.length));
  }

  bool Raw(std::uint32_t index) const {
    Payload(index);
    const auto& type = schema_.types[schema_.structs[blocks_[index].sdnaIndex].typeIndex];
    return type.name == "raw_data" && type.length == 0;
  }

  std::string PointerString(std::uint64_t address, std::uint32_t referrer) const {
    const auto index = Resolve(address, referrer);
    const auto bytes = Payload(index);
    const auto end = std::find(bytes.begin(), bytes.end(), std::byte{0});
    if (!Raw(index) || blocks_[index].count != 1 || end == bytes.end()) {
      Fail("BLEND_MESH_STORAGE_INVALID", "Saved attribute name must reference terminated raw character data", index);
    }
    return {reinterpret_cast<const char*>(bytes.data()), static_cast<std::size_t>(end - bytes.begin())};
  }

  void Add(Attribute attribute) {
    if (attribute.name.empty() ||
        !names_.emplace(std::pair{attribute.domain, attribute.name}, attributes_.size()).second) {
      Fail("BLEND_MESH_STORAGE_INVALID", "Mesh attribute names must be nonempty and unique within a domain", attribute.index);
    }
    if (attribute.name == "custom_normal" || attribute.name == ".custom_normal" ||
        (attribute.domain == 3 && attribute.name == "normal")) {
      Fail("BLEND_MESH_NORMALS_UNSUPPORTED", "Custom corner normal storage is not decoded", attribute.index);
    }
    attributes_.push_back(std::move(attribute));
  }

  void ReadAttributes(const DnaValueView& mesh) {
    const auto storage = Take(mesh.Member("attribute_storage"));
    if (storage.Type().name != "AttributeStorage" || storage.PointerLevel() != 0 ||
        !storage.ArrayDimensions().empty()) {
      Fail("BLEND_MESH_STORAGE_INVALID", "Mesh attribute storage must be embedded", index_);
    }
    const auto count = Count(storage, "dna_attributes_num", index_);
    const auto address = Pointer(storage, "dna_attributes", "Attribute", index_);
    if (count != 0) {
      Records(address, count, "Attribute", index_);
      const auto index = Resolve(address, index_);
      for (std::uint32_t element = 0; element < count; ++element) {
        const auto source = Take(ViewDnaBlock(bytes_, blocks_, schema_, header_, index, element));
        const auto type = Scalar(source, "data_type", "short", 2, index);
        const auto domain = Scalar(source, "domain", "int8_t", 1, index);
        const auto kind = type == 0 ? Kind::Boolean : type == 3 ? Kind::Integer
                                                  : type == 6   ? Kind::Float2
                                                  : type == 7   ? Kind::Float3
                                                                : Kind::Other;
        Add({PointerString(Pointer(source, "name", "char", index), index),
            domain, kind, source, index, true});
      }
    } else if (address != 0) {
      Fail("BLEND_MESH_STORAGE_INVALID", "Empty attribute storage must have a null record pointer", index_);
    }
    std::int64_t domain = 0;
    for (const auto member : {"vdata", "edata", "pdata", "ldata"}) {
      const auto data = Take(mesh.Member(member));
      if (data.Type().name != "CustomData" || data.PointerLevel() != 0 ||
          !data.ArrayDimensions().empty()) {
        Fail("BLEND_MESH_STORAGE_INVALID", "Mesh domain data must be embedded CustomData", index_);
      }
      const auto layers = Count(data, "totlayer", index_);
      const auto layerAddress = Pointer(data, "layers", "CustomDataLayer", index_);
      if (Pointer(data, "external", "CustomDataExternal", index_) != 0) {
        Fail("BLEND_MESH_STORAGE_UNSUPPORTED", "External CustomData storage is not read", index_);
      }
      if (count != 0 && layers != 0) {
        Fail("BLEND_MESH_STORAGE_UNSUPPORTED", "Mixed Attribute and CustomData Mesh storage is not decoded", index_);
      }
      if (layers != 0) {
        Records(layerAddress, layers, "CustomDataLayer", index_);
        const auto index = Resolve(layerAddress, index_);
        for (std::uint32_t element = 0; element < layers; ++element) {
          const auto source = Take(ViewDnaBlock(bytes_, blocks_, schema_, header_, index, element));
          const auto type = Scalar(source, "type", "int", 4, index);
          if (type == 41) {
            Fail("BLEND_MESH_NORMALS_UNSUPPORTED", "Packed custom corner normals are not decoded", index);
          }
          if (type == 16) {
            Fail("BLEND_MESH_STORAGE_UNSUPPORTED", "Legacy MLoopUV layers are not decoded", index);
          }
          const auto kind = type == 11 ? Kind::Integer : type == 48 ? Kind::Float3
                                                     : type == 49   ? Kind::Float2
                                                     : type == 50   ? Kind::Boolean
                                                                    : Kind::Other;
          Add({String(Take(source.Member("name")), index), domain, kind, source, index, false});
        }
      } else if (layerAddress != 0) {
        Fail("BLEND_MESH_STORAGE_INVALID", "Empty CustomData must have a null layer pointer", index_);
      }
      ++domain;
    }
  }

  const Attribute* Find(std::string_view name, std::int64_t domain,
      Kind kind, bool required) const {
    const Attribute* result = nullptr;
    for (const auto& attribute : attributes_) {
      if (attribute.name == name) {
        if (attribute.domain != domain || attribute.kind != kind) {
          Fail("BLEND_MESH_STORAGE_UNSUPPORTED", "Core Mesh attribute has an unsupported type or domain", attribute.index);
        }
        result = &attribute;
      }
    }
    if (!result && required) {
      Fail("BLEND_MESH_STORAGE_UNSUPPORTED", "Required source Mesh attribute is absent", index_);
    }
    return result;
  }

  struct ValuesView {
    std::span<const std::byte> bytes;
    std::uint32_t index;
    Kind kind;
    bool raw;
  };

  ValuesView Array(std::uint64_t address, std::uint64_t count,
      Kind kind, std::uint32_t referrer) const {
    const auto index = Resolve(address, referrer);
    const auto bytes = Payload(index);
    const std::uint64_t stride = kind == Kind::Float3 ? 12 : kind == Kind::Float2 ? 8
                                                         : kind == Kind::Integer  ? 4
                                                                                  : 1;
    if (bytes.size() % stride != 0 || bytes.size() / stride != count) {
      Fail("BLEND_MESH_STORAGE_INVALID", "Mesh array length differs from the domain element count", index);
    }
    const auto raw = Raw(index);
    if (raw) {
      if (blocks_[index].count != 1) {
        Fail("BLEND_MESH_STORAGE_INVALID", "Raw Mesh arrays must have one block element", index);
      }
    } else {
      const auto type = kind == Kind::Float3 ? "vec3f" : kind == Kind::Float2 ? "vec2f"
                                                     : kind == Kind::Integer  ? "MIntProperty"
                                                                              : "MBoolProperty";
      Records(address, count, type, referrer);
    }
    return {bytes, index, kind, raw};
  }

  ValuesView Values(const Attribute& attribute, std::uint32_t count) const {
    auto address = Pointer(attribute.source, "data", "void", attribute.index);
    if (attribute.modern) {
      if (Scalar(attribute.source, "storage_type", "int8_t", 1, attribute.index) != 0) {
        Fail("BLEND_MESH_STORAGE_UNSUPPORTED", "Only dense AttributeArray storage is decoded", attribute.index);
      }
      const auto array = Records(address, 1, "AttributeArray", attribute.index);
      const auto index = Resolve(address, attribute.index);
      if (Scalar(array, "size", "int64_t", 8, index) != count) {
        Fail("BLEND_MESH_STORAGE_INVALID", "AttributeArray size differs from its Mesh domain", index);
      }
      if (Scalar(array, "is_single", "int8_t", 1, index) != 0) {
        Fail("BLEND_MESH_STORAGE_UNSUPPORTED", "Constant AttributeArray storage is not decoded", index);
      }
      address = Pointer(array, "data", "void", index);
      if (count == 0 && address == 0) {
        return {{}, index, attribute.kind, true};
      }
      return Array(address, count, attribute.kind, index);
    }
    if (Scalar(attribute.source, "flag", "int", 4, attribute.index) != 0) {
      Fail("BLEND_MESH_STORAGE_UNSUPPORTED", "Flagged CustomData storage is not decoded", attribute.index);
    }
    if (count == 0 && address == 0) {
      return {{}, attribute.index, attribute.kind, true};
    }
    return Array(address, count, attribute.kind, attribute.index);
  }

  std::uint32_t Bits(std::span<const std::byte> bytes) const {
    std::uint32_t value = 0;
    for (std::size_t byte = 0; byte < bytes.size(); ++byte) {
      const auto shift = header_.byteOrder == ByteOrder::Little ? byte : bytes.size() - 1 - byte;
      value |= std::to_integer<std::uint32_t>(bytes[byte]) << (8 * shift);
    }
    return value;
  }

  std::int32_t Integer(const ValuesView& values, std::uint64_t element) const {
    if (values.raw) {
      return std::bit_cast<std::int32_t>(Bits(values.bytes.subspan(static_cast<std::size_t>(element * 4), 4)));
    }
    const auto source = Take(ViewDnaBlock(bytes_, blocks_, schema_, header_, values.index, element));
    return static_cast<std::int32_t>(Scalar(source, "i", "int", 4, values.index));
  }

  bool Boolean(const ValuesView& values, std::uint32_t element) const {
    std::uint64_t value;
    if (values.raw) {
      value = std::to_integer<std::uint8_t>(values.bytes[element]);
    } else {
      const auto source = Take(ViewDnaBlock(bytes_, blocks_, schema_, header_, values.index, element));
      const auto member = Take(source.Member("b"));
      if (member.Type().name != "uchar" || member.Type().length != 1 ||
          member.PointerLevel() != 0 || !member.ArrayDimensions().empty()) {
        Fail("BLEND_MESH_STORAGE_INVALID", "Boolean Mesh data must be a one-byte scalar", values.index);
      }
      value = Take(member.UnsignedInteger());
    }
    if (value > 1) {
      Fail("BLEND_MESH_STORAGE_INVALID", "Boolean Mesh values must be zero or one", values.index);
    }
    return value != 0;
  }

  template <std::size_t Size>
  std::array<double, Size> Vector(const ValuesView& values, std::uint32_t element) const {
    std::array<double, Size> result{};
    constexpr std::array<std::string_view, 3> members = {"x", "y", "z"};
    for (std::size_t axis = 0; axis < Size; ++axis) {
      if (values.raw) {
        result[axis] = std::bit_cast<float>(Bits(values.bytes.subspan((static_cast<std::size_t>(element) * Size + axis) * 4, 4)));
      } else {
        const auto source = Take(ViewDnaBlock(bytes_, blocks_, schema_, header_, values.index, element));
        const auto member = Take(source.Member(members[axis]));
        if (member.Type().name != "float" || member.Type().length != 4 ||
            member.PointerLevel() != 0 || !member.ArrayDimensions().empty()) {
          Fail("BLEND_MESH_STORAGE_INVALID", "Vector Mesh data must contain scalar floats", values.index);
        }
        result[axis] = Take(member.FloatingPoint());
      }
      if (!std::isfinite(result[axis])) {
        Fail("BLEND_MESH_VALUE_INVALID", "Mesh positions and UV values must be finite", values.index);
      }
    }
    return result;
  }

  Vector3 Normalize(Vector3 value) const {
    const auto length = std::hypot(value[0], value[1], value[2]);
    if (!std::isfinite(length) || length == 0) {
      Fail("BLEND_MESH_NORMALS_INVALID", "Constructed normal or corner direction is degenerate or nonfinite", index_);
    }
    for (auto& component : value) {
      component /= length;
    }
    return value;
  }

  Vector3 FaceNormal(const Mesh& mesh, std::size_t start, std::size_t count) const {
    const auto& origin = sourcePoints_[mesh.faceVertexIndices[start]];
    Vector3 normal{};
    for (std::size_t corner = 1; corner + 1 < count; ++corner) {
      Vector3 a{}, b{};
      for (std::size_t axis = 0; axis < 3; ++axis) {
        a[axis] = sourcePoints_[mesh.faceVertexIndices[start + corner]][axis] - origin[axis];
        b[axis] = sourcePoints_[mesh.faceVertexIndices[start + corner + 1]][axis] - origin[axis];
      }
      for (std::size_t axis = 0; axis < 3; ++axis) {
        normal[axis] += a[(axis + 1) % 3] * b[(axis + 2) % 3] -
                        a[(axis + 2) % 3] * b[(axis + 1) % 3];
      }
    }
    return Normalize(normal);
  }

  double CornerAngle(const Mesh& mesh, std::size_t corner,
      std::size_t previous, std::size_t next) const {
    const auto& point = sourcePoints_[mesh.faceVertexIndices[corner]];
    Vector3 a{}, b{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
      a[axis] = sourcePoints_[mesh.faceVertexIndices[previous]][axis] - point[axis];
      b[axis] = sourcePoints_[mesh.faceVertexIndices[next]][axis] - point[axis];
    }
    a = Normalize(a);
    b = Normalize(b);
    return std::acos(std::clamp(a[0] * b[0] + a[1] * b[1] + a[2] * b[2], -1.0, 1.0));
  }

  void ReadNormals(Mesh& mesh, const DnaValueView& source, std::uint32_t faces) const {
    if (faces == 0) {
      return;
    }
    const auto sharp = Find("sharp_face", 2, Kind::Boolean, false);
    std::vector<bool> flat(faces, false);
    if (sharp) {
      const auto values = Values(*sharp, faces);
      for (std::uint32_t face = 0; face < faces; ++face) {
        flat[face] = Boolean(values, face);
      }
    }
    const auto sharpEdge = Find("sharp_edge", 1, Kind::Boolean, false);
    std::vector<bool> sharpEdges;
    if (sharpEdge) {
      const auto edges = Count(source, "totedge", index_);
      const auto values = Values(*sharpEdge, edges);
      sharpEdges.reserve(edges);
      for (std::uint32_t edge = 0; edge < edges; ++edge) {
        sharpEdges.push_back(Boolean(values, edge));
      }
    }
    const auto allFlat = std::all_of(flat.begin(), flat.end(), [](bool value) { return value; });
    if (allFlat) {
      mesh.cornerNormals.reserve(mesh.faceVertexIndices.size());
      std::size_t start = 0;
      for (const auto count : mesh.faceVertexCounts) {
        const auto normal = ToUsdBasis(FaceNormal(mesh, start, count));
        mesh.cornerNormals.insert(mesh.cornerNormals.end(), count, normal);
        start += count;
      }
      return;
    }
    const auto split = std::any_of(flat.begin(), flat.end(), [](bool value) { return value; }) ||
                       std::any_of(sharpEdges.begin(), sharpEdges.end(), [](bool value) { return value; });
    const auto corners = mesh.faceVertexIndices.size();
    std::vector<std::size_t> groups(corners), next(corners);
    std::vector<std::uint32_t> cornerFaces(corners);
    std::vector<Vector3> normals(faces);
    std::vector<double> angles(corners);
    std::size_t start = 0;
    for (std::uint32_t face = 0; face < faces; ++face) {
      const auto count = static_cast<std::size_t>(mesh.faceVertexCounts[face]);
      normals[face] = FaceNormal(mesh, start, count);
      for (std::size_t offset = 0; offset < count; ++offset) {
        const auto corner = start + offset;
        next[corner] = start + (offset + 1) % count;
        cornerFaces[corner] = face;
        groups[corner] = split ? corner : static_cast<std::size_t>(mesh.faceVertexIndices[corner]);
        if (!flat[face]) {
          angles[corner] = CornerAngle(mesh, corner, start + (offset + count - 1) % count, next[corner]);
        }
      }
      start += count;
    }
    if (split) {
      const auto edges = Count(source, "totedge", index_);
      const auto cornerEdge = Find(".corner_edge", 3, Kind::Integer, true);
      const auto values = Values(*cornerEdge, static_cast<std::uint32_t>(corners));
      struct Edge {
        std::pair<std::int32_t, std::int32_t> vertices;
        std::vector<std::size_t> uses;
      };
      std::map<std::int32_t, Edge> topology;
      for (std::size_t corner = 0; corner < corners; ++corner) {
        const auto edge = Integer(values, corner);
        if (edge < 0 || static_cast<std::uint32_t>(edge) >= edges) {
          Fail("BLEND_MESH_TOPOLOGY_INVALID", "Corner edge index is outside the source edges", values.index);
        }
        const auto vertices = std::minmax(mesh.faceVertexIndices[corner], mesh.faceVertexIndices[next[corner]]);
        const std::pair<std::int32_t, std::int32_t> endpoints{vertices.first, vertices.second};
        const auto [entry, inserted] = topology.try_emplace(edge, Edge{endpoints, {}});
        if (!inserted && entry->second.vertices != endpoints) {
          Fail("BLEND_MESH_TOPOLOGY_INVALID", "Shared corner edge has inconsistent vertex endpoints", values.index);
        }
        entry->second.uses.push_back(corner);
      }
      const auto root = [&](std::size_t corner) {
        while (groups[corner] != corner) {
          groups[corner] = groups[groups[corner]];
          corner = groups[corner];
        }
        return corner;
      };
      const auto join = [&](std::size_t left, std::size_t right) {
        const auto a = root(left);
        const auto b = root(right);
        groups[std::max(a, b)] = std::min(a, b);
      };
      for (const auto& [edge, topologyEdge] : topology) {
        if (topologyEdge.uses.size() != 2 || (!sharpEdges.empty() && sharpEdges[edge])) {
          continue;
        }
        const auto a = topologyEdge.uses[0], b = topologyEdge.uses[1];
        if (cornerFaces[a] == cornerFaces[b] || flat[cornerFaces[a]] || flat[cornerFaces[b]] ||
            mesh.faceVertexIndices[a] != mesh.faceVertexIndices[next[b]] ||
            mesh.faceVertexIndices[next[a]] != mesh.faceVertexIndices[b]) {
          continue;
        }
        join(a, next[b]);
        join(next[a], b);
      }
      for (std::size_t corner = 0; corner < corners; ++corner) {
        groups[corner] = root(corner);
      }
    }
    // Blender uses point normals for an entirely smooth mesh, even across
    // disconnected/nonmanifold fans; sharp data selects split corner fans.
    std::vector<Vector3> sums(split ? corners : sourcePoints_.size());
    for (std::size_t corner = 0; corner < corners; ++corner) {
      const auto face = cornerFaces[corner];
      if (!flat[face]) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
          sums[groups[corner]][axis] += normals[face][axis] * angles[corner];
        }
      }
    }
    mesh.cornerNormals.reserve(corners);
    for (std::size_t corner = 0; corner < corners; ++corner) {
      const auto face = cornerFaces[corner];
      mesh.cornerNormals.push_back(ToUsdBasis(flat[face] ? normals[face] : Normalize(sums[groups[corner]])));
    }
  }

  void ReadUvs(Mesh& mesh, const DnaValueView& source, std::uint32_t corners) const {
    std::optional<std::int64_t> legacyActive;
    for (const auto& attribute : attributes_) {
      if (attribute.domain != 3 || attribute.kind != Kind::Float2) {
        continue;
      }
      const auto values = Values(attribute, corners);
      UvMap uv;
      uv.sourceName = attribute.name;
      uv.values.reserve(corners);
      uv.indices.reserve(corners);
      std::map<Vector2, std::int32_t> indices;
      for (std::uint32_t corner = 0; corner < corners; ++corner) {
        const auto value = Vector<2>(values, corner);
        const auto [entry, inserted] = indices.emplace(value, static_cast<std::int32_t>(indices.size()));
        if (inserted) {
          uv.values.push_back(value);
        }
        uv.indices.push_back(entry->second);
      }
      if (!attribute.modern) {
        const auto active = Scalar(attribute.source, "active_rnd", "int", 4, attribute.index);
        if (active < 0 || (legacyActive && *legacyActive != active)) {
          Fail("BLEND_MESH_STORAGE_INVALID", "UV layers must agree on a nonnegative active render index", attribute.index);
        }
        legacyActive = active;
      }
      mesh.uvMaps.push_back(std::move(uv));
    }
    const auto address = Pointer(source, "default_uv_map_attribute", "char", index_);
    if (address != 0) {
      const auto name = PointerString(address, index_);
      const auto found = std::find_if(mesh.uvMaps.begin(), mesh.uvMaps.end(),
          [&](const auto& uv) { return uv.sourceName == name; });
      if (found == mesh.uvMaps.end()) {
        Fail("BLEND_MESH_STORAGE_INVALID", "Default render UV name does not identify a decoded UV map", index_);
      }
      found->activeRender = true;
    } else if (legacyActive) {
      if (static_cast<std::uint64_t>(*legacyActive) >= mesh.uvMaps.size()) {
        Fail("BLEND_MESH_STORAGE_INVALID", "Active render UV index is outside the saved UV maps", index_);
      }
      mesh.uvMaps[static_cast<std::size_t>(*legacyActive)].activeRender = true;
    }
  }

  std::span<const std::byte> bytes_;
  std::span<const BlendBlock> blocks_;
  const DnaSchema& schema_;
  const Header& header_;
  const PointerMap& pointers_;
  std::uint32_t index_;
  const UnitConversion& units_;
  std::vector<Diagnostic>& diagnostics_;
  std::vector<Attribute> attributes_;
  std::vector<Vector3> sourcePoints_;
  std::map<std::pair<std::int64_t, std::string>, std::size_t> names_;
};

} // namespace

Mesh DecodeMesh(std::span<const std::byte> bytes,
    std::span<const BlendBlock> blocks, const DnaSchema& schema,
    const Header& header, const PointerMap& pointers, std::uint32_t index,
    const UnitConversion& units, std::vector<Diagnostic>& diagnostics) {
  return MeshDecoder(bytes, blocks, schema, header, pointers, index, units, diagnostics).Run();
}

} // namespace blend::detail
