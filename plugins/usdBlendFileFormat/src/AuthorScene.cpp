// SPDX-License-Identifier: Apache-2.0
#include "AuthorScene.h"

#include <blendScene/Naming.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/errorMark.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/nodeGraph.h>
#include <pxr/usd/usdShade/shader.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
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

[[noreturn]] void Fail(std::string code, std::string message, std::string datablock = {}) {
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
  if (!mesh.faceMaterialIndices.empty() &&
      mesh.faceMaterialIndices.size() != mesh.faceVertexCounts.size()) {
    Fail("BLEND_USD_MATERIAL_INVALID", "Face material indices must have one entry per polygon", mesh.sourceName);
  }
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

struct TextureNames {
  std::string prefix;
  std::string input;
  double fallback = 0;
};

TextureNames Names(const Material& material, TextureInput input) {
  switch (input) {
  case TextureInput::BaseColor:
    return {"BaseColor", "diffuseColor"};
  case TextureInput::Metallic:
    return {"Metallic", "metallic", material.metallic};
  case TextureInput::Roughness:
    return {"Roughness", "roughness", material.roughness};
  case TextureInput::Ior:
    return {"Ior", "ior", material.ior};
  case TextureInput::Clearcoat:
    return {"Clearcoat", "clearcoat", material.clearcoat};
  case TextureInput::ClearcoatRoughness:
    return {"ClearcoatRoughness", "clearcoatRoughness", material.clearcoatRoughness};
  case TextureInput::Normal:
    return {"Normal", "normal"};
  }
  Fail("BLEND_USD_MATERIAL_INVALID", "Texture has an unknown target input", material.sourceName);
}

TfToken ColorSpace(const MaterialTexture& texture, const std::string& name) {
  switch (texture.colorSpace) {
  case TextureColorSpace::SRgb:
    return TfToken("sRGB");
  case TextureColorSpace::Raw:
    return TfToken("raw");
  }
  Fail("BLEND_USD_MATERIAL_INVALID", "Texture has an unknown color space", name);
}

TfToken Wrap(const MaterialTexture& texture, const std::string& name) {
  switch (texture.wrap) {
  case TextureWrap::Repeat:
    return TfToken("repeat");
  case TextureWrap::Clamp:
    return TfToken("clamp");
  case TextureWrap::Black:
    return TfToken("black");
  case TextureWrap::Mirror:
    return TfToken("mirror");
  }
  Fail("BLEND_USD_MATERIAL_INVALID", "Texture has an unknown wrapping mode", name);
}

struct PreparedTexture {
  std::size_t index;
  std::string varname;
  SdfAssetPath assetPath;
};

std::optional<std::string> TextureUvIdentifier(const Scene& scene,
    std::size_t material, const std::string& uvMap,
    const std::vector<std::optional<PreparedMesh>>& meshes) {
  std::optional<std::string> identifier;
  for (const auto& object : scene.objects) {
    if (!object.mesh || std::find(object.materialSlots.begin(), object.materialSlots.end(), material) == object.materialSlots.end()) {
      continue;
    }
    const auto& mesh = scene.meshes[*object.mesh];
    const auto map = std::find_if(mesh.uvMaps.begin(), mesh.uvMaps.end(),
        [&](const auto& value) { return uvMap.empty() ? value.activeRender : value.sourceName == uvMap; });
    if (map == mesh.uvMaps.end()) {
      return {};
    }
    const auto& candidate = meshes[*object.mesh]->uvIdentifiers[static_cast<std::size_t>(map - mesh.uvMaps.begin())];
    if (identifier && *identifier != candidate) {
      return {};
    }
    identifier = candidate;
  }
  if (!identifier && uvMap.empty()) {
    return "st";
  }
  return identifier;
}

std::vector<PreparedTexture> PrepareTextures(const Scene& scene, std::size_t materialIndex,
    const std::vector<std::optional<PreparedMesh>>& meshes, std::vector<Diagnostic>& diagnostics) {
  const auto& material = scene.materials[materialIndex];
  std::set<TextureInput> inputs;
  std::vector<PreparedTexture> result;
  for (std::size_t index = 0; index < material.textures.size(); ++index) {
    const auto& texture = material.textures[index];
    Names(material, texture.input);
    ColorSpace(texture, material.sourceName);
    Wrap(texture, material.sourceName);
    if (!inputs.insert(texture.input).second || texture.assetPath.empty() ||
        texture.assetPath.find('\0') != std::string::npos ||
        texture.assetPath.find('\\') != std::string::npos ||
        (texture.input != TextureInput::Normal && !texture.normalUvMap.empty())) {
      Fail("BLEND_USD_MATERIAL_INVALID", "Texture inputs must be unique, paths nonempty/normalized and tangent UV selection normal-only", material.sourceName);
    }
    const SdfAssetPath assetPath(texture.assetPath);
    const auto varname = TextureUvIdentifier(scene, materialIndex, texture.uvMap, meshes);
    const auto normalVarname = texture.input == TextureInput::Normal
                                   ? TextureUvIdentifier(scene, materialIndex, texture.normalUvMap, meshes)
                                   : varname;
    if (!varname || !normalVarname || *varname != *normalVarname) {
      diagnostics.push_back({"BLEND_MATERIAL_UV_UNSUPPORTED", Severity::Unsupported,
          "Texture and tangent UVs must exist and map to one consistent primvar on every bound Mesh; the constant fallback is used",
          {}, {}, material.sourceName, true});
      continue;
    }
    result.push_back({index, *varname, assetPath});
  }
  std::sort(result.begin(), result.end(), [&](const auto& left, const auto& right) {
    return material.textures[left.index].input < material.textures[right.index].input;
  });
  return result;
}

void AuthorTexture(const UsdStageRefPtr& stage, const UsdShadeNodeGraph& preview,
    UsdShadeShader surface, const Material& material,
    const PreparedTexture& prepared, bool metadataOnly) {
  const auto& texture = material.textures[prepared.index];
  const auto names = Names(material, texture.input);
  const auto readerName = prepared.varname == "st" ? "StReader" : names.prefix + "StReader";
  auto reader = UsdShadeShader::Define(stage, preview.GetPath().AppendChild(TfToken(readerName)));
  auto shader = UsdShadeShader::Define(stage, preview.GetPath().AppendChild(TfToken(names.prefix + "Texture")));
  Check(static_cast<bool>(reader));
  Check(static_cast<bool>(shader));
  if (metadataOnly) {
    return;
  }
  Check(reader.CreateIdAttr().Set(TfToken("UsdPrimvarReader_float2")));
  Check(reader.CreateInput(TfToken("varname"), SdfValueTypeNames->String).Set(prepared.varname));
  const auto coordinates = reader.CreateOutput(TfToken("result"), SdfValueTypeNames->Float2);
  Check(shader.CreateIdAttr().Set(TfToken("UsdUVTexture")));
  Check(shader.CreateInput(TfToken("file"), SdfValueTypeNames->Asset).Set(prepared.assetPath));
  Check(shader.CreateInput(TfToken("st"), SdfValueTypeNames->Float2).ConnectToSource(coordinates));
  Check(shader.CreateInput(TfToken("sourceColorSpace"), SdfValueTypeNames->Token).Set(ColorSpace(texture, material.sourceName)));
  for (const auto axis : {"wrapS", "wrapT"}) {
    Check(shader.CreateInput(TfToken(axis), SdfValueTypeNames->Token).Set(Wrap(texture, material.sourceName)));
  }
  const auto normal = texture.input == TextureInput::Normal;
  const auto color = texture.input == TextureInput::BaseColor;
  GfVec4f fallback(FloatValue(names.fallback, material.sourceName));
  if (color) {
    fallback = GfVec4f(FloatValue(material.diffuseColor[0], material.sourceName),
        FloatValue(material.diffuseColor[1], material.sourceName), FloatValue(material.diffuseColor[2], material.sourceName), 1);
  } else if (normal) {
    fallback = GfVec4f(0.5f, 0.5f, 1, 1);
    Check(shader.CreateInput(TfToken("scale"), SdfValueTypeNames->Float4).Set(GfVec4f(2, 2, 2, 1)));
    Check(shader.CreateInput(TfToken("bias"), SdfValueTypeNames->Float4).Set(GfVec4f(-1, -1, -1, 0)));
  }
  Check(shader.CreateInput(TfToken("fallback"), SdfValueTypeNames->Float4).Set(fallback));
  const auto output = shader.CreateOutput(TfToken(color || normal ? "rgb" : "a"),
      color || normal ? SdfValueTypeNames->Float3 : SdfValueTypeNames->Float);
  const auto input = surface.CreateInput(TfToken(names.input),
      normal ? SdfValueTypeNames->Normal3f : color ? SdfValueTypeNames->Color3f
                                                   : SdfValueTypeNames->Float);
  Check(input.ConnectToSource(output));
}

UsdShadeMaterial AuthorMaterial(const UsdStageRefPtr& stage, const SdfPath& path,
    const Material& source, const std::string& identifier,
    const std::vector<PreparedTexture>& textures, bool metadataOnly) {
  const auto material = UsdShadeMaterial::Define(stage, path);
  Check(static_cast<bool>(material));
  Provenance(material.GetPrim(), source.sourceName, identifier);
  const auto preview = UsdShadeNodeGraph::Define(stage, path.AppendChild(TfToken("preview")));
  auto surface = UsdShadeShader::Define(stage, preview.GetPath().AppendChild(TfToken("Surface")));
  Check(static_cast<bool>(preview));
  Check(static_cast<bool>(surface));
  if (metadataOnly) {
    for (const auto& texture : textures) {
      AuthorTexture(stage, preview, surface, source, texture, true);
    }
    return material;
  }
  Check(surface.CreateIdAttr().Set(TfToken("UsdPreviewSurface")));
  const auto shaderOutput = surface.CreateOutput(TfToken("surface"), SdfValueTypeNames->Token);
  const auto graphOutput = preview.CreateOutput(TfToken("surface"), SdfValueTypeNames->Token);
  Check(graphOutput.ConnectToSource(shaderOutput));
  Check(material.CreateSurfaceOutput().ConnectToSource(graphOutput));
  if (!metadataOnly) {
    Check(surface.CreateInput(TfToken("diffuseColor"), SdfValueTypeNames->Color3f).Set(GfVec3f(FloatValue(source.diffuseColor[0], source.sourceName), FloatValue(source.diffuseColor[1], source.sourceName), FloatValue(source.diffuseColor[2], source.sourceName))));
    for (const auto& [name, value] : {
             std::pair{"metallic", source.metallic}, {"roughness", source.roughness},
             {"ior", source.ior}, {"clearcoat", source.clearcoat},
             {"clearcoatRoughness", source.clearcoatRoughness}}) {
      Check(surface.CreateInput(TfToken(name), SdfValueTypeNames->Float).Set(FloatValue(value, source.sourceName)));
    }
    Check(surface.CreateInput(TfToken("useSpecularWorkflow"), SdfValueTypeNames->Int).Set(0));
  }
  for (const auto& texture : textures) {
    AuthorTexture(stage, preview, surface, source, texture, false);
  }
  return material;
}

void AuthorBindings(const UsdStageRefPtr& stage, const SdfPath& meshPath,
    const Object& object, const Mesh& mesh, const std::vector<UsdShadeMaterial>& materials,
    bool metadataOnly, std::vector<Diagnostic>& diagnostics) {
  for (const auto slot : object.materialSlots) {
    if (slot && *slot >= materials.size()) {
      Fail("BLEND_USD_MATERIAL_INVALID", "Effective material slots must reference a valid Material", object.sourceName);
    }
  }
  std::map<std::size_t, VtIntArray> faces;
  bool invalid = false;
  bool empty = false;
  for (std::size_t face = 0; face < mesh.faceVertexCounts.size(); ++face) {
    const auto slot = mesh.faceMaterialIndices.empty() ? 0 : mesh.faceMaterialIndices[face];
    if (slot < 0 || static_cast<std::size_t>(slot) >= object.materialSlots.size()) {
      invalid = invalid || !object.materialSlots.empty() || slot != 0;
    } else if (!object.materialSlots[static_cast<std::size_t>(slot)]) {
      empty = true;
    } else {
      if (face > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        Fail("BLEND_USD_MATERIAL_INVALID", "Material subset face indices exceed the USD int range", object.sourceName);
      }
      faces[static_cast<std::size_t>(slot)].push_back(static_cast<int>(face));
    }
  }
  if (invalid) {
    diagnostics.push_back({"BLEND_MATERIAL_SLOT_INVALID", Severity::Unsupported,
        "Faces outside the effective material slot range are left unbound", {}, {}, object.sourceName, true});
  }
  if (empty) {
    diagnostics.push_back({"BLEND_MATERIAL_SLOT_EMPTY", Severity::Unsupported,
        "Faces using empty material slots are left unbound", {}, {}, object.sourceName, true});
  }
  if (object.materialSlots.empty()) {
    return;
  }
  if (metadataOnly) {
    if (object.materialSlots.size() != 1 || !object.materialSlots[0] || invalid) {
      for (const auto& [slot, indices] : faces) {
        Check(static_cast<bool>(UsdGeomSubset::Define(stage,
            meshPath.AppendChild(TfToken("material_" + std::to_string(slot))))));
      }
    }
    return;
  }
  auto api = UsdShadeMaterialBindingAPI::Apply(stage->GetPrimAtPath(meshPath));
  Check(static_cast<bool>(api));
  if (object.materialSlots.size() == 1 && object.materialSlots[0] && !invalid) {
    Check(api.Bind(materials[*object.materialSlots[0]]));
  } else {
    for (const auto& [slot, indices] : faces) {
      const auto subset = api.CreateMaterialBindSubset(TfToken("material_" + std::to_string(slot)), indices);
      Check(static_cast<bool>(subset));
      Check(UsdShadeMaterialBindingAPI::Apply(subset.GetPrim()).Bind(materials[*object.materialSlots[slot]]));
    }
    Check(api.SetMaterialBindSubsetsFamilyType(UsdGeomTokens->nonOverlapping));
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
    const auto materialIdentifiers = Take(MaterialIdentifiers(scene), diagnostics);
    for (const auto& material : scene.materials) {
      for (const auto value : {material.diffuseColor[0], material.diffuseColor[1], material.diffuseColor[2],
               material.metallic, material.roughness, material.ior, material.clearcoat, material.clearcoatRoughness}) {
        FloatValue(value, material.sourceName);
      }
    }
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
    std::vector<std::vector<PreparedTexture>> textures(scene.materials.size());
    for (std::size_t index = 0; index < scene.materials.size(); ++index) {
      textures[index] = PrepareTextures(scene, index, meshes, diagnostics);
    }
    const auto stage = Take(CreateAssetStage(scene.metadata.sourceVersion, scene.metadata.sourceScene), diagnostics);
    std::vector<std::size_t> materialOrder(scene.materials.size());
    std::iota(materialOrder.begin(), materialOrder.end(), 0);
    std::sort(materialOrder.begin(), materialOrder.end(), [&](auto left, auto right) {
      return SourceNameLess(scene.materials[left].sourceName, scene.materials[right].sourceName);
    });
    std::vector<UsdShadeMaterial> materials(scene.materials.size());
    for (const auto index : materialOrder) {
      materials[index] = AuthorMaterial(stage,
          SdfPath("/Asset/mtl").AppendChild(TfToken(materialIdentifiers[index])),
          scene.materials[index], materialIdentifiers[index], textures[index], metadataOnly);
    }
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
        const auto meshPath = paths[index].AppendChild(pxr::TfToken("mesh"));
        AuthorMesh(stage, meshPath,
            scene.meshes[*object.mesh], *meshes[*object.mesh], metadataOnly);
        AuthorBindings(stage, meshPath, object, scene.meshes[*object.mesh],
            materials, metadataOnly, diagnostics);
      }
    }
    return Result<pxr::SdfLayerRefPtr>(stage->GetRootLayer(), std::move(diagnostics));
  });
}

} // namespace blend
