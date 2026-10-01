#include "ArAssetByteSource.h"

#include <limits>

bool ArAssetByteSource::Read(std::uint64_t offset, std::span<std::byte> destination) {
    const auto size = Size();
    if (offset > size || destination.size() > size - offset ||
        offset > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    return destination.empty() || asset_->Read(destination.data(), destination.size(),
        static_cast<std::size_t>(offset)) == destination.size();
}