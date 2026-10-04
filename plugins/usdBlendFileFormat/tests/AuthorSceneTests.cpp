// SPDX-License-Identifier: Apache-2.0
#include "AuthorScene.h"
#include <blendScene/Decode.h>
#include <blendScene/Naming.h>
#include <pxr/base/gf/matrix4d.h>
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
#include <numeric>
#include <stdexcept>

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

blend::Scene Decode(const std::filesystem::path& path, bool reverseBlocks = false) {
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
  return Take(blend::DecodeScene(bytes, blocks, schema, header, {10000, 64}));
}

void CheckFixture(const std::filesystem::path& path) {
  const auto scene = Decode(path);
  const auto layer = Take(blend::AuthorScene(scene));
  const auto stage = UsdStage::Open(layer);
  CheckScene(scene, stage);
  Require(Text(layer) == Text(Take(blend::AuthorScene(Decode(path)))), "Repeated native reads author identical stages");
  Require(Text(layer) == Text(Take(blend::AuthorScene(Decode(path, true)))),
      "Reversed native block enumeration authors an identical stage");
  if (path.stem() != "transforms") {
    Require(scene.objects.size() == 2 && scene.meshes.size() == 2, "Independent two-Mesh native fixture");
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

} // namespace

int main(int argc, char** argv) {
  try {
    Require(argc == 5, "Expected two transform and two independent Mesh fixtures");
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
