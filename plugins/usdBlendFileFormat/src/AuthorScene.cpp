// SPDX-License-Identifier: Apache-2.0
#include "AuthorScene.h"

#include <blendScene/Naming.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/errorMark.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/xform.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace blend {
namespace {

using namespace pxr;

struct AuthoringFailure : std::runtime_error {
  Diagnostic diagnostic;
  explicit AuthoringFailure(Diagnostic value)
      : std::runtime_error(value.message), diagnostic(std::move(value)) {
  }
};

void Fail(std::string code, std::string message, std::string datablock = {}) {
  throw AuthoringFailure({std::move(code), Severity::Fatal, std::move(message),
      {}, {}, std::move(datablock), false});
}

void Check(bool success) {
  if (!success) {
    Fail("BLEND_USD_AUTHORING_FAILED", "OpenUSD could not author the requested value");
  }
}

template <class Value>
Value Take(const Result<Value>& result, std::vector<Diagnostic>& diagnostics) {
  if (!result.HasValue()) {
    throw AuthoringFailure(result.GetError());
  }
  diagnostics.insert(diagnostics.end(), result.Diagnostics().begin(), result.Diagnostics().end());
  return result.GetValue();
}

template <class Value, class Function>
Result<Value> TryAuthor(Function function) {
  TfErrorMark mark;
  try {
    auto result = function();
    if (!mark.IsClean()) {
      std::string message;
      for (const auto& error : mark) {
        message += error.GetCommentary() + "\n";
      }
      mark.Clear();
      return Result<Value>(Diagnostic{"BLEND_USD_AUTHORING_FAILED", Severity::Fatal,
          std::move(message), {}, {}, {}, false});
    }
    return result;
  } catch (const AuthoringFailure& error) {
    mark.Clear();
    return Result<Value>(error.diagnostic);
  } catch (const std::bad_alloc& error) {
    mark.Clear();
    return Result<Value>(Diagnostic{"BLEND_USD_ALLOCATION", Severity::Fatal,
        error.what(), {}, {}, {}, false});
  } catch (const std::length_error& error) {
    mark.Clear();
    return Result<Value>(Diagnostic{"BLEND_USD_ALLOCATION", Severity::Fatal,
        error.what(), {}, {}, {}, false});
  }
}

float FloatValue(double value, const std::string& name) {
  if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
    Fail("BLEND_USD_VALUE_INVALID", "A float-valued USD attribute requires finite float-range values", name);
  }
  return static_cast<float>(value);
}

VtVec3fArray Vectors(const std::vector<Vector3>& values, const std::string& name) {
  VtVec3fArray result;
  result.reserve(values.size());
  for (const auto& value : values) {
    result.push_back(GfVec3f(FloatValue(value[0], name), FloatValue(value[1], name), FloatValue(value[2], name)));
  }
  return result;
}

template <class ObjectType>
void Provenance(const ObjectType& object, const std::string& sourceName, const std::string& identifier) {
  object.SetCustomDataByKey(TfToken("blend:sourceName"), VtValue(sourceName));
  const auto display = NameForDisplay(sourceName);
  if (!display.HasValue()) {
    throw AuthoringFailure(display.GetError());
  }
  if (display.GetValue() != identifier) {
    Check(object.SetDisplayName(display.GetValue()));
  }
}

struct PreparedMesh {
  VtVec3fArray points;
  VtVec3fArray normals;
  VtVec3fArray extent;
  std::vector<std::string> uvIdentifiers;
};

PreparedMesh PrepareMesh(const Mesh& mesh, std::vector<Diagnostic>& diagnostics) {
  std::size_t corners = 0;
  for (const auto count : mesh.faceVertexCounts) {
    if (count < 3 || static_cast<std::size_t>(count) > mesh.faceVertexIndices.size() - corners) {
      Fail("BLEND_USD_TOPOLOGY_INVALID", "Polygon counts must be at least three and fit the corner array", mesh.sourceName);
    }
    corners += static_cast<std::size_t>(count);
  }
  if (corners != mesh.faceVertexIndices.size() || corners != mesh.cornerNormals.size()) {
    Fail("BLEND_USD_TOPOLOGY_INVALID", "Counts, corner indices and face-varying normals must agree", mesh.sourceName);
  }
  for (const auto index : mesh.faceVertexIndices) {
    if (index < 0 || static_cast<std::size_t>(index) >= mesh.points.size()) {
      Fail("BLEND_USD_TOPOLOGY_INVALID", "A corner index is outside the point array", mesh.sourceName);
    }
  }
  PreparedMesh result;
  result.points = Vectors(mesh.points, mesh.sourceName);
  result.normals = Vectors(mesh.cornerNormals, mesh.sourceName);
  if (!result.points.empty()) {
    auto minimum = result.points.front();
    auto maximum = minimum;
    for (const auto& point : result.points) {
      for (std::size_t axis = 0; axis < 3; ++axis) {
        minimum[axis] = std::min(minimum[axis], point[axis]);
        maximum[axis] = std::max(maximum[axis], point[axis]);
      }
    }
    result.extent = {minimum, maximum};
  }
  Take(NameForDisplay(mesh.sourceName), diagnostics);
  result.uvIdentifiers = Take(UvIdentifiers(mesh), diagnostics);
  for (const auto& map : mesh.uvMaps) {
    if (map.indices.size() != corners) {
      Fail("BLEND_USD_UV_INVALID", "Indexed face-varying UVs must have one index per corner", mesh.sourceName);
    }
    for (const auto index : map.indices) {
      if (index < 0 || static_cast<std::size_t>(index) >= map.values.size()) {
        Fail("BLEND_USD_UV_INVALID", "A UV index is outside its value array", mesh.sourceName);
      }
    }
    for (const auto& value : map.values) {
      FloatValue(value[0], mesh.sourceName);
      FloatValue(value[1], mesh.sourceName);
    }
  }
  if (mesh.faceVertexCounts.empty()) {
    diagnostics.push_back({"BLEND_MESH_EMPTY", Severity::Warning,
        "A Mesh without polygons is authored with its points and empty topology",
        {}, {}, mesh.sourceName, true});
  }
  return result;
}

void AuthorMesh(const UsdStageRefPtr& stage, const SdfPath& path,
    const Mesh& source, const PreparedMesh& prepared, bool metadataOnly) {
  auto mesh = UsdGeomMesh::Define(stage, path);
  Check(static_cast<bool>(mesh));
  Provenance(mesh.GetPrim(), source.sourceName, "mesh");
  if (metadataOnly) {
    return;
  }
  Check(mesh.CreatePointsAttr().Set(prepared.points));
  Check(mesh.CreateFaceVertexCountsAttr().Set(VtIntArray(source.faceVertexCounts.begin(), source.faceVertexCounts.end())));
  Check(mesh.CreateFaceVertexIndicesAttr().Set(VtIntArray(source.faceVertexIndices.begin(), source.faceVertexIndices.end())));
  Check(mesh.CreateNormalsAttr().Set(prepared.normals));
  Check(mesh.SetNormalsInterpolation(UsdGeomTokens->faceVarying));
  Check(mesh.CreateExtentAttr().Set(prepared.extent));
  Check(mesh.CreateOrientationAttr().Set(UsdGeomTokens->rightHanded));
  Check(mesh.CreateSubdivisionSchemeAttr().Set(UsdGeomTokens->none));
  std::vector<std::size_t> order(source.uvMaps.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](auto left, auto right) {
    return SourceNameLess(source.uvMaps[left].sourceName, source.uvMaps[right].sourceName);
  });
  const UsdGeomPrimvarsAPI api(mesh);
  for (const auto index : order) {
    const auto& map = source.uvMaps[index];
    const auto& identifier = prepared.uvIdentifiers[index];
    const auto primvar = api.CreatePrimvar(TfToken(identifier), SdfValueTypeNames->TexCoord2fArray,
        UsdGeomTokens->faceVarying);
    Check(static_cast<bool>(primvar));
    VtVec2fArray values;
    values.reserve(map.values.size());
    for (const auto& value : map.values) {
      values.push_back(GfVec2f(FloatValue(value[0], source.sourceName), FloatValue(value[1], source.sourceName)));
    }
    Check(primvar.Set(values));
    Check(primvar.SetIndices(VtIntArray(map.indices.begin(), map.indices.end())));
    Provenance(primvar.GetAttr(), map.sourceName, identifier);
  }
}

GfMatrix4d LocalTransform(const Scene& scene, const Object& object) {
  Matrix4 local;
  try {
    local = ParentRelativeTransform(object.worldTransform,
        object.parent ? scene.objects[*object.parent].worldTransform : IdentityMatrix);
  } catch (const std::invalid_argument& error) {
    const std::string message = error.what();
    Fail(message.starts_with("BLEND_SCENE_TRANSFORM_SINGULAR")
             ? "BLEND_SCENE_TRANSFORM_SINGULAR"
             : "BLEND_SCENE_TRANSFORM_INVALID",
        message, object.sourceName);
  } catch (const std::overflow_error& error) {
    Fail("BLEND_SCENE_TRANSFORM_INVALID", error.what(), object.sourceName);
  }
  GfMatrix4d result(1);
  for (std::size_t row = 0; row < 4; ++row) {
    for (std::size_t column = 0; column < 4; ++column) {
      result[row][column] = local[column][row];
    }
  }
  return result;
}

} // namespace

Result<pxr::UsdStageRefPtr> CreateAssetStage(
    std::string_view sourceVersion, std::optional<std::string_view> sourceScene) {
  return TryAuthor<pxr::UsdStageRefPtr>([&] {
    std::vector<Diagnostic> diagnostics;
    const auto stage = pxr::UsdStage::CreateInMemory("usdBlendFileFormat.generated.usda");
    Check(static_cast<bool>(stage));
    const auto root = pxr::UsdGeomXform::Define(stage, pxr::SdfPath("/Asset")).GetPrim();
    Check(static_cast<bool>(root));
    Check(root.SetMetadata(pxr::TfToken("kind"), pxr::VtValue(pxr::TfToken("component"))));
    root.SetCustomDataByKey(pxr::TfToken("blend:stageContractVersion"), pxr::VtValue(1));
    root.SetCustomDataByKey(pxr::TfToken("blend:sourceVersion"), pxr::VtValue(std::string(sourceVersion)));
    if (sourceScene) {
      Take(NameForDisplay(*sourceScene), diagnostics);
      root.SetCustomDataByKey(pxr::TfToken("blend:sourceScene"), pxr::VtValue(std::string(*sourceScene)));
    }
    stage->SetDefaultPrim(root);
    Check(static_cast<bool>(pxr::UsdGeomScope::Define(stage, pxr::SdfPath("/Asset/geo"))));
    Check(static_cast<bool>(pxr::UsdGeomScope::Define(stage, pxr::SdfPath("/Asset/mtl"))));
    Check(pxr::UsdGeomSetStageUpAxis(stage, pxr::UsdGeomTokens->y));
    Check(pxr::UsdGeomSetStageMetersPerUnit(stage, 1));
    return Result<pxr::UsdStageRefPtr>(stage, std::move(diagnostics));
  });
}

Result<pxr::SdfLayerRefPtr> AuthorScene(const Scene& scene, bool metadataOnly) {
  return TryAuthor<pxr::SdfLayerRefPtr>([&] {
    std::vector<Diagnostic> diagnostics;
    const auto identifiers = Take(ObjectIdentifiers(scene), diagnostics);
    std::vector<std::vector<std::size_t>> children(scene.objects.size() + 1);
    for (std::size_t index = 0; index < scene.objects.size(); ++index) {
      const auto& object = scene.objects[index];
      if (object.identifier != identifiers[index]) {
        Fail("BLEND_USD_IDENTIFIER_INVALID", "Object identifiers must match the native naming contract", object.sourceName);
      }
      children[object.parent.value_or(scene.objects.size())].push_back(index);
    }
    for (auto& siblings : children) {
      std::sort(siblings.begin(), siblings.end(), [&](auto left, auto right) {
        return SourceNameLess(scene.objects[left].sourceName, scene.objects[right].sourceName);
      });
    }
    std::vector<std::size_t> order;
    std::vector<std::size_t> pending;
    const auto append = [&](const auto& siblings) {
      pending.insert(pending.end(), siblings.rbegin(), siblings.rend());
    };
    append(children.back());
    while (!pending.empty()) {
      const auto index = pending.back();
      pending.pop_back();
      order.push_back(index);
      append(children[index]);
    }
    if (order.size() != scene.objects.size()) {
      Fail("BLEND_SCENE_CYCLE", "Object parent indices contain a cycle");
    }
    std::vector<pxr::GfMatrix4d> locals(scene.objects.size());
    std::vector<std::optional<PreparedMesh>> meshes(scene.meshes.size());
    for (const auto index : order) {
      const auto& object = scene.objects[index];
      locals[index] = LocalTransform(scene, object);
      if (object.mesh && !meshes[*object.mesh]) {
        meshes[*object.mesh] = PrepareMesh(scene.meshes[*object.mesh], diagnostics);
      }
    }
    const auto stage = Take(CreateAssetStage(scene.metadata.sourceVersion, scene.metadata.sourceScene), diagnostics);
    std::vector<pxr::SdfPath> paths(scene.objects.size());
    for (const auto index : order) {
      const auto& object = scene.objects[index];
      const auto parent = object.parent ? paths[*object.parent] : pxr::SdfPath("/Asset/geo");
      paths[index] = parent.AppendChild(pxr::TfToken(object.identifier));
      const auto xform = pxr::UsdGeomXform::Define(stage, paths[index]);
      Check(static_cast<bool>(xform));
      Provenance(xform.GetPrim(), object.sourceName, object.identifier);
      Check(xform.AddTransformOp().Set(locals[index]));
      Check(xform.CreateVisibilityAttr().Set(
          object.hiddenForRender ? pxr::UsdGeomTokens->invisible : pxr::UsdGeomTokens->inherited));
      if (object.mesh) {
        AuthorMesh(stage, paths[index].AppendChild(pxr::TfToken("mesh")),
            scene.meshes[*object.mesh], *meshes[*object.mesh], metadataOnly);
      }
    }
    return Result<pxr::SdfLayerRefPtr>(stage->GetRootLayer(), std::move(diagnostics));
  });
}

} // namespace blend
