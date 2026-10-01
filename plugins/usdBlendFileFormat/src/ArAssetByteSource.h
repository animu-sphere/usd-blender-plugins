#pragma once

#include "blend/BlendFile.h"
#include "pxr/usd/ar/asset.h"

#include <memory>

class ArAssetByteSource final : public blend::ByteSource {
public:
    explicit ArAssetByteSource(std::shared_ptr<PXR_NS::ArAsset> asset) : asset_(std::move(asset)) {}
    std::uint64_t Size() const override { return asset_->GetSize(); }
    bool Read(std::uint64_t offset, std::span<std::byte> destination) override;

private:
    std::shared_ptr<PXR_NS::ArAsset> asset_;
};