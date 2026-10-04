#include <blendScene/Decode.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {

void Require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

blend::Matrix4 ReadMatrix(std::istream& input) {
  blend::Matrix4 result{};
  for (auto& row : result) {
    for (auto& value : row) {
      Require(static_cast<bool>(input >> value) && std::isfinite(value), "Invalid oracle matrix");
    }
  }
  // Blender's matrix_local inversion can round the homogeneous component.
  for (std::size_t column = 0; column < 4; ++column) {
    Require(std::abs(result[3][column] - blend::IdentityMatrix[3][column]) <= 1e-6,
        "Oracle matrix must be affine within Blender float precision");
  }
  result[3] = blend::IdentityMatrix[3];
  return result;
}

void Compare(const blend::Matrix4& actual, const blend::Matrix4& expected,
    const std::string& name) {
  for (std::size_t row = 0; row < 4; ++row) {
    for (std::size_t column = 0; column < 4; ++column) {
      const auto reference = expected[row][column];
      Require(std::abs(actual[row][column] - reference) <= 2e-5 * (1 + std::abs(reference)),
          name + " matrix differs at " + std::to_string(row) + "," + std::to_string(column) +
              ": got " + std::to_string(actual[row][column]) + ", expected " + std::to_string(reference));
    }
  }
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

void CheckFixture(const std::filesystem::path& path) {
  blend::FileByteSource source(path);
  const auto bytes = blend::ReadFileBytes(source, {4 * 1024 * 1024, 4 * 1024 * 1024, 16, 23});
  Require(bytes.HasValue(), "Transform fixture reads");
  blend::MemoryByteSource memory(bytes.GetValue());
  const auto header = blend::ReadHeader(memory);
  const auto blocks = blend::ReadBlocks(memory, 10000);
  Require(header.HasValue() && blocks.HasValue(), "Transform container reads");
  const auto dna = std::find_if(blocks.GetValue().begin(), blocks.GetValue().end(),
      [](const auto& block) { return block.code == std::array<char, 4>{'D', 'N', 'A', '1'}; });
  Require(dna != blocks.GetValue().end(), "Transform fixture has DNA1");
  const auto schema = blend::ReadDna(std::span<const std::byte>(bytes.GetValue()).subspan(static_cast<std::size_t>(dna->offset), static_cast<std::size_t>(dna->length)), header.GetValue());
  Require(schema.HasValue(), "Transform SDNA reads");
  const auto decoded = blend::DecodeScene(bytes.GetValue(), blocks.GetValue(),
      schema.GetValue(), header.GetValue(), {10000, 64});
  Require(decoded.HasValue(), "Transform fixture decodes: " +
                                  (decoded.HasValue() ? std::string{} : decoded.GetError().code + ": " + decoded.GetError().message));
  const auto& scene = decoded.GetValue();
  Require(scene.metadata.sourceScene == "Transforms" && scene.meshes.empty() && decoded.Diagnostics().empty(),
      "Transform fixture preserves Scene metadata and needs no evaluation");
  std::unordered_map<std::string, std::size_t> indices;
  for (std::size_t index = 0; index < scene.objects.size(); ++index) {
    Require(indices.emplace(scene.objects[index].sourceName, index).second, "Unique oracle object names");
  }
  auto oraclePath = path;
  oraclePath.replace_extension(".oracle.txt");
  std::ifstream oracle(oraclePath);
  std::string line;
  Require(static_cast<bool>(std::getline(oracle, line)) && line == "BLEND_TRANSFORMS_ORACLE 1", "Oracle version");
  Require(static_cast<bool>(std::getline(oracle, line)), "Oracle Blender version");
  double scale = 0;
  std::size_t count = 0;
  Require(static_cast<bool>(oracle >> scale >> count) && scale == scene.metadata.sourceUnitScale &&
              count == scene.objects.size() && count == 27,
      "Oracle unit scale and object count");
  const blend::UnitConversion units(scale);
  for (std::size_t record = 0; record < count; ++record) {
    std::string name, parent;
    bool hidden = false;
    Require(static_cast<bool>(oracle >> std::quoted(name) >> std::quoted(parent) >> hidden) &&
                indices.contains(name),
        "Oracle object record");
    const auto& object = scene.objects[indices.at(name)];
    const auto world = units.WorldTransform(ReadMatrix(oracle));
    const auto local = units.WorldTransform(ReadMatrix(oracle));
    Require(object.hiddenForRender == hidden && object.identifier == name && !object.mesh,
        name + " preserves visibility without inheriting the parent's render flag");
    Compare(object.worldTransform, world, name + " world");
    if (parent.empty()) {
      Require(!object.parent, name + " is an IR root");
      Compare(local, world, name + " root local");
      Compare(blend::ParentRelativeTransform(object.worldTransform), local,
          name + " constructed root local");
    } else {
      Require(indices.contains(parent) && object.parent == indices.at(parent), name + " retains its selected parent");
      Compare(Multiply(scene.objects[*object.parent].worldTransform, local), object.worldTransform,
          name + " converted parent/local composition");
      const auto constructed = blend::ParentRelativeTransform(
          object.worldTransform, scene.objects[*object.parent].worldTransform);
      Compare(constructed, local, name + " constructed parent-relative local");
      Compare(Multiply(scene.objects[*object.parent].worldTransform, constructed),
          object.worldTransform, name + " reconstructed world");
    }
  }
  oracle >> std::ws;
  Require(oracle.eof(), "Oracle has no trailing records");
  auto reordered = blocks.GetValue();
  std::reverse(reordered.begin(), reordered.end());
  const auto repeated = blend::DecodeScene(bytes.GetValue(), reordered,
      schema.GetValue(), header.GetValue(), {10000, 64});
  Require(repeated.HasValue() && repeated.GetValue().objects.size() == scene.objects.size(), "Reordered transform decode");
  for (std::size_t index = 0; index < scene.objects.size(); ++index) {
    const auto& object = repeated.GetValue().objects[index];
    Require(object.sourceName == scene.objects[index].sourceName && object.parent == scene.objects[index].parent &&
                object.identifier == scene.objects[index].identifier &&
                object.worldTransform == scene.objects[index].worldTransform,
        "Repeated/reordered reads are deterministic");
    const auto localTransform = [](const blend::Scene& value, std::size_t objectIndex) {
      const auto& entry = value.objects[objectIndex];
      return blend::ParentRelativeTransform(entry.worldTransform,
          entry.parent ? value.objects[*entry.parent].worldTransform : blend::IdentityMatrix);
    };
    Require(localTransform(repeated.GetValue(), index) == localTransform(scene, index),
        "Repeated/reordered parent-relative matrices are deterministic");
  }
}

} // namespace

int main(int argc, char** argv) {
  try {
    Require(argc == 3, "Two Blender-written transform fixtures are required");
    CheckFixture(argv[1]);
    CheckFixture(argv[2]);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
