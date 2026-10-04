#include "ScenePointers.h"
#include "DecodeInternal.h"

#include <algorithm>
#include <numeric>
#include <set>
#include <stdexcept>

namespace blend::detail {

Result<ScenePointers> BuildScenePointers(std::span<const std::byte> bytes,
    std::span<const BlendBlock> blocks, const DnaSchema& schema, const Header& header) {
  try {
    const auto strict = BuildPointerMap(blocks);
    ScenePointers result;
    if (strict.HasValue()) {
      result.global_ = strict.GetValue();
      return Result<ScenePointers>(std::move(result));
    }
    if (strict.GetError().code != "BLEND_POINTER_DUPLICATE" ||
        header.version < 500 || header.version >= 600 ||
        header.containerVersion != BlendContainerVersion::Blender5) {
      return Result<ScenePointers>(strict.GetError());
    }
    const auto reject = [&]() -> void { throw strict.GetError(); };
    std::vector<std::uint32_t> order(blocks.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(),
        [&](auto a, auto b) { return blocks[a].offset < blocks[b].offset; });
    std::vector<std::optional<std::uint32_t>> owners(blocks.size());
    std::optional<std::uint32_t> owner;
    std::map<std::uint64_t, std::vector<std::uint32_t>> addresses;
    const auto isId = [](const BlendBlock& block) {
      return block.code[0] >= 'A' && block.code[0] <= 'Z' &&
             block.code[1] >= 'A' && block.code[1] <= 'Z' &&
             block.code[2] == 0 && block.code[3] == 0;
    };
    for (const auto index : order) {
      const auto& block = blocks[index];
      if (isId(block)) {
        owner = index;
      } else if (block.code != std::array<char, 4>{'D', 'A', 'T', 'A'}) {
        owner.reset();
      }
      owners[index] = owner;
      if (block.oldAddress != 0 &&
          (block.code == std::array<char, 4>{'D', 'A', 'T', 'A'} || isId(block))) {
        addresses[block.oldAddress].push_back(index);
      }
    }
    auto unique = std::vector<BlendBlock>(blocks.begin(), blocks.end());
    std::set<std::uint32_t> collided;
    for (const auto& [address, indices] : addresses) {
      if (indices.size() < 2) {
        continue;
      }
      std::optional<std::string> type;
      for (const auto index : indices) {
        const auto& block = blocks[index];
        if (block.code != std::array<char, 4>{'D', 'A', 'T', 'A'} || !owners[index] ||
            blocks[*owners[index]].code != std::array<char, 4>{'M', 'E', 0, 0}) {
          reject();
        }
        if (block.sdnaIndex < schema.structs.size()) {
          const auto& name = schema.types[schema.structs[block.sdnaIndex].typeIndex].name;
          if (name != "Attribute" && name != "AttributeArray" && name != "AttributeSingle") {
            reject();
          }
        }
        const auto view = Take(ViewDnaBlock(bytes, blocks, schema, header, index));
        if ((view.Type().name != "Attribute" && view.Type().name != "AttributeArray" &&
                view.Type().name != "AttributeSingle") ||
            (type && *type != view.Type().name) ||
            !result.owned_.emplace(std::pair{address, *owners[index]}, index).second) {
          reject();
        }
        type = view.Type().name;
        unique[index].oldAddress = 0;
        collided.insert(index);
      }
    }
    result.global_ = Take(BuildPointerMap(unique));
    for (const auto index : collided) {
      const auto meshIndex = *owners[index];
      const auto mesh = Take(ViewDnaBlock(bytes, blocks, schema, header, meshIndex));
      if (mesh.Type().name != "Mesh" || blocks[meshIndex].count != 1) {
        reject();
      }
      const auto storage = Take(mesh.Member("attribute_storage"));
      const auto pointer = Take(storage.Member("dna_attributes"));
      const auto countView = Take(storage.Member("dna_attributes_num"));
      const auto count = Take(countView.SignedInteger());
      if (storage.Type().name != "AttributeStorage" || storage.PointerLevel() != 0 ||
          !storage.ArrayDimensions().empty() || pointer.Type().name != "Attribute" ||
          pointer.PointerLevel() != 1 || !pointer.ArrayDimensions().empty() ||
          countView.Type().name != "int" || countView.Type().length != 4 ||
          countView.PointerLevel() != 0 || !countView.ArrayDimensions().empty() || count <= 0) {
        reject();
      }
      const auto records = Take(result.Resolve(Take(pointer.Pointer()), meshIndex));
      if (!records || owners[*records] != meshIndex ||
          blocks[*records].count != static_cast<std::uint64_t>(count) ||
          Take(ViewDnaBlock(bytes, blocks, schema, header, *records)).Type().name != "Attribute") {
        reject();
      }
      if (Take(ViewDnaBlock(bytes, blocks, schema, header, index)).Type().name == "Attribute") {
        if (*records != index) {
          reject();
        }
      } else {
        bool referenced = false;
        for (std::uint64_t element = 0; element < blocks[*records].count; ++element) {
          const auto attribute = Take(ViewDnaBlock(bytes, blocks, schema, header, *records, element));
          const auto data = Take(attribute.Member("data"));
          if (data.Type().name != "void" || data.PointerLevel() != 1 || !data.ArrayDimensions().empty()) {
            reject();
          }
          if (Take(data.Pointer()) == blocks[index].oldAddress) {
            const auto storageType = Take(attribute.Member("storage_type"));
            if (storageType.Type().name != "int8_t" || storageType.Type().length != 1 ||
                storageType.PointerLevel() != 0 || !storageType.ArrayDimensions().empty() ||
                Take(storageType.SignedInteger()) !=
                    (Take(ViewDnaBlock(bytes, blocks, schema, header, index)).Type().name == "AttributeSingle" ? 1 : 0)) {
              reject();
            }
            referenced = true;
          }
        }
        if (!referenced || blocks[index].count != 1) {
          reject();
        }
      }
    }
    return Result<ScenePointers>(std::move(result));
  } catch (const Diagnostic& error) {
    return Result<ScenePointers>(error);
  } catch (const std::bad_alloc&) {
    return Result<ScenePointers>(Diagnostic{"BLEND_POINTER_ALLOCATION", Severity::Fatal,
        "Could not allocate scene pointer ownership", {}, {}, {}, false});
  } catch (const std::length_error&) {
    return Result<ScenePointers>(Diagnostic{"BLEND_POINTER_ALLOCATION", Severity::Fatal,
        "Scene pointer ownership exceeds addressable memory", {}, {}, {}, false});
  }
}

Result<std::optional<std::uint32_t>> ScenePointers::Resolve(std::uint64_t address,
    std::optional<std::uint32_t> mesh) const {
  if (mesh) {
    const auto found = owned_.find({address, *mesh});
    if (found != owned_.end()) {
      return Result<std::optional<std::uint32_t>>(found->second);
    }
  }
  return global_.Resolve(address);
}

} // namespace blend::detail
