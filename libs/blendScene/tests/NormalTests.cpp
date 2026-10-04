#include <blendScene/Decode.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

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

blend::Vector3 ReadVector(std::istream& input) {
  blend::Vector3 value{};
  for (auto& component : value) {
    Require(static_cast<bool>(input >> component) && std::isfinite(component), "Invalid normal oracle vector");
  }
  return value;
}

void Compare(const blend::Vector3& actual, const blend::Vector3& expected,
    const std::string& name) {
  for (std::size_t axis = 0; axis < 3; ++axis) {
    Require(std::abs(actual[axis] - expected[axis]) <= 2e-5,
        name + " differs at axis " + std::to_string(axis) + ": got " +
            std::to_string(actual[axis]) + ", expected " + std::to_string(expected[axis]));
  }
}

void CheckFixture(const std::filesystem::path& path) {
  blend::FileByteSource source(path);
  const auto bytes = Take(blend::ReadFileBytes(source, {4 * 1024 * 1024, 4 * 1024 * 1024, 16, 23}));
  blend::MemoryByteSource memory(bytes);
  const auto header = Take(blend::ReadHeader(memory));
  const auto blocks = Take(blend::ReadBlocks(memory, 10000));
  const auto dna = std::find_if(blocks.begin(), blocks.end(),
      [](const auto& block) { return block.code == std::array<char, 4>{'D', 'N', 'A', '1'}; });
  Require(dna != blocks.end(), "Normal fixture has DNA1");
  const auto schema = Take(blend::ReadDna(std::span<const std::byte>(bytes).subspan(
                                              static_cast<std::size_t>(dna->offset), static_cast<std::size_t>(dna->length)),
      header));
  const auto decoded = blend::DecodeScene(bytes, blocks, schema, header, {10000, 64});
  const auto scene = Take(decoded);
  const std::size_t expectedCount = path.stem() == "multi" ? 2 : 1;
  Require(scene.metadata.sourceScene == "Normals" && scene.objects.size() == expectedCount &&
              scene.meshes.size() == expectedCount && decoded.Diagnostics().empty(),
      "Normal fixture has only source Mesh objects without evaluation diagnostics");
  auto oraclePath = path;
  oraclePath.replace_extension(".oracle.txt");
  std::ifstream oracle(oraclePath);
  std::string line;
  Require(static_cast<bool>(std::getline(oracle, line)) && line == "BLEND_NORMALS_ORACLE 1", "Normal oracle version");
  Require(static_cast<bool>(std::getline(oracle, line)), "Oracle Blender version");
  double scale = 0;
  std::size_t count = 0;
  Require(static_cast<bool>(oracle >> scale >> count) && scale == scene.metadata.sourceUnitScale &&
              count == scene.objects.size(),
      "Normal oracle scale and mesh count");
  const blend::UnitConversion units(scale);
  for (std::size_t record = 0; record < count; ++record) {
    std::string name;
    std::size_t points = 0, faces = 0, corners = 0;
    Require(static_cast<bool>(oracle >> std::quoted(name) >> points >> faces >> corners), "Normal oracle mesh header");
    const auto object = std::find_if(scene.objects.begin(), scene.objects.end(),
        [&](const auto& entry) { return entry.sourceName == name; });
    Require(object != scene.objects.end() && object->mesh, name + " references a decoded Mesh");
    const auto& mesh = scene.meshes[*object->mesh];
    Require(mesh.sourceName == name && mesh.points.size() == points && mesh.faceVertexCounts.size() == faces &&
                mesh.faceVertexIndices.size() == corners && mesh.cornerNormals.size() == corners && mesh.uvMaps.empty(),
        name + " preserves mesh array sizes");
    for (std::size_t vertex = 0; vertex < points; ++vertex) {
      Compare(mesh.points[vertex], units.Position(ReadVector(oracle)), name + " point " + std::to_string(vertex));
    }
    for (const auto value : mesh.faceVertexCounts) {
      std::int32_t expected = 0;
      Require(static_cast<bool>(oracle >> expected) && value == expected, name + " preserves face counts");
    }
    for (const auto value : mesh.faceVertexIndices) {
      std::int32_t expected = 0;
      Require(static_cast<bool>(oracle >> expected) && value == expected, name + " preserves corner order");
    }
    for (std::size_t corner = 0; corner < corners; ++corner) {
      const auto& normal = mesh.cornerNormals[corner];
      Compare(normal, blend::ToUsdBasis(ReadVector(oracle)), name + " normal " + std::to_string(corner));
      Require(std::abs(std::hypot(normal[0], normal[1], normal[2]) - 1) <= 1e-12,
          name + " has unit-length corner normals");
    }
  }
  oracle >> std::ws;
  Require(oracle.eof(), "Normal oracle has no trailing records");
  auto reordered = blocks;
  std::reverse(reordered.begin(), reordered.end());
  const auto repeated = Take(blend::DecodeScene(bytes, reordered, schema, header, {10000, 64}));
  Require(repeated.meshes.size() == scene.meshes.size(), "Reordered normal fixture decodes");
  for (std::size_t index = 0; index < scene.meshes.size(); ++index) {
    const auto& left = scene.meshes[index];
    const auto& right = repeated.meshes[index];
    Require(left.sourceName == right.sourceName && left.points == right.points &&
                left.faceVertexCounts == right.faceVertexCounts && left.faceVertexIndices == right.faceVertexIndices &&
                left.cornerNormals == right.cornerNormals,
        "Repeated/reordered normal reads are deterministic");
  }
}

} // namespace

int main(int argc, char** argv) {
  try {
    Require(argc == 3, "Two Blender-written normal fixture directories are required");
    for (const auto directory : {argv[1], argv[2]}) {
      for (const auto name : {"smooth.blend", "flat.blend", "split.blend"}) {
        CheckFixture(std::filesystem::path(directory) / name);
      }
    }
    CheckFixture(std::filesystem::path(argv[1]) / "multi.blend");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
