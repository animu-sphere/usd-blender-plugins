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

std::uint32_t Node(const Fixture& fixture, std::string_view material, std::string_view type) {
  const auto source = fixture.View(fixture.ids.at("MA" + std::string(material)));
  const auto tree = fixture.View(fixture.Resolve(Take(Take(source.Member("nodetree")).Pointer())));
  auto address = Take(Take(Take(tree.Member("nodes")).Member("first")).Pointer());
  while (address != 0) {
    const auto index = fixture.Resolve(address);
    const auto value = fixture.View(index);
    const auto bytes = Take(value.Member("idname")).Bytes();
    if (std::string_view(reinterpret_cast<const char*>(bytes.data())) == type) {
      return index;
    }
    address = Take(Take(value.Member("next")).Pointer());
  }
  throw std::runtime_error("Missing fixture node");
}

blend::DnaValueView InputSocket(const Fixture& fixture, std::uint32_t node, std::string_view name) {
  auto address = Take(Take(Take(fixture.View(node).Member("inputs")).Member("first")).Pointer());
  while (address != 0) {
    const auto socket = fixture.View(fixture.Resolve(address));
    const auto identifier = Take(socket.Member("identifier")).Bytes();
    if (std::string_view(reinterpret_cast<const char*>(identifier.data())) == name) {
      return socket;
    }
    address = Take(Take(socket.Member("next")).Pointer());
  }
  throw std::runtime_error("Missing fixture socket");
}

void CheckTextures(const Fixture& fixture) {
  const auto result = fixture.Decode(fixture.bytes);
  const auto scene = Take(result);
  Require(scene.materials.size() == 27 && scene.meshes.size() == 27 && scene.objects.size() == 27,
      "Texture fixture retains all effective Materials and geometry");
  std::map<std::string, const blend::Material*> materials;
  for (const auto& material : scene.materials) {
    materials.emplace(material.sourceName, &material);
    Require(std::abs(material.diffuseColor[0] - 0.2) < 1e-6 && std::abs(material.roughness - 0.3) < 1e-6,
        "Textures retain their saved constant fallback");
  }
  const auto check = [&](const char* name, blend::TextureInput input, const char* path,
                         blend::TextureColorSpace colorSpace, blend::TextureWrap wrap, const char* uv) {
    const auto& textures = materials.at(name)->textures;
    Require(textures.size() == 1, std::string("One owning texture for ") + name);
    const auto& texture = textures.front();
    Require(texture.input == input && texture.assetPath == path && texture.colorSpace == colorSpace &&
                texture.wrap == wrap && texture.uvMap == uv &&
                texture.normalUvMap == (input == blend::TextureInput::Normal ? uv : ""),
        std::string("Saved texture settings match Blender oracle for ") + name);
  };
  using Input = blend::TextureInput;
  using Color = blend::TextureColorSpace;
  using Wrap = blend::TextureWrap;
  for (const auto name : {"Relative", "Closest"}) {
    check(name, Input::BaseColor, "./textures/color.png", Color::SRgb, Wrap::Repeat, "");
  }
  check("NamedUV", Input::BaseColor, "./textures/color.png", Color::SRgb, Wrap::Clamp, "Detail_UV");
  check("ActiveUV", Input::BaseColor, "./textures/color.png", Color::SRgb, Wrap::Black, "Render");
  check("Mirror", Input::BaseColor, "./textures/color.png", Color::SRgb, Wrap::Mirror, "");
  check("Absolute", Input::BaseColor, "C:/usd-blend-fixtures/color.png", Color::SRgb, Wrap::Repeat, "");
  check("UNC", Input::BaseColor, "//usd-blend-fixtures/textures/color.png", Color::SRgb, Wrap::Repeat, "");
  check("Raw", Input::BaseColor, "./textures/color.png", Color::Raw, Wrap::Repeat, "");
  check("AlphaScalar", Input::Roughness, "./textures/color.png", Color::SRgb, Wrap::Repeat, "");
  check("MetallicAlpha", Input::Metallic, "./textures/color.png", Color::SRgb, Wrap::Repeat, "");
  check("IorAlpha", Input::Ior, "./textures/color.png", Color::SRgb, Wrap::Repeat, "");
  check("ClearcoatAlpha", Input::Clearcoat, "./textures/color.png", Color::SRgb, Wrap::Repeat, "");
  check("ClearcoatRoughnessAlpha", Input::ClearcoatRoughness, "./textures/color.png", Color::SRgb, Wrap::Repeat, "");
  check("Normal", Input::Normal, "./textures/color.png", Color::Raw, Wrap::Repeat, "");
  check("NamedNormal", Input::Normal, "./textures/color.png", Color::Raw, Wrap::Repeat, "Detail_UV");
  for (const auto& [code, name] : {
           std::pair{"BLEND_IMAGE_PACKED", "Packed"}, {"BLEND_IMAGE_MISSING", "Missing"},
           {"BLEND_IMAGE_SOURCE_UNSUPPORTED", "Generated"}, {"BLEND_IMAGE_SOURCE_UNSUPPORTED", "Movie"},
           {"BLEND_IMAGE_SOURCE_UNSUPPORTED", "Sequence"}, {"BLEND_IMAGE_SOURCE_UNSUPPORTED", "Tiled"},
           {"BLEND_MATERIAL_UNSUPPORTED_NODE", "NormalStrength"}, {"BLEND_MATERIAL_UNSUPPORTED_NODE", "ObjectNormal"},
           {"BLEND_MATERIAL_UNSUPPORTED_NODE", "Projection"}, {"BLEND_MATERIAL_UNSUPPORTED_NODE", "MutedImage"},
           {"BLEND_MATERIAL_UNSUPPORTED_NODE", "MappedVector"}, {"BLEND_MATERIAL_UNSUPPORTED_NODE", "ColorScalar"}}) {
    Require(materials.at(name)->textures.empty() && Has(result, code, name),
        std::string("Unsupported texture uses diagnosed constants: ") + name);
  }
  Require(Has(result, "BLEND_IMAGE_INTERPOLATION_UNSUPPORTED", "Closest") &&
              Has(result, "BLEND_IMAGE_ABSOLUTE_PATH", "Absolute") &&
              Has(result, "BLEND_IMAGE_ABSOLUTE_PATH", "UNC"),
      "Approximate filtering and absolute paths are contextual diagnostics");
  auto blocks = fixture.blocks;
  std::reverse(blocks.begin(), blocks.end());
  const auto reversed = Take(blend::DecodeScene(fixture.bytes, blocks, fixture.schema, fixture.header, {1000, 100}));
  Require(reversed.materials.size() == scene.materials.size(), "Reordered texture materials retain their count");
  for (std::size_t index = 0; index < scene.materials.size(); ++index) {
    Require(reversed.materials[index].sourceName == scene.materials[index].sourceName &&
                reversed.materials[index].textures == scene.materials[index].textures,
        "Saved block order cannot change owning texture data");
  }
  const auto reject = [&](const blend::DnaValueView& value, std::uint64_t bits, const char* code) {
    auto changed = fixture.bytes;
    fixture.Store(changed, value, bits);
    const auto failure = fixture.Decode(changed);
    Require(!failure.HasValue() && failure.GetError().code == code && !failure.GetError().recoverable,
        std::string("Invalid texture storage fails without partial Scene: ") + code);
  };
  const auto texture = fixture.View(Node(fixture, "Relative", "ShaderNodeTexImage"));
  reject(Take(texture.Member("storage")), 123, "BLEND_MATERIAL_REFERENCE_INVALID");
  reject(Take(texture.Member("id")), fixture.blocks[fixture.ids.at("MARelative")].oldAddress,
      "BLEND_MATERIAL_REFERENCE_INVALID");
  const auto uv = fixture.View(Node(fixture, "NamedUV", "ShaderNodeUVMap"));
  reject(Take(uv.Member("storage")), 123, "BLEND_MATERIAL_REFERENCE_INVALID");
  const auto normalIndex = Node(fixture, "Normal", "ShaderNodeNormalMap");
  const auto normal = fixture.View(normalIndex);
  reject(Take(normal.Member("storage")), 123, "BLEND_MATERIAL_REFERENCE_INVALID");
  const auto strength = fixture.View(fixture.Resolve(Take(Take(InputSocket(fixture, normalIndex, "Strength").Member("default_value")).Pointer())));
  reject(Take(strength.Member("value")), std::bit_cast<std::uint32_t>(std::numeric_limits<float>::infinity()),
      "BLEND_MATERIAL_VALUE_INVALID");
  const auto image = fixture.View(fixture.ids.at("IMRelativeImage"));
  auto unterminated = fixture.bytes;
  const auto path = Take(image.Member("name"));
  const auto pathOffset = static_cast<std::size_t>(path.Bytes().data() - fixture.bytes.data());
  std::fill_n(unterminated.begin() + pathOffset, path.Bytes().size(), std::byte{'x'});
  const auto invalidPath = fixture.Decode(unterminated);
  Require(!invalidPath.HasValue() && invalidPath.GetError().code == "BLEND_MATERIAL_STORAGE_INVALID",
      "Unterminated image paths fail without a partial Scene");
  const auto packed = fixture.View(fixture.ids.at("IMPackedImage"));
  const auto files = Take(packed.Member("packedfiles"));
  const auto packedAddress = Take(Take(files.Member("first")).Pointer());
  const auto packedFile = fixture.View(fixture.Resolve(packedAddress));
  reject(Take(packedFile.Member("next")), packedAddress, "BLEND_MATERIAL_GRAPH_INVALID");
  auto changed = fixture.bytes;
  fixture.Store(changed, Take(Take(image.Member("name")).Element(0)), 0);
  const auto missing = fixture.Decode(changed);
  Require(missing.HasValue() && Has(missing, "BLEND_IMAGE_MISSING", "Relative"),
      "An empty image path is diagnosed without accessing the filesystem");
  const auto diagnose = [&](const blend::DnaValueView& value, std::uint64_t bits, const char* code, const char* name) {
    auto modified = fixture.bytes;
    fixture.Store(modified, value, bits);
    const auto fallback = fixture.Decode(modified);
    Require(fallback.HasValue() && Has(fallback, code, name),
        std::string("Unsupported texture settings have explicit contextual fallback: ") + code);
    const auto found = std::find_if(fallback.GetValue().materials.begin(), fallback.GetValue().materials.end(),
        [&](const auto& material) { return material.sourceName == name; });
    Require(found != fallback.GetValue().materials.end() && found->textures.empty(),
        "Unsupported image settings do not author a success-shaped texture");
  };
  diagnose(Take(texture.Member("id")), 0, "BLEND_IMAGE_MISSING", "Relative");
  const auto colorSpace = Take(Take(image.Member("colorspace_settings")).Member("name"));
  diagnose(Take(colorSpace.Element(0)), 'X', "BLEND_IMAGE_COLORSPACE_UNSUPPORTED", "Relative");
  diagnose(Take(Take(image.Member("name")).Element(0)), 'x', "BLEND_IMAGE_PATH_UNSUPPORTED", "Relative");
  diagnose(Take(Take(image.Member("id")).Member("lib")), 123, "BLEND_IMAGE_LINKED_UNSUPPORTED", "Relative");
  diagnose(Take(image.Member("alpha_mode")), 1, "BLEND_IMAGE_ALPHA_UNSUPPORTED", "Relative");
  const auto imageStorage = fixture.View(fixture.Resolve(Take(Take(texture.Member("storage")).Pointer())));
  const auto mapping = Take(Take(imageStorage.Member("base")).Member("tex_mapping"));
  diagnose(Take(Take(mapping.Member("loc")).Element(0)), std::bit_cast<std::uint32_t>(0.5f),
      "BLEND_MATERIAL_UNSUPPORTED_NODE", "Relative");
  const auto colorMapping = Take(Take(imageStorage.Member("base")).Member("color_mapping"));
  diagnose(Take(colorMapping.Member("bright")), std::bit_cast<std::uint32_t>(0.5f),
      "BLEND_MATERIAL_UNSUPPORTED_NODE", "Relative");
  reject(Take(Take(mapping.Member("size")).Element(0)),
      std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN()), "BLEND_MATERIAL_VALUE_INVALID");
  const auto normalStorage = fixture.View(fixture.Resolve(Take(Take(normal.Member("storage")).Pointer())));
  diagnose(Take(normalStorage.Member("convention")), 1, "BLEND_MATERIAL_UNSUPPORTED_NODE", "Normal");
  diagnose(Take(normalStorage.Member("base")), 0, "BLEND_MATERIAL_UNSUPPORTED_NODE", "Normal");
}

} // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 3 && std::string(argv[1]) == "--textures") {
      CheckTextures(Fixture(argv[2]));
      std::cout << "External image, UV, normal-map and malformed-storage checks passed\n";
      return 0;
    }
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
