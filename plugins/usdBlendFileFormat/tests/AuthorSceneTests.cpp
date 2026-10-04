// SPDX-License-Identifier: Apache-2.0
#include "AuthorScene.h"
#include "ReadScene.h"
#include <blendScene/Decode.h>
#include <blendScene/Naming.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdGeom/xform.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace {

using namespace pxr;

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

template <class Value>
Value Get(const UsdAttribute& attr) {
  Value value;
  Require(attr.Get(&value), "Authored attribute has the expected type: " + attr.GetPath().GetString());
  return value;
}

void Identify(blend::Scene& scene) {
  const auto identifiers = Take(blend::ObjectIdentifiers(scene));
  for (std::size_t index = 0; index < scene.objects.size(); ++index) {
    scene.objects[index].identifier = identifiers[index];
  }
}

GfMatrix4d Transpose(const blend::Matrix4& matrix) {
  GfMatrix4d result(1);
  for (std::size_t row = 0; row < 4; ++row) {
    for (std::size_t column = 0; column < 4; ++column) {
      result[row][column] = matrix[column][row];
    }
  }
  return result;
}

void Compare(const GfMatrix4d& actual, const GfMatrix4d& expected, double tolerance = 1e-10) {
  for (std::size_t row = 0; row < 4; ++row) {
    for (std::size_t column = 0; column < 4; ++column) {
      Require(std::abs(actual[row][column] - expected[row][column]) <=
                  tolerance * (1 + std::abs(expected[row][column])),
          "USD row-vector matrix matches the normalized reference");
    }
  }
}

std::string Text(const SdfLayerRefPtr& layer) {
  std::string result;
  Require(layer->ExportToString(&result), "Layer serializes");
  return result;
}

std::vector<std::string> Children(const UsdPrim& prim) {
  std::vector<std::string> result;
  for (const auto& child : prim.GetChildren()) {
    result.push_back(child.GetName().GetString());
  }
  return result;
}

blend::Scene Synthetic() {
  blend::Scene scene;
  scene.metadata = {"5.2", "Authoring", 0.001};
  blend::Mesh mesh;
  mesh.sourceName = "Shared Mesh";
  mesh.points = {{2, 3, -4}, {-1, 3, 2}, {2, -2, 2}, {3, 1, 0}};
  mesh.faceVertexCounts = {3, 3};
  mesh.faceVertexIndices = {0, 1, 2, 0, 2, 3};
  mesh.cornerNormals = {{1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}};
  mesh.uvMaps = {{"st", {{0.2, 0.3}, {1, 0}, {0, 1}}, {0, 1, 2, 0, 2, 1}, false},
      {"Render Map", {{0, 0}, {1, 0}, {0, 1}, {1, 1}}, {0, 1, 2, 0, 2, 3}, true},
      {"A/B", {{0, 0}}, {0, 0, 0, 0, 0, 0}, false},
      {"A_B", {{1, 1}}, {0, 0, 0, 0, 0, 0}, false}};
  scene.meshes.push_back(mesh);
  blend::Object parent;
  parent.sourceName = "Parent";
  parent.worldTransform = {{{-2, 0.5, 0, 1}, {0, 3, 0, 2}, {0, 0, 4, 3}, {0, 0, 0, 1}}};
  parent.hiddenForRender = true;
  blend::Object child;
  child.sourceName = "mesh";
  child.parent = 0;
  child.mesh = 0;
  child.worldTransform = {{{1, 0, 0, 4}, {0, -1, 0, 5}, {0, 0, 2, 6}, {0, 0, 0, 1}}};
  blend::Object shared;
  shared.sourceName = "A/B";
  shared.mesh = 0;
  blend::Object collision;
  collision.sourceName = "A_B";
  blend::Object empty;
  empty.sourceName = "Empty";
  scene.objects = {parent, child, collision, shared, empty};
  Identify(scene);
  return scene;
}

void CheckMesh(const UsdGeomMesh& mesh, const blend::Mesh& source) {
  Require(mesh && Get<VtIntArray>(mesh.GetFaceVertexCountsAttr()) == VtIntArray(source.faceVertexCounts.begin(), source.faceVertexCounts.end()) &&
              Get<VtIntArray>(mesh.GetFaceVertexIndicesAttr()) ==
                  VtIntArray(source.faceVertexIndices.begin(), source.faceVertexIndices.end()),
      "Polygon counts and right-handed corner winding are preserved");
  const auto points = Get<VtVec3fArray>(mesh.GetPointsAttr());
  const auto normals = Get<VtVec3fArray>(mesh.GetNormalsAttr());
  Require(points.size() == source.points.size() && normals.size() == source.cornerNormals.size(),
      "Geometry arrays have exact IR shapes");
  for (std::size_t index = 0; index < points.size(); ++index) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      Require(points[index][axis] == static_cast<float>(source.points[index][axis]),
          "Points are authored without a second basis or unit conversion");
    }
  }
  for (std::size_t index = 0; index < normals.size(); ++index) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      Require(normals[index][axis] == static_cast<float>(source.cornerNormals[index][axis]),
          "Face-varying normals are authored unchanged");
    }
  }
  Require(mesh.GetNormalsInterpolation() == UsdGeomTokens->faceVarying &&
              Get<TfToken>(mesh.GetOrientationAttr()) == UsdGeomTokens->rightHanded &&
              Get<TfToken>(mesh.GetSubdivisionSchemeAttr()) == UsdGeomTokens->none,
      "Mesh interpolation, orientation and polygon scheme match the contract");
  const auto identifiers = Take(blend::UvIdentifiers(source));
  for (std::size_t index = 0; index < source.uvMaps.size(); ++index) {
    const auto& map = source.uvMaps[index];
    const auto primvar = UsdGeomPrimvarsAPI(mesh).GetPrimvar(TfToken(identifiers[index]));
    Require(primvar && primvar.GetTypeName() == SdfValueTypeNames->TexCoord2fArray &&
                primvar.GetInterpolation() == UsdGeomTokens->faceVarying &&
                Get<VtIntArray>(primvar.GetIndicesAttr()) == VtIntArray(map.indices.begin(), map.indices.end()),
        "UVs have indexed texCoord2f face-varying schema");
    const auto values = Get<VtVec2fArray>(primvar.GetAttr());
    Require(values.size() == map.values.size(), "UVs retain indexed value shape");
    for (std::size_t uv = 0; uv < values.size(); ++uv) {
      Require(values[uv] == GfVec2f(static_cast<float>(map.values[uv][0]), static_cast<float>(map.values[uv][1])),
          "UV origin, values and indexing remain unchanged");
    }
    Require(primvar.GetAttr().GetCustomDataByKey(TfToken("blend:sourceName")) == VtValue(map.sourceName),
        "UV properties preserve exact source names");
  }
}

std::vector<SdfPath> Paths(const blend::Scene& scene) {
  std::vector<SdfPath> result(scene.objects.size());
  for (std::size_t index = 0; index < scene.objects.size(); ++index) {
    std::vector<std::size_t> chain;
    auto current = std::optional<std::size_t>(index);
    while (current) {
      chain.push_back(*current);
      current = scene.objects[*current].parent;
    }
    auto path = SdfPath("/Asset/geo");
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
      path = path.AppendChild(TfToken(scene.objects[*it].identifier));
    }
    result[index] = path;
  }
  return result;
}

void CheckScene(const blend::Scene& scene, const UsdStageRefPtr& stage) {
  TfToken kind;
  Require(stage->GetDefaultPrim().GetMetadata(TfToken("kind"), &kind), "Root kind is authored");
  Require(stage->GetDefaultPrim().GetPath() == SdfPath("/Asset") &&
              stage->GetDefaultPrim().GetTypeName() == TfToken("Xform") &&
              kind == TfToken("component") &&
              stage->GetDefaultPrim().GetCustomDataByKey(TfToken("blend:stageContractVersion")) == VtValue(1) &&
              stage->GetDefaultPrim().GetCustomDataByKey(TfToken("blend:sourceVersion")) == VtValue(scene.metadata.sourceVersion) &&
              stage->GetDefaultPrim().GetCustomDataByKey(TfToken("blend:sourceScene")) == VtValue(scene.metadata.sourceScene) &&
              UsdGeomGetStageUpAxis(stage) == UsdGeomTokens->y && UsdGeomGetStageMetersPerUnit(stage) == 1,
      "Scene metadata, fixed axis/units and contract version are authored");
  Require(Children(stage->GetDefaultPrim()) == std::vector<std::string>{"geo", "mtl"},
      "Required scopes have fixed order");
  const auto paths = Paths(scene);
  UsdGeomXformCache cache;
  for (std::size_t index = 0; index < scene.objects.size(); ++index) {
    const auto& object = scene.objects[index];
    const UsdGeomXform xform(stage->GetPrimAtPath(paths[index]));
    Require(static_cast<bool>(xform), "Each Object has an Xform");
    Compare(cache.GetLocalToWorldTransform(xform.GetPrim()), Transpose(object.worldTransform));
    const auto local = blend::ParentRelativeTransform(object.worldTransform,
        object.parent ? scene.objects[*object.parent].worldTransform : blend::IdentityMatrix);
    Compare(Get<GfMatrix4d>(xform.GetPrim().GetAttribute(TfToken("xformOp:transform"))), Transpose(local));
    Require(Get<VtTokenArray>(xform.GetXformOpOrderAttr()) == VtTokenArray{TfToken("xformOp:transform")} &&
                Get<TfToken>(xform.GetVisibilityAttr()) ==
                    (object.hiddenForRender ? UsdGeomTokens->invisible : UsdGeomTokens->inherited) &&
                xform.GetPrim().GetCustomDataByKey(TfToken("blend:sourceName")) == VtValue(object.sourceName),
        "One matrix op, saved render visibility and exact source provenance");
    if (object.mesh) {
      CheckMesh(UsdGeomMesh(stage->GetPrimAtPath(paths[index].AppendChild(TfToken("mesh")))),
          scene.meshes[*object.mesh]);
    }
  }
}

void CheckSynthetic() {
  const auto scene = Synthetic();
  const auto layer = Take(blend::AuthorScene(scene));
  const auto stage = UsdStage::Open(layer);
  CheckScene(scene, stage);
  Require(Children(stage->GetPrimAtPath(SdfPath("/Asset/geo"))) ==
              std::vector<std::string>{"A_B", "A_B_1", "Empty", "Parent"},
      "Children are ordered by raw source bytes");
  const auto mesh = UsdGeomMesh(stage->GetPrimAtPath(SdfPath("/Asset/geo/Parent/mesh/mesh")));
  Require(Get<VtVec3fArray>(mesh.GetExtentAttr()) == VtVec3fArray{GfVec3f(-1, -2, -4), GfVec3f(3, 3, 2)},
      "Extent encloses the authored meter-space points");
  Require(!stage->GetPrimAtPath(SdfPath("/Asset/geo/A_B/mesh")).IsInstanceable(),
      "Shared meshes are duplicated, not instanced");
  Require(stage->GetPrimAtPath(SdfPath("/Asset/geo/A_B")).GetDisplayName() == "A/B" &&
              stage->GetPrimAtPath(SdfPath("/Asset/geo/Empty")).GetDisplayName().empty(),
      "Display names are authored only when distinct from identifiers");
  Require(UsdGeomPrimvarsAPI(mesh).GetPrimvar(TfToken("st")).GetAttr().GetDisplayName() == "Render Map" &&
              UsdGeomPrimvarsAPI(mesh).GetPrimvar(TfToken("st_1")),
      "Render st wins over a non-render source named st");
  const auto independent = UsdGeomMesh(stage->GetPrimAtPath(SdfPath("/Asset/geo/A_B/mesh")));
  auto edited = Get<VtVec3fArray>(independent.GetPointsAttr());
  edited[0] = GfVec3f(100);
  Require(independent.GetPointsAttr().Set(edited) &&
              Get<VtVec3fArray>(mesh.GetPointsAttr())[0] == GfVec3f(2, 3, -4),
      "Duplicated shared Mesh attributes can be edited independently");
  Require(independent.GetPointsAttr().Set(Get<VtVec3fArray>(mesh.GetPointsAttr())), "Restore the test edit");
  const auto text = Text(layer);
  Require(text == Text(Take(blend::AuthorScene(scene))), "Repeated authoring produces identical layer text");
  auto reordered = scene;
  std::reverse(reordered.objects.begin(), reordered.objects.end());
  for (auto& object : reordered.objects) {
    if (object.parent) {
      object.parent = reordered.objects.size() - 1 - *object.parent;
    }
  }
  std::reverse(reordered.meshes[0].uvMaps.begin(), reordered.meshes[0].uvMaps.end());
  Require(text == Text(Take(blend::AuthorScene(reordered))), "IR and UV enumeration do not affect the authored layer");
  const auto referenced = UsdStage::CreateInMemory();
  const auto root = referenced->DefinePrim(SdfPath("/Reference"));
  Require(root.GetReferences().AddReference(layer->GetIdentifier()), "Authored Scene composes as a reference");
  Require(root.GetCustomDataByKey(TfToken("blend:stageContractVersion")) == VtValue(1) &&
              referenced->GetPrimAtPath(SdfPath("/Reference/geo/A_B/mesh")),
      "Contract provenance and geometry survive referencing");

  blend::Scene empty;
  empty.metadata = scene.metadata;
  CheckScene(empty, UsdStage::Open(Take(blend::AuthorScene(empty))));
  auto noFaces = scene;
  noFaces.meshes[0].faceVertexCounts.clear();
  noFaces.meshes[0].faceVertexIndices.clear();
  noFaces.meshes[0].cornerNormals.clear();
  noFaces.meshes[0].uvMaps.clear();
  auto result = blend::AuthorScene(noFaces);
  Require(result.HasValue() && result.Diagnostics().size() == 1 &&
              result.Diagnostics()[0].code == "BLEND_MESH_EMPTY" &&
              result.Diagnostics()[0].recoverable && result.Diagnostics()[0].datablock == "Shared Mesh",
      "A shared empty Mesh reports one warning and still authors points");
  CheckScene(noFaces, UsdStage::Open(result.GetValue()));
  noFaces.meshes[0].points.clear();
  result = blend::AuthorScene(noFaces);
  Require(Get<VtVec3fArray>(UsdGeomMesh(UsdStage::Open(Take(result))->GetPrimAtPath(SdfPath("/Asset/geo/A_B/mesh"))).GetExtentAttr()).empty(),
      "A Mesh with no points has an explicitly empty extent");
}

void Failure(const blend::Scene& scene, const std::string& code, const std::string& name = {}) {
  const auto result = blend::AuthorScene(scene);
  Require(!result.HasValue() && result.GetError().code == code &&
              result.GetError().severity == blend::Severity::Fatal &&
              !result.GetError().recoverable && result.GetError().datablock == name,
      "Failure returns no partial layer and the expected diagnostic: " + code);
}

void CheckFailures() {
  auto scene = Synthetic();
  scene.objects[0].parent = 100;
  Failure(scene, "BLEND_SCENE_REFERENCE_INVALID", "Parent");
  scene = Synthetic();
  scene.objects[1].mesh = 100;
  Failure(scene, "BLEND_SCENE_REFERENCE_INVALID", "mesh");
  scene = Synthetic();
  scene.objects[0].parent = 1;
  Identify(scene);
  Failure(scene, "BLEND_SCENE_CYCLE");
  scene = Synthetic();
  scene.objects[1].identifier = "bad/name";
  Failure(scene, "BLEND_USD_IDENTIFIER_INVALID", "mesh");
  scene = Synthetic();
  scene.objects[0].worldTransform[0] = {0, 0, 0, 0};
  Failure(scene, "BLEND_SCENE_TRANSFORM_SINGULAR", "mesh");
  scene = Synthetic();
  scene.objects[0].worldTransform[0][0] = std::numeric_limits<double>::quiet_NaN();
  Failure(scene, "BLEND_SCENE_TRANSFORM_INVALID", "Parent");
  scene = Synthetic();
  scene.objects[0].worldTransform[3][0] = 1;
  Failure(scene, "BLEND_SCENE_TRANSFORM_INVALID", "Parent");
  scene = Synthetic();
  scene.objects[1].parent.reset();
  scene.objects[0].worldTransform[0] = {0, 0, 0, 0};
  Identify(scene);
  Require(blend::AuthorScene(scene).HasValue(), "A singular root without children is valid");
  scene = Synthetic();
  scene.objects[1].worldTransform[0][0] = 0;
  Require(blend::AuthorScene(scene).HasValue(), "A zero-scale leaf is not a singular-parent error");
  scene = Synthetic();
  scene.meshes[0].faceVertexCounts[0] = 2;
  Failure(scene, "BLEND_USD_TOPOLOGY_INVALID", "Shared Mesh");
  scene = Synthetic();
  scene.meshes[0].faceVertexIndices[0] = -1;
  Failure(scene, "BLEND_USD_TOPOLOGY_INVALID", "Shared Mesh");
  scene = Synthetic();
  scene.meshes[0].cornerNormals.pop_back();
  Failure(scene, "BLEND_USD_TOPOLOGY_INVALID", "Shared Mesh");
  scene = Synthetic();
  scene.meshes[0].points[0][0] = std::numeric_limits<double>::max();
  Failure(scene, "BLEND_USD_VALUE_INVALID", "Shared Mesh");
  scene = Synthetic();
  scene.meshes[0].points[0][0] = std::numeric_limits<float>::max();
  Require(blend::AuthorScene(scene).HasValue(), "The exact finite float limit is accepted");
  scene.meshes[0].points[0][0] = std::nextafter(
      static_cast<double>(std::numeric_limits<float>::max()), std::numeric_limits<double>::infinity());
  Failure(scene, "BLEND_USD_VALUE_INVALID", "Shared Mesh");
  scene = Synthetic();
  scene.meshes[0].cornerNormals[0][0] = std::numeric_limits<double>::infinity();
  Failure(scene, "BLEND_USD_VALUE_INVALID", "Shared Mesh");
  scene = Synthetic();
  scene.meshes[0].uvMaps[0].indices[0] = 10;
  Failure(scene, "BLEND_USD_UV_INVALID", "Shared Mesh");
  scene = Synthetic();
  scene.meshes[0].uvMaps[0].indices.pop_back();
  Failure(scene, "BLEND_USD_UV_INVALID", "Shared Mesh");
  scene = Synthetic();
  scene.meshes[0].uvMaps[0].values[0][0] = std::numeric_limits<double>::quiet_NaN();
  Failure(scene, "BLEND_USD_VALUE_INVALID", "Shared Mesh");
  scene = Synthetic();
  scene.meshes[0].uvMaps[0].activeRender = true;
  Failure(scene, "BLEND_NAME_RENDER_UV_INVALID", "Shared Mesh");
  scene = Synthetic();
  scene.meshes[0].uvMaps[0].sourceName = scene.meshes[0].uvMaps[1].sourceName;
  Failure(scene, "BLEND_NAME_DUPLICATE", "Render Map");
  scene = Synthetic();
  scene.objects[4].sourceName = "\xff";
  Identify(scene);
  const auto result = blend::AuthorScene(scene);
  Require(result.HasValue() && result.Diagnostics().size() == 1 &&
              result.Diagnostics()[0].code == "BLEND_NAME_INVALID_UTF8" &&
              UsdStage::Open(result.GetValue())->GetPrimAtPath(SdfPath("/Asset/geo/Object")).GetDisplayName() == "\xef\xbf\xbd",
      "Malformed raw names retain provenance and emit repaired display warnings");
}

blend::Scene Decode(const std::filesystem::path& path, bool reverseBlocks = false, bool unitFixture = false) {
  blend::FileByteSource source(path);
  const auto bytes = Take(blend::ReadFileBytes(source, {4 * 1024 * 1024, 4 * 1024 * 1024, 16, 23}));
  blend::MemoryByteSource memory(bytes);
  const auto header = Take(blend::ReadHeader(memory));
  auto blocks = Take(blend::ReadBlocks(memory, 10000));
  if (reverseBlocks) {
    std::reverse(blocks.begin(), blocks.end());
  }
  const auto dna = std::find_if(blocks.begin(), blocks.end(),
      [](const auto& block) { return block.code == std::array<char, 4>{'D', 'N', 'A', '1'}; });
  Require(dna != blocks.end(), "Fixture has DNA1");
  const auto schema = Take(blend::ReadDna(std::span<const std::byte>(bytes).subspan(
                                              static_cast<std::size_t>(dna->offset), static_cast<std::size_t>(dna->length)),
      header));
  if (unitFixture) {
    const auto selected = Take(blend::SelectSceneObjectValues(bytes, blocks, schema, header, {10000, 64}));
    for (const auto& object : selected.objects) {
      const bool negative = object.sourceName == "Parent" || object.sourceName == "mesh.1" || object.sourceName == "mesh_1";
      Require(object.values && object.values->transformFlags == (negative ? 4 : 0),
          "Blender saves bit 2 only for negative world handedness, including inherited negative scale");
    }
  }
  const auto decoded = blend::DecodeScene(bytes, blocks, schema, header, {10000, 64});
  if (unitFixture) {
    Require(decoded.Diagnostics().empty(), "Unit fixture needs no native evaluation or repair");
  }
  return Take(decoded);
}

void CheckReadBoundary(const std::filesystem::path& path, const SdfLayerRefPtr& expected) {
  blend::FileByteSource source(path);
  const auto scene = Take(blend::ReadScene(source));
  Require(Text(expected) == Text(Take(blend::AuthorScene(scene))),
      "Importer byte-to-Scene composition matches independently composed native authoring");
  const auto full = UsdStage::Open(expected);
  const auto metadata = UsdStage::Open(Take(blend::AuthorScene(scene, true)));
  std::size_t count = 0;
  for (const auto& prim : full->Traverse()) {
    const auto other = metadata->GetPrimAtPath(prim.GetPath());
    Require(other && other.GetTypeName() == prim.GetTypeName(), "Metadata retains every prim and its type");
    if (prim.IsA<UsdGeomMesh>()) {
      Require(other.GetAuthoredAttributes().empty(), "Metadata Meshes have no geometry attributes");
    } else {
      for (const auto& attr : prim.GetAuthoredAttributes()) {
        VtValue value, otherValue;
        Require(attr.Get(&value) && other.GetAttribute(attr.GetName()).Get(&otherValue) && value == otherValue,
            "Metadata retains Object transforms and visibility");
      }
    }
    ++count;
  }
  std::size_t metadataCount = 0;
  for (const auto& prim : metadata->Traverse()) {
    (void)prim;
    ++metadataCount;
  }
  Require(count == metadataCount, "Metadata has exactly the full hierarchy");
}

void CheckFixture(const std::filesystem::path& path) {
  const auto scene = Decode(path);
  const auto layer = Take(blend::AuthorScene(scene));
  const auto stage = UsdStage::Open(layer);
  CheckScene(scene, stage);
  CheckReadBoundary(path, layer);
  Require(Text(layer) == Text(Take(blend::AuthorScene(Decode(path)))), "Repeated native reads author identical stages");
  Require(Text(layer) == Text(Take(blend::AuthorScene(Decode(path, true)))),
      "Reversed native block enumeration authors an identical stage");
  if (path.stem() != "transforms") {
    const auto objectCount = path.stem() == "scene" ? 7 : path.stem() == "single_cube" ? 1
                                                                                       : 2;
    const auto meshCount = path.stem() == "single_cube" ? 1 : 2;
    Require(scene.objects.size() == objectCount && scene.meshes.size() == meshCount, "Native fixture Object/Mesh counts");
    return;
  }
  auto oraclePath = path;
  oraclePath.replace_extension(".oracle.txt");
  std::ifstream oracle(oraclePath);
  std::string line;
  Require(static_cast<bool>(std::getline(oracle, line)) && line == "BLEND_TRANSFORMS_ORACLE 1", "Transform oracle header");
  Require(static_cast<bool>(std::getline(oracle, line)), "Transform oracle version");
  double scale = 0;
  std::size_t count = 0;
  Require(static_cast<bool>(oracle >> scale >> count) && count == 27, "All 27 oracle Objects are present");
  const auto paths = Paths(scene);
  UsdGeomXformCache cache;
  for (std::size_t record = 0; record < count; ++record) {
    std::string name, parent;
    bool hidden = false;
    Require(static_cast<bool>(oracle >> std::quoted(name) >> std::quoted(parent) >> hidden), "Oracle Object");
    blend::Matrix4 world{}, local{};
    for (auto* matrix : {&world, &local}) {
      for (auto& row : *matrix) {
        for (auto& value : row) {
          Require(static_cast<bool>(oracle >> value), "Oracle matrix");
        }
      }
      (*matrix)[3] = blend::IdentityMatrix[3];
    }
    const auto object = std::find_if(scene.objects.begin(), scene.objects.end(),
        [&](const auto& value) { return value.sourceName == name; });
    Require(object != scene.objects.end(), "Oracle Object exists");
    const auto prim = stage->GetPrimAtPath(paths[static_cast<std::size_t>(object - scene.objects.begin())]);
    const blend::UnitConversion units(scale);
    Compare(cache.GetLocalToWorldTransform(prim), Transpose(units.WorldTransform(world)), 2e-5);
    Compare(Get<GfMatrix4d>(prim.GetAttribute(TfToken("xformOp:transform"))),
        Transpose(units.WorldTransform(local)), 2e-5);
  }
}

void Marker(std::istream& input, const std::string& expected) {
  std::string marker;
  Require(static_cast<bool>(input >> marker) && marker == expected, "Expected unit oracle record " + expected);
}

template <std::size_t Size>
std::array<double, Size> ReadVector(std::istream& input, const std::string& marker) {
  Marker(input, marker);
  std::array<double, Size> result{};
  for (auto& value : result) {
    Require(static_cast<bool>(input >> value) && std::isfinite(value), "Finite unit oracle vector");
  }
  return result;
}

GfVec3d MeterPoint(const blend::Vector3& source, double scale) {
  return {source[0] * scale, source[2] * scale, -source[1] * scale};
}

GfMatrix4d MeterMatrix(std::istream& input, const std::string& marker, double scale) {
  const auto values = ReadVector<16>(input, marker);
  for (std::size_t column = 0; column < 4; ++column) {
    Require(std::abs(values[12 + column] - blend::IdentityMatrix[3][column]) <= 1e-6,
        "Blender oracle matrix is affine");
  }
  // Independent oracle mapping: USD rows transpose Blender columns after (x,z,-y).
  constexpr std::array<std::size_t, 3> axes{0, 2, 1};
  constexpr std::array<double, 3> signs{1, 1, -1};
  GfMatrix4d result(1);
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t column = 0; column < 3; ++column) {
      result[column][row] = values[axes[row] * 4 + axes[column]] * signs[row] * signs[column];
    }
    result[3][row] = values[axes[row] * 4 + 3] * signs[row] * scale;
  }
  return result;
}

template <class Left, class Right>
void CompareVector(const Left& actual, const Right& expected, double tolerance = 2e-5) {
  for (std::size_t axis = 0; axis < 3; ++axis) {
    if (!(std::abs(actual[axis] - expected[axis]) <= tolerance * (1 + std::abs(expected[axis])))) {
      std::ostringstream message;
      message << std::setprecision(17) << "Unit fixture vector differs at axis " << axis << ": got "
              << actual[axis] << ", expected " << expected[axis] << ", tolerance " << tolerance;
      throw std::runtime_error(message.str());
    }
  }
}

UsdStageRefPtr CheckUnitFixture(const std::filesystem::path& path, double expectedScale,
    const std::string& expectedSystem) {
  const auto scene = Decode(path, false, true);
  const auto authored = blend::AuthorScene(scene);
  const auto layer = Take(authored);
  Require(authored.Diagnostics().empty(), "Unit fixture needs no authoring repair");
  CheckReadBoundary(path, layer);
  const auto stage = UsdStage::Open(layer);
  CheckScene(scene, stage);
  Require(scene.metadata.sourceScene == "Units" && scene.objects.size() == 16 && scene.meshes.size() == 1,
      "Active Scene selection excludes the unselected Scene and retains one shared cube");
  std::map<std::string, std::size_t> objects;
  for (std::size_t index = 0; index < scene.objects.size(); ++index) {
    Require(objects.emplace(scene.objects[index].sourceName, index).second, "Unique saved Object names");
  }
  const auto paths = Paths(scene);
  const std::string japanese = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";
  const std::map<std::string, std::string> identifiers{
      {"3D Text", "_3D_Text"}, {"A B", "A_B"}, {"A/B", "A_B_1"}, {"A_B", "A_B_2"},
      {"A_B_1", "A_B_1_1"}, {"Cube", "Cube"}, {"Cube.001", "Cube_001"},
      {"Cube_001", "Cube_001_1"}, {"Object", "Object"}, {"Parent", "Parent"},
      {"Translated", "Translated"}, {"mesh", "mesh_1"}, {"mesh.1", "mesh_1_1"},
      {"mesh_1", "mesh_1_2"}, {japanese, "Object_1"}, {japanese + "2", "_2"}};
  for (const auto& [name, identifier] : identifiers) {
    const auto index = objects.at(name);
    const auto prim = stage->GetPrimAtPath(paths[index]);
    Require(scene.objects[index].identifier == identifier && prim.GetName().GetString() == identifier &&
                prim.GetDisplayName() == (name == identifier ? "" : name),
        "Blender-written names freeze ASCII identifiers and exact UTF-8 display names");
  }
  Require(Children(stage->GetPrimAtPath(SdfPath("/Asset/geo"))) ==
                  std::vector<std::string>{"_3D_Text", "A_B", "A_B_1", "A_B_2", "A_B_1_1",
                      "Cube", "Cube_001", "Cube_001_1", "Object", "Parent", "Translated", "Object_1", "_2"} &&
              Children(stage->GetPrimAtPath(SdfPath("/Asset/geo/Parent"))) ==
                  std::vector<std::string>{"mesh", "mesh_1", "mesh_1_1", "mesh_1_2"},
      "Unsigned source-byte ordering and Mesh fixed-child reservation survive USD authoring");
  auto oraclePath = path;
  oraclePath.replace_extension(".oracle.txt");
  std::ifstream oracle(oraclePath);
  std::string line;
  Require(static_cast<bool>(std::getline(oracle, line)) && line == "BLEND_SCENE_ORACLE 1", "Unit oracle header");
  Require(static_cast<bool>(std::getline(oracle, line)), "Unit oracle Blender version");
  double scale = 0;
  std::size_t objectCount = 0, meshCount = 0;
  Require(static_cast<bool>(oracle >> scale >> objectCount >> meshCount) &&
              std::abs(scale - expectedScale) <= expectedScale * 1e-7 &&
              scale == scene.metadata.sourceUnitScale && objectCount == scene.objects.size() && meshCount == 1,
      "Saved active Scene scale, not a label or unselected Scene, supplies normalization");
  UsdGeomXformCache cache;
  for (std::size_t record = 0; record < objectCount; ++record) {
    Marker(oracle, "OBJECT");
    std::string name, parent, data;
    bool hidden = false;
    Require(static_cast<bool>(oracle >> std::quoted(name) >> std::quoted(parent) >> std::quoted(data) >> hidden),
        "Unit oracle Object");
    const auto index = objects.at(name);
    const auto& object = scene.objects[index];
    Require(object.hiddenForRender == hidden &&
                object.parent == (parent.empty() ? std::optional<std::size_t>{} : objects.at(parent)) &&
                object.mesh == (data.empty() ? std::optional<std::size_t>{} : 0) &&
                (data.empty() || data == "UnitCube"),
        "Native parent, shared Mesh and visibility edges match Blender");
    const auto world = MeterMatrix(oracle, "WORLD", scale);
    const auto local = MeterMatrix(oracle, "LOCAL", scale);
    Compare(Transpose(object.worldTransform), world, 2e-5);
    const auto prim = stage->GetPrimAtPath(paths[index]);
    Compare(cache.GetLocalToWorldTransform(prim), world, 2e-5);
    Compare(Get<GfMatrix4d>(prim.GetAttribute(TfToken("xformOp:transform"))), local, 2e-5);
  }
  Marker(oracle, "MESH");
  std::string meshName;
  std::size_t points = 0, faces = 0, corners = 0, uvs = 0;
  Require(static_cast<bool>(oracle >> std::quoted(meshName) >> points >> faces >> corners >> uvs) &&
              meshName == "UnitCube" && points == 8 && faces == 6 && corners == 24 && uvs == 5,
      "Oracle cube domains");
  const auto& mesh = scene.meshes[0];
  Require(mesh.sourceName == meshName && mesh.points.size() == points &&
              mesh.faceVertexCounts.size() == faces && mesh.faceVertexIndices.size() == corners &&
              mesh.cornerNormals.size() == corners && mesh.uvMaps.size() == uvs,
      "Native cube domain shapes match Blender");
  for (const auto& point : mesh.points) {
    const auto expected = MeterPoint(ReadVector<3>(oracle, "POINT"), scale);
    CompareVector(point, expected);
    for (const auto value : point) {
      Require(std::abs(std::abs(value) - 0.5) <= 1e-7, "Every scale yields a one-meter centered cube");
    }
  }
  Marker(oracle, "COUNTS");
  for (const auto count : mesh.faceVertexCounts) {
    std::int32_t expected = 0;
    Require(static_cast<bool>(oracle >> expected) && count == expected, "Blender polygon counts");
  }
  Marker(oracle, "INDICES");
  for (const auto index : mesh.faceVertexIndices) {
    std::int32_t expected = 0;
    Require(static_cast<bool>(oracle >> expected) && index == expected, "Blender right-handed winding");
  }
  for (const auto& normal : mesh.cornerNormals) {
    CompareVector(normal, MeterPoint(ReadVector<3>(oracle, "NORMAL"), 1), 2e-5);
  }
  const std::map<std::string, std::string> uvIdentifiers{
      {"A/B", "A_B"}, {"A_B", "A_B_1"}, {"st", "st_1"}, {"Render Map", "st"}, {japanese, "UVMap"}};
  const auto cube = UsdGeomMesh(stage->GetPrimAtPath(SdfPath("/Asset/geo/Cube/mesh")));
  for (std::size_t uv = 0; uv < uvs; ++uv) {
    Marker(oracle, "UV");
    std::string name;
    bool active = false;
    Require(static_cast<bool>(oracle >> std::quoted(name) >> active), "Unit oracle UV");
    const auto& map = mesh.uvMaps[uv];
    Require(map.sourceName == name && map.activeRender == active && map.indices.size() == corners,
        "Saved UV names and active-render selection");
    const auto primvar = UsdGeomPrimvarsAPI(cube).GetPrimvar(TfToken(uvIdentifiers.at(name)));
    Require(primvar && primvar.GetAttr().GetDisplayName() == (name == uvIdentifiers.at(name) ? "" : name),
        "Fixture-backed UV collisions, fallback, st reservation and UTF-8 display");
    for (const auto index : map.indices) {
      Require(index >= 0 && static_cast<std::size_t>(index) < map.values.size() &&
                  map.values[static_cast<std::size_t>(index)] == ReadVector<2>(oracle, "VALUE"),
          "UV coordinates are independent of units and basis");
    }
  }
  Marker(oracle, "UNIT_SYSTEM");
  Require(static_cast<bool>(oracle >> line) && line == expectedSystem, "Saved presentation unit system");
  for (std::size_t record = 0; record < 4; ++record) {
    Marker(oracle, "WORLD_MESH");
    std::string name;
    std::size_t count = 0;
    Require(static_cast<bool>(oracle >> std::quoted(name) >> count) && count == points, "World cube oracle");
    const auto index = objects.at(name);
    const auto usdMesh = UsdGeomMesh(stage->GetPrimAtPath(paths[index].AppendChild(TfToken("mesh"))));
    const auto authoredPoints = Get<VtVec3fArray>(usdMesh.GetPointsAttr());
    const auto world = cache.GetLocalToWorldTransform(usdMesh.GetPrim());
    GfVec3d minimum, maximum, actualMinimum, actualMaximum;
    for (std::size_t point = 0; point < count; ++point) {
      const auto expected = MeterPoint(ReadVector<3>(oracle, "POINT"), scale);
      const auto actual = world.Transform(GfVec3d(authoredPoints[point]));
      CompareVector(actual, expected);
      if (point == 0) {
        minimum = maximum = expected;
        actualMinimum = actualMaximum = actual;
      } else {
        for (std::size_t axis = 0; axis < 3; ++axis) {
          minimum[axis] = std::min(minimum[axis], expected[axis]);
          maximum[axis] = std::max(maximum[axis], expected[axis]);
          actualMinimum[axis] = std::min(actualMinimum[axis], actual[axis]);
          actualMaximum[axis] = std::max(actualMaximum[axis], actual[axis]);
        }
      }
    }
    CompareVector(actualMaximum - actualMinimum, maximum - minimum);
    if (name == "Cube" || name == "Translated") {
      CompareVector(actualMaximum - actualMinimum, GfVec3d(1), 1e-7);
    }
    const auto extent = Get<VtVec3fArray>(usdMesh.GetExtentAttr());
    Require(extent.size() == 2, "Authored local extent shape");
    CompareVector(extent[0], GfVec3d(-0.5), 1e-7);
    CompareVector(extent[1], GfVec3d(0.5), 1e-7);
  }
  oracle >> std::ws;
  Require(oracle.eof(), "Unit oracle has no trailing records");
  Require(Text(layer) == Text(Take(blend::AuthorScene(Decode(path, false, true)))) &&
              Text(layer) == Text(Take(blend::AuthorScene(Decode(path, true, true)))),
      "Repeated and reversed native reads retain identical geometry, names and stage text");
  return stage;
}

void CompareUnitStages(const UsdStageRefPtr& reference, const UsdStageRefPtr& stage) {
  UsdGeomXformCache expectedCache, actualCache;
  for (const auto& prim : reference->Traverse()) {
    const auto actual = stage->GetPrimAtPath(prim.GetPath());
    Require(actual && actual.GetTypeName() == prim.GetTypeName() && Children(actual) == Children(prim),
        "Unit scale leaves identifiers, schemas and hierarchy unchanged");
    const auto mesh = UsdGeomMesh(prim);
    if (mesh) {
      const auto expectedPoints = Get<VtVec3fArray>(mesh.GetPointsAttr());
      const auto points = Get<VtVec3fArray>(UsdGeomMesh(actual).GetPointsAttr());
      Require(points.size() == expectedPoints.size(), "Equivalent cube point count");
      const auto expectedWorld = expectedCache.GetLocalToWorldTransform(prim);
      const auto actualWorld = actualCache.GetLocalToWorldTransform(actual);
      for (std::size_t index = 0; index < points.size(); ++index) {
        CompareVector(points[index], expectedPoints[index], 1e-7);
        CompareVector(actualWorld.Transform(GfVec3d(points[index])),
            expectedWorld.Transform(GfVec3d(expectedPoints[index])));
      }
    } else if (UsdGeomXform(prim)) {
      Compare(actualCache.GetLocalToWorldTransform(actual), expectedCache.GetLocalToWorldTransform(prim), 2e-5);
      if (prim.GetPath() != SdfPath("/Asset")) {
        Compare(Get<GfMatrix4d>(actual.GetAttribute(TfToken("xformOp:transform"))),
            Get<GfMatrix4d>(prim.GetAttribute(TfToken("xformOp:transform"))), 2e-5);
      }
    }
  }
}

void CheckUnitFixtures(const std::filesystem::path& first, const std::filesystem::path& second) {
  UsdStageRefPtr reference;
  for (const auto& directory : {first, second}) {
    for (const auto& [name, scale, system] :
        std::vector<std::tuple<std::string, double, std::string>>{
            {"unit-1m", 1, "NONE"}, {"unit-1cm", 0.01, "METRIC"},
            {"unit-1mm", 0.001, "IMPERIAL"}, {"unit-10m", 10, "METRIC"}}) {
      std::cout << "Checking " << directory.string() << " / " << name << '\n';
      const auto stage = CheckUnitFixture(directory / (name + ".blend"), scale, system);
      if (reference) {
        CompareUnitStages(reference, stage);
      } else {
        reference = stage;
      }
    }
  }
}

} // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 4 && std::string(argv[1]) == "--units") {
      CheckUnitFixtures(argv[2], argv[3]);
      std::cout << "Blender-written multi-scale native-to-USD unit and ASCII naming policies passed\n";
      return 0;
    }
    Require(argc == 8, "Expected transform, independent Mesh, integrated Scene and cube fixtures");
    CheckSynthetic();
    CheckFailures();
    for (int index = 1; index < argc; ++index) {
      CheckFixture(std::filesystem::path(argv[index]));
    }
    std::cout << "Scene IR USD authoring, diagnostics, determinism and native oracle checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
