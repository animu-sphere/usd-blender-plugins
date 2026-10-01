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
    return;
  }
  if (!selected.HasValue()) {
    throw std::runtime_error(selected.GetError().code + ": " + selected.GetError().message);
  }
  Require(selected.GetValue().metadata.sourceScene == "Scene" &&
              selected.GetValue().metadata.sourceVersion == header.GetValue().SourceVersion() &&
              selected.GetValue().metadata.sourceUnitScale > 0,
      "Saved active scene metadata is selected through SDNA");
}
}

int main(int argc, char** argv) {
  try {
    CheckBasis();
    CheckUnits();
    CheckScene();
    CheckSelectionLayouts();
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