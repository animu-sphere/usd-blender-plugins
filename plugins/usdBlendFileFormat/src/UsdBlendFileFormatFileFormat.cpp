// SPDX-License-Identifier: Apache-2.0
#include "UsdBlendFileFormatFileFormat.h"
#include "ArAssetByteSource.h"
#include "AuthorScene.h"
#include "ReadScene.h"

#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/type.h"
#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/ar/resolvedPath.h"
#include "pxr/usd/sdf/layer.h"

#include <exception>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

void Report(const blend::Diagnostic& diagnostic) {
  std::string message = diagnostic.message;
  if (diagnostic.byteOffset) {
    message += " [byte " + std::to_string(*diagnostic.byteOffset) + "]";
  }
  if (diagnostic.blockIndex) {
    message += " [block " + std::to_string(*diagnostic.blockIndex) + "]";
  }
  if (!diagnostic.datablock.empty()) {
    message += " [datablock " + diagnostic.datablock + "]";
  }
  if (diagnostic.severity == blend::Severity::Fatal) {
    TF_RUNTIME_ERROR("%s: %s", diagnostic.code.c_str(), message.c_str());
  } else {
    TF_WARN("%s: %s", diagnostic.code.c_str(), message.c_str());
  }
}

template <class Value>
bool Check(const blend::Result<Value>& result) {
  if (!result.HasValue()) {
    Report(result.GetError());
    return false;
  }
  for (const auto& diagnostic : result.Diagnostics()) {
    Report(diagnostic);
  }
  return true;
}

} // namespace

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
  try {
    auto asset = ArGetResolver().OpenAsset(ArResolvedPath(resolvedPath));
    if (!asset) {
      TF_RUNTIME_ERROR("BLEND_HEADER_OPEN_FAILED: Could not open '%s'", resolvedPath.c_str());
      return false;
    }
    ArAssetByteSource source(std::move(asset));
    const auto scene = blend::ReadScene(source);
    if (!Check(scene)) {
      return false;
    }
    const auto authored = blend::AuthorScene(scene.GetValue(), metadataOnly);
    if (!Check(authored)) {
      return false;
    }
    layer->TransferContent(authored.GetValue());
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
