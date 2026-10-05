#include <blendScene/Scene.h>
#include <blendScene/Selection.h>
#include <blendScene/Decode.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

void CheckNativeMeshes(const std::vector<std::byte>& bytes,
    const std::vector<blend::BlendBlock>& blocks, const blend::DnaSchema& schema,
    const blend::Header& header);
void CheckCorpusMesh(const std::vector<std::byte>& bytes,
    const std::vector<blend::BlendBlock>& blocks, const blend::DnaSchema& schema,
    const blend::Header& header, const blend::SelectedSceneObjects& selected);

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

void CheckLocalTransforms() {
  const auto compare = [](const blend::Matrix4& actual, const blend::Matrix4& expected) {
    for (std::size_t row = 0; row < 4; ++row) {
      for (std::size_t column = 0; column < 4; ++column) {
        Require(Near(actual[row][column], expected[row][column]),
            "Parent-relative matrix matches expected local transform");
      }
    }
    Require(actual[3] == blend::IdentityMatrix[3], "Local transform remains exactly affine");
  };
  const blend::Matrix4 parent = {{{0, -3, 1, 7},
      {2, 1, 0, -11},
      {0, 0, -4, 13},
      {0, 0, 0, 1}}};
  const blend::Matrix4 local = {{{0, -2, 1, 3},
      {3, 0, 0, -5},
      {0, 1, -1, 7},
      {0, 0, 0, 1}}};
  const auto world = Multiply(parent, local);
  Require(blend::ParentRelativeTransform(world) == world,
      "Root local transform is the unchanged world transform");
  Require(blend::ParentRelativeTransform(parent, parent) == blend::IdentityMatrix,
      "Equal invertible world matrices produce an identity local transform");
  compare(blend::ParentRelativeTransform(world, parent), local);
  for (const double scale : {1.0, 0.01, 0.001, 10.0}) {
    const blend::UnitConversion units(scale);
    const auto convertedParent = units.WorldTransform(parent);
    const auto convertedWorld = units.WorldTransform(world);
    const auto actual = blend::ParentRelativeTransform(convertedWorld, convertedParent);
    compare(actual, units.WorldTransform(local));
    compare(Multiply(convertedParent, actual), convertedWorld);
  }
  auto zeroScale = local;
  for (std::size_t row = 0; row < 3; ++row) {
    zeroScale[row][0] = 0;
  }
  compare(blend::ParentRelativeTransform(Multiply(parent, zeroScale), parent), zeroScale);
  Require(blend::ParentRelativeTransform(zeroScale) == zeroScale,
      "Singular roots and children do not require their own inverse");
  for (const double scale : {1e-200, 1e200, -1e-200, -1e200}) {
    auto scaledParent = blend::IdentityMatrix;
    for (std::size_t axis = 0; axis < 3; ++axis) {
      scaledParent[axis][axis] = scale;
    }
    compare(blend::ParentRelativeTransform(Multiply(scaledParent, local), scaledParent), local);
  }
  auto tinyParent = blend::IdentityMatrix;
  tinyParent[0][0] = std::numeric_limits<double>::denorm_min();
  compare(blend::ParentRelativeTransform(tinyParent, tinyParent), blend::IdentityMatrix);
  auto largeParent = blend::IdentityMatrix;
  largeParent[0][0] = std::numeric_limits<double>::max();
  largeParent[0][3] = -std::numeric_limits<double>::max();
  auto largeWorld = largeParent;
  largeWorld[0][3] = std::numeric_limits<double>::max();
  auto largeLocal = blend::IdentityMatrix;
  largeLocal[0][3] = 2;
  compare(blend::ParentRelativeTransform(largeWorld, largeParent), largeLocal);
  for (auto singular : {zeroScale, blend::IdentityMatrix}) {
    if (singular == blend::IdentityMatrix) {
      singular[1] = singular[0];
    }
    RequireFailure<std::invalid_argument>(
        [&] { blend::ParentRelativeTransform(world, singular); },
        "BLEND_SCENE_TRANSFORM_SINGULAR:");
  }
  auto zeroRow = blend::IdentityMatrix;
  zeroRow[2][2] = 0;
  RequireFailure<std::invalid_argument>(
      [&] { blend::ParentRelativeTransform(world, zeroRow); },
      "BLEND_SCENE_TRANSFORM_SINGULAR:");
  const blend::Matrix4 dependentRows = {{{2, 3, 5, 7},
      {4, 6, 10, 11},
      {1, -1, 2, 13},
      {0, 0, 0, 1}}};
  RequireFailure<std::invalid_argument>(
      [&] { blend::ParentRelativeTransform(world, dependentRows); },
      "BLEND_SCENE_TRANSFORM_SINGULAR:");
  auto unstableParent = blend::IdentityMatrix;
  unstableParent[0][1] = unstableParent[1][0] = 1;
  unstableParent[1][1] = 1 + std::numeric_limits<double>::epsilon();
  RequireFailure<std::invalid_argument>(
      [&] { blend::ParentRelativeTransform(world, unstableParent); },
      "BLEND_SCENE_TRANSFORM_SINGULAR:");
  for (const double invalid : {std::numeric_limits<double>::quiet_NaN(),
           std::numeric_limits<double>::infinity(),
           -std::numeric_limits<double>::infinity()}) {
    auto matrix = parent;
    matrix[0][1] = invalid;
    RequireFailure<std::invalid_argument>(
        [&] { blend::ParentRelativeTransform(matrix, parent); },
        "BLEND_SCENE_TRANSFORM_INVALID:");
    RequireFailure<std::invalid_argument>(
        [&] { blend::ParentRelativeTransform(world, matrix); },
        "BLEND_SCENE_TRANSFORM_INVALID:");
  }
  auto projective = parent;
  projective[3][0] = 0.1;
  RequireFailure<std::invalid_argument>(
      [&] { blend::ParentRelativeTransform(projective, parent); },
      "BLEND_SCENE_TRANSFORM_INVALID:");
  RequireFailure<std::invalid_argument>(
      [&] { blend::ParentRelativeTransform(world, projective); },
      "BLEND_SCENE_TRANSFORM_INVALID:");
  RequireFailure<std::overflow_error>(
      [&] { blend::ParentRelativeTransform(blend::IdentityMatrix, tinyParent); },
      "BLEND_SCENE_TRANSFORM_INVALID:");
  auto largeTranslation = blend::IdentityMatrix;
  largeTranslation[0][3] = std::numeric_limits<double>::max();
  auto oppositeTranslation = largeTranslation;
  oppositeTranslation[0][3] = -oppositeTranslation[0][3];
  RequireFailure<std::overflow_error>(
      [&] { blend::ParentRelativeTransform(largeTranslation, oppositeTranslation); },
      "BLEND_SCENE_TRANSFORM_INVALID:");
  auto eliminationParent = blend::IdentityMatrix;
  eliminationParent[0][1] = eliminationParent[1][0] = 1;
  eliminationParent[1][1] = 1 + 1e-8;
  auto eliminationWorld = blend::IdentityMatrix;
  eliminationWorld[0][0] = std::numeric_limits<double>::max();
  RequireFailure<std::overflow_error>(
      [&] { blend::ParentRelativeTransform(eliminationWorld, eliminationParent); },
      "BLEND_SCENE_TRANSFORM_INVALID:");
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

void CheckNativeDecode(std::vector<std::byte> bytes,
    std::vector<blend::BlendBlock> blocks, blend::DnaSchema schema,
    const blend::Header& header) {
  const auto width = header.pointerSize;
  const auto order = header.byteOrder;
  const auto idSize = schema.types[3].length;
  const auto* savedObject = schema.FindStruct("Object");
  const auto typeOffset = savedObject->FindMember("type")->offset;
  const auto visibilityOffset = savedObject->FindMember(width == 4 ? "visibility_flag" : "restrictflag")->offset;
  const auto flagsOffset = savedObject->FindMember("transflag")->offset;
  const auto instanceOffset = savedObject->FindMember(width == 4 ? "instance_collection" : "dup_group")->offset;
  const auto shortType = savedObject->FindMember("type")->typeIndex;
  auto offset = static_cast<std::uint64_t>(schema.types[10].length);
  auto& members = schema.structs[6].members;
  const auto rotationModeOffset = offset;
  members.push_back({shortType, 0, "rotmode", 0, {}, offset, 2});
  offset += 2;
  const auto parentingModeOffset = offset;
  members.push_back({shortType, 0, "partype", 0, {}, offset, 2});
  offset += 2;
  const auto animationType = static_cast<std::uint16_t>(schema.types.size());
  schema.types.push_back({"AnimData", 0});
  const auto animationOffset = offset;
  members.push_back({animationType, 0, "adt", 1, {}, offset, width});
  offset += width;
  const auto constraintsOffset = offset;
  members.push_back({8, 0, "constraints", 0, {}, offset, static_cast<std::uint64_t>(2 * width)});
  offset += 2 * width;
  members.push_back({8, 0, "modifiers", 0, {}, offset, static_cast<std::uint64_t>(2 * width)});
  offset += 2 * width;
  const auto locationOffset = offset;
  for (const auto name : {"loc", "dloc", "size", "dscale", "rot", "drot"}) {
    members.push_back({1, 0, name, 0, {3}, offset, 12});
    offset += 12;
  }
  const auto inverseOffset = offset;
  members.push_back({1, 0, "parentinv", 0, {4, 4}, offset, 64});
  offset += 64;
  const auto quaternionOffset = offset;
  for (const auto name : {"quat", "dquat"}) {
    members.push_back({1, 0, name, 0, {4}, offset, 16});
    offset += 16;
  }
  const auto axisOffset = offset;
  members.push_back({1, 0, "rotAxis", 0, {3}, offset, 12});
  offset += 12;
  const auto angleOffset = offset;
  members.push_back({1, 0, "rotAngle", 0, {}, offset, 4});
  offset += 4;
  schema.types[10].length = static_cast<std::uint16_t>(offset);
  for (const auto index : {4, 5, 10}) {
    std::vector<std::byte> payload(static_cast<std::size_t>(offset));
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(blocks[index].offset),
        static_cast<std::size_t>(blocks[index].length), payload.begin());
    blocks[index].offset = bytes.size();
    blocks[index].length = offset;
    bytes.insert(bytes.end(), payload.begin(), payload.end());
  }
  auto bits = [&](auto& input, std::uint32_t index, std::uint64_t member,
                  std::uint64_t value, std::size_t size) {
    StoreBits(input, static_cast<std::size_t>(blocks[index].offset + member), value, size, order);
  };
  auto scalar = [&](auto& input, std::uint32_t index, std::uint64_t member, float value) {
    bits(input, index, member, std::bit_cast<std::uint32_t>(value), 4);
  };
  auto vector = [&](auto& input, std::uint32_t index, std::uint64_t member, blend::Vector3 value) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      scalar(input, index, member + axis * 4, static_cast<float>(value[axis]));
    }
  };
  auto matrix = [&](auto& input, std::uint32_t index, const blend::Matrix4& value) {
    for (std::size_t row = 0; row < 4; ++row) {
      for (std::size_t column = 0; column < 4; ++column) {
        scalar(input, index, inverseOffset + (column * 4 + row) * 4, static_cast<float>(value[row][column]));
      }
    }
  };
  for (const auto index : {4, 5, 10}) {
    bits(bytes, index, 16, 0, width);
    bits(bytes, index, idSize, 0, width);
    bits(bytes, index, idSize + width, 0, width);
    bits(bytes, index, typeOffset, 0, 2);
    bits(bytes, index, flagsOffset, 0, 2);
    bits(bytes, index, instanceOffset, 0, width);
    bits(bytes, index, rotationModeOffset, 1, 2);
    vector(bytes, index, locationOffset + 24, {1, 1, 1});
    vector(bytes, index, locationOffset + 36, {1, 1, 1});
    matrix(bytes, index, blend::IdentityMatrix);
    scalar(bytes, index, quaternionOffset, 1);
    scalar(bytes, index, quaternionOffset + 16, 1);
    vector(bytes, index, axisOffset, {0, 0, 1});
  }
  vector(bytes, 4, locationOffset, {7, 19, 37});
  vector(bytes, 4, locationOffset + 12, {1, 2, 3});
  vector(bytes, 4, locationOffset + 24, {2, -3, 5});
  vector(bytes, 5, locationOffset, {3, 5, 7});
  bits(bytes, 5, idSize, blocks[4].oldAddress, width);
  auto inverse = blend::IdentityMatrix;
  inverse[0][3] = -1;
  inverse[1][3] = -2;
  inverse[2][3] = -3;
  matrix(bytes, 5, inverse);
  auto root = blend::IdentityMatrix;
  root[0][0] = 2;
  root[1][1] = -3;
  root[2][2] = 5;
  root[0][3] = 8;
  root[1][3] = 21;
  root[2][3] = 40;
  auto child = blend::IdentityMatrix;
  child[0][3] = 3;
  child[1][3] = 5;
  child[2][3] = 7;
  auto decode = [&](const auto& input, const auto& records, const auto& dna,
                    blend::SceneTraversalLimits limits = {8, 2}) {
    return blend::DecodeScene(input, records, dna, header, limits);
  };
  auto failure = [&](const auto& input, const auto& records, const auto& dna,
                     std::string_view code, std::optional<std::uint32_t> index,
                     blend::SceneTraversalLimits limits = {64, 8}) {
    const auto result = decode(input, records, dna, limits);
    if (result.HasValue() || result.GetError().code != code || result.GetError().blockIndex != index) {
      throw std::runtime_error("Native decoder expected " + std::string(code) +
                               (result.HasValue() ? ", got a Scene" : ", got " + result.GetError().code));
    }
    Require(result.GetError().severity == blend::Severity::Fatal && !result.GetError().recoverable &&
                result.GetError().byteOffset == (index ? std::optional<std::uint64_t>(records[*index].offset) : std::nullopt),
        "Native decoder failures preserve exact fatal source context");
  };
  const auto result = decode(bytes, blocks, schema);
  Require(result.HasValue() && result.GetValue().objects.size() == 2 && result.GetValue().meshes.empty() &&
              result.GetValue().metadata.sourceScene == "Chosen" &&
              result.GetValue().metadata.sourceVersion == header.SourceVersion() &&
              result.GetValue().metadata.sourceUnitScale == 1 &&
              result.GetValue().objects[0].sourceName == "One" &&
              result.GetValue().objects[1].sourceName == "Two" &&
              result.GetValue().objects[0].identifier == "One" &&
              result.GetValue().objects[1].identifier == "Two" &&
              !result.GetValue().objects[0].mesh && !result.GetValue().objects[1].mesh &&
              !result.GetValue().objects[0].parent && result.GetValue().objects[1].parent == 0 &&
              result.GetValue().objects[0].hiddenForRender && !result.GetValue().objects[1].hiddenForRender &&
              result.GetValue().objects[0].worldTransform == blend::ToUsdBasis(root) &&
              result.GetValue().objects[1].worldTransform == blend::ToUsdBasis(Multiply(Multiply(root, inverse), child)) &&
              result.Diagnostics().empty(),
      "Native decoding publishes only owning normalized Empty objects and selected parent indices");
  CheckNativeMeshes(bytes, blocks, schema, header);
  const auto rename = [&](auto& input, std::uint32_t index, std::string_view name) {
    const auto start = input.begin() + static_cast<std::ptrdiff_t>(blocks[index].offset);
    std::fill_n(start, 16, std::byte{0});
    start[0] = std::byte{'O'};
    start[1] = std::byte{'B'};
    for (std::size_t byte = 0; byte < name.size(); ++byte) {
      start[2 + byte] = static_cast<std::byte>(name[byte]);
    }
  };
  auto namedBytes = bytes;
  bits(namedBytes, 5, idSize, 0, width);
  rename(namedBytes, 4, "Cube_001");
  rename(namedBytes, 5, "Cube.001");
  const auto named = decode(namedBytes, blocks, schema);
  Require(named.HasValue() && named.Diagnostics().empty() &&
              named.GetValue().objects[0].sourceName == "Cube_001" &&
              named.GetValue().objects[1].sourceName == "Cube.001" &&
              named.GetValue().objects[0].identifier == "Cube_001_1" &&
              named.GetValue().objects[1].identifier == "Cube_001",
      "Native naming uses source byte order, not saved Object discovery order");
  rename(namedBytes, 4, "\xc3(");
  const auto invalidName = decode(namedBytes, blocks, schema);
  Require(invalidName.HasValue() && invalidName.GetValue().objects[0].sourceName == "\xc3(" &&
              invalidName.GetValue().objects[0].identifier == "Object" &&
              invalidName.Diagnostics().size() == 1 &&
              invalidName.Diagnostics()[0].code == "BLEND_NAME_INVALID_UTF8" &&
              invalidName.Diagnostics()[0].severity == blend::Severity::Warning &&
              invalidName.Diagnostics()[0].recoverable &&
              invalidName.Diagnostics()[0].datablock == "\xc3(" &&
              invalidName.Diagnostics()[0].blockIndex == 4 &&
              invalidName.Diagnostics()[0].byteOffset == blocks[4].offset,
      "Native malformed UTF-8 retains raw source bytes and exact recoverable Object context");
  rename(namedBytes, 4, "Cube.001");
  failure(namedBytes, blocks, schema, "BLEND_NAME_DUPLICATE", 4);
  bits(namedBytes, 5, idSize, blocks[4].oldAddress, width);
  rename(namedBytes, 4, "\xff");
  rename(namedBytes, 5, "\xff");
  const auto scopedNames = decode(namedBytes, blocks, schema);
  Require(scopedNames.HasValue() && scopedNames.Diagnostics().size() == 2 &&
              scopedNames.Diagnostics()[0].blockIndex == 4 &&
              scopedNames.Diagnostics()[0].byteOffset == blocks[4].offset &&
              scopedNames.Diagnostics()[1].blockIndex == 5 &&
              scopedNames.Diagnostics()[1].byteOffset == blocks[5].offset,
      "Equal raw names in separate naming scopes retain each Object's warning context");
  for (const float scale : {1.0f, 0.01f, 0.001f, 10.0f}) {
    auto changed = bytes;
    scalar(changed, 1, idSize, scale);
    const auto converted = decode(changed, blocks, schema);
    const blend::UnitConversion units(scale);
    Require(converted.HasValue() && converted.GetValue().metadata.sourceUnitScale == scale &&
                converted.GetValue().objects[0].worldTransform == units.WorldTransform(root) &&
                converted.GetValue().objects[1].worldTransform == units.WorldTransform(Multiply(Multiply(root, inverse), child)),
        "Native decoding normalizes full parent-world translations exactly once");
  }
  auto reorderedBlocks = blocks;
  std::reverse(reorderedBlocks.begin(), reorderedBlocks.end());
  const auto reordered = decode(bytes, reorderedBlocks, schema);
  Require(reordered.HasValue() && reordered.GetValue().objects[0].sourceName == "One" &&
              reordered.GetValue().objects[0].identifier == "One" &&
              reordered.GetValue().objects[1].identifier == "Two" &&
              reordered.GetValue().objects[1].parent == 0 &&
              reordered.GetValue().objects[1].worldTransform == result.GetValue().objects[1].worldTransform,
      "Native parent indices and object discovery order do not depend on block enumeration");
  auto changed = bytes;
  bits(changed, 4, idSize, blocks[10].oldAddress, width);
  vector(changed, 10, locationOffset, {11, 13, 17});
  auto externalWorld = root;
  externalWorld[0][3] += 11;
  externalWorld[1][3] += 13;
  externalWorld[2][3] += 17;
  const auto external = decode(changed, blocks, schema, {9, 2});
  Require(external.HasValue() && external.GetValue().objects.size() == 2 &&
              !external.GetValue().objects[0].parent && external.GetValue().objects[1].parent == 0 &&
              external.GetValue().objects[0].worldTransform == blend::ToUsdBasis(externalWorld),
      "Parent-only Empty transforms contribute to world space without expanding membership");
  failure(changed, blocks, schema, "BLEND_SCENE_VISIT_LIMIT", 4, {8, 2});
  bits(changed, 10, 16, 999, width);
  failure(changed, blocks, schema, "BLEND_SCENE_LINKED_UNSUPPORTED", 10);
  failure(bytes, blocks, schema, "BLEND_SCENE_LIMITS", std::nullopt, {0, 2});
  failure(bytes, blocks, schema, "BLEND_SCENE_DEPTH_LIMIT", 2, {8, 1});
  changed = bytes;
  bits(changed, 4, idSize, blocks[4].oldAddress, width);
  failure(changed, blocks, schema, "BLEND_SCENE_CYCLE", 4);
  changed = bytes;
  bits(changed, 4, typeOffset, 1, 2);
  bits(changed, 4, idSize + width, 3000, width);
  failure(changed, blocks, schema, "BLEND_DNA_MEMBER", static_cast<std::uint32_t>(blocks.size() - 2));
  bits(changed, 4, idSize + width, 0, width);
  failure(changed, blocks, schema, "BLEND_SCENE_REFERENCE_INVALID", 4);
  changed = bytes;
  auto imageBlocks = blocks;
  auto imageDna = schema;
  const auto imageIndex = static_cast<std::uint32_t>(blocks.size() - 2);
  imageBlocks[imageIndex].code = {'I', 'M', 0, 0};
  imageDna.types[imageDna.structs[imageBlocks[imageIndex].sdnaIndex].typeIndex].name = "Image";
  changed[static_cast<std::size_t>(imageBlocks[imageIndex].offset)] = std::byte{'I'};
  changed[static_cast<std::size_t>(imageBlocks[imageIndex].offset) + 1] = std::byte{'M'};
  bits(changed, 4, idSize + width, imageBlocks[imageIndex].oldAddress, width);
  const auto fallbackBytes = changed;
  const std::array mappings = {
      std::tuple{0, "IM", "Image"}, std::tuple{2, "CU", "Curve"},
      std::tuple{3, "CU", "Curve"}, std::tuple{4, "CU", "Curve"},
      std::tuple{5, "MB", "MetaBall"}, std::tuple{10, "LA", "Lamp"},
      std::tuple{11, "CA", "Camera"}, std::tuple{12, "SK", "Speaker"},
      std::tuple{13, "LP", "LightProbe"}, std::tuple{22, "LT", "Lattice"},
      std::tuple{25, "AR", "bArmature"}, std::tuple{26, "GD", "bGPdata"},
      std::tuple{27, "CV", "Curves"}, std::tuple{28, "PT", "PointCloud"},
      std::tuple{29, "VO", "Volume"}, std::tuple{30, "GP", "GreasePencil"}};
  for (const auto& [type, code, dnaType] : mappings) {
    changed = fallbackBytes;
    imageBlocks[imageIndex].code = {code[0], code[1], 0, 0};
    imageDna.types[imageDna.structs[imageBlocks[imageIndex].sdnaIndex].typeIndex].name = dnaType;
    changed[static_cast<std::size_t>(imageBlocks[imageIndex].offset)] = static_cast<std::byte>(code[0]);
    changed[static_cast<std::size_t>(imageBlocks[imageIndex].offset) + 1] = static_cast<std::byte>(code[1]);
    bits(changed, 4, typeOffset, type, 2);
    const auto fallback = decode(changed, imageBlocks, imageDna, {9, 2});
    Require(fallback.HasValue() && fallback.GetValue().objects.size() == 2 &&
                fallback.GetValue().meshes.empty() && fallback.GetValue().objects[1].parent == 0 &&
                fallback.GetValue().objects[0].worldTransform == result.GetValue().objects[0].worldTransform &&
                fallback.GetValue().objects[1].worldTransform == result.GetValue().objects[1].worldTransform &&
                fallback.GetValue().objects[0].hiddenForRender && fallback.Diagnostics().size() == 1,
        "Known unsupported data preserves hierarchy, visibility and source transforms as Empty IR");
    const auto& diagnostic = fallback.Diagnostics()[0];
    Require(diagnostic.code == "BLEND_SCENE_OBJECT_DATA_UNSUPPORTED" &&
                diagnostic.severity == blend::Severity::Unsupported && diagnostic.recoverable &&
                diagnostic.blockIndex == 4 && diagnostic.byteOffset == blocks[4].offset &&
                diagnostic.datablock == "One",
        "Unsupported data warns once with exact referring Object context");
    bits(changed, 4, flagsOffset, 1, 2);
    failure(changed, imageBlocks, imageDna, "BLEND_SCENE_TRANSFORM_UNSUPPORTED", 4);
    bits(changed, 4, flagsOffset, 0, 2);
    bits(changed, imageIndex, 16, 999, width);
    failure(changed, imageBlocks, imageDna, "BLEND_SCENE_LINKED_UNSUPPORTED", imageIndex);
    bits(changed, imageIndex, 16, 0, width);
    bits(changed, 4, idSize + width, 999, width);
    failure(changed, imageBlocks, imageDna, "BLEND_SCENE_REFERENCE_INVALID", 4);
  }
  changed = bytes;
  bits(changed, 4, typeOffset, 6, 2);
  failure(changed, blocks, schema, "BLEND_SCENE_OBJECT_TYPE_UNSUPPORTED", 4);
  changed = bytes;
  bits(changed, 4, flagsOffset, 256, 2);
  bits(changed, 4, instanceOffset, blocks.back().oldAddress, width);
  failure(changed, blocks, schema, "BLEND_SCENE_INSTANCE_UNSUPPORTED", 4);
  bits(changed, 4, instanceOffset, blocks[2].oldAddress, width);
  failure(changed, blocks, schema, "BLEND_SCENE_CYCLE", 4);
  bits(changed, 4, instanceOffset, 0, width);
  failure(changed, blocks, schema, "BLEND_SCENE_REFERENCE_INVALID", 4);
  for (const auto mode : {65534, 7, 32767}) {
    changed = bytes;
    bits(changed, 4, rotationModeOffset, mode, 2);
    failure(changed, blocks, schema, "BLEND_SCENE_TRANSFORM_UNSUPPORTED", 4);
  }
  for (const auto mode : {0, 1, 2, 3, 4, 5, 6, 65535}) {
    changed = bytes;
    bits(changed, 4, rotationModeOffset, mode, 2);
    const auto identityRotation = decode(changed, blocks, schema);
    Require(identityRotation.HasValue() &&
                identityRotation.GetValue().objects[0].worldTransform == result.GetValue().objects[0].worldTransform,
        "Every supported rotation mode retains identity channels across all layouts");
  }
  for (const auto member : {"quat", "dquat", "rotAxis", "rotAngle"}) {
    const bool axisAngle = std::string_view(member) == "rotAxis" || std::string_view(member) == "rotAngle";
    const auto* field = schema.structs[6].FindMember(member);
    changed = bytes;
    bits(changed, 4, rotationModeOffset, axisAngle ? 65535 : 0, 2);
    const auto components = std::string_view(member) == "rotAngle" ? 1u : axisAngle ? 3u
                                                                                    : 4u;
    for (std::size_t component = 0; component < components; ++component) {
      for (const auto invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        auto nonfinite = changed;
        scalar(nonfinite, 4, field->offset + 4 * component, invalid);
        failure(nonfinite, blocks, schema, "BLEND_SCENE_TRANSFORM_INVALID", 4);
      }
    }
    scalar(changed, 4, field->offset, 0);
    auto dna = schema;
    auto& malformed = *std::find_if(dna.structs[6].members.begin(), dna.structs[6].members.end(),
        [&](const auto& value) { return value.baseName == member; });
    malformed.arrayDimensions = std::string_view(member) == "rotAngle"
                                    ? std::vector<std::uint64_t>{1}
                                    : std::vector<std::uint64_t>{1, axisAngle ? 3u : 4u};
    failure(changed, blocks, dna, "BLEND_SCENE_TRANSFORM_INVALID", 4);
    malformed.baseName = "missing";
    const auto missing = decode(changed, blocks, dna);
    Require(!missing.HasValue() && missing.GetError().code == "BLEND_DNA_MEMBER" &&
                missing.GetError().blockIndex == 4,
        "Active rotation channels must not silently default");
    changed = bytes;
    scalar(changed, 4, field->offset, std::numeric_limits<float>::quiet_NaN());
    Require(decode(changed, blocks, schema).HasValue(), "Inactive rotation channels are not interpreted");
  }
  changed = bytes;
  bits(changed, 4, flagsOffset, 4, 2);
  bits(changed, 5, flagsOffset, 4, 2);
  const auto cachedHandedness = decode(changed, blocks, schema);
  Require(cachedHandedness.HasValue() &&
              cachedHandedness.GetValue().objects[0].worldTransform == result.GetValue().objects[0].worldTransform &&
              cachedHandedness.GetValue().objects[1].worldTransform == result.GetValue().objects[1].worldTransform,
      "Cached negative-handedness bits do not reapply scale or override source channels");
  for (const auto flag : {1, 2, 8, 16, 32, 64, 128, 512, 1024, 2048, 4096, 8192, 16384, 32768}) {
    for (const auto handedness : {0, 4}) {
      changed = bytes;
      bits(changed, 4, flagsOffset, flag | handedness, 2);
      failure(changed, blocks, schema, "BLEND_SCENE_TRANSFORM_UNSUPPORTED", 4);
    }
  }
  changed = bytes;
  bits(changed, 5, parentingModeOffset, 4, 2);
  failure(changed, blocks, schema, "BLEND_SCENE_TRANSFORM_UNSUPPORTED", 5);
  for (const auto memberOffset : {locationOffset, locationOffset + 12, locationOffset + 24,
           locationOffset + 36, locationOffset + 48, locationOffset + 60, inverseOffset}) {
    for (const auto invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
      changed = bytes;
      scalar(changed, 5, memberOffset, invalid);
      failure(changed, blocks, schema, "BLEND_SCENE_TRANSFORM_INVALID", 5);
    }
  }
  changed = bytes;
  scalar(changed, 5, inverseOffset + 12, 1);
  failure(changed, blocks, schema, "BLEND_SCENE_TRANSFORM_INVALID", 5);
  for (const auto member : {"loc", "parentinv", "rotmode"}) {
    auto dna = schema;
    auto& field = *std::find_if(dna.structs[6].members.begin(), dna.structs[6].members.end(),
        [&](const auto& value) { return value.baseName == member; });
    field.arrayDimensions = std::string_view(member) == "loc" ? std::vector<std::uint64_t>{1, 3} : std::string_view(member) == "parentinv" ? std::vector<std::uint64_t>{16}
                                                                                                                                           : std::vector<std::uint64_t>{1};
    failure(bytes, blocks, dna, "BLEND_SCENE_TRANSFORM_INVALID",
        std::string_view(member) == "parentinv" ? 5 : 4);
    field.baseName = "missing";
    const auto missing = decode(bytes, blocks, dna);
    Require(!missing.HasValue() && missing.GetError().code == "BLEND_DNA_MEMBER" &&
                missing.GetError().blockIndex == (std::string_view(member) == "parentinv" ? 5 : 4),
        "Native decoder preserves reader member errors rather than defaulting transforms");
  }
  changed = bytes;
  const float angle = 1.5707963267948966f;
  vector(changed, 4, locationOffset + 48, {0, 0, angle});
  vector(changed, 4, locationOffset + 60, {angle, 0, 0});
  vector(changed, 4, locationOffset + 36, {2, 3, -1});
  blend::Matrix4 rotationZ = {{{std::cos(angle), -std::sin(angle), 0, 0},
      {std::sin(angle), std::cos(angle), 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
  blend::Matrix4 deltaX = {{{1, 0, 0, 0}, {0, std::cos(angle), -std::sin(angle), 0},
      {0, std::sin(angle), std::cos(angle), 0}, {0, 0, 0, 1}}};
  auto rotatedExpected = Multiply(deltaX, rotationZ);
  for (std::size_t axis = 0; axis < 3; ++axis) {
    for (std::size_t row = 0; row < 3; ++row) {
      rotatedExpected[row][axis] *= std::array<double, 3>{4, -9, -5}[axis];
    }
    rotatedExpected[axis][3] = root[axis][3];
  }
  rotatedExpected = blend::ToUsdBasis(rotatedExpected);
  const auto rotated = decode(changed, blocks, schema);
  Require(rotated.HasValue(), "XYZ Euler and delta rotation decode");
  for (std::size_t row = 0; row < 4; ++row) {
    for (std::size_t column = 0; column < 4; ++column) {
      Require(std::abs(rotated.GetValue().objects[0].worldTransform[row][column] - rotatedExpected[row][column]) < 1e-6,
          "Delta rotation precedes source rotation; scale multiplies columns");
    }
  }
  for (const auto mode : {0, 65535}) {
    changed = bytes;
    bits(changed, 4, rotationModeOffset, mode, 2);
    vector(changed, 4, locationOffset + 36, {2, 3, -1});
    auto expected = rotationZ;
    if (mode == 0) {
      scalar(changed, 4, quaternionOffset, 2);
      scalar(changed, 4, quaternionOffset + 12, 2);
      scalar(changed, 4, quaternionOffset + 16, -3);
      scalar(changed, 4, quaternionOffset + 20, -3);
      expected = Multiply(deltaX, rotationZ);
    } else {
      vector(changed, 4, axisOffset, {0, 0, 7});
      scalar(changed, 4, angleOffset, angle);
      scalar(changed, 4, locationOffset + 60, std::numeric_limits<float>::quiet_NaN());
      scalar(changed, 4, quaternionOffset + 16, std::numeric_limits<float>::quiet_NaN());
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
      for (std::size_t row = 0; row < 3; ++row) {
        expected[row][axis] *= std::array<double, 3>{4, -9, -5}[axis];
      }
      expected[axis][3] = root[axis][3];
    }
    const auto otherRotation = decode(changed, blocks, schema);
    Require(otherRotation.HasValue(), "Nonunit quaternion/axis channels decode across all layouts");
    expected = blend::ToUsdBasis(expected);
    for (std::size_t row = 0; row < 4; ++row) {
      for (std::size_t column = 0; column < 4; ++column) {
        Require(std::abs(otherRotation.GetValue().objects[0].worldTransform[row][column] - expected[row][column]) < 1e-6,
            "Quaternion delta order, sign and normalization; axis-angle ignores delta rotation");
      }
    }
  }
  for (const auto memberOffset : {animationOffset, constraintsOffset, constraintsOffset + width}) {
    changed = bytes;
    bits(changed, 4, memberOffset, 999, width);
    const auto sourceOnly = decode(changed, blocks, schema);
    Require(sourceOnly.HasValue() && sourceOnly.Diagnostics().size() == 1 &&
                sourceOnly.Diagnostics()[0].code == "BLEND_SCENE_EVALUATION_UNAPPLIED" &&
                sourceOnly.Diagnostics()[0].severity == blend::Severity::Unsupported &&
                sourceOnly.Diagnostics()[0].recoverable && sourceOnly.Diagnostics()[0].blockIndex == 4 &&
                sourceOnly.Diagnostics()[0].byteOffset == blocks[4].offset &&
                sourceOnly.GetValue().objects[0].worldTransform == result.GetValue().objects[0].worldTransform,
        "Animation/constraint presence is reported without following or evaluating it");
  }
  changed = bytes;
  for (const auto index : {2, 3}) {
    bits(changed, index, idSize, 0, width);
    bits(changed, index, idSize + width, 0, width);
  }
  const auto empty = decode(changed, blocks, schema, {3, 2});
  Require(empty.HasValue() && empty.GetValue().objects.empty() && empty.GetValue().meshes.empty(),
      "A saved active Scene with empty Collections publishes an empty IR");
  changed = bytes;
  const auto firstParent = static_cast<std::uint32_t>(blocks.size());
  const std::vector<std::byte> parentPayload(
      bytes.begin() + static_cast<std::ptrdiff_t>(blocks[10].offset),
      bytes.begin() + static_cast<std::ptrdiff_t>(blocks[10].offset + blocks[10].length));
  for (std::uint32_t next = 0; next < 256; ++next) {
    auto block = blocks[10];
    block.oldAddress = 10000 + 100 * next;
    block.offset = changed.size();
    blocks.push_back(block);
    changed.insert(changed.end(), parentPayload.begin(), parentPayload.end());
    if (next != 0) {
      bits(changed, firstParent + next - 1, idSize, block.oldAddress, width);
    }
  }
  bits(changed, 4, idSize, blocks[firstParent].oldAddress, width);
  const auto deep = decode(changed, blocks, schema, {264, 257});
  Require(deep.HasValue() && deep.GetValue().objects[0].worldTransform == blend::ToUsdBasis(root) &&
              deep.GetValue().objects.size() == 2,
      "Native world construction uses an iterative parent chain at exact traversal budgets");
  failure(changed, blocks, schema, "BLEND_SCENE_DEPTH_LIMIT", firstParent + 254, {264, 256});
  failure(changed, blocks, schema, "BLEND_SCENE_VISIT_LIMIT", firstParent + 254, {263, 257});
  bits(changed, firstParent + 7, idSize, 0, width);
  for (std::uint32_t next = 0; next < 8; ++next) {
    vector(changed, firstParent + next, locationOffset + 24, {1e38f, 1e38f, 1e38f});
  }
  vector(changed, firstParent + 7, locationOffset, {1e38f, 0, 0});
  scalar(changed, 1, idSize, 1e38f);
  failure(changed, blocks, schema, "BLEND_SCENE_UNIT_VALUE_INVALID", 4, {16, 9});
  scalar(changed, 1, idSize, 1);
  bits(changed, firstParent + 7, idSize, blocks[firstParent + 8].oldAddress, width);
  vector(changed, firstParent + 8, locationOffset + 24, {1e38f, 1e38f, 1e38f});
  bits(changed, firstParent + 8, idSize, 0, width);
  vector(changed, firstParent + 8, locationOffset, {1e38f, 0, 0});
  failure(changed, blocks, schema, "BLEND_SCENE_TRANSFORM_INVALID", firstParent, {17, 10});
  bytes[static_cast<std::size_t>(blocks[4].offset) + 2] = std::byte{'X'};
  schema.types[10].name = "Changed";
  Require(result.GetValue().objects[0].sourceName == "One" &&
              result.GetValue().objects[0].worldTransform == blend::ToUsdBasis(root),
      "Native Scene IR owns its names and matrices independently of input storage");
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
      const auto instanceIndex = static_cast<std::uint32_t>(valueBlocks.size());
      valueBlocks.push_back({{'G', 'R', 0, 0}, collectionSize, 3100, 5, 1, valueBytes.size()});
      valueBytes.resize(valueBytes.size() + collectionSize);
      valueBytes[static_cast<std::size_t>(valueBlocks.back().offset)] = std::byte{'G'};
      valueBytes[static_cast<std::size_t>(valueBlocks.back().offset) + 1] = std::byte{'R'};
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
      setValue(valueBytes, 4, instanceOffset, valueBlocks[instanceIndex].oldAddress, width);
      auto readValues = [&](const auto& input, const auto& records, const auto& dna,
                            blend::SceneTraversalLimits limits = {10, 3}) {
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
                  values.GetValue().objects[0].values->instanceCollectionBlockIndex == instanceIndex &&
                  !values.GetValue().objects[1].values->hiddenForRender &&
                  !values.GetValue().objects[1].values->instanceCollectionBlockIndex &&
                  !result.GetValue().objects[0].values,
          "Opt-in Object values retain saved flags and references without changing generic selection");
      CheckNativeDecode(valueBytes, valueBlocks, valueDna, header);
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
      Require(readValues(changedValues, valueBlocks, valueDna, {11, 3}).HasValue(),
          "Parent-only Object values are validated but do not expand membership");
      for (const auto index : {4, 5}) {
        setValue(valueBytes, index, instanceOffset, 3100, width);
      }
      Require(readValues(valueBytes, valueBlocks, valueDna, {10, 3}).HasValue(),
          "Shared empty instance targets consume one additional visit");
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
      const auto reorderedValues = readValues(valueBytes, badInstance, valueDna, {10, 3});
      Require(reorderedValues.HasValue() &&
                  badInstance[*reorderedValues.GetValue().objects[0].values->instanceCollectionBlockIndex].oldAddress == 3100,
          "Instance indices refer to the caller's reordered blocks");
      const auto mappingBytes = valueBytes;
      const auto mappingBlocks = valueBlocks;
      const auto instanceNodeIndex = static_cast<std::uint32_t>(valueBlocks.size());
      valueBlocks.push_back({{'D', 'A', 'T', 'A'}, nodeSize, 3200, 7, 1, valueBytes.size()});
      valueBytes.resize(valueBytes.size() + nodeSize);
      setValue(valueBytes, instanceIndex, idSize, valueBlocks[instanceNodeIndex].oldAddress, width);
      setValue(valueBytes, instanceIndex, idSize + width, valueBlocks[instanceNodeIndex].oldAddress, width);
      setValue(valueBytes, instanceNodeIndex, 2 * width, valueBlocks[4].oldAddress, width);
      valueFailure(valueBytes, valueBlocks, valueDna, "BLEND_SCENE_VISIT_LIMIT",
          instanceNodeIndex, {10, 3});
      valueFailure(valueBytes, valueBlocks, valueDna, "BLEND_SCENE_CYCLE", 4);
      const auto nestedObjectIndex = static_cast<std::uint32_t>(valueBlocks.size());
      valueBlocks.push_back({{'O', 'B', 0, 0}, valueSize, 3600, 6, 1, valueBytes.size()});
      valueBytes.resize(valueBytes.size() + valueSize);
      constexpr std::string_view nestedObjectName = "OBNested";
      for (std::size_t character = 0; character < nestedObjectName.size(); ++character) {
        valueBytes[static_cast<std::size_t>(valueBlocks[nestedObjectIndex].offset) + character] =
            static_cast<std::byte>(nestedObjectName[character]);
      }
      setValue(valueBytes, nestedObjectIndex, typeOffset, 0, 2);
      setValue(valueBytes, instanceNodeIndex, 2 * width,
          valueBlocks[nestedObjectIndex].oldAddress, width);
      for (const auto address : {std::uint64_t{999}, valueBlocks[instanceIndex].oldAddress + 1,
               valueBlocks[valueMesh].oldAddress}) {
        setValue(valueBytes, nestedObjectIndex, instanceOffset, address, width);
        valueFailure(valueBytes, valueBlocks, valueDna, "BLEND_SCENE_REFERENCE_INVALID",
            address == valueBlocks[valueMesh].oldAddress ? valueMesh : nestedObjectIndex);
        Require(select(valueBytes, valueBlocks, valueDna, {9, 2}).HasValue(),
            "Generic selection does not follow recursive instance references");
      }
      const auto linkedInstanceIndex = static_cast<std::uint32_t>(valueBlocks.size());
      valueBlocks.push_back({{'G', 'R', 0, 0}, collectionSize, 3700, 5, 1, valueBytes.size()});
      valueBytes.resize(valueBytes.size() + collectionSize);
      valueBytes[static_cast<std::size_t>(valueBlocks[linkedInstanceIndex].offset)] = std::byte{'G'};
      valueBytes[static_cast<std::size_t>(valueBlocks[linkedInstanceIndex].offset) + 1] = std::byte{'R'};
      setValue(valueBytes, linkedInstanceIndex, 16, 999, width);
      setValue(valueBytes, nestedObjectIndex, instanceOffset,
          valueBlocks[linkedInstanceIndex].oldAddress, width);
      valueFailure(valueBytes, valueBlocks, valueDna, "BLEND_SCENE_LINKED_UNSUPPORTED",
          linkedInstanceIndex);
      setValue(valueBytes, nestedObjectIndex, instanceOffset,
          valueBlocks[instanceIndex].oldAddress, width);
      valueFailure(valueBytes, valueBlocks, valueDna, "BLEND_SCENE_CYCLE",
          nestedObjectIndex);
      setValue(valueBytes, nestedObjectIndex, instanceOffset,
          valueBlocks[2].oldAddress, width);
      valueFailure(valueBytes, valueBlocks, valueDna, "BLEND_SCENE_CYCLE",
          nestedObjectIndex);
      setValue(valueBytes, nestedObjectIndex, instanceOffset, 0, width);
      const auto deepCollectionIndex = static_cast<std::uint32_t>(valueBlocks.size());
      valueBlocks.push_back({{'G', 'R', 0, 0}, collectionSize, 3300, 5, 1, valueBytes.size()});
      valueBytes.resize(valueBytes.size() + collectionSize);
      valueBytes[static_cast<std::size_t>(valueBlocks[deepCollectionIndex].offset)] = std::byte{'G'};
      valueBytes[static_cast<std::size_t>(valueBlocks[deepCollectionIndex].offset) + 1] = std::byte{'R'};
      const auto childNodeIndex = static_cast<std::uint32_t>(valueBlocks.size());
      valueBlocks.push_back({{'D', 'A', 'T', 'A'}, nodeSize, 3400, 8, 1, valueBytes.size()});
      valueBytes.resize(valueBytes.size() + nodeSize);
      setValue(valueBytes, instanceIndex, idSize + 2 * width,
          valueBlocks[childNodeIndex].oldAddress, width);
      setValue(valueBytes, instanceIndex, idSize + 3 * width,
          valueBlocks[childNodeIndex].oldAddress, width);
      setValue(valueBytes, childNodeIndex, 2 * width,
          valueBlocks[deepCollectionIndex].oldAddress, width);
      const auto deepObjectNodeIndex = static_cast<std::uint32_t>(valueBlocks.size());
      valueBlocks.push_back({{'D', 'A', 'T', 'A'}, nodeSize, 3500, 7, 1, valueBytes.size()});
      valueBytes.resize(valueBytes.size() + nodeSize);
      setValue(valueBytes, deepCollectionIndex, idSize,
          valueBlocks[deepObjectNodeIndex].oldAddress, width);
      setValue(valueBytes, deepCollectionIndex, idSize + width,
          valueBlocks[deepObjectNodeIndex].oldAddress, width);
      setValue(valueBytes, deepObjectNodeIndex, 2 * width,
          valueBlocks[4].oldAddress, width);
      valueFailure(valueBytes, valueBlocks, valueDna, "BLEND_SCENE_DEPTH_LIMIT",
          instanceIndex, {32, 3});
      setValue(valueBytes, nestedObjectIndex, instanceOffset, 0, width);
      setValue(valueBytes, deepObjectNodeIndex, 2 * width,
          valueBlocks[nestedObjectIndex].oldAddress, width);
      const auto recursive = readValues(valueBytes, valueBlocks, valueDna, {15, 4});
      Require(recursive.HasValue() && recursive.GetValue().objects.size() == 2 &&
                  recursive.GetValue().objects[0].blockIndex == 4 &&
                  recursive.GetValue().objects[1].blockIndex == 5,
          "Exactly sufficient recursive budgets validate nested Objects without publishing membership");
      valueFailure(valueBytes, valueBlocks, valueDna, "BLEND_SCENE_VISIT_LIMIT",
          deepObjectNodeIndex, {14, 4});
      auto previousCollection = deepCollectionIndex;
      auto recursiveReferrer = deepCollectionIndex;
      for (std::uint32_t child = 0; child < 256; ++child) {
        const auto childCollection = static_cast<std::uint32_t>(valueBlocks.size());
        valueBlocks.push_back({{'G', 'R', 0, 0}, collectionSize,
            5000 + 200 * child, 5, 1, valueBytes.size()});
        valueBytes.resize(valueBytes.size() + collectionSize);
        valueBytes[static_cast<std::size_t>(valueBlocks[childCollection].offset)] = std::byte{'G'};
        valueBytes[static_cast<std::size_t>(valueBlocks[childCollection].offset) + 1] = std::byte{'R'};
        const auto childNode = static_cast<std::uint32_t>(valueBlocks.size());
        valueBlocks.push_back({{'D', 'A', 'T', 'A'}, nodeSize,
            5100 + 200 * child, 8, 1, valueBytes.size()});
        valueBytes.resize(valueBytes.size() + nodeSize);
        setValue(valueBytes, previousCollection, idSize + 2 * width,
            valueBlocks[childNode].oldAddress, width);
        setValue(valueBytes, previousCollection, idSize + 3 * width,
            valueBlocks[childNode].oldAddress, width);
        setValue(valueBytes, childNode, 2 * width,
            valueBlocks[childCollection].oldAddress, width);
        recursiveReferrer = previousCollection;
        previousCollection = childCollection;
      }
      Require(readValues(valueBytes, valueBlocks, valueDna, {527, 260}).HasValue(),
          "A 256-child chain below an instance target uses bounded iterative traversal");
      valueFailure(valueBytes, valueBlocks, valueDna, "BLEND_SCENE_DEPTH_LIMIT",
          recursiveReferrer, {527, 259});
      valueFailure(valueBytes, valueBlocks, valueDna, "BLEND_SCENE_VISIT_LIMIT",
          recursiveReferrer, {526, 260});
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
        auto records = mappingBlocks;
        changedValues = mappingBytes;
        for (const auto index : {4, 5}) {
          setValue(changedValues, index, typeOffset, mapping.type, 2);
        }
        records[valueMesh].code = {mapping.code[0], mapping.code[1], 0, 0};
        changedValues[static_cast<std::size_t>(records[valueMesh].offset)] = static_cast<std::byte>(mapping.code[0]);
        changedValues[static_cast<std::size_t>(records[valueMesh].offset) + 1] = static_cast<std::byte>(mapping.code[1]);
        dna.types[13].name = mapping.dnaType;
        Require(readValues(changedValues, records, dna, {10, 3}).HasValue(),
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
                  values.GetValue().objects[0].values->instanceCollectionBlockIndex == instanceIndex,
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
    const auto native = blend::DecodeScene(decoded.GetValue(), blocks.GetValue(),
        schema.GetValue(), header.GetValue(), {10000, 64});
    Require(!native.HasValue() && native.GetError().code == "BLEND_SCENE_ACTIVE_MISSING",
        "Native decoding preserves the Scene-only library's lack of an active Scene");
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
  const auto native = blend::DecodeScene(decoded.GetValue(), blocks.GetValue(),
      schema.GetValue(), header.GetValue(), {10000, 64});
  if (!native.HasValue()) {
    throw std::runtime_error(native.GetError().code + ": " + native.GetError().message);
  }
  Require(native.GetValue().objects.size() == 3 && native.GetValue().meshes.size() == 1 &&
              std::count_if(native.Diagnostics().begin(), native.Diagnostics().end(),
                  [](const auto& diagnostic) {
                    return diagnostic.code == "BLEND_SCENE_OBJECT_DATA_UNSUPPORTED";
                  }) == 2,
      "Corpus Camera and Light preserve the Scene as diagnostic-bearing Empty Objects");
  for (const auto& diagnostic : native.Diagnostics()) {
    if (diagnostic.code != "BLEND_SCENE_OBJECT_DATA_UNSUPPORTED") {
      continue;
    }
    Require(diagnostic.severity == blend::Severity::Unsupported && diagnostic.recoverable &&
                diagnostic.blockIndex && diagnostic.byteOffset == blocks.GetValue()[*diagnostic.blockIndex].offset &&
                (diagnostic.datablock == "Camera" || diagnostic.datablock == "Light"),
        "Corpus unsupported data retains exact source Object context");
  }
  auto emptyObjects = decoded.GetValue();
  const auto* objectStruct = schema.GetValue().FindStruct("Object");
  for (const auto& object : objects.GetValue().objects) {
    const auto base = static_cast<std::size_t>(blocks.GetValue()[object.blockIndex].offset);
    for (const auto member : {"type", "transflag"}) {
      StoreBits(emptyObjects, base + objectStruct->FindMember(member)->offset, 0, 2, header.GetValue().byteOrder);
    }
    StoreBits(emptyObjects, base + objectStruct->FindMember("data")->offset, 0,
        header.GetValue().pointerSize, header.GetValue().byteOrder);
    StoreBits(emptyObjects, base + objectStruct->FindMember("rotmode")->offset, 1, 2, header.GetValue().byteOrder);
  }
  const auto nativeEmpty = blend::DecodeScene(emptyObjects, blocks.GetValue(),
      schema.GetValue(), header.GetValue(), {10000, 64});
  if (!nativeEmpty.HasValue()) {
    throw std::runtime_error(nativeEmpty.GetError().code + ": " + nativeEmpty.GetError().message);
  }
  Require(nativeEmpty.GetValue().objects.size() == 3 && nativeEmpty.GetValue().meshes.empty(),
      "Corpus SDNA supports Empty decoding when Object kind/data are mutated without fabricating layouts");
  CheckCorpusMesh(decoded.GetValue(), blocks.GetValue(), schema.GetValue(),
      header.GetValue(), objects.GetValue());
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
        Require(!checked.HasValue() && checked.GetError().code == "BLEND_SCENE_CYCLE" &&
                    checked.GetError().blockIndex == object.blockIndex &&
                    checked.GetError().byteOffset == blocks.GetValue()[object.blockIndex].offset,
            "A corpus Object instancing its containing master Collection fails with exact cycle context");
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
    CheckLocalTransforms();
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