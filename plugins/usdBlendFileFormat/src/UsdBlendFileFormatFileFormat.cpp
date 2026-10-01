// SPDX-License-Identifier: Apache-2.0
#include "UsdBlendFileFormatFileFormat.h"
#include "ArAssetByteSource.h"

#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/type.h"
#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/ar/resolvedPath.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdGeom/scope.h"
#include "pxr/usd/usdGeom/xform.h"

#include <exception>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(UsdBlendFileFormatFileFormatTokens, USDBLENDFILEFORMAT_FILE_FORMAT_TOKENS);

// Register the format with USD's type system so the plug system can find it.
TF_REGISTRY_FUNCTION(TfType) {
  SDF_DEFINE_FILE_FORMAT(UsdBlendFileFormatFileFormat, SdfFileFormat);
}

UsdBlendFileFormatFileFormat::UsdBlendFileFormatFileFormat()
    : SdfFileFormat(
          UsdBlendFileFormatFileFormatTokens->Id,
          UsdBlendFileFormatFileFormatTokens->Version,
          UsdBlendFileFormatFileFormatTokens->Target,
          UsdBlendFileFormatFileFormatTokens->Extension) {
}

UsdBlendFileFormatFileFormat::~UsdBlendFileFormatFileFormat() = default;

bool UsdBlendFileFormatFileFormat::CanRead(const std::string& file) const {
  try {
    auto& resolver = ArGetResolver();
    auto asset = resolver.OpenAsset(resolver.Resolve(file));
    if (!asset) {
      return false;
    }
    ArAssetByteSource source(std::move(asset));
    return blend::ReadHeader(source).HasValue();
  } catch (const std::exception&) {
    return false;
  }
}

bool UsdBlendFileFormatFileFormat::Read(
    SdfLayer* layer,
    const std::string& resolvedPath,
    bool metadataOnly) const {
  (void)metadataOnly;

  try {
    auto asset = ArGetResolver().OpenAsset(ArResolvedPath(resolvedPath));
    if (!asset) {
      TF_RUNTIME_ERROR("BLEND_HEADER_OPEN_FAILED: Could not open '%s'", resolvedPath.c_str());
      return false;
    }
    ArAssetByteSource source(std::move(asset));
    const auto header = blend::ReadHeader(source);
    if (!header.HasValue()) {
      const auto& error = header.GetError();
      TF_RUNTIME_ERROR("%s: %s", error.code.c_str(), error.message.c_str());
      return false;
    }
    const auto stage = UsdStage::CreateInMemory("usdBlendFileFormat.generated.usda");
    if (!stage) {
      TF_RUNTIME_ERROR("BLEND_USD_AUTHORING_FAILED: Could not create a stage");
      return false;
    }
    const auto root = UsdGeomXform::Define(stage, SdfPath("/Asset")).GetPrim();
    root.SetMetadata(TfToken("kind"), VtValue(TfToken("component")));
    root.SetCustomDataByKey(TfToken("blend:stageContractVersion"), VtValue(1));
    root.SetCustomDataByKey(TfToken("blend:sourceVersion"), VtValue(header.GetValue().SourceVersion()));
    stage->SetDefaultPrim(root);
    UsdGeomScope::Define(stage, SdfPath("/Asset/geo"));
    UsdGeomScope::Define(stage, SdfPath("/Asset/mtl"));
    UsdGeomSetStageUpAxis(stage, UsdGeomTokens->y);
    UsdGeomSetStageMetersPerUnit(stage, 1.0);
    layer->TransferContent(stage->GetRootLayer());
    return true;
  } catch (const std::exception& error) {
    TF_RUNTIME_ERROR("BLEND_USD_READ_FAILED: %s", error.what());
    return false;
  }
}

bool UsdBlendFileFormatFileFormat::WriteToString(
    const SdfLayer& layer,
    std::string* str,
    const std::string& comment) const {
  SdfFileFormatConstPtr usda = SdfFileFormat::FindByExtension("usda");
  if (usda) {
    return usda->WriteToString(layer, str, comment);
  }
  return layer.ExportToString(str);
}

PXR_NAMESPACE_CLOSE_SCOPE
