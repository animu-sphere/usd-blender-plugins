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

template <class Value>
Value Take(const blend::Result<Value>& result) {
  Require(result.HasValue(), result.HasValue() ? "" : result.GetError().code + ": " + result.GetError().message + (result.GetError().blockIndex ? " at block " + std::to_string(*result.GetError().blockIndex) : ""));
  return result.GetValue();
}

void Marker(std::istream& input, const std::string& expected) {
  std::string marker;
  Require(static_cast<bool>(input >> marker) && marker == expected, "Expected oracle record " + expected);
}

template <std::size_t Size>
std::array<double, Size> ReadVector(std::istream& input, const std::string& marker) {
  Marker(input, marker);
  std::array<double, Size> result{};
  for (auto& value : result) {
    Require(static_cast<bool>(input >> value) && std::isfinite(value), "Finite oracle vector");
  }
  return result;
}

blend::Matrix4 ReadMatrix(std::istream& input, const std::string& marker) {
  Marker(input, marker);
  blend::Matrix4 result{};
  for (auto& row : result) {
    for (auto& value : row) {
      Require(static_cast<bool>(input >> value) && std::isfinite(value), "Finite oracle matrix");
    }
  }
  for (std::size_t column = 0; column < 4; ++column) {
    Require(std::abs(result[3][column] - blend::IdentityMatrix[3][column]) <= 1e-6,
        "Oracle matrix is affine within Blender float precision");
  }
  result[3] = blend::IdentityMatrix[3];
  return result;
}

template <std::size_t Size>
void Compare(const std::array<double, Size>& actual, const std::array<double, Size>& expected,
    const std::string& context) {
  for (std::size_t index = 0; index < Size; ++index) {
    Require(std::abs(actual[index] - expected[index]) <= 2e-5 * (1 + std::abs(expected[index])),
        context + " differs at component " + std::to_string(index) + ": got " +
            std::to_string(actual[index]) + ", expected " + std::to_string(expected[index]));
  }
}

void Compare(const blend::Matrix4& actual, const blend::Matrix4& expected, const std::string& context) {
  for (std::size_t row = 0; row < 4; ++row) {
    Compare(actual[row], expected[row], context + " row " + std::to_string(row));
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

blend::Scene LoadScene(const std::filesystem::path& path, bool reverse = false,
    bool meshDomains = false, bool fallbacks = false) {
  blend::FileByteSource source(path);
  const auto bytes = Take(blend::ReadFileBytes(source, {4 * 1024 * 1024, 4 * 1024 * 1024, 16, 23}));
  blend::MemoryByteSource memory(bytes);
  const auto header = Take(blend::ReadHeader(memory));
  auto blocks = Take(blend::ReadBlocks(memory, 10000));
  const auto dna = std::find_if(blocks.begin(), blocks.end(),
      [](const auto& block) { return block.code == std::array<char, 4>{'D', 'N', 'A', '1'}; });
  Require(dna != blocks.end(), "Integrated fixture has DNA1");
  const auto schema = Take(blend::ReadDna(
      std::span<const std::byte>(bytes).subspan(static_cast<std::size_t>(dna->offset), static_cast<std::size_t>(dna->length)), header));
  if (reverse) {
    std::reverse(blocks.begin(), blocks.end());
  }
  const auto decoded = blend::DecodeScene(bytes, blocks, schema, header, {10000, 64});
  auto scene = Take(decoded);
  if (meshDomains) {
    Require(decoded.Diagnostics().size() == 2, "Only the two polygon-free Meshes need diagnostics");
    std::vector<std::string> names;
    for (const auto& diagnostic : decoded.Diagnostics()) {
      Require(diagnostic.code == "BLEND_MESH_EMPTY" && diagnostic.severity == blend::Severity::Warning &&
                  diagnostic.recoverable && diagnostic.blockIndex && *diagnostic.blockIndex < blocks.size() &&
                  diagnostic.byteOffset == blocks[*diagnostic.blockIndex].offset &&
                  blocks[*diagnostic.blockIndex].code == std::array<char, 4>{'M', 'E', '\0', '\0'},
          "Empty Mesh warning retains exact Mesh block context");
      names.push_back(diagnostic.datablock);
    }
    std::sort(names.begin(), names.end());
    Require(names == std::vector<std::string>{"Empty", "Loose"}, "Each empty Mesh warns exactly once");
  } else if (fallbacks) {
    std::vector<std::string> names;
    for (const auto& diagnostic : decoded.Diagnostics()) {
      Require(diagnostic.code == "BLEND_SCENE_OBJECT_DATA_UNSUPPORTED" &&
                  diagnostic.severity == blend::Severity::Unsupported && diagnostic.recoverable &&
                  diagnostic.blockIndex && *diagnostic.blockIndex < blocks.size() &&
                  diagnostic.byteOffset == blocks[*diagnostic.blockIndex].offset &&
                  blocks[*diagnostic.blockIndex].code == std::array<char, 4>{'O', 'B', 0, 0},
          "Unsupported data reports exact Object context, including parent-only Objects");
      names.push_back(diagnostic.datablock);
    }
    std::sort(names.begin(), names.end());
    Require(names == std::vector<std::string>{
                "CameraFallback", "ImageFallback", "LightFallback", "OutsideParent", "TextFallback"},
        "Each decoded unsupported Object reports once, even with shared Camera data");
  } else {
    Require(decoded.Diagnostics().empty(), "Integrated source fixture needs no evaluation or repair");
  }
  return scene;
}

void CompareScenes(const blend::Scene& left, const blend::Scene& right) {
  Require(left.metadata.sourceScene == right.metadata.sourceScene &&
              left.metadata.sourceVersion == right.metadata.sourceVersion &&
              left.metadata.sourceUnitScale == right.metadata.sourceUnitScale &&
              left.objects.size() == right.objects.size() && left.meshes.size() == right.meshes.size(),
      "Repeated/reordered metadata and counts are identical");
  for (std::size_t index = 0; index < left.objects.size(); ++index) {
    const auto& a = left.objects[index];
    const auto& b = right.objects[index];
    Require(a.sourceName == b.sourceName && a.identifier == b.identifier && a.parent == b.parent &&
                a.mesh == b.mesh && a.hiddenForRender == b.hiddenForRender && a.worldTransform == b.worldTransform,
        "Repeated/reordered Object order and owning values are identical");
    const auto local = [](const blend::Scene& scene, std::size_t objectIndex) {
      const auto& object = scene.objects[objectIndex];
      return blend::ParentRelativeTransform(object.worldTransform,
          object.parent ? scene.objects[*object.parent].worldTransform : blend::IdentityMatrix);
    };
    Require(local(left, index) == local(right, index), "Repeated/reordered parent-local matrices are identical");
  }
  for (std::size_t index = 0; index < left.meshes.size(); ++index) {
    const auto& a = left.meshes[index];
    const auto& b = right.meshes[index];
    Require(a.sourceName == b.sourceName && a.points == b.points && a.faceVertexCounts == b.faceVertexCounts &&
                a.faceVertexIndices == b.faceVertexIndices && a.cornerNormals == b.cornerNormals &&
                a.uvMaps.size() == b.uvMaps.size(),
        "Repeated/reordered Mesh order and arrays are identical");
    for (std::size_t uv = 0; uv < a.uvMaps.size(); ++uv) {
      Require(a.uvMaps[uv].sourceName == b.uvMaps[uv].sourceName &&
                  a.uvMaps[uv].activeRender == b.uvMaps[uv].activeRender &&
                  a.uvMaps[uv].values == b.uvMaps[uv].values && a.uvMaps[uv].indices == b.uvMaps[uv].indices,
          "Repeated/reordered indexed UV maps are identical");
    }
  }
}

void CheckFixture(const std::filesystem::path& path, bool meshDomains, bool fallbacks) {
  const auto scene = LoadScene(path, false, meshDomains, fallbacks);
  Require(scene.metadata.sourceScene == (meshDomains ? "MeshDomains" : fallbacks ? "Fallbacks" : "Integrated") &&
              scene.objects.size() == (meshDomains ? 5 : fallbacks ? 11 : 7) &&
              scene.meshes.size() == (meshDomains ? 4 : 2),
      "Only selected membership and its two unique Meshes are published");
  std::unordered_map<std::string, std::size_t> objects, meshes;
  for (std::size_t index = 0; index < scene.objects.size(); ++index) {
    Require(objects.emplace(scene.objects[index].sourceName, index).second, "Unique selected Object names");
  }
  for (std::size_t index = 0; index < scene.meshes.size(); ++index) {
    Require(meshes.emplace(scene.meshes[index].sourceName, index).second, "Unique selected Mesh names");
  }
  if (meshDomains) {
    const auto shared = scene.objects[objects.at("Seams")].mesh;
    Require(shared && scene.objects[objects.at("SharedSeams")].mesh == shared,
        "Transformed Objects share one Mesh without transforming its UV values");
    for (const auto& [name, shape] : std::vector<std::pair<std::string, std::array<std::size_t, 4>>>{
             {"Empty", {0, 0, 0, 1}}, {"Loose", {3, 0, 0, 1}},
             {"NoUv", {3, 1, 3, 0}}, {"Seams", {5, 2, 7, 2}}}) {
      const auto& mesh = scene.meshes[meshes.at(name)];
      Require(mesh.points.size() == shape[0] && mesh.faceVertexCounts.size() == shape[1] &&
                  mesh.faceVertexIndices.size() == shape[2] && mesh.uvMaps.size() == shape[3],
          name + " preserves its empty, loose, UV-free or seam-bearing domains");
    }
    const auto& maps = scene.meshes[*shared].uvMaps;
    Require(maps[0].sourceName == "Seams" && !maps[0].activeRender &&
                maps[0].values.size() == 5 && maps[0].indices == std::vector<std::int32_t>{0, 1, 2, 0, 3, 4, 1} &&
                maps[1].sourceName == "Constant" && maps[1].activeRender && maps[1].values.size() == 1,
        "Corner seams, numeric signed-zero equality and the non-editing render map are preserved");
  } else {
    Require(!objects.contains("OutsideParent") && !meshes.contains("ParentOnlyGeometry"),
        "Parent-only Mesh contributes transforms without membership or geometry");
    const auto shared = scene.objects[objects.at("MeshParent")].mesh;
    Require(shared && scene.objects[objects.at("SharedChild")].mesh == shared &&
                scene.objects[objects.at("SharedRoot")].mesh == shared &&
                scene.objects[objects.at("Independent")].mesh != shared,
        "Three Mesh Objects share one IR index, independently of hierarchy");
    Require(scene.objects[objects.at("mesh")].identifier == "mesh_1",
        "Mesh parent's fixed child name is reserved for its Empty child");
    if (fallbacks) {
      for (const auto name : {"CameraFallback", "LightFallback", "TextFallback", "ImageFallback"}) {
        Require(!scene.objects[objects.at(name)].mesh, "Unsupported data has no fabricated Mesh index");
      }
      Require(scene.objects[objects.at("SharedRoot")].parent == objects.at("TextFallback"),
          "Supported Mesh children retain unsupported parents");
    }
  }
  auto oraclePath = path;
  oraclePath.replace_extension(".oracle.txt");
  std::ifstream oracle(oraclePath);
  std::string line;
  Require(static_cast<bool>(std::getline(oracle, line)) && line == "BLEND_SCENE_ORACLE 1", "Scene oracle version");
  Require(static_cast<bool>(std::getline(oracle, line)), "Oracle Blender version");
  double scale = 0;
  std::size_t objectCount = 0, meshCount = 0;
  Require(static_cast<bool>(oracle >> scale >> objectCount >> meshCount) &&
              scale == scene.metadata.sourceUnitScale && objectCount == scene.objects.size() && meshCount == scene.meshes.size(),
      "Oracle unit scale and selected counts");
  const blend::UnitConversion units(scale);
  for (std::size_t record = 0; record < objectCount; ++record) {
    Marker(oracle, "OBJECT");
    std::string name, parent, data;
    bool hidden = false;
    Require(static_cast<bool>(oracle >> std::quoted(name) >> std::quoted(parent) >> std::quoted(data) >> hidden) &&
                objects.contains(name),
        "Oracle Object references selected membership");
    const auto& object = scene.objects[objects.at(name)];
    const auto world = units.WorldTransform(ReadMatrix(oracle, "WORLD"));
    const auto savedLocal = units.WorldTransform(ReadMatrix(oracle, "LOCAL"));
    Require(object.hiddenForRender == hidden && object.identifier == (name == "mesh" ? "mesh_1" : name),
        name + " preserves its own render bit and deterministic identifier");
    if (data.empty()) {
      Require(!object.mesh, name + " is data-less Empty");
    } else {
      Require(meshes.contains(data) && object.mesh == meshes.at(data), name + " retains the saved shared Mesh edge");
    }
    Compare(object.worldTransform, world, name + " world");
    if (!parent.empty() && objects.contains(parent)) {
      Require(object.parent == objects.at(parent), name + " retains its selected parent");
      const auto& parentWorld = scene.objects[*object.parent].worldTransform;
      const auto local = blend::ParentRelativeTransform(object.worldTransform, parentWorld);
      Compare(local, savedLocal, name + " local");
      Compare(Multiply(parentWorld, local), world, name + " parent/local composition");
    } else {
      Require(!object.parent && (parent.empty() || parent == "OutsideParent"), name + " is an IR root");
      Compare(blend::ParentRelativeTransform(object.worldTransform), world, name + " root local");
    }
  }
  for (std::size_t record = 0; record < meshCount; ++record) {
    Marker(oracle, "MESH");
    std::string name;
    std::size_t points = 0, faces = 0, corners = 0, uvs = 0;
    Require(static_cast<bool>(oracle >> std::quoted(name) >> points >> faces >> corners >> uvs) &&
                meshes.contains(name) && (meshDomains || (points == 5 && faces == 2 && corners == 7 && uvs == 2)),
        "Oracle unique Mesh shape");
    const auto& mesh = scene.meshes[meshes.at(name)];
    Require(mesh.points.size() == points && mesh.faceVertexCounts.size() == faces &&
                mesh.faceVertexIndices.size() == corners && mesh.cornerNormals.size() == corners && mesh.uvMaps.size() == uvs,
        name + " preserves all Mesh domains");
    for (const auto& point : mesh.points) {
      Compare(point, units.Position(ReadVector<3>(oracle, "POINT")), name + " meter-space point");
    }
    Marker(oracle, "COUNTS");
    for (const auto count : mesh.faceVertexCounts) {
      std::int32_t expected = 0;
      Require(static_cast<bool>(oracle >> expected) && count == expected, name + " face counts");
    }
    Marker(oracle, "INDICES");
    for (const auto index : mesh.faceVertexIndices) {
      std::int32_t expected = 0;
      Require(static_cast<bool>(oracle >> expected) && index == expected, name + " right-handed corner order");
    }
    for (const auto& normal : mesh.cornerNormals) {
      const auto expected = blend::ToUsdBasis(ReadVector<3>(oracle, "NORMAL"));
      for (std::size_t axis = 0; axis < 3; ++axis) {
        Require(std::abs(normal[axis] - expected[axis]) <= 2e-5, name + " unit-independent corner normal");
      }
      Require(std::abs(std::hypot(normal[0], normal[1], normal[2]) - 1) <= 1e-12, name + " normalized corner normal");
    }
    for (std::size_t uv = 0; uv < uvs; ++uv) {
      Marker(oracle, "UV");
      std::string uvName;
      bool active = false;
      Require(static_cast<bool>(oracle >> std::quoted(uvName) >> active), "Oracle UV header");
      const auto& map = mesh.uvMaps[uv];
      Require(map.sourceName == uvName && map.activeRender == active && map.indices.size() == corners,
          name + " indexed UV names, deduplication and non-first render map");
      std::vector<blend::Vector2> expectedValues;
      for (const auto index : map.indices) {
        const auto value = ReadVector<2>(oracle, "VALUE");
        const auto found = std::find(expectedValues.begin(), expectedValues.end(), value);
        const auto expectedIndex = static_cast<std::int32_t>(found - expectedValues.begin());
        if (found == expectedValues.end()) {
          expectedValues.push_back(value);
        }
        Require(index == expectedIndex && index >= 0 && static_cast<std::size_t>(index) < map.values.size(),
            "UV indices follow numeric first occurrence, not vertices or lexicographic order");
        Require(map.values[static_cast<std::size_t>(index)] == value,
            name + " UV coordinates are unchanged by units, basis and Object transforms");
      }
      Require(map.values == expectedValues, "Indexed UV values have no unused entries");
      for (std::size_t value = 0; value < expectedValues.size(); ++value) {
        for (std::size_t axis = 0; axis < 2; ++axis) {
          Require(std::signbit(map.values[value][axis]) == std::signbit(expectedValues[value][axis]),
              "Indexed UV values preserve the first signed-zero representation");
        }
      }
    }
  }
  oracle >> std::ws;
  Require(oracle.eof(), "Scene oracle has no trailing records");
  CompareScenes(scene, LoadScene(path, false, meshDomains, fallbacks));
  CompareScenes(scene, LoadScene(path, true, meshDomains, fallbacks));
}

} // namespace

int main(int argc, char** argv) {
  try {
    const bool meshDomains = argc >= 3 && std::string(argv[1]) == "--mesh-domains";
    const bool fallbacks = argc >= 3 && std::string(argv[1]) == "--fallbacks";
    Require(argc == 3 || meshDomains, "Blender-written Scene fixtures and an optional mode are required");
    for (int index = meshDomains || fallbacks ? 2 : 1; index < argc; ++index) {
      CheckFixture(argv[index], meshDomains, fallbacks);
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
