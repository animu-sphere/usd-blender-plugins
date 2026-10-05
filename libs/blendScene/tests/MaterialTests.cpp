#include <blendScene/Decode.h>
#include <blendScene/Naming.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>

namespace {

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

struct Fixture {
  std::vector<std::byte> bytes;
  blend::Header header;
  std::vector<blend::BlendBlock> blocks;
  blend::DnaSchema schema;
  std::map<std::string, std::uint32_t> ids;

  explicit Fixture(const std::filesystem::path& path) {
    blend::FileByteSource file(path);
    bytes = Take(blend::ReadFileBytes(file, {4 * 1024 * 1024, 4 * 1024 * 1024, 16, 23}));
    blend::MemoryByteSource memory(bytes);
    header = Take(blend::ReadHeader(memory));
    blocks = Take(blend::ReadBlocks(memory, 10000));
    const auto dna = std::find_if(blocks.begin(), blocks.end(),
        [](const auto& block) { return block.code == std::array<char, 4>{'D', 'N', 'A', '1'}; });
    Require(dna != blocks.end(), "Material fixture has DNA1");
    schema = Take(blend::ReadDna(std::span<const std::byte>(bytes).subspan(
                                     static_cast<std::size_t>(dna->offset), static_cast<std::size_t>(dna->length)),
        header));
    for (const auto& id : Take(blend::ListDatablocks(bytes, blocks, schema))) {
      Require(ids.emplace(id.name, id.blockIndex).second, "Unique fixture ID names");
    }
  }

  blend::DnaValueView View(std::uint32_t index) const {
    return Take(blend::ViewDnaBlock(bytes, blocks, schema, header, index));
  }

  std::uint32_t Resolve(std::uint64_t address) const {
    const auto found = std::find_if(blocks.begin(), blocks.end(),
        [&](const auto& block) { return block.oldAddress == address; });
    Require(address != 0 && found != blocks.end(), "Fixture pointer resolves");
    return static_cast<std::uint32_t>(found - blocks.begin());
  }

  void Store(std::vector<std::byte>& changed, const blend::DnaValueView& value, std::uint64_t bits) const {
    const auto offset = static_cast<std::size_t>(value.Bytes().data() - bytes.data());
    Require(value.Bytes().size() <= 8 && offset + value.Bytes().size() <= changed.size(), "Bounded mutation");
    for (std::size_t byte = 0; byte < value.Bytes().size(); ++byte) {
      changed[offset + byte] = static_cast<std::byte>((bits >> (byte * 8)) & 255);
    }
  }

  blend::Result<blend::Scene> Decode(std::span<const std::byte> input) const {
    return blend::DecodeScene(input, blocks, schema, header, {1000, 100});
  }
};

bool Has(const blend::Result<blend::Scene>& result, std::string_view code, std::string_view name) {
  return std::any_of(result.Diagnostics().begin(), result.Diagnostics().end(), [&](const auto& diagnostic) {
    return diagnostic.code == code && diagnostic.datablock == name && diagnostic.recoverable &&
           diagnostic.blockIndex && diagnostic.byteOffset;
  });
}

void Check(const Fixture& fixture) {
  const auto result = fixture.Decode(fixture.bytes);
  const auto scene = Take(result);
  Require(scene.materials.size() == 8 && scene.objects.size() == 10 && scene.meshes.size() == 9,
      "Only effective materials and selected Meshes are decoded once");
  std::map<std::string, std::size_t> materials;
  for (std::size_t index = 0; index < scene.materials.size(); ++index) {
    materials.emplace(scene.materials[index].sourceName, index);
  }
  const auto& principled = scene.materials[materials.at("A_B")];
  Require(std::abs(principled.diffuseColor[0] - 0.6) < 1e-6 &&
              std::abs(principled.metallic - 0.8) < 1e-6 &&
              std::abs(principled.roughness - 0.2) < 1e-6 &&
              std::abs(principled.ior - 1.7) < 1e-6 &&
              std::abs(principled.clearcoat - 0.4) < 1e-6 &&
              std::abs(principled.clearcoatRoughness - 0.1) < 1e-6,
      "Saved Principled constants match the Blender oracle");
  const auto& fallback = scene.materials[materials.at("Fallback")];
  Require(fallback.diffuseColor == blend::Vector3{0.125, 0.25, 0.5} &&
              fallback.metallic == 0.25 && fallback.roughness == 0.75,
      "Unsupported active surface uses saved viewport constants");
  const auto identifiers = Take(blend::MaterialIdentifiers(scene));
  Require(identifiers[materials.at("A/B")] == "A_B" &&
              identifiers[materials.at("A_B")] == "A_B_1",
      "Material collisions use source-byte order");
  std::map<std::string, const blend::Object*> objects;
  for (const auto& object : scene.objects) {
    objects.emplace(object.sourceName, &object);
  }
  Require(objects.at("Multi")->mesh == objects.at("Override")->mesh &&
              objects.at("Multi")->materialSlots == std::vector<std::optional<std::size_t>>{materials.at("A/B"), materials.at("A_B")} &&
              objects.at("Override")->materialSlots == std::vector<std::optional<std::size_t>>{materials.at("Linked"), std::nullopt},
      "Shared Mesh material slots are resolved per Object, including null overrides");
  Require(scene.meshes[*objects.at("Multi")->mesh].faceMaterialIndices == std::vector<std::int32_t>{0, 1, 1, 0} &&
              scene.meshes[*objects.at("EmptySlot")->mesh].faceMaterialIndices == std::vector<std::int32_t>{0, 1, 2, 1} &&
              objects.at("Unbound")->materialSlots.empty(),
      "Saved face-domain material indices and no-slot Meshes match the oracle");
  for (const auto& [code, name] : {
           std::pair{"BLEND_MATERIAL_UNSUPPORTED_NODE", "Linked"}, {"BLEND_MATERIAL_UNSUPPORTED_NODE", "Fallback"},
           {"BLEND_MATERIAL_UNSUPPORTED_NODE", "Muted"},
           {"BLEND_MATERIAL_UNSUPPORTED_NODE", "MutedLink"},
           {"BLEND_MATERIAL_UNSUPPORTED_INPUT", "Unsupported"}, {"BLEND_MATERIAL_DEFERRED_INPUT", "Deferred"},
           {"BLEND_MATERIAL_SLOT_EMPTY", "EmptySlot"}, {"BLEND_MATERIAL_SLOT_EMPTY", "Override"}}) {
    Require(Has(result, code, name), "Expected contextual recoverable material diagnostic");
  }
}

void Boundaries(const Fixture& fixture) {
  auto missingMaterial = fixture.schema;
  missingMaterial.types[missingMaterial.FindStruct("Material")->typeIndex].name = "UnknownMaterial";
  const auto missingResult = blend::DecodeScene(fixture.bytes, fixture.blocks, missingMaterial,
      fixture.header, {1000, 100});
  Require(!missingResult.HasValue() && missingResult.GetError().code == "BLEND_MATERIAL_STORAGE_INVALID",
      "A missing Material definition cannot silently discard saved Material IDs");
  const auto reject = [&](const blend::DnaValueView& value, std::uint64_t bits, std::string_view code) {
    auto changed = fixture.bytes;
    fixture.Store(changed, value, bits);
    const auto result = fixture.Decode(changed);
    Require(!result.HasValue() && result.GetError().code == code &&
                !result.GetError().recoverable && result.GetError().blockIndex &&
                result.GetError().byteOffset,
        "Invalid material storage fails explicitly: " + std::string(code));
  };
  const auto single = fixture.View(fixture.ids.at("OBSingle"));
  reject(Take(single.Member("totcol")), std::numeric_limits<std::uint32_t>::max(), "BLEND_MATERIAL_STORAGE_INVALID");
  reject(Take(single.Member("matbits")), 0, "BLEND_MATERIAL_REFERENCE_INVALID");
  reject(Take(single.Member("matbits")), Take(Take(single.Member("mat")).Pointer()), "BLEND_MATERIAL_STORAGE_INVALID");
  reject(Take(single.Member("mat")), 123, "BLEND_MATERIAL_REFERENCE_INVALID");
  const auto material = fixture.View(fixture.ids.at("MAA_B"));
  reject(Take(material.Member("roughness")), std::bit_cast<std::uint32_t>(std::numeric_limits<float>::infinity()),
      "BLEND_MATERIAL_VALUE_INVALID");
  reject(Take(material.Member("nodetree")), 123, "BLEND_MATERIAL_REFERENCE_INVALID");
  reject(Take(material.Member("use_nodes")), 2, "BLEND_MATERIAL_STORAGE_INVALID");
  const auto tree = fixture.View(fixture.Resolve(Take(Take(material.Member("nodetree")).Pointer())));
  const auto nodes = Take(tree.Member("nodes"));
  const auto node = fixture.View(fixture.Resolve(Take(Take(nodes.Member("first")).Pointer())));
  reject(Take(node.Member("next")), Take(Take(nodes.Member("first")).Pointer()), "BLEND_MATERIAL_GRAPH_INVALID");
  const auto inputs = Take(node.Member("inputs"));
  const auto socket = fixture.View(fixture.Resolve(Take(Take(inputs.Member("first")).Pointer())));
  reject(Take(socket.Member("default_value")), 123, "BLEND_MATERIAL_REFERENCE_INVALID");
  const auto color = fixture.View(fixture.Resolve(Take(Take(socket.Member("default_value")).Pointer())));
  reject(Take(Take(color.Member("value")).Element(0)),
      std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN()), "BLEND_MATERIAL_VALUE_INVALID");
  const auto links = Take(tree.Member("links"));
  const auto link = fixture.View(fixture.Resolve(Take(Take(links.Member("first")).Pointer())));
  reject(Take(link.Member("tonode")), Take(Take(link.Member("fromnode")).Pointer()), "BLEND_MATERIAL_GRAPH_INVALID");
  auto invalidOwner = fixture.bytes;
  const auto bitsIndex = fixture.Resolve(Take(Take(single.Member("matbits")).Pointer()));
  invalidOwner[static_cast<std::size_t>(fixture.blocks[bitsIndex].offset)] = std::byte{2};
  const auto ownerResult = fixture.Decode(invalidOwner);
  Require(!ownerResult.HasValue() && ownerResult.GetError().code == "BLEND_MATERIAL_STORAGE_INVALID",
      "Invalid slot ownership flags fail explicitly");
  auto invalidSlot = fixture.bytes;
  const auto mesh = fixture.View(fixture.ids.at("MESingleData"));
  const auto slotsIndex = fixture.Resolve(Take(Take(mesh.Member("mat")).Pointer()));
  const auto address = fixture.blocks[fixture.ids.at("OBSingle")].oldAddress;
  for (std::size_t byte = 0; byte < fixture.header.pointerSize; ++byte) {
    invalidSlot[static_cast<std::size_t>(fixture.blocks[slotsIndex].offset) + byte] =
        static_cast<std::byte>((address >> (byte * 8)) & 255);
  }
  const auto slotResult = fixture.Decode(invalidSlot);
  Require(!slotResult.HasValue() && slotResult.GetError().code == "BLEND_MATERIAL_REFERENCE_INVALID",
      "Material slots cannot target an Object ID");
  auto changed = fixture.bytes;
  fixture.Store(changed, Take(material.Member("use_nodes")), 0);
  const auto viewport = Take(fixture.Decode(changed));
  const auto found = std::find_if(viewport.materials.begin(), viewport.materials.end(),
      [](const auto& value) { return value.sourceName == "A_B"; });
  Require(found != viewport.materials.end() && found->diffuseColor == blend::Vector3{0.125, 0.25, 0.5} &&
              found->metallic == 0.25 && found->roughness == 0.75,
      "Synthetic non-node flag selects saved viewport constants without requiring a graph");
}

} // namespace

int main(int argc, char** argv) {
  try {
    Require(argc == 2, "Pass the Blender-written material fixture");
    const Fixture fixture(argv[1]);
    Check(fixture);
    Boundaries(fixture);
    std::cout << "Material constants, effective slots and storage boundaries passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
