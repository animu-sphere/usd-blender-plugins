#include <blendScene/Scene.h>
#include <blendScene/Selection.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {

void Require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool Near(double left, double right) {
  return std::abs(left - right) <=
         1e-12 * (1 + std::abs(left) + std::abs(right));
}

template <typename Exception, typename Operation>
void RequireFailure(Operation operation, std::string_view code) {
  try {
    operation();
  } catch (const Exception& error) {
    Require(std::string_view(error.what()).starts_with(code),
        "Unit failure has the expected diagnostic code");
    return;
  }
  throw std::runtime_error("Expected unit conversion failure");
}

blend::Vector3 TransformPoint(const blend::Matrix4& transform,
    const blend::Vector3& point) {
  blend::Vector3 result{};
  for (std::size_t row = 0; row < 3; ++row) {
    result[row] = transform[row][3];
    for (std::size_t column = 0; column < 3; ++column) {
      result[row] += transform[row][column] * point[column];
    }
  }
  return result;
}

blend::Matrix4 Multiply(const blend::Matrix4& left, const blend::Matrix4& right) {
  blend::Matrix4 result{};
  for (std::size_t row = 0; row < 4; ++row) {
    for (std::size_t column = 0; column < 4; ++column) {
      for (std::size_t axis = 0; axis < 4; ++axis) {
        result[row][column] += left[row][axis] * right[axis][column];
      }
    }
  }
  return result;
}

blend::Vector3 Cross(const blend::Vector3& left, const blend::Vector3& right) {
  return {left[1] * right[2] - left[2] * right[1],
      left[2] * right[0] - left[0] * right[2],
      left[0] * right[1] - left[1] * right[0]};
}

void CheckBasis() {
  Require(blend::ToUsdBasis(blend::Vector3{1, 2, 3}) ==
              blend::Vector3{1, 3, -2},
      "Point basis conversion");
  Require(blend::ToUsdBasis(blend::Vector3{0, 0, 1}) ==
              blend::Vector3{0, 1, 0},
      "Z-up becomes Y-up");
  Require(blend::ToUsdBasis(blend::Vector3{0, -1, 0}) ==
              blend::Vector3{0, 0, 1},
      "Front becomes +Z");
  Require(blend::ToUsdBasis(blend::IdentityMatrix) == blend::IdentityMatrix,
      "Identity transform is unchanged");
  const blend::Matrix4 source = {{{2, 3, 5, 7},
      {11, 13, 17, 19},
      {23, 29, 31, 37},
      {0, 0, 0, 1}}};
  const blend::Matrix4 expected = {{{2, 5, -3, 7},
      {23, 31, -29, 37},
      {-11, -17, 13, -19},
      {0, 0, 0, 1}}};
  const auto converted = blend::ToUsdBasis(source);
  Require(converted == expected, "Matrix conversion is C W C inverse");
  const blend::Vector3 local = {41, 43, 47};
  Require(TransformPoint(converted, blend::ToUsdBasis(local)) ==
              blend::ToUsdBasis(TransformPoint(source, local)),
      "Converted world matrix and local point compose");
  const blend::Matrix4 child = {{{0, -1, 0, 3},
      {1, 0, 0, 5},
      {0, 0, -2, 7},
      {0, 0, 0, 1}}};
  Require(blend::ToUsdBasis(Multiply(source, child)) ==
              Multiply(converted, blend::ToUsdBasis(child)),
      "Basis conversion preserves parent-child composition");
  const blend::Vector3 edgeA = {1, 2, 3};
  const blend::Vector3 edgeB = {5, 7, 11};
  Require(Cross(blend::ToUsdBasis(edgeA), blend::ToUsdBasis(edgeB)) ==
              blend::ToUsdBasis(Cross(edgeA, edgeB)),
      "Basis rotation preserves right-handed winding");
  const auto normal = blend::ToUsdBasis(blend::Vector3{2, -3, 6});
  Require(normal[0] * normal[0] + normal[1] * normal[1] +
                  normal[2] * normal[2] ==
              49,
      "Basis rotation preserves normal length");
}

void CheckUnits() {
  const blend::Matrix4 parent = {{{2, 3, 5, 7},
      {11, 13, 17, 19},
      {23, 29, 31, 37},
      {0, 0, 0, 1}}};
  const blend::Matrix4 child = {{{0, -1, 0, 3},
      {1, 0, 0, 5},
      {0, 0, -2, 7},
      {0, 0, 0, 1}}};
  for (const double scale : {1.0, 0.01, 0.001, 10.0}) {
    const blend::UnitConversion units(scale);
    Require(units.MetersPerBlenderUnit() == scale,
        "Validated source scale is retained as provenance");
    Require(Near(units.Distance(1 / scale), 1),
        "Equivalent source distances normalize to one meter");
    const auto position = units.Position({1 / scale, 2 / scale, 3 / scale});
    const blend::Vector3 expectedPosition = {1, 3, -2};
    for (std::size_t axis = 0; axis < 3; ++axis) {
      Require(Near(position[axis], expectedPosition[axis]),
          "Position normalization applies units and basis once");
    }
    const auto convertedParent = units.WorldTransform(parent);
    const auto basisParent = blend::ToUsdBasis(parent);
    for (std::size_t row = 0; row < 4; ++row) {
      for (std::size_t column = 0; column < 4; ++column) {
        const double expected = row < 3 && column == 3
                                    ? basisParent[row][column] * scale
                                    : basisParent[row][column];
        Require(Near(convertedParent[row][column], expected),
            "Only transform translation is unit-scaled");
      }
    }
    Require(units.WorldTransform(blend::IdentityMatrix) == blend::IdentityMatrix,
        "Units do not change identity or dimensionless object scale");
    const blend::Vector3 local = {41, 43, 47};
    const auto actualWorld = TransformPoint(convertedParent, units.Position(local));
    const auto expectedWorld = units.Position(TransformPoint(parent, local));
    for (std::size_t axis = 0; axis < 3; ++axis) {
      Require(Near(actualWorld[axis], expectedWorld[axis]),
          "Meter-space mesh points and world transform compose");
    }
    const auto actualChild = Multiply(convertedParent, units.WorldTransform(child));
    const auto expectedChild = units.WorldTransform(Multiply(parent, child));
    for (std::size_t row = 0; row < 4; ++row) {
      for (std::size_t column = 0; column < 4; ++column) {
        Require(Near(actualChild[row][column], expectedChild[row][column]),
            "Unit normalization preserves parent-child composition");
      }
    }
    const double halfWidth = 0.5 / scale;
    const auto minimum = units.Position({-halfWidth, -halfWidth, -halfWidth});
    const auto maximum = units.Position({halfWidth, halfWidth, halfWidth});
    for (std::size_t axis = 0; axis < 3; ++axis) {
      Require(Near(std::abs(maximum[axis] - minimum[axis]), 1),
          "Equivalent synthetic cubes normalize to one-meter extents");
    }
  }
  for (const double invalid : {0.0, -0.01,
           std::numeric_limits<double>::quiet_NaN(),
           std::numeric_limits<double>::infinity(),
           -std::numeric_limits<double>::infinity()}) {
    RequireFailure<std::invalid_argument>(
        [invalid] { blend::UnitConversion units(invalid); },
        "BLEND_SCENE_UNIT_SCALE_INVALID:");
  }
  const blend::UnitConversion units(10);
  for (const double invalid : {std::numeric_limits<double>::quiet_NaN(),
           std::numeric_limits<double>::infinity()}) {
    RequireFailure<std::invalid_argument>([&] { units.Distance(invalid); },
        "BLEND_SCENE_UNIT_VALUE_INVALID:");
    RequireFailure<std::invalid_argument>([&] { units.Position({0, invalid, 0}); },
        "BLEND_SCENE_UNIT_VALUE_INVALID:");
    auto transform = blend::IdentityMatrix;
    transform[2][3] = invalid;
    RequireFailure<std::invalid_argument>([&] { units.WorldTransform(transform); },
        "BLEND_SCENE_UNIT_VALUE_INVALID:");
  }
  RequireFailure<std::overflow_error>(
      [&] { units.Distance(std::numeric_limits<double>::max()); },
      "BLEND_SCENE_UNIT_VALUE_INVALID:");
  auto projective = blend::IdentityMatrix;
  projective[3][0] = 1;
  RequireFailure<std::invalid_argument>([&] { units.WorldTransform(projective); },
      "BLEND_SCENE_UNIT_TRANSFORM_INVALID:");
}

void CheckScene() {
  blend::Scene scene;
  Require(scene.objects.empty() && scene.meshes.empty(), "Empty Scene IR");
  scene.metadata.sourceVersion = "4.5";
  scene.metadata.sourceScene = "Scene";
  scene.metadata.sourceUnitScale = 0.01;
  scene.meshes.emplace_back();
  scene.meshes[0].sourceName = "SharedMesh";
  const blend::UnitConversion units(scene.metadata.sourceUnitScale);
  scene.meshes[0].points.push_back(units.Position({100, 200, 300}));
  scene.objects.emplace_back();
  scene.objects[0].sourceName = "Parent";
  scene.objects.emplace_back();
  scene.objects[1].sourceName = "Child";
  scene.objects[1].parent = 0;
  scene.objects[1].mesh = 0;
  scene.objects.emplace_back();
  scene.objects[2].mesh = 0;
  Require(!scene.objects[0].parent && !scene.objects[0].mesh,
      "Empty object has no parent or mesh");
  Require(scene.objects[0].worldTransform == blend::IdentityMatrix,
      "Default object transform is identity");
  Require(scene.objects[1].parent == 0 && scene.objects[1].mesh == 0 &&
              scene.objects[2].mesh == 0,
      "Objects reference parents and shared meshes by index");
  const auto copy = scene;
  scene.meshes[0].points[0][0] = 99;
  scene.objects[1].sourceName = "Changed";
  Require(copy.meshes[0].points[0] == blend::Vector3{1, 3, -2} &&
              copy.objects[1].sourceName == "Child",
      "Scene IR owns its strings and arrays");
  Require(copy.metadata.sourceVersion == "4.5" &&
              copy.metadata.sourceScene == "Scene" &&
              copy.metadata.sourceUnitScale == 0.01,
      "Source metadata is independent of basis conversion");
}

void StoreBits(std::vector<std::byte>& bytes, std::size_t offset,
    std::uint64_t value, std::size_t width, blend::ByteOrder order) {
  for (std::size_t index = 0; index < width; ++index) {
    const auto shift = 8 * (order == blend::ByteOrder::Little ? index : width - index - 1);
    bytes.at(offset + index) = static_cast<std::byte>((value >> shift) & 255);
  }
}

void CheckSelectionLayouts() {
  for (const std::uint8_t width : {4, 8}) {
    for (const auto order : {blend::ByteOrder::Little, blend::ByteOrder::Big}) {
      const blend::Header header{width, order, 405};
      const auto idSize = static_cast<std::uint16_t>(16 + width);
      const auto sceneSize = static_cast<std::uint16_t>(idSize + 4);
      blend::DnaSchema schema;
      schema.names = {"name[16]", "*lib", "scale_length", "id", "unit", "*curscene"};
      schema.types = {{"char", 1}, {"float", 4}, {"Library", 0}, {"ID", idSize},
          {"UnitSettings", 4}, {"Scene", sceneSize}, {"FileGlobal", width}};
      schema.structs = {{3, {{0, 0, "name", 0, {16}, 0, 16},
                                {2, 1, "lib", 1, {}, 16, width}}},
          {4, {{1, 2, "scale_length", 0, {}, 0, 4}}},
          {5, {{3, 3, "id", 0, {}, 0, idSize},
                  {4, 4, "unit", 0, {}, idSize, 4}}},
          {6, {{5, 5, "curscene", 1, {}, 0, width}}}};
      std::vector<std::byte> bytes(width + 2 * sceneSize);
      StoreBits(bytes, 0, 100, width, order);
      for (const std::size_t offset : {static_cast<std::size_t>(width),
               static_cast<std::size_t>(width + sceneSize)}) {
        const std::string_view name = offset == width ? "SCChosen" : "SCOther";
        for (std::size_t index = 0; index < name.size(); ++index) {
          bytes[offset + index] = static_cast<std::byte>(name[index]);
        }
        StoreBits(bytes, offset + idSize, std::bit_cast<std::uint32_t>(0.01f), 4, order);
      }
      const std::vector<blend::BlendBlock> blocks = {
          {{'G', 'L', 'O', 'B'}, width, 16, 3, 1, 0},
          {{'S', 'C', 0, 0}, sceneSize, 100, 2, 1, width},
          {{'S', 'C', 0, 0}, sceneSize, 200, 2, 1, static_cast<std::uint64_t>(width + sceneSize)}};
      auto select = [&](const auto& input, const auto& records, const auto& dna) {
        return blend::SelectScene(input, records, dna, header);
      };
      const auto selected = select(bytes, blocks, schema);
      Require(selected.HasValue() && selected.GetValue().blockIndex == 1 &&
                  selected.GetValue().metadata.sourceScene == "Chosen" &&
                  selected.GetValue().metadata.sourceVersion == "4.5" &&
                  selected.GetValue().metadata.sourceUnitScale == static_cast<double>(0.01f),
          "Scene selection uses saved pointers, byte order and SDNA offsets");
      auto alternate = bytes;
      StoreBits(alternate, 0, 200, width, order);
      const auto other = select(alternate, blocks, schema);
      Require(other.HasValue() && other.GetValue().blockIndex == 2 &&
                  other.GetValue().metadata.sourceScene == "Other",
          "Saved selection does not choose the first Scene in the file");
      auto failure = [&](const auto& input, const auto& records, const auto& dna,
                         std::string_view code, std::optional<std::uint32_t> index) {
        const auto result = select(input, records, dna);
        Require(!result.HasValue(), "Malformed scene selection fails");
        const auto& error = result.GetError();
        Require(error.code == code && error.severity == blend::Severity::Fatal &&
                    !error.recoverable && error.blockIndex == index &&
                    error.byteOffset == (index ? std::optional<std::uint64_t>(records[*index].offset)
                                               : std::nullopt),
            "Scene selection failure has exact code and block context");
      };
      for (const std::uint64_t address : {0, 101, 999, 16}) {
        auto changed = bytes;
        StoreBits(changed, 0, address, width, order);
        failure(changed, blocks, schema,
            address == 0 ? "BLEND_SCENE_ACTIVE_MISSING" : "BLEND_SCENE_REFERENCE_INVALID", 0);
      }
      auto records = blocks;
      records.erase(records.begin());
      failure(bytes, records, schema, "BLEND_SCENE_GLOBAL_INVALID", std::nullopt);
      records = blocks;
      records.push_back(blocks[0]);
      failure(bytes, records, schema, "BLEND_SCENE_GLOBAL_INVALID", 3);
      records = blocks;
      records[0].count = 2;
      failure(bytes, records, schema, "BLEND_SCENE_GLOBAL_INVALID", 0);
      records = blocks;
      records[0].sdnaIndex = 0;
      records[0].length = idSize;
      failure(bytes, records, schema, "BLEND_SCENE_GLOBAL_INVALID", 0);
      for (const auto code : {std::array<char, 4>{'D', 'A', 'T', 'A'},
               std::array<char, 4>{'O', 'B', 0, 0}}) {
        records = blocks;
        records[1].code = code;
        failure(bytes, records, schema, "BLEND_SCENE_REFERENCE_INVALID", 1);
      }
      records = blocks;
      records[1].count = 2;
      failure(bytes, records, schema, "BLEND_SCENE_REFERENCE_INVALID", 1);
      records = blocks;
      records[1].sdnaIndex = 0;
      records[1].length = idSize;
      failure(bytes, records, schema, "BLEND_SCENE_REFERENCE_INVALID", 1);
      records = blocks;
      records[1].sdnaIndex = static_cast<std::uint32_t>(schema.structs.size());
      failure(bytes, records, schema, "BLEND_DNA_INDEX", 1);
      records = blocks;
      records[1].length -= 1;
      failure(bytes, records, schema, "BLEND_DNA_SIZE", 1);
      records = blocks;
      records[2].oldAddress = 100;
      failure(bytes, records, schema, "BLEND_POINTER_DUPLICATE", 2);
      auto changed = bytes;
      StoreBits(changed, width + 16, 999, width, order);
      failure(changed, blocks, schema, "BLEND_SCENE_LINKED_UNSUPPORTED", 1);
      for (const float scale : {0.0f, -1.0f,
               std::numeric_limits<float>::infinity(),
               std::numeric_limits<float>::quiet_NaN()}) {
        changed = bytes;
        StoreBits(changed, width + idSize, std::bit_cast<std::uint32_t>(scale), 4, order);
        failure(changed, blocks, schema, "BLEND_SCENE_UNIT_SCALE_INVALID", 1);
      }
      changed = bytes;
      changed[width] = std::byte{'O'};
      failure(changed, blocks, schema, "BLEND_SCENE_NAME_INVALID", 1);
      changed = bytes;
      std::fill_n(changed.begin() + width, 16, std::byte{'X'});
      changed[width] = std::byte{'S'};
      changed[width + 1] = std::byte{'C'};
      failure(changed, blocks, schema, "BLEND_SCENE_NAME_INVALID", 1);
      auto dna = schema;
      dna.structs[3].members[0].typeIndex = 2;
      failure(bytes, blocks, dna, "BLEND_SCENE_REFERENCE_INVALID", 0);
      dna = schema;
      dna.structs[2].members.pop_back();
      failure(bytes, blocks, dna, "BLEND_DNA_MEMBER", 1);
      changed = bytes;
      StoreBits(changed, width + sceneSize + idSize, 0, 4, order);
      Require(select(changed, blocks, schema).HasValue(),
          "Other scenes are not selected or semantically decoded");
      StoreBits(changed, 0, 200, width, order);
      failure(changed, blocks, schema, "BLEND_SCENE_UNIT_SCALE_INVALID", 2);
      records = blocks;
      std::swap(records[0], records[1]);
      const auto reordered = select(bytes, records, schema);
      Require(reordered.HasValue() && reordered.GetValue().blockIndex == 0 &&
                  reordered.GetValue().metadata.sourceScene == "Chosen",
          "Saved scene selection is independent of block enumeration order");
      bytes[width + 2] = std::byte{'X'};
      schema.types[5].name = "Changed";
      Require(selected.GetValue().metadata.sourceScene == "Chosen",
          "Selected scene metadata owns its source strings");
    }
  }
}

void CheckCollectionLayouts() {
  for (const std::uint8_t width : {4, 8}) {
    for (const auto order : {blend::ByteOrder::Little, blend::ByteOrder::Big}) {
      const blend::Header header{width, order, 405};
      const auto idSize = static_cast<std::uint16_t>(16 + width);
      const auto sceneSize = static_cast<std::uint16_t>(idSize + 4 + width);
      const auto collectionSize = static_cast<std::uint16_t>(idSize + 4 * width);
      const auto objectSize = static_cast<std::uint16_t>(idSize + 2 * width);
      const auto nodeSize = static_cast<std::uint16_t>(3 * width);
      blend::DnaSchema schema;
      schema.names = {"name[16]", "*lib", "scale_length", "id", "unit", "*curscene",
          "*master_collection", "*first", "*last", "gobject", "children", "*next", "*prev", "*ob", "*collection", "*parent", "*data"};
      schema.types = {{"char", 1}, {"float", 4}, {"Library", 0}, {"ID", idSize},
          {"UnitSettings", 4}, {"Scene", sceneSize}, {"FileGlobal", width}, {"void", 0},
          {"ListBase", static_cast<std::uint16_t>(2 * width)}, {"Collection", collectionSize},
          {"Object", objectSize}, {"CollectionObject", nodeSize}, {"CollectionChild", nodeSize}};
      schema.structs = {{3, {{0, 0, "name", 0, {16}, 0, 16}, {2, 1, "lib", 1, {}, 16, width}}},
          {4, {{1, 2, "scale_length", 0, {}, 0, 4}}},
          {5, {{3, 3, "id", 0, {}, 0, idSize}, {4, 4, "unit", 0, {}, idSize, 4},
                  {9, 6, "master_collection", 1, {}, static_cast<std::uint64_t>(idSize + 4), width}}},
          {6, {{5, 5, "curscene", 1, {}, 0, width}}},
          {8, {{7, 7, "first", 1, {}, 0, width}, {7, 8, "last", 1, {}, width, width}}},
          {9, {{3, 3, "id", 0, {}, 0, idSize}, {8, 9, "gobject", 0, {}, idSize, static_cast<std::uint64_t>(2 * width)},
                  {8, 10, "children", 0, {}, static_cast<std::uint64_t>(idSize + 2 * width), static_cast<std::uint64_t>(2 * width)}}},
          {10, {{3, 3, "id", 0, {}, 0, idSize}, {10, 15, "parent", 1, {}, idSize, width},
                   {7, 16, "data", 1, {}, static_cast<std::uint64_t>(idSize + width), width}}},
          {11, {{11, 11, "next", 1, {}, 0, width}, {11, 12, "prev", 1, {}, width, width},
                   {10, 13, "ob", 1, {}, static_cast<std::uint64_t>(2 * width), width}}},
          {12, {{12, 11, "next", 1, {}, 0, width}, {12, 12, "prev", 1, {}, width, width},
                   {9, 14, "collection", 1, {}, static_cast<std::uint64_t>(2 * width), width}}}};
      std::vector<std::byte> bytes;
      std::vector<blend::BlendBlock> blocks;
      auto add = [&](std::array<char, 4> code, std::uint32_t dna, std::uint16_t size) {
        blocks.push_back({code, size, 1000 + 100 * blocks.size(), dna, 1, bytes.size()});
        bytes.resize(bytes.size() + size);
      };
      add({'G', 'L', 'O', 'B'}, 3, width);
      add({'S', 'C', 0, 0}, 2, sceneSize);
      add({'D', 'A', 'T', 'A'}, 5, collectionSize);
      add({'G', 'R', 0, 0}, 5, collectionSize);
      add({'O', 'B', 0, 0}, 6, objectSize);
      add({'O', 'B', 0, 0}, 6, objectSize);
      add({'D', 'A', 'T', 'A'}, 7, nodeSize);
      add({'D', 'A', 'T', 'A'}, 7, nodeSize);
      add({'D', 'A', 'T', 'A'}, 7, nodeSize);
      add({'D', 'A', 'T', 'A'}, 8, nodeSize);
      add({'O', 'B', 0, 0}, 6, objectSize);
      add({'D', 'A', 'T', 'A'}, 8, nodeSize);
      auto store = [&](auto& input, std::size_t index, std::size_t member, std::uint64_t value) {
        StoreBits(input, static_cast<std::size_t>(blocks[index].offset) + member, value, width, order);
      };
      auto link = [&](std::size_t index, std::size_t member, std::size_t target) {
        store(bytes, index, member, blocks[target].oldAddress);
      };
      auto name = [&](std::size_t index, std::string_view value) {
        for (std::size_t character = 0; character < value.size(); ++character) {
          bytes[static_cast<std::size_t>(blocks[index].offset) + character] = static_cast<std::byte>(value[character]);
        }
      };
      name(1, "SCChosen");
      name(2, "GRRoot");
      name(3, "GRChild");
      name(4, "OBOne");
      name(5, "OBTwo");
      name(10, "OBUnused");
      store(bytes, 10, 16, 999);
      StoreBits(bytes, static_cast<std::size_t>(blocks[1].offset) + idSize,
          std::bit_cast<std::uint32_t>(1.0f), 4, order);
      link(0, 0, 1);
      link(1, idSize + 4, 2);
      link(2, idSize, 6);
      link(2, idSize + width, 6);
      link(2, idSize + 2 * width, 9);
      link(2, idSize + 3 * width, 9);
      link(3, idSize, 7);
      link(3, idSize + width, 8);
      link(6, 2 * width, 4);
      link(7, 0, 8);
      link(7, 2 * width, 4);
      link(8, width, 7);
      link(8, 2 * width, 5);
      link(9, 2 * width, 3);
      auto select = [&](const auto& input, const auto& records, const auto& dna,
                        blend::SceneTraversalLimits limits = {8, 2}) {
        return blend::SelectSceneObjects(input, records, dna, header, limits);
      };
      const auto result = select(bytes, blocks, schema);
      Require(result.HasValue() && result.GetValue().objects.size() == 2 &&
                  result.GetValue().objects[0].blockIndex == 4 && result.GetValue().objects[0].sourceName == "One" &&
                  result.GetValue().objects[1].blockIndex == 5 && result.GetValue().objects[1].sourceName == "Two",
          "Nested Collection membership deduplicates shared Objects at exact budgets");
      const auto membershipBytes = bytes;
      const auto membershipBlocks = blocks;
      auto failure = [&](const auto& input, const auto& records, const auto& dna,
                         std::string_view code, std::optional<std::uint32_t> index,
                         blend::SceneTraversalLimits limits = {32, 8}) {
        const auto failed = select(input, records, dna, limits);
        Require(!failed.HasValue(), "Malformed Collection graph fails");
        const auto& error = failed.GetError();
        if (error.code != code || error.blockIndex != index) {
          throw std::runtime_error("Expected " + std::string(code) + " at " +
                                   (index ? std::to_string(*index) : "none") + ", got " + error.code + " at " +
                                   (error.blockIndex ? std::to_string(*error.blockIndex) : "none"));
        }
        Require(error.code == code && error.severity == blend::Severity::Fatal &&
                    !error.recoverable && error.blockIndex == index &&
                    error.byteOffset == (index ? std::optional<std::uint64_t>(records[*index].offset) : std::nullopt),
            "Collection failure has exact fatal code and block context");
      };
      Require(!result.GetValue().objects[0].parentBlockIndex &&
                  !result.GetValue().objects[1].parentBlockIndex,
          "Null saved parents are valid roots");
      Require(!result.GetValue().objects[0].dataBlockIndex &&
                  !result.GetValue().objects[1].dataBlockIndex,
          "Null saved Object data is retained without inventing a target");
      auto parented = bytes;
      store(parented, 4, idSize, blocks[5].oldAddress);
      const auto hierarchy = select(parented, blocks, schema);
      Require(hierarchy.HasValue() && hierarchy.GetValue().objects[0].parentBlockIndex == 5 &&
                  !hierarchy.GetValue().objects[1].parentBlockIndex,
          "Saved parent references retain caller-sequence block indices");
      store(parented, 5, idSize, blocks[4].oldAddress);
      failure(parented, blocks, schema, "BLEND_SCENE_CYCLE", 5);
      parented = bytes;
      store(parented, 4, idSize, blocks[4].oldAddress);
      failure(parented, blocks, schema, "BLEND_SCENE_CYCLE", 4);
      parented = bytes;
      store(parented, 4, idSize, blocks[10].oldAddress);
      store(parented, 10, 16, 0);
      const auto externalParent = select(parented, blocks, schema, {9, 2});
      Require(externalParent.HasValue() && externalParent.GetValue().objects.size() == 2 &&
                  externalParent.GetValue().objects[0].parentBlockIndex == 10,
          "An unselected parent is validated without changing Collection membership");
      failure(parented, blocks, schema, "BLEND_SCENE_VISIT_LIMIT", 4, {8, 2});
      store(parented, 10, idSize, blocks[5].oldAddress);
      failure(parented, blocks, schema, "BLEND_SCENE_DEPTH_LIMIT", 10, {9, 2});
      const auto longerHierarchy = select(parented, blocks, schema, {9, 3});
      Require(longerHierarchy.HasValue() && longerHierarchy.GetValue().objects.size() == 2 &&
                  longerHierarchy.GetValue().objects[0].parentBlockIndex == 10,
          "An exactly sufficient parent depth succeeds");
      store(parented, 10, idSize, blocks[4].oldAddress);
      failure(parented, blocks, schema, "BLEND_SCENE_CYCLE", 10);
      store(parented, 10, idSize, 0);
      store(parented, 5, idSize, blocks[10].oldAddress);
      const auto sharedParent = select(parented, blocks, schema, {9, 2});
      Require(sharedParent.HasValue() && sharedParent.GetValue().objects.size() == 2 &&
                  sharedParent.GetValue().objects[0].parentBlockIndex == 10 &&
                  sharedParent.GetValue().objects[1].parentBlockIndex == 10,
          "Shared unselected parents consume one visit and do not create cycles");
      auto parentRecords = blocks;
      std::reverse(parentRecords.begin(), parentRecords.end());
      const auto reorderedParents = select(parented, parentRecords, schema, {9, 2});
      Require(reorderedParents.HasValue() &&
                  parentRecords[*reorderedParents.GetValue().objects[0].parentBlockIndex].oldAddress == blocks[10].oldAddress &&
                  parentRecords[*reorderedParents.GetValue().objects[1].parentBlockIndex].oldAddress == blocks[10].oldAddress,
          "Parent indices follow the caller's reordered blocks");
      parented = bytes;
      for (const auto address : {std::uint64_t{999}, blocks[5].oldAddress + 1, blocks[3].oldAddress}) {
        store(parented, 4, idSize, address);
        failure(parented, blocks, schema, "BLEND_SCENE_REFERENCE_INVALID",
            address == blocks[3].oldAddress ? 3 : 4);
      }
      store(parented, 4, idSize, blocks[10].oldAddress);
      failure(parented, blocks, schema, "BLEND_SCENE_LINKED_UNSUPPORTED", 10);
      store(parented, 10, 16, 0);
      auto parentDna = schema;
      parentDna.structs[6].members[1].typeIndex = 9;
      failure(parented, blocks, parentDna, "BLEND_SCENE_REFERENCE_INVALID", 4);
      parentDna = schema;
      parentDna.structs[6].members[1].pointerLevel = 2;
      failure(parented, blocks, parentDna, "BLEND_SCENE_REFERENCE_INVALID", 4);
      parentDna = schema;
      parentDna.structs[6].members[1].arrayDimensions = {1};
      failure(parented, blocks, parentDna, "BLEND_SCENE_REFERENCE_INVALID", 4);
      parentDna = schema;
      parentDna.structs[6].members[1].baseName = "other";
      failure(parented, blocks, parentDna, "BLEND_DNA_MEMBER", 4);
      for (const std::size_t index : {4, 10}) {
        parentRecords = blocks;
        parentRecords[index].count = 2;
        failure(parented, parentRecords, schema, "BLEND_SCENE_REFERENCE_INVALID", static_cast<std::uint32_t>(index));
        parentRecords = blocks;
        parentRecords[index].code = {'D', 'A', 'T', 'A'};
        failure(parented, parentRecords, schema, "BLEND_SCENE_REFERENCE_INVALID", static_cast<std::uint32_t>(index));
        parentRecords = blocks;
        parentRecords[index].sdnaIndex = 0;
        parentRecords[index].length = idSize;
        failure(parented, parentRecords, schema, "BLEND_SCENE_REFERENCE_INVALID", static_cast<std::uint32_t>(index));
        auto invalidName = parented;
        invalidName[static_cast<std::size_t>(blocks[index].offset)] = std::byte{'X'};
        failure(invalidName, blocks, schema, "BLEND_SCENE_NAME_INVALID", static_cast<std::uint32_t>(index));
      }
      failure(bytes, blocks, schema, "BLEND_SCENE_VISIT_LIMIT", 8, {7, 2});
      failure(bytes, blocks, schema, "BLEND_SCENE_DEPTH_LIMIT", 2, {8, 1});
      failure(bytes, blocks, schema, "BLEND_SCENE_LIMITS", std::nullopt, {0, 2});
      failure(bytes, blocks, schema, "BLEND_SCENE_LIMITS", std::nullopt, {8, 0});
      for (const auto address : {std::uint64_t{0}, blocks[2].oldAddress + 1, std::uint64_t{999}}) {
        auto changed = bytes;
        store(changed, 1, idSize + 4, address);
        failure(changed, blocks, schema, "BLEND_SCENE_REFERENCE_INVALID", 1);
        changed = bytes;
        store(changed, 6, 2 * width, address);
        failure(changed, blocks, schema, "BLEND_SCENE_REFERENCE_INVALID", 6);
        changed = bytes;
        store(changed, 9, 2 * width, address);
        failure(changed, blocks, schema, "BLEND_SCENE_REFERENCE_INVALID", 9);
      }
      for (const std::size_t index : {2, 3, 4, 5}) {
        auto changed = bytes;
        store(changed, index, 16, 999);
        failure(changed, blocks, schema, "BLEND_SCENE_LINKED_UNSUPPORTED", static_cast<std::uint32_t>(index));
        changed = bytes;
        changed[static_cast<std::size_t>(blocks[index].offset)] = std::byte{'X'};
        failure(changed, blocks, schema, "BLEND_SCENE_NAME_INVALID", static_cast<std::uint32_t>(index));
      }
      auto changed = bytes;
      store(changed, 2, idSize + width, 0);
      failure(changed, blocks, schema, "BLEND_SCENE_LIST_INVALID", 2);
      changed = bytes;
      store(changed, 2, idSize, 999);
      failure(changed, blocks, schema, "BLEND_SCENE_REFERENCE_INVALID", 2);
      changed = bytes;
      store(changed, 7, 0, 999);
      failure(changed, blocks, schema, "BLEND_SCENE_REFERENCE_INVALID", 7);
      changed = bytes;
      store(changed, 8, width, 0);
      failure(changed, blocks, schema, "BLEND_SCENE_LIST_INVALID", 8);
      changed = bytes;
      store(changed, 3, idSize + width, blocks[7].oldAddress);
      failure(changed, blocks, schema, "BLEND_SCENE_LIST_INVALID", 7);
      changed = bytes;
      store(changed, 3, idSize + width, blocks[6].oldAddress);
      store(changed, 7, 0, blocks[7].oldAddress);
      failure(changed, blocks, schema, "BLEND_SCENE_CYCLE", 7);
      changed = bytes;
      store(changed, 3, idSize + 2 * width, blocks[11].oldAddress);
      store(changed, 3, idSize + 3 * width, blocks[11].oldAddress);
      store(changed, 11, 2 * width, blocks[2].oldAddress);
      failure(changed, blocks, schema, "BLEND_SCENE_CYCLE", 3);
      changed = bytes;
      store(changed, 9, 2 * width, blocks[2].oldAddress);
      failure(changed, blocks, schema, "BLEND_SCENE_CYCLE", 2);
      changed = bytes;
      store(changed, 3, idSize, blocks[6].oldAddress);
      store(changed, 3, idSize + width, blocks[6].oldAddress);
      failure(changed, blocks, schema, "BLEND_SCENE_LIST_INVALID", 6);
      for (const std::size_t index : {2, 4, 6, 9}) {
        auto records = blocks;
        records[index].count = 2;
        failure(bytes, records, schema, "BLEND_SCENE_REFERENCE_INVALID", static_cast<std::uint32_t>(index));
        records = blocks;
        records[index].code = {'S', 'C', 0, 0};
        failure(bytes, records, schema, "BLEND_SCENE_REFERENCE_INVALID", static_cast<std::uint32_t>(index));
        records = blocks;
        records[index].sdnaIndex = 0;
        records[index].length = idSize;
        failure(bytes, records, schema, "BLEND_SCENE_REFERENCE_INVALID", static_cast<std::uint32_t>(index));
      }
      auto dna = schema;
      dna.structs[5].members[1].typeIndex = 3;
      failure(bytes, blocks, dna, "BLEND_DNA_SIZE", 2);
      dna = schema;
      dna.structs[7].members[2].typeIndex = 9;
      failure(bytes, blocks, dna, "BLEND_SCENE_REFERENCE_INVALID", 6);
      dna = schema;
      dna.structs[5].members.pop_back();
      failure(bytes, blocks, dna, "BLEND_DNA_MEMBER", 2);
      changed = bytes;
      for (std::size_t member = idSize; member < collectionSize; member += width) {
        store(changed, 2, member, 0);
      }
      const auto empty = select(changed, blocks, schema, {1, 1});
      Require(empty.HasValue() && empty.GetValue().objects.empty(),
          "An empty master Collection succeeds with one visit and depth one");
      changed = bytes;
      store(changed, 9, 0, blocks[11].oldAddress);
      store(changed, 11, width, blocks[9].oldAddress);
      store(changed, 11, 2 * width, blocks[3].oldAddress);
      store(changed, 2, idSize + 3 * width, blocks[11].oldAddress);
      const auto shared = select(changed, blocks, schema, {9, 2});
      Require(shared.HasValue() && shared.GetValue().objects.size() == 2,
          "Repeated child Collection references are shared, not cyclic");
      auto records = blocks;
      std::reverse(records.begin(), records.end());
      const auto reordered = select(bytes, records, schema);
      Require(reordered.HasValue() && reordered.GetValue().objects[0].sourceName == "One" &&
                  reordered.GetValue().objects[1].sourceName == "Two" &&
                  records[reordered.GetValue().objects[0].blockIndex].oldAddress == blocks[4].oldAddress,
          "Object membership and discovery order do not depend on block enumeration");
      std::size_t objectParent = 4;
      std::size_t parentReferrer = objectParent;
      for (std::size_t depth = 0; depth < 256; ++depth) {
        const auto parentIndex = blocks.size();
        add({'O', 'B', 0, 0}, 6, objectSize);
        name(parentIndex, "OBParent");
        link(objectParent, idSize, parentIndex);
        parentReferrer = objectParent;
        objectParent = parentIndex;
      }
      const auto deepParents = select(bytes, blocks, schema, {264, 257});
      Require(deepParents.HasValue() && deepParents.GetValue().objects.size() == 2 &&
                  deepParents.GetValue().objects[0].parentBlockIndex == 12,
          "Deep parent chains are iterative and do not add parent-only Objects to membership");
      failure(bytes, blocks, schema, "BLEND_SCENE_DEPTH_LIMIT",
          static_cast<std::uint32_t>(parentReferrer), {264, 256});
      failure(bytes, blocks, schema, "BLEND_SCENE_VISIT_LIMIT",
          static_cast<std::uint32_t>(parentReferrer), {263, 257});
      store(bytes, 4, idSize, 0);
      for (std::size_t member = idSize; member < collectionSize; member += width) {
        store(bytes, 2, member, 0);
      }
      std::size_t parent = 2;
      std::size_t finalReferrer = parent;
      for (std::size_t depth = 0; depth < 256; ++depth) {
        const auto childIndex = blocks.size();
        add({'G', 'R', 0, 0}, 5, collectionSize);
        name(childIndex, "GRDeep");
        const auto nodeIndex = blocks.size();
        add({'D', 'A', 'T', 'A'}, 8, nodeSize);
        link(parent, idSize + 2 * width, nodeIndex);
        link(parent, idSize + 3 * width, nodeIndex);
        link(nodeIndex, 2 * width, childIndex);
        finalReferrer = parent;
        parent = childIndex;
      }
      const auto deep = select(bytes, blocks, schema, {513, 257});
      Require(deep.HasValue() && deep.GetValue().objects.empty(),
          "Deep Collection chains use an explicit stack at exact visit and depth budgets");
      failure(bytes, blocks, schema, "BLEND_SCENE_DEPTH_LIMIT",
          static_cast<std::uint32_t>(finalReferrer), {513, 256});
      bytes = membershipBytes;
      blocks = membershipBlocks;
      schema.types.push_back({"Mesh", idSize});
      schema.structs.push_back({13, {{3, 3, "id", 0, {}, 0, idSize}}});
      const auto meshIndex = static_cast<std::uint32_t>(blocks.size());
      add({'M', 'E', 0, 0}, 9, idSize);
      name(meshIndex, "MEShared");
      const auto dataOffset = static_cast<std::size_t>(idSize + width);
      link(4, dataOffset, meshIndex);
      link(5, dataOffset, meshIndex);
      const auto sharedData = select(bytes, blocks, schema, {9, 2});
      Require(sharedData.HasValue() && sharedData.GetValue().objects.size() == 2 &&
                  sharedData.GetValue().objects[0].dataBlockIndex == meshIndex &&
                  sharedData.GetValue().objects[1].dataBlockIndex == meshIndex,
          "Shared Object data consumes one visit and retains caller-sequence indices");
      failure(bytes, blocks, schema, "BLEND_SCENE_VISIT_LIMIT", 4, {8, 2});
      auto dataDna = schema;
      dataDna.structs[6].members[2].typeIndex = 3;
      Require(select(bytes, blocks, dataDna, {9, 2}).HasValue(),
          "Saved Object data accepts both void and ID pointer declarations");
      for (const auto address : {std::uint64_t{999}, blocks[meshIndex].oldAddress + 1}) {
        auto invalidData = bytes;
        store(invalidData, 4, dataOffset, address);
        failure(invalidData, blocks, schema, "BLEND_SCENE_REFERENCE_INVALID", 4);
      }
      for (const std::size_t targetIndex : {0, 6}) {
        auto invalidData = bytes;
        store(invalidData, 4, dataOffset, blocks[targetIndex].oldAddress);
        failure(invalidData, blocks, schema, "BLEND_SCENE_REFERENCE_INVALID",
            targetIndex == 0 ? 4 : 6);
      }
      auto invalidData = bytes;
      store(invalidData, meshIndex, 16, 999);
      failure(invalidData, blocks, schema, "BLEND_SCENE_LINKED_UNSUPPORTED", meshIndex);
      invalidData = bytes;
      invalidData[static_cast<std::size_t>(blocks[meshIndex].offset)] = std::byte{'X'};
      failure(invalidData, blocks, schema, "BLEND_SCENE_NAME_INVALID", meshIndex);
      invalidData = bytes;
      std::fill_n(invalidData.begin() + static_cast<std::ptrdiff_t>(blocks[meshIndex].offset) + 2,
          14, std::byte{'A'});
      failure(invalidData, blocks, schema, "BLEND_SCENE_NAME_INVALID", meshIndex);
      for (const auto code : {std::array<char, 4>{'D', 'A', 'T', 'A'},
               std::array<char, 4>{'M', 'E', 'X', 0}, std::array<char, 4>{'m', 'e', 0, 0}}) {
        auto invalidBlocks = blocks;
        invalidBlocks[meshIndex].code = code;
        failure(bytes, invalidBlocks, schema, "BLEND_SCENE_REFERENCE_INVALID",
            code == std::array<char, 4>{'D', 'A', 'T', 'A'} ? meshIndex : 4);
      }
      auto invalidBlocks = blocks;
      invalidBlocks[meshIndex].count = 2;
      failure(bytes, invalidBlocks, schema, "BLEND_SCENE_REFERENCE_INVALID", meshIndex);
      invalidBlocks = blocks;
      invalidBlocks[meshIndex].sdnaIndex = 0;
      failure(bytes, invalidBlocks, schema, "BLEND_DNA_MEMBER", meshIndex);
      invalidBlocks[meshIndex].sdnaIndex = static_cast<std::uint32_t>(schema.structs.size());
      failure(bytes, invalidBlocks, schema, "BLEND_DNA_INDEX", meshIndex);
      dataDna = schema;
      dataDna.structs[6].members[2].typeIndex = 10;
      failure(bytes, blocks, dataDna, "BLEND_SCENE_REFERENCE_INVALID", 4);
      dataDna = schema;
      dataDna.structs[6].members[2].pointerLevel = 2;
      failure(bytes, blocks, dataDna, "BLEND_SCENE_REFERENCE_INVALID", 4);
      dataDna = schema;
      dataDna.structs[6].members[2].arrayDimensions = {1};
      failure(bytes, blocks, dataDna, "BLEND_SCENE_REFERENCE_INVALID", 4);
      dataDna = schema;
      dataDna.structs[6].members[2].baseName = "other";
      failure(bytes, blocks, dataDna, "BLEND_DNA_MEMBER", 4);
      auto dataParents = bytes;
      store(dataParents, 4, idSize, blocks[10].oldAddress);
      store(dataParents, 10, 16, 0);
      store(dataParents, 10, dataOffset, blocks[meshIndex].oldAddress);
      const auto parentData = select(dataParents, blocks, schema, {10, 2});
      Require(parentData.HasValue() && parentData.GetValue().objects.size() == 2 &&
                  parentData.GetValue().objects[0].parentBlockIndex == 10,
          "Parent-only Object data is validated without expanding membership");
      store(dataParents, 10, dataOffset, 999);
      failure(dataParents, blocks, schema, "BLEND_SCENE_REFERENCE_INVALID", 10);
      dataParents = bytes;
      store(dataParents, 4, idSize, blocks[10].oldAddress);
      store(dataParents, 10, 16, 0);
      store(dataParents, 4, dataOffset, blocks[10].oldAddress);
      Require(select(dataParents, blocks, schema, {10, 2}).HasValue(),
          "Generic ID data validation counts the Object/data target union once");
      failure(dataParents, blocks, schema, "BLEND_SCENE_VISIT_LIMIT", 5, {9, 2});
      auto dataRecords = blocks;
      std::reverse(dataRecords.begin(), dataRecords.end());
      const auto reorderedData = select(bytes, dataRecords, schema, {9, 2});
      Require(reorderedData.HasValue() &&
                  dataRecords[*reorderedData.GetValue().objects[0].dataBlockIndex].oldAddress == blocks[meshIndex].oldAddress &&
                  dataRecords[*reorderedData.GetValue().objects[1].dataBlockIndex].oldAddress == blocks[meshIndex].oldAddress,
          "Data indices follow reordered caller block sequences");
      auto valueBytes = membershipBytes;
      auto valueBlocks = membershipBlocks;
      auto valueDna = schema;
      const auto shortType = static_cast<std::uint16_t>(valueDna.types.size());
      valueDna.types.push_back({"short", 2});
      const auto intType = static_cast<std::uint16_t>(valueDna.types.size());
      valueDna.types.push_back({"int", 4});
      const std::uint64_t visibilitySize = order == blend::ByteOrder::Big ? 4 : 2;
      const auto typeOffset = static_cast<std::uint64_t>(objectSize);
      const auto visibilityOffset = typeOffset + 2;
      const auto flagsOffset = visibilityOffset + visibilitySize;
      const auto instanceOffset = flagsOffset + 2;
      const auto valueSize = static_cast<std::uint16_t>(instanceOffset + width);
      valueDna.types[10].length = valueSize;
      valueDna.structs[6].members.insert(valueDna.structs[6].members.end(),
          {{shortType, 0, "type", 0, {}, typeOffset, 2},
              {visibilitySize == 2 ? shortType : intType, 0,
                  width == 4 ? "visibility_flag" : "restrictflag", 0, {}, visibilityOffset, visibilitySize},
              {shortType, 0, "transflag", 0, {}, flagsOffset, 2},
              {9, 0, width == 4 ? "instance_collection" : "dup_group", 1, {}, instanceOffset, width}});
      for (const auto index : {4, 5, 10}) {
        const auto offset = valueBytes.size();
        std::vector<std::byte> payload(valueSize);
        std::copy_n(valueBytes.begin() + static_cast<std::ptrdiff_t>(valueBlocks[index].offset),
            objectSize, payload.begin());
        valueBytes.insert(valueBytes.end(), payload.begin(), payload.end());
        valueBlocks[index].offset = offset;
        valueBlocks[index].length = valueSize;
      }
      const auto valueMesh = static_cast<std::uint32_t>(valueBlocks.size());
      valueBlocks.push_back({{'M', 'E', 0, 0}, idSize, 3000, 9, 1, valueBytes.size()});
      valueBytes.resize(valueBytes.size() + idSize);
      valueBytes[static_cast<std::size_t>(valueBlocks.back().offset)] = std::byte{'M'};
      valueBytes[static_cast<std::size_t>(valueBlocks.back().offset) + 1] = std::byte{'E'};
      auto setValue = [&](auto& input, std::uint32_t index, std::uint64_t offset,
                          std::uint64_t value, std::size_t size) {
        StoreBits(input, static_cast<std::size_t>(valueBlocks[index].offset + offset), value, size, order);
      };
      for (const auto index : {4, 5}) {
        setValue(valueBytes, index, typeOffset, 1, 2);
        setValue(valueBytes, index, idSize + width, 3000, width);
      }
      setValue(valueBytes, 4, visibilityOffset, 4, visibilitySize);
      setValue(valueBytes, 4, flagsOffset, 256, 2);
      setValue(valueBytes, 4, instanceOffset, valueBlocks[3].oldAddress, width);
      auto readValues = [&](const auto& input, const auto& records, const auto& dna,
                            blend::SceneTraversalLimits limits = {9, 2}) {
        return blend::SelectSceneObjectValues(input, records, dna, header, limits);
      };
      auto valueFailure = [&](const auto& input, const auto& records, const auto& dna,
                              std::string_view code, std::uint32_t index,
                              blend::SceneTraversalLimits limits = {32, 8}) {
        const auto failed = readValues(input, records, dna, limits);
        if (failed.HasValue()) {
          throw std::runtime_error("Expected Object value failure " + std::string(code));
        }
        if (failed.GetError().code != code || failed.GetError().blockIndex != index) {
          throw std::runtime_error("Expected " + std::string(code) + " at " + std::to_string(index) +
                                   ", got " + failed.GetError().code + " at " +
                                   (failed.GetError().blockIndex ? std::to_string(*failed.GetError().blockIndex) : "none"));
        }
        Require(!failed.HasValue() && failed.GetError().code == code &&
                    failed.GetError().severity == blend::Severity::Fatal && !failed.GetError().recoverable &&
                    failed.GetError().blockIndex == index && failed.GetError().byteOffset == records[index].offset,
            "Object value failure has exact fatal code and block context");
      };
      const auto values = readValues(valueBytes, valueBlocks, valueDna);
      Require(values.HasValue() && values.GetValue().objects.size() == 2 &&
                  values.GetValue().objects[0].values && values.GetValue().objects[0].values->type == 1 &&
                  values.GetValue().objects[0].values->hiddenForRender &&
                  values.GetValue().objects[0].values->transformFlags == 256 &&
                  values.GetValue().objects[0].values->instanceCollectionBlockIndex == 3 &&
                  !values.GetValue().objects[1].values->hiddenForRender &&
                  !values.GetValue().objects[1].values->instanceCollectionBlockIndex &&
                  !result.GetValue().objects[0].values,
          "Opt-in Object values retain saved flags and references without changing generic selection");
      valueFailure(valueBytes, valueBlocks, valueDna, "BLEND_SCENE_VISIT_LIMIT", 4, {8, 2});
      auto changedValues = valueBytes;
      setValue(changedValues, 4, idSize + width, 0, width);
      valueFailure(changedValues, valueBlocks, valueDna, "BLEND_SCENE_REFERENCE_INVALID", 4);
      setValue(changedValues, 4, typeOffset, 0, 2);
      Require(readValues(changedValues, valueBlocks, valueDna).HasValue(),
          "An Empty permits null data");
      valueFailure(valueBytes, valueBlocks, [&] {
        auto dna = valueDna;
        dna.structs[6].members.back().typeIndex = 10;
        return dna; }(), "BLEND_SCENE_REFERENCE_INVALID", 4);
      for (const auto badType : {6, 65535}) {
        changedValues = valueBytes;
        setValue(changedValues, 4, typeOffset, badType, 2);
        valueFailure(changedValues, valueBlocks, valueDna, "BLEND_SCENE_OBJECT_TYPE_UNSUPPORTED", 4);
        Require(select(changedValues, valueBlocks, valueDna, {9, 2}).HasValue(),
            "Generic selection does not impose the opt-in Object type policy");
      }
      for (std::size_t member = 3; member < 6; ++member) {
        auto dna = valueDna;
        dna.structs[6].members[member].arrayDimensions = {1};
        valueFailure(valueBytes, valueBlocks, dna, "BLEND_SCENE_REFERENCE_INVALID", 4);
        dna = valueDna;
        dna.structs[6].members[member].typeIndex = 1;
        valueFailure(valueBytes, valueBlocks, dna,
            member == 4 && visibilitySize == 4 ? "BLEND_SCENE_REFERENCE_INVALID" : "BLEND_DNA_SIZE", 4);
      }
      auto missingValue = valueDna;
      missingValue.structs[6].members.pop_back();
      valueFailure(valueBytes, valueBlocks, missingValue, "BLEND_DNA_MEMBER", 4);
      for (const auto address : {std::uint64_t{0}, valueBlocks[3].oldAddress + 1,
               std::uint64_t{999}, valueBlocks[valueMesh].oldAddress}) {
        changedValues = valueBytes;
        setValue(changedValues, 4, instanceOffset, address, width);
        valueFailure(changedValues, valueBlocks, valueDna, "BLEND_SCENE_REFERENCE_INVALID",
            address == valueBlocks[valueMesh].oldAddress ? valueMesh : 4);
      }
      changedValues = valueBytes;
      setValue(changedValues, 4, flagsOffset, 0, 2);
      Require(readValues(changedValues, valueBlocks, valueDna).HasValue(),
          "Inactive nonnull instance references are still validated");
      setValue(changedValues, 4, instanceOffset, 999, width);
      valueFailure(changedValues, valueBlocks, valueDna, "BLEND_SCENE_REFERENCE_INVALID", 4);
      changedValues = valueBytes;
      setValue(changedValues, 4, idSize, valueBlocks[10].oldAddress, width);
      setValue(changedValues, 10, 16, 0, width);
      setValue(changedValues, 10, typeOffset, 1, 2);
      valueFailure(changedValues, valueBlocks, valueDna, "BLEND_SCENE_REFERENCE_INVALID", 10);
      setValue(changedValues, 10, idSize + width, 3000, width);
      Require(readValues(changedValues, valueBlocks, valueDna, {10, 2}).HasValue(),
          "Parent-only Object values are validated but do not expand membership");
      const auto instanceIndex = static_cast<std::uint32_t>(valueBlocks.size());
      valueBlocks.push_back({{'G', 'R', 0, 0}, collectionSize, 3100, 5, 1, valueBytes.size()});
      valueBytes.resize(valueBytes.size() + collectionSize);
      valueBytes[static_cast<std::size_t>(valueBlocks.back().offset)] = std::byte{'G'};
      valueBytes[static_cast<std::size_t>(valueBlocks.back().offset) + 1] = std::byte{'R'};
      for (const auto index : {4, 5}) {
        setValue(valueBytes, index, instanceOffset, 3100, width);
      }
      Require(readValues(valueBytes, valueBlocks, valueDna, {10, 2}).HasValue(),
          "Shared instance targets consume one additional visit without traversal");
      valueFailure(valueBytes, valueBlocks, valueDna, "BLEND_SCENE_VISIT_LIMIT", 4, {9, 2});
      changedValues = valueBytes;
      setValue(changedValues, instanceIndex, 16, 999, width);
      valueFailure(changedValues, valueBlocks, valueDna, "BLEND_SCENE_LINKED_UNSUPPORTED", instanceIndex);
      changedValues = valueBytes;
      changedValues[static_cast<std::size_t>(valueBlocks[instanceIndex].offset)] = std::byte{'X'};
      valueFailure(changedValues, valueBlocks, valueDna, "BLEND_SCENE_NAME_INVALID", instanceIndex);
      auto badInstance = valueBlocks;
      badInstance[instanceIndex].count = 2;
      valueFailure(valueBytes, badInstance, valueDna, "BLEND_SCENE_REFERENCE_INVALID", instanceIndex);
      std::reverse(badInstance.begin(), badInstance.end());
      badInstance.front().count = 1;
      const auto reorderedValues = readValues(valueBytes, badInstance, valueDna, {10, 2});
      Require(reorderedValues.HasValue() &&
                  badInstance[*reorderedValues.GetValue().objects[0].values->instanceCollectionBlockIndex].oldAddress == 3100,
          "Instance indices refer to the caller's reordered blocks");
      struct Mapping {
        std::uint16_t type;
        std::string_view code;
        std::string_view dnaType;
      };
      for (const auto mapping : {Mapping{0, "IM", "Image"}, {1, "ME", "Mesh"},
               {2, "CU", "Curve"}, {3, "CU", "Curve"}, {4, "CU", "Curve"},
               {5, "MB", "MetaBall"}, {10, "LA", "Lamp"}, {11, "CA", "Camera"},
               {12, "SK", "Speaker"}, {13, "LP", "LightProbe"}, {22, "LT", "Lattice"},
               {25, "AR", "bArmature"}, {26, "GD", "bGPdata"}, {27, "CV", "Curves"},
               {28, "PT", "PointCloud"}, {29, "VO", "Volume"}, {30, "GP", "GreasePencil"}}) {
        auto dna = valueDna;
        auto records = valueBlocks;
        changedValues = valueBytes;
        for (const auto index : {4, 5}) {
          setValue(changedValues, index, typeOffset, mapping.type, 2);
        }
        records[valueMesh].code = {mapping.code[0], mapping.code[1], 0, 0};
        changedValues[static_cast<std::size_t>(records[valueMesh].offset)] = static_cast<std::byte>(mapping.code[0]);
        changedValues[static_cast<std::size_t>(records[valueMesh].offset) + 1] = static_cast<std::byte>(mapping.code[1]);
        dna.types[13].name = mapping.dnaType;
        Require(readValues(changedValues, records, dna, {10, 2}).HasValue(),
            "Each verified Object type requires the corresponding code and SDNA data type");
        dna.types[13].name = "Wrong";
        valueFailure(changedValues, records, dna, "BLEND_SCENE_REFERENCE_INVALID", valueMesh);
        setValue(changedValues, 4, idSize + width, 0, width);
        if (mapping.type != 0) {
          valueFailure(changedValues, records, valueDna, "BLEND_SCENE_REFERENCE_INVALID", 4);
        }
      }
      valueBytes.clear();
      Require(values.GetValue().objects[0].values->hiddenForRender &&
                  values.GetValue().objects[0].values->instanceCollectionBlockIndex == 3,
          "Saved Object values own their output after inputs are released");
      name(4, "OBChanged");
      schema.types[10].name = "Changed";
      Require(result.GetValue().objects[0].sourceName == "One" &&
                  result.GetValue().scene.metadata.sourceScene == "Chosen",
          "Selected Object and Scene names own their bytes");
    }
  }
}

void CheckSelectedScene(const char* path, bool hasSavedScene) {
  blend::FileByteSource source(path);
  Require(source.IsOpen(), "Scene selection fixture opens");
  const auto decoded = blend::ReadFileBytes(source, {1024 * 1024, 2 * 1024 * 1024, 16, 23});
  Require(decoded.HasValue(), "Scene selection fixture decodes");
  blend::MemoryByteSource memory(decoded.GetValue());
  const auto header = blend::ReadHeader(memory);
  const auto blocks = blend::ReadBlocks(memory, 10000);
  Require(header.HasValue() && blocks.HasValue(), "Scene selection container reads");
  const auto dna = std::find_if(blocks.GetValue().begin(), blocks.GetValue().end(),
      [](const auto& block) { return block.code == std::array<char, 4>{'D', 'N', 'A', '1'}; });
  Require(dna != blocks.GetValue().end(), "Scene selection fixture has DNA1");
  const auto schema = blend::ReadDna(
      std::span<const std::byte>(decoded.GetValue()).subspan(static_cast<std::size_t>(dna->offset), static_cast<std::size_t>(dna->length)),
      header.GetValue());
  Require(schema.HasValue(), "Scene selection fixture SDNA reads");
  const auto selected = blend::SelectScene(decoded.GetValue(), blocks.GetValue(),
      schema.GetValue(), header.GetValue());
  if (!hasSavedScene) {
    Require(!selected.HasValue() && selected.GetError().code == "BLEND_SCENE_ACTIVE_MISSING",
        "Scene-only library has no implicit fallback");
    const auto objects = blend::SelectSceneObjects(decoded.GetValue(), blocks.GetValue(),
        schema.GetValue(), header.GetValue(), {10000, 64});
    Require(!objects.HasValue() && objects.GetError().code == "BLEND_SCENE_ACTIVE_MISSING",
        "Object selection preserves the Scene-only library's lack of a saved active Scene");
    return;
  }
  if (!selected.HasValue()) {
    throw std::runtime_error(selected.GetError().code + ": " + selected.GetError().message);
  }
  Require(selected.GetValue().metadata.sourceScene == "Scene" &&
              selected.GetValue().metadata.sourceVersion == header.GetValue().SourceVersion() &&
              selected.GetValue().metadata.sourceUnitScale > 0,
      "Saved active scene metadata is selected through SDNA");
  const auto scene = blend::ViewDnaBlock(decoded.GetValue(), blocks.GetValue(),
      schema.GetValue(), header.GetValue(), selected.GetValue().blockIndex);
  Require(scene.HasValue(), "Selected Scene binds");
  const auto master = scene.GetValue().Member("master_collection");
  Require(master.HasValue() && master.GetValue().Type().name == "Collection" &&
              master.GetValue().PointerLevel() == 1 &&
              master.GetValue().ArrayDimensions().empty(),
      "Saved Scene has a scalar master Collection pointer");
  const auto address = master.GetValue().Pointer();
  const auto pointers = blend::BuildPointerMap(blocks.GetValue());
  Require(address.HasValue() && address.GetValue() != 0 && pointers.HasValue(),
      "Master Collection has a saved address");
  const auto target = pointers.GetValue().Resolve(address.GetValue());
  Require(target.HasValue() && target.GetValue().has_value(),
      "Master Collection resolves exactly");
  const auto collection = blend::ViewDnaBlock(decoded.GetValue(), blocks.GetValue(),
      schema.GetValue(), header.GetValue(), *target.GetValue());
  Require(collection.HasValue() && collection.GetValue().Type().name == "Collection",
      "Master Collection binds through SDNA");
  for (const auto member : {"gobject", "children"}) {
    const auto list = collection.GetValue().Member(member);
    Require(list.HasValue() && list.GetValue().Type().name == "ListBase" &&
                list.GetValue().PointerLevel() == 0 &&
                list.GetValue().ArrayDimensions().empty(),
        "Collection stores embedded object and child ListBases");
  }
  const auto objects = blend::SelectSceneObjects(decoded.GetValue(), blocks.GetValue(),
      schema.GetValue(), header.GetValue(), {10000, 64});
  if (!objects.HasValue()) {
    throw std::runtime_error(objects.GetError().code + ": " + objects.GetError().message);
  }
  std::vector<std::string> names;
  const auto objectValues = blend::SelectSceneObjectValues(decoded.GetValue(), blocks.GetValue(),
      schema.GetValue(), header.GetValue(), {10000, 64});
  if (!objectValues.HasValue()) {
    throw std::runtime_error(objectValues.GetError().code + ": " + objectValues.GetError().message);
  }
  Require(objectValues.GetValue().objects.size() == objects.GetValue().objects.size(),
      "Object value reading preserves corpus membership");
  for (const auto& object : objects.GetValue().objects) {
    names.push_back(object.sourceName);
    const auto view = blend::ViewDnaBlock(decoded.GetValue(), blocks.GetValue(),
        schema.GetValue(), header.GetValue(), object.blockIndex);
    Require(view.HasValue(), "Corpus Object binds through SDNA");
    const auto parent = view.GetValue().Member("parent");
    Require(parent.HasValue() && parent.GetValue().Type().name == "Object" &&
                parent.GetValue().PointerLevel() == 1 && parent.GetValue().ArrayDimensions().empty(),
        "Corpus Object has a scalar Object parent pointer");
    const auto savedParent = parent.GetValue().Pointer();
    Require(savedParent.HasValue() && savedParent.GetValue() == 0 && !object.parentBlockIndex,
        "Both normal-save corpus files have unparented Objects");
    const auto data = view.GetValue().Member("data");
    Require(data.HasValue() && data.GetValue().Type().name == (header.GetValue().version == 405 ? "void" : "ID") &&
                data.GetValue().PointerLevel() == 1 && data.GetValue().ArrayDimensions().empty(),
        "Corpus Object data declarations retain the verified void/ID version difference");
    const auto savedData = data.GetValue().Pointer();
    Require(savedData.HasValue() && object.dataBlockIndex &&
                blocks.GetValue()[*object.dataBlockIndex].oldAddress == savedData.GetValue(),
        "Corpus Object data resolves to the exact saved ID address");
    const auto targetData = blend::ViewDnaBlock(decoded.GetValue(), blocks.GetValue(),
        schema.GetValue(), header.GetValue(), *object.dataBlockIndex);
    const std::string_view expectedType = object.sourceName == "Camera" ? "Camera" : object.sourceName == "Cube" ? "Mesh"
                                                                                                                 : "Lamp";
    const auto expectedCode = expectedType == "Camera" ? std::array<char, 4>{'C', 'A', 0, 0} : expectedType == "Mesh" ? std::array<char, 4>{'M', 'E', 0, 0}
                                                                                                                      : std::array<char, 4>{'L', 'A', 0, 0};
    Require(targetData.HasValue() && targetData.GetValue().Type().name == expectedType &&
                blocks.GetValue()[*object.dataBlockIndex].code == expectedCode,
        "Corpus Camera/Cube/Light data resolves to Camera/Mesh/Lamp ID records");
    const auto& savedValues = *std::find_if(objectValues.GetValue().objects.begin(), objectValues.GetValue().objects.end(),
        [&](const auto& candidate) { return candidate.blockIndex == object.blockIndex; })
                                   ->values;
    Require(savedValues.type == (object.sourceName == "Camera" ? 11 : object.sourceName == "Cube" ? 1
                                                                                                  : 10) &&
                !savedValues.hiddenForRender && !savedValues.instanceCollectionBlockIndex,
        "Both Blender-written corpus files decode type, render visibility and null instance targets");
    const auto visibility = view.GetValue().Member("restrictflag");
    Require(visibility.HasValue() &&
                visibility.GetValue().Type().name == (header.GetValue().version == 405 ? "short" : "int"),
        "Corpus visibility retains the verified short/int version difference");
    for (const auto flags : {1, 2, 4, 5, 8}) {
      auto changed = decoded.GetValue();
      StoreBits(changed, static_cast<std::size_t>(visibility.GetValue().Bytes().data() - decoded.GetValue().data()),
          flags, visibility.GetValue().Bytes().size(), header.GetValue().byteOrder);
      const auto checked = blend::SelectSceneObjectValues(changed, blocks.GetValue(),
          schema.GetValue(), header.GetValue(), {10000, 64});
      Require(checked.HasValue(), "Corpus visibility mutation reads");
      const auto found = std::find_if(checked.GetValue().objects.begin(), checked.GetValue().objects.end(),
          [&](const auto& candidate) { return candidate.blockIndex == object.blockIndex; });
      Require(found->values->hiddenForRender == ((flags & 4) != 0),
          "Only the saved render bit determines hiddenForRender");
    }
    const auto instance = view.GetValue().Member("dup_group");
    const auto transformFlags = view.GetValue().Member("transflag");
    Require(instance.HasValue() && transformFlags.HasValue(),
        "Corpus saved instance and transform-flag members bind");
    for (const auto instanceAddress : {blocks.GetValue()[*target.GetValue()].oldAddress,
             blocks.GetValue()[*target.GetValue()].oldAddress + 1,
             std::numeric_limits<std::uint64_t>::max()}) {
      auto changed = decoded.GetValue();
      StoreBits(changed, static_cast<std::size_t>(instance.GetValue().Bytes().data() - decoded.GetValue().data()),
          instanceAddress, header.GetValue().pointerSize, header.GetValue().byteOrder);
      const auto checked = blend::SelectSceneObjectValues(changed, blocks.GetValue(),
          schema.GetValue(), header.GetValue(), {10000, 64});
      if (instanceAddress == blocks.GetValue()[*target.GetValue()].oldAddress) {
        Require(checked.HasValue(), "A corpus instance reference resolves to its local master Collection");
        const auto found = std::find_if(checked.GetValue().objects.begin(), checked.GetValue().objects.end(),
            [&](const auto& candidate) { return candidate.blockIndex == object.blockIndex; });
        Require(found->values->instanceCollectionBlockIndex == *target.GetValue(),
            "Corpus instance output retains the caller's exact block index without expanding membership");
      } else {
        Require(!checked.HasValue() && checked.GetError().code == "BLEND_SCENE_REFERENCE_INVALID" &&
                    checked.GetError().blockIndex == object.blockIndex &&
                    checked.GetError().byteOffset == blocks.GetValue()[object.blockIndex].offset,
            "Invalid corpus instance references have exact referring Object context");
      }
    }
    auto missingInstance = decoded.GetValue();
    StoreBits(missingInstance, static_cast<std::size_t>(transformFlags.GetValue().Bytes().data() - decoded.GetValue().data()),
        256, 2, header.GetValue().byteOrder);
    const auto instancing = blend::SelectSceneObjectValues(missingInstance, blocks.GetValue(),
        schema.GetValue(), header.GetValue(), {10000, 64});
    Require(!instancing.HasValue() && instancing.GetError().code == "BLEND_SCENE_REFERENCE_INVALID" &&
                instancing.GetError().blockIndex == object.blockIndex &&
                instancing.GetError().byteOffset == blocks.GetValue()[object.blockIndex].offset,
        "Corpus collection-instancing flags require a nonnull Collection reference");
    const auto dataOffset = static_cast<std::size_t>(data.GetValue().Bytes().data() - decoded.GetValue().data());
    for (const auto address : {std::uint64_t{0}, savedData.GetValue() + 1,
             std::numeric_limits<std::uint64_t>::max()}) {
      auto changed = decoded.GetValue();
      StoreBits(changed, dataOffset, address, header.GetValue().pointerSize, header.GetValue().byteOrder);
      const auto checked = blend::SelectSceneObjects(changed, blocks.GetValue(),
          schema.GetValue(), header.GetValue(), {10000, 64});
      if (address == 0) {
        Require(checked.HasValue(), "Generic data selection does not impose Object type-specific null policy");
        const auto found = std::find_if(checked.GetValue().objects.begin(), checked.GetValue().objects.end(),
            [&](const auto& candidate) { return candidate.blockIndex == object.blockIndex; });
        Require(found != checked.GetValue().objects.end() && !found->dataBlockIndex,
            "Null corpus data is retained as an absent target");
        const auto strict = blend::SelectSceneObjectValues(changed, blocks.GetValue(),
            schema.GetValue(), header.GetValue(), {10000, 64});
        Require(!strict.HasValue() && strict.GetError().code == "BLEND_SCENE_REFERENCE_INVALID" &&
                    strict.GetError().blockIndex == object.blockIndex &&
                    strict.GetError().byteOffset == blocks.GetValue()[object.blockIndex].offset,
            "Opt-in corpus Object values reject missing type-required data");
      } else {
        Require(!checked.HasValue() && checked.GetError().code == "BLEND_SCENE_REFERENCE_INVALID" &&
                    checked.GetError().severity == blend::Severity::Fatal && !checked.GetError().recoverable &&
                    checked.GetError().blockIndex == object.blockIndex &&
                    checked.GetError().byteOffset == blocks.GetValue()[object.blockIndex].offset,
            "Mutated corpus data has exact fatal diagnostics and referring Object context");
      }
    }
    const auto offset = static_cast<std::size_t>(parent.GetValue().Bytes().data() - decoded.GetValue().data());
    for (const auto address : {blocks.GetValue()[object.blockIndex].oldAddress,
             blocks.GetValue()[object.blockIndex].oldAddress + 1,
             std::numeric_limits<std::uint64_t>::max()}) {
      auto changed = decoded.GetValue();
      StoreBits(changed, offset, address, header.GetValue().pointerSize, header.GetValue().byteOrder);
      const auto failed = blend::SelectSceneObjects(changed, blocks.GetValue(),
          schema.GetValue(), header.GetValue(), {10000, 64});
      Require(!failed.HasValue(), "Mutated corpus parent references fail");
      const auto& error = failed.GetError();
      Require(error.code == (address == blocks.GetValue()[object.blockIndex].oldAddress
                                    ? "BLEND_SCENE_CYCLE"
                                    : "BLEND_SCENE_REFERENCE_INVALID") &&
                  error.severity == blend::Severity::Fatal && !error.recoverable &&
                  error.blockIndex == object.blockIndex &&
                  error.byteOffset == blocks.GetValue()[object.blockIndex].offset,
          "Mutated corpus parents have exact fatal diagnostics and source context");
    }
  }
  std::sort(names.begin(), names.end());
  Require(names == std::vector<std::string>{"Camera", "Cube", "Light"},
      "Saved Collection membership selects the corpus objects");
}
} // namespace

int main(int argc, char** argv) {
  try {
    CheckBasis();
    CheckUnits();
    CheckScene();
    CheckSelectionLayouts();
    CheckCollectionLayouts();
    Require(argc == 4, "Three scene selection fixtures are required");
    CheckSelectedScene(argv[1], false);
    CheckSelectedScene(argv[2], true);
    CheckSelectedScene(argv[3], true);
    std::cout << "Scene IR, coordinate basis and unit checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}