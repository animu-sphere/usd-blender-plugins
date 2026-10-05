#include "DecodeInternal.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

namespace blend::detail {
namespace {

constexpr std::int64_t NodeActiveOutput = 1 << 6;
constexpr std::int64_t NodeMuted = 1 << 9;
constexpr std::int64_t LinkValid = 1 << 1;
constexpr std::int64_t LinkMuted = 1 << 4;

using NodeInputs = std::map<std::uint32_t, std::vector<std::uint32_t>>;
using IncomingLinks = std::map<std::uint32_t, std::pair<std::uint32_t, std::uint32_t>>;

class MaterialDecoder {
public:
  MaterialDecoder(std::span<const std::byte> bytes,
      std::span<const BlendBlock> blocks, const DnaSchema& schema,
      const Header& header, const ScenePointers& pointers,
      Scene& scene, std::vector<Diagnostic>& diagnostics)
      : bytes_(bytes), blocks_(blocks), schema_(schema), header_(header),
        pointers_(pointers), scene_(scene), diagnostics_(diagnostics) {
  }

  void Run(const SelectedSceneObjects& selected) {
    if (!schema_.FindStruct("Material")) {
      for (std::size_t index = 0; index < blocks_.size(); ++index) {
        if (blocks_[index].code == std::array<char, 4>{'M', 'A', 0, 0}) {
          context_ = static_cast<std::uint32_t>(index);
          Fail("BLEND_MATERIAL_STORAGE_INVALID", "Material IDs require a Material SDNA definition");
        }
      }
      for (const auto& object : selected.objects) {
        if (object.values->type == 1 && schema_.FindStruct("Mesh")->FindMember("totcol")) {
          context_ = object.blockIndex;
          name_ = object.sourceName;
          if (Integer(View(*object.dataBlockIndex, "Mesh"), "totcol", "short") != 0) {
            Fail("BLEND_MATERIAL_STORAGE_INVALID", "Nonempty material slots require a Material SDNA definition");
          }
        }
      }
      return;
    }
    std::map<std::uint32_t, std::optional<std::size_t>> materials;
    for (std::size_t index = 0; index < selected.objects.size(); ++index) {
      auto& object = scene_.objects[index];
      if (!object.mesh) {
        continue;
      }
      context_ = selected.objects[index].blockIndex;
      name_ = object.sourceName;
      const auto source = View(context_, "Object");
      const auto meshIndex = *selected.objects[index].dataBlockIndex;
      const auto mesh = View(meshIndex, "Mesh");
      const auto count = Integer(mesh, "totcol", "short");
      const auto objectCount = Integer(source, "totcol", "int");
      if (count < 0 || objectCount < 0 || objectCount != count) {
        Fail("BLEND_MATERIAL_STORAGE_INVALID", "Object and Mesh material slot counts must agree and be nonnegative");
      }
      const auto meshSlots = PointerArray(mesh, "mat", count);
      const auto objectSlots = PointerArray(source, "mat", count);
      const auto bits = Raw(Pointer(source, "matbits", "char"), count);
      object.materialSlots.reserve(static_cast<std::size_t>(count));
      for (std::int64_t slot = 0; slot < count; ++slot) {
        const auto bit = std::to_integer<unsigned>(bits[static_cast<std::size_t>(slot)]);
        if (bit > 1) {
          Fail("BLEND_MATERIAL_STORAGE_INVALID", "Material slot ownership flags must be zero or one");
        }
        const auto address = bit ? objectSlots[static_cast<std::size_t>(slot)]
                                 : meshSlots[static_cast<std::size_t>(slot)];
        if (address == 0) {
          object.materialSlots.push_back(std::nullopt);
          Warn("BLEND_MATERIAL_SLOT_EMPTY", "Effective material slot " + std::to_string(slot) + " is empty");
          continue;
        }
        const auto material = Resolve(address);
        if (blocks_[material].code != std::array<char, 4>{'M', 'A', 0, 0}) {
          Fail("BLEND_MATERIAL_REFERENCE_INVALID", "A material slot must reference a Material ID");
        }
        auto found = materials.find(material);
        if (found == materials.end()) {
          const auto savedContext = context_;
          const auto savedName = name_;
          context_ = material;
          const auto value = Decode();
          std::optional<std::size_t> materialIndex;
          if (value) {
            materialIndex = scene_.materials.size();
            scene_.materials.push_back(*value);
          }
          found = materials.emplace(material, materialIndex).first;
          context_ = savedContext;
          name_ = savedName;
        }
        object.materialSlots.push_back(found->second);
      }
      const auto& geometry = scene_.meshes[*object.mesh];
      if (count != 0 && std::any_of(geometry.faceMaterialIndices.begin(), geometry.faceMaterialIndices.end(),
                            [&](auto slot) { return slot < 0 || slot >= count; })) {
        Warn("BLEND_MATERIAL_SLOT_INVALID", "Some faces reference material indices outside the effective slots");
      }
    }
  }

private:
  [[noreturn]] void Fail(const char* code, std::string message) const {
    throw Diagnostic{code, Severity::Fatal, std::move(message),
        blocks_[context_].offset, context_, name_, false};
  }

  void Warn(const char* code, std::string message) {
    diagnostics_.push_back({code, Severity::Unsupported, std::move(message),
        blocks_[context_].offset, context_, name_, true});
  }

  DnaValueView View(std::uint32_t index, std::string_view type) const {
    const auto value = Take(ViewDnaBlock(bytes_, blocks_, schema_, header_, index));
    if (value.Type().name != type || blocks_[index].count != 1) {
      Fail("BLEND_MATERIAL_STORAGE_INVALID", "Material storage has an incompatible type or record count");
    }
    return value;
  }

  std::uint32_t Resolve(std::uint64_t address) const {
    const auto index = Take(pointers_.Resolve(address));
    if (!index) {
      Fail("BLEND_MATERIAL_REFERENCE_INVALID", "Material storage pointer is null, unresolved or interior");
    }
    return *index;
  }

  std::uint64_t Pointer(const DnaValueView& source, std::string_view member,
      std::string_view type, std::uint32_t level = 1) const {
    const auto value = Take(source.Member(member));
    if (value.Type().name != type || value.PointerLevel() != level ||
        !value.ArrayDimensions().empty()) {
      Fail("BLEND_MATERIAL_STORAGE_INVALID", "Material pointer has incompatible SDNA storage");
    }
    return Take(value.Pointer());
  }

  std::int64_t Integer(const DnaValueView& source, std::string_view member,
      std::string_view type) const {
    const auto value = Take(source.Member(member));
    if (value.Type().name != type || value.PointerLevel() != 0 ||
        !value.ArrayDimensions().empty() || value.Type().length != (type == "short" ? 2 : 4)) {
      Fail("BLEND_MATERIAL_STORAGE_INVALID", "Material integer has incompatible SDNA storage");
    }
    return Take(value.SignedInteger());
  }

  unsigned Byte(const DnaValueView& source, std::string_view member) const {
    const auto value = Take(source.Member(member));
    if (value.Type().name != "char" || value.Type().length != 1 ||
        value.PointerLevel() != 0 || !value.ArrayDimensions().empty()) {
      Fail("BLEND_MATERIAL_STORAGE_INVALID", "Material flag must be a scalar char");
    }
    return std::to_integer<unsigned>(value.Bytes()[0]);
  }

  double Float(const DnaValueView& value) const {
    if (value.Type().name != "float" || value.Type().length != 4 ||
        value.PointerLevel() != 0 || !value.ArrayDimensions().empty()) {
      Fail("BLEND_MATERIAL_STORAGE_INVALID", "Material value must be a scalar float");
    }
    const auto number = Take(value.FloatingPoint());
    if (!std::isfinite(number)) {
      Fail("BLEND_MATERIAL_VALUE_INVALID", "Material values must be finite");
    }
    return number;
  }

  std::string String(const DnaValueView& value) const {
    const auto end = std::find(value.Bytes().begin(), value.Bytes().end(), std::byte{0});
    if (value.Type().name != "char" || value.PointerLevel() != 0 ||
        value.ArrayDimensions().size() != 1 || end == value.Bytes().end()) {
      Fail("BLEND_MATERIAL_STORAGE_INVALID", "Material names must be terminated character arrays");
    }
    return {reinterpret_cast<const char*>(value.Bytes().data()),
        static_cast<std::size_t>(end - value.Bytes().begin())};
  }

  std::span<const std::byte> Raw(std::uint64_t address, std::int64_t length) const {
    if (length == 0 && address == 0) {
      return {};
    }
    const auto index = Resolve(address);
    const auto& block = blocks_[index];
    if (length < 0 || block.code != std::array<char, 4>{'D', 'A', 'T', 'A'} ||
        block.count != 1 || block.length != static_cast<std::uint64_t>(length) ||
        block.offset > bytes_.size() || block.length > bytes_.size() - block.offset ||
        block.sdnaIndex >= schema_.structs.size() ||
        schema_.types[schema_.structs[block.sdnaIndex].typeIndex].name != "raw_data") {
      Fail("BLEND_MATERIAL_STORAGE_INVALID", "Material arrays must be exact-length raw DATA blocks");
    }
    return bytes_.subspan(static_cast<std::size_t>(block.offset), static_cast<std::size_t>(block.length));
  }

  std::vector<std::uint64_t> PointerArray(const DnaValueView& source,
      std::string_view member, std::int64_t count) const {
    const auto bytes = Raw(Pointer(source, member, "Material", 2), count * header_.pointerSize);
    std::vector<std::uint64_t> addresses;
    for (std::int64_t element = 0; element < count; ++element) {
      std::uint64_t address = 0;
      for (std::size_t byte = 0; byte < header_.pointerSize; ++byte) {
        const auto shift = header_.byteOrder == ByteOrder::Little ? byte : header_.pointerSize - byte - 1;
        address |= std::to_integer<std::uint64_t>(bytes[static_cast<std::size_t>(element) * header_.pointerSize + byte]) << (shift * 8);
      }
      addresses.push_back(address);
    }
    return addresses;
  }

  std::vector<std::uint32_t> List(const DnaValueView& owner,
      std::string_view member, std::string_view type) const {
    const auto list = Take(owner.Member(member));
    if (list.Type().name != "ListBase" || list.PointerLevel() != 0 ||
        !list.ArrayDimensions().empty()) {
      Fail("BLEND_MATERIAL_STORAGE_INVALID", "Material lists must be embedded ListBase records");
    }
    auto address = Pointer(list, "first", "void");
    const auto last = Pointer(list, "last", "void");
    std::uint64_t previous = 0;
    std::set<std::uint32_t> seen;
    std::vector<std::uint32_t> result;
    while (address != 0) {
      const auto index = Resolve(address);
      if (!seen.insert(index).second) {
        Fail("BLEND_MATERIAL_GRAPH_INVALID", "Material list contains a cycle");
      }
      const auto value = View(index, type);
      if (Pointer(value, "prev", type) != previous) {
        Fail("BLEND_MATERIAL_GRAPH_INVALID", "Material list previous pointers must agree");
      }
      result.push_back(index);
      previous = address;
      address = Pointer(value, "next", type);
    }
    if (previous != last) {
      Fail("BLEND_MATERIAL_GRAPH_INVALID", "Material list endpoints must agree");
    }
    return result;
  }

  DnaValueView Default(const DnaValueView& socket, std::string_view type) const {
    return View(Resolve(Pointer(socket, "default_value", "void")), type);
  }

  std::uint32_t Socket(std::uint32_t node, std::string_view identifier,
      const NodeInputs& inputs) const {
    std::optional<std::uint32_t> found;
    for (const auto index : inputs.at(node)) {
      if (String(Take(View(index, "bNodeSocket").Member("identifier"))) == identifier) {
        if (found) {
          Fail("BLEND_MATERIAL_GRAPH_INVALID", "Node input identifiers must be unique");
        }
        found = index;
      }
    }
    if (!found) {
      Fail("BLEND_MATERIAL_STORAGE_INVALID", "Texture graph node is missing a required socket");
    }
    return *found;
  }

  bool NodeIs(std::uint32_t index, std::string_view type) const {
    const auto node = View(index, "bNode");
    return String(Take(node.Member("idname"))) == type &&
           (Integer(node, "flag", "int") & NodeMuted) == 0;
  }

  std::string SocketName(std::uint32_t index) const {
    return String(Take(View(index, "bNodeSocket").Member("identifier")));
  }

  bool DefaultTextureMapping(const DnaValueView& storage) const {
    const auto base = Take(storage.Member("base"));
    const auto mapping = Take(base.Member("tex_mapping"));
    const auto color = Take(base.Member("color_mapping"));
    for (const auto& [value, type] : {
             std::pair{base, "NodeTexBase"}, {mapping, "TexMapping"}, {color, "ColorMapping"}}) {
      if (value.Type().name != type || value.PointerLevel() != 0 || !value.ArrayDimensions().empty()) {
        Fail("BLEND_MATERIAL_STORAGE_INVALID", "Texture mappings must be embedded records of the expected type");
      }
    }
    bool standard = Integer(mapping, "flag", "int") == 0 && Integer(mapping, "type", "int") == 0 &&
                    Byte(mapping, "mapping") == 0 && Byte(mapping, "projx") == 1 &&
                    Byte(mapping, "projy") == 2 && Byte(mapping, "projz") == 3 &&
                    Pointer(mapping, "ob", "Object") == 0;
    for (const auto member : {"loc", "rot", "size"}) {
      const auto components = Take(mapping.Member(member));
      if (components.ArrayDimensions().size() != 1 || components.ArrayDimensions()[0] != 3) {
        Fail("BLEND_MATERIAL_STORAGE_INVALID", "Texture mapping vectors must contain three float components");
      }
      for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto value = Float(Take(components.Element(axis)));
        standard = (value == (std::string_view(member) == "size" ? 1 : 0)) && standard;
      }
    }
    for (const auto member : {"bright", "contrast", "saturation"}) {
      const auto value = Float(Take(color.Member(member)));
      standard = (value == 1) && standard;
    }
    const auto factor = Float(Take(color.Member("blend_factor")));
    return factor == 0 && Integer(color, "flag", "int") == 0 &&
           Integer(color, "blend_type", "int") == 0 && standard;
  }

  std::optional<MaterialTexture> Texture(TextureInput input, std::uint32_t socket,
      const NodeInputs& inputs, const IncomingLinks& incoming) {
    auto [nodeIndex, output] = incoming.at(socket);
    MaterialTexture texture;
    texture.input = input;
    const auto unsupported = [&]() -> std::optional<MaterialTexture> {
      Warn("BLEND_MATERIAL_UNSUPPORTED_NODE", "Linked input is outside the external-image/UV/tangent-normal subset; its constant fallback is used");
      return {};
    };
    if (input == TextureInput::Normal) {
      if (!NodeIs(nodeIndex, "ShaderNodeNormalMap") || SocketName(output) != "Normal") {
        return unsupported();
      }
      const auto normal = View(nodeIndex, "bNode");
      const auto storage = View(Resolve(Pointer(normal, "storage", "void")), "NodeShaderNormalMap");
      const auto strength = Socket(nodeIndex, "Strength", inputs);
      const auto value = Float(Take(Default(View(strength, "bNodeSocket"), "bNodeSocketValueFloat").Member("value")));
      if (Integer(storage, "space", "int") != 0 || Byte(storage, "convention") != 0 ||
          Byte(storage, "base") != 1 || value != 1 || incoming.contains(strength)) {
        return unsupported();
      }
      texture.normalUvMap = String(Take(storage.Member("uv_map")));
      const auto color = Socket(nodeIndex, "Color", inputs);
      if (!incoming.contains(color)) {
        return unsupported();
      }
      std::tie(nodeIndex, output) = incoming.at(color);
    }
    if (!NodeIs(nodeIndex, "ShaderNodeTexImage") ||
        SocketName(output) != (input == TextureInput::BaseColor || input == TextureInput::Normal ? "Color" : "Alpha")) {
      return unsupported();
    }
    const auto node = View(nodeIndex, "bNode");
    const auto storage = View(Resolve(Pointer(node, "storage", "void")), "NodeTexImage");
    if (Integer(storage, "projection", "int") != 0 || !DefaultTextureMapping(storage)) {
      return unsupported();
    }
    const auto vector = Socket(nodeIndex, "Vector", inputs);
    if (incoming.contains(vector)) {
      const auto [uvIndex, uvOutput] = incoming.at(vector);
      if (!NodeIs(uvIndex, "ShaderNodeUVMap") || SocketName(uvOutput) != "UV" ||
          Integer(View(uvIndex, "bNode"), "custom1", "short") != 0) {
        return unsupported();
      }
      const auto uv = View(Resolve(Pointer(View(uvIndex, "bNode"), "storage", "void")), "NodeShaderUVMap");
      texture.uvMap = String(Take(uv.Member("uv_map")));
    }
    const auto address = Pointer(node, "id", "ID");
    if (address == 0) {
      Warn("BLEND_IMAGE_MISSING", "Image Texture has no Image; its constant fallback is used");
      return {};
    }
    const auto imageIndex = Resolve(address);
    if (blocks_[imageIndex].code != std::array<char, 4>{'I', 'M', 0, 0}) {
      Fail("BLEND_MATERIAL_REFERENCE_INVALID", "Image Texture must reference an Image ID");
    }
    const auto image = View(imageIndex, "Image");
    const auto id = Take(image.Member("id"));
    if (!String(Take(id.Member("name"))).starts_with("IM")) {
      Fail("BLEND_MATERIAL_STORAGE_INVALID", "Image ID name must carry its IM prefix");
    }
    if (Pointer(id, "lib", "Library") != 0) {
      Warn("BLEND_IMAGE_LINKED_UNSUPPORTED", "Linked Images are not followed; the constant fallback is used");
      return {};
    }
    const auto packed = List(image, "packedfiles", "ImagePackedFile");
    if (Pointer(image, "packedfile", "PackedFile") != 0 || !packed.empty()) {
      Warn("BLEND_IMAGE_PACKED", "Packed images need an asset resolver and are not authored as textures");
      return {};
    }
    if (Integer(image, "source", "short") != 1) {
      Warn("BLEND_IMAGE_SOURCE_UNSUPPORTED", "Only external file Images are translated; generated/movie/sequence/tiled images use constants");
      return {};
    }
    if (Byte(image, "alpha_mode") != 0) {
      Warn("BLEND_IMAGE_ALPHA_UNSUPPORTED", "Only straight image alpha is translated; other alpha interpretations use constants");
      return {};
    }
    const auto colorSpace = String(Take(Take(image.Member("colorspace_settings")).Member("name")));
    if (colorSpace == "sRGB") {
      texture.colorSpace = TextureColorSpace::SRgb;
    } else if (colorSpace == "Non-Color") {
      texture.colorSpace = TextureColorSpace::Raw;
    } else {
      Warn("BLEND_IMAGE_COLORSPACE_UNSUPPORTED", "Image color space is outside the sRGB/Non-Color subset");
      return {};
    }
    switch (Integer(storage, "extension", "int")) {
    case 0:
      texture.wrap = TextureWrap::Repeat;
      break;
    case 1:
      texture.wrap = TextureWrap::Clamp;
      break;
    case 2:
      texture.wrap = TextureWrap::Black;
      break;
    case 3:
      texture.wrap = TextureWrap::Mirror;
      break;
    default:
      return unsupported();
    }
    auto path = String(Take(image.Member("name")));
    if (path.empty()) {
      Warn("BLEND_IMAGE_MISSING", "Image has no file path; its constant fallback is used");
      return {};
    }
    if (path.starts_with("//")) {
      path = "./" + path.substr(2);
    } else if (path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':')) {
      Warn("BLEND_IMAGE_ABSOLUTE_PATH", "Absolute Image path is kept as stored, without probing or rewriting");
    } else {
      Warn("BLEND_IMAGE_PATH_UNSUPPORTED", "Image path is neither Blender-relative nor absolute; its constant fallback is used");
      return {};
    }
    std::replace(path.begin(), path.end(), '\\', '/');
    texture.assetPath = std::move(path);
    if (Integer(storage, "interpolation", "int") != 0) {
      Warn("BLEND_IMAGE_INTERPOLATION_UNSUPPORTED", "USD has no texture filter input; non-linear interpolation is not reproduced");
    }
    return texture;
  }

  std::optional<Material> Decode() {
    const auto source = View(context_, "Material");
    const auto id = Take(source.Member("id"));
    const auto fullName = String(Take(id.Member("name")));
    if (!fullName.starts_with("MA")) {
      Fail("BLEND_MATERIAL_STORAGE_INVALID", "Material ID name must carry its MA prefix");
    }
    name_ = fullName.substr(2);
    if (Pointer(id, "lib", "Library") != 0) {
      Warn("BLEND_MATERIAL_LINKED_UNSUPPORTED", "Linked Material data is not followed or bound");
      return {};
    }
    Material material;
    material.sourceName = name_;
    material.diffuseColor = {Float(Take(source.Member("r"))),
        Float(Take(source.Member("g"))), Float(Take(source.Member("b")))};
    material.metallic = Float(Take(source.Member("metallic")));
    material.roughness = Float(Take(source.Member("roughness")));
    if (Byte(source, "use_nodes") == 0) {
      if (Float(Take(source.Member("a"))) != 1) {
        Warn("BLEND_MATERIAL_DEFERRED_INPUT", "Viewport Alpha is deferred; an opaque surface is authored");
      }
      return material;
    }
    if (Byte(source, "use_nodes") != 1) {
      Fail("BLEND_MATERIAL_STORAGE_INVALID", "Material use_nodes flag must be zero or one");
    }
    if (header_.version < 500 || header_.version >= 600) {
      Warn("BLEND_MATERIAL_VERSION_UNSUPPORTED", "Node constants target Blender 5.x; viewport constants are used");
      return material;
    }
    const auto tree = View(Resolve(Pointer(source, "nodetree", "bNodeTree")), "bNodeTree");
    const auto nodes = List(tree, "nodes", "bNode");
    const auto links = List(tree, "links", "bNodeLink");
    std::map<std::uint32_t, std::uint32_t> socketOwners;
    NodeInputs inputs;
    std::set<std::uint32_t> outputs;
    std::optional<std::uint32_t> active;
    for (const auto nodeIndex : nodes) {
      const auto node = View(nodeIndex, "bNode");
      inputs[nodeIndex] = List(node, "inputs", "bNodeSocket");
      const auto nodeOutputs = List(node, "outputs", "bNodeSocket");
      for (const auto& sockets : {inputs[nodeIndex], nodeOutputs}) {
        for (const auto socket : sockets) {
          if (!socketOwners.emplace(socket, nodeIndex).second) {
            Fail("BLEND_MATERIAL_GRAPH_INVALID", "A socket must belong to exactly one node");
          }
        }
      }
      for (const auto socket : nodeOutputs) {
        outputs.insert(socket);
      }
      if (String(Take(node.Member("idname"))) == "ShaderNodeOutputMaterial" &&
          (Integer(node, "flag", "int") & NodeActiveOutput) != 0) {
        if (active || Integer(node, "custom1", "short") != 0 || (Integer(node, "flag", "int") & NodeMuted) != 0) {
          Warn("BLEND_MATERIAL_UNSUPPORTED_NODE", "Muted, renderer-specific or multiple active outputs use viewport constants");
          return material;
        }
        active = nodeIndex;
      }
    }
    IncomingLinks incoming;
    std::set<std::uint32_t> destinations;
    for (const auto linkIndex : links) {
      const auto link = View(linkIndex, "bNodeLink");
      const auto fromNode = Resolve(Pointer(link, "fromnode", "bNode"));
      const auto toNode = Resolve(Pointer(link, "tonode", "bNode"));
      const auto from = Resolve(Pointer(link, "fromsock", "bNodeSocket"));
      const auto to = Resolve(Pointer(link, "tosock", "bNodeSocket"));
      if (!socketOwners.contains(from) || !socketOwners.contains(to) ||
          socketOwners.at(from) != fromNode || socketOwners.at(to) != toNode ||
          !outputs.contains(from) || outputs.contains(to) ||
          !destinations.insert(to).second) {
        Fail("BLEND_MATERIAL_GRAPH_INVALID", "Material links must connect owned output and input sockets once");
      }
      const auto flags = Integer(link, "flag", "int");
      if ((flags & LinkValid) == 0) {
        Warn("BLEND_MATERIAL_UNSUPPORTED_NODE", "Invalid shader links use viewport constants");
        return material;
      }
      if ((flags & LinkMuted) == 0) {
        incoming.emplace(to, std::pair{fromNode, from});
      }
    }
    std::optional<std::uint32_t> surface;
    if (active) {
      for (const auto socket : inputs.at(*active)) {
        if (String(Take(View(socket, "bNodeSocket").Member("identifier"))) == "Surface" && incoming.contains(socket)) {
          const auto [node, output] = incoming.at(socket);
          if (String(Take(View(node, "bNode").Member("idname"))) == "ShaderNodeBsdfPrincipled" &&
              String(Take(View(output, "bNodeSocket").Member("identifier"))) == "BSDF" &&
              (Integer(View(node, "bNode"), "flag", "int") & NodeMuted) == 0) {
            surface = node;
          }
        }
      }
    }
    if (!surface) {
      Warn("BLEND_MATERIAL_UNSUPPORTED_NODE", "Active Surface is not directly linked to Principled BSDF; viewport constants are used");
      return material;
    }
    std::set<std::string> found;
    bool unsupported = false;
    bool deferred = false;
    bool linked = false;
    const std::map<std::string_view, TextureInput> textureInputs = {
        {"Base Color", TextureInput::BaseColor}, {"Metallic", TextureInput::Metallic},
        {"Roughness", TextureInput::Roughness}, {"IOR", TextureInput::Ior},
        {"Coat Weight", TextureInput::Clearcoat}, {"Coat Roughness", TextureInput::ClearcoatRoughness},
        {"Normal", TextureInput::Normal}};
    for (const auto socketIndex : inputs.at(*surface)) {
      const auto socket = View(socketIndex, "bNodeSocket");
      const auto identifier = String(Take(socket.Member("identifier")));
      if (!found.insert(identifier).second) {
        Fail("BLEND_MATERIAL_GRAPH_INVALID", "Principled socket identifiers must be unique");
      }
      const auto isLinked = incoming.contains(socketIndex);
      if (isLinked && textureInputs.contains(identifier)) {
        const auto texture = Texture(textureInputs.at(identifier), socketIndex, inputs, incoming);
        if (texture) {
          material.textures.push_back(*texture);
        }
      } else {
        linked = linked || isLinked;
      }
      deferred = deferred || (identifier == "Emission Color" && isLinked);
      if (identifier == "Base Color") {
        const auto color = Take(Default(socket, "bNodeSocketValueRGBA").Member("value"));
        if (color.ArrayDimensions().size() != 1 || color.ArrayDimensions()[0] != 4) {
          Fail("BLEND_MATERIAL_STORAGE_INVALID", "Base Color must contain four float components");
        }
        for (std::size_t axis = 0; axis < 4; ++axis) {
          const auto value = Float(Take(color.Element(axis)));
          if (axis < 3) {
            material.diffuseColor[axis] = value;
          }
        }
      } else if (identifier == "Metallic" || identifier == "Roughness" || identifier == "IOR" ||
                 identifier == "Coat Weight" || identifier == "Coat Roughness") {
        const auto value = Float(Take(Default(socket, "bNodeSocketValueFloat").Member("value")));
        if (identifier == "Metallic")
          material.metallic = value;
        if (identifier == "Roughness")
          material.roughness = value;
        if (identifier == "IOR")
          material.ior = value;
        if (identifier == "Coat Weight")
          material.clearcoat = value;
        if (identifier == "Coat Roughness")
          material.clearcoatRoughness = value;
      } else if (identifier == "Alpha" || identifier == "Emission Strength") {
        const auto value = Float(Take(Default(socket, "bNodeSocketValueFloat").Member("value")));
        deferred = deferred || isLinked || value != (identifier == "Alpha" ? 1 : 0);
      } else if (Integer(socket, "type", "short") == 0) {
        const auto value = Float(Take(Default(socket, "bNodeSocketValueFloat").Member("value")));
        const auto expected = identifier == "Subsurface Scale"                                        ? 0.005f
                              : identifier == "Subsurface IOR"                                        ? 1.4f
                              : identifier == "Specular IOR Level" || identifier == "Sheen Roughness" ? 0.5f
                              : identifier == "Coat IOR"                                              ? 1.5f
                              : identifier == "Thin Film IOR"                                         ? 1.33f
                                                                                                      : 0.0f;
        unsupported = unsupported || value != expected;
      } else {
        const auto valueIndex = Resolve(Pointer(socket, "default_value", "void"));
        const auto value = Take(ViewDnaBlock(bytes_, blocks_, schema_, header_, valueIndex));
        if (blocks_[valueIndex].count != 1) {
          Fail("BLEND_MATERIAL_STORAGE_INVALID", "Socket defaults must contain exactly one record");
        }
        if (value.Type().name == "bNodeSocketValueRGBA" || value.Type().name == "bNodeSocketValueVector") {
          const auto components = Take(value.Member("value"));
          if (components.ArrayDimensions().size() != 1 || components.ArrayDimensions()[0] != 4) {
            Fail("BLEND_MATERIAL_STORAGE_INVALID", "Color and vector socket defaults must contain four components");
          }
          for (std::size_t axis = 0; axis < 4; ++axis) {
            const auto component = Float(Take(components.Element(axis)));
            if (identifier == "Emission Color") {
              continue;
            }
            const auto expected = value.Type().name == "bNodeSocketValueRGBA" ? 1.0
                                  : identifier == "Subsurface Radius"         ? (axis == 0 ? 1.0 : axis == 1 ? static_cast<double>(0.2f)
                                                                                               : axis == 2   ? static_cast<double>(0.1f)
                                                                                                             : 0.0)
                                                                              : 0.0;
            unsupported = unsupported || component != expected;
          }
        } else if (value.Type().name == "bNodeSocketValueBoolean") {
          const auto flag = Byte(value, "value");
          if (flag > 1) {
            Fail("BLEND_MATERIAL_STORAGE_INVALID", "Boolean socket values must be zero or one");
          }
          unsupported = unsupported || flag != 0;
        } else {
          Warn("BLEND_MATERIAL_UNSUPPORTED_INPUT", "Unsupported Principled socket type is not translated: " + identifier);
        }
      }
    }
    for (const auto required : {"Base Color", "Metallic", "Roughness", "IOR", "Coat Weight", "Coat Roughness", "Alpha", "Emission Strength"}) {
      if (!found.contains(required)) {
        Fail("BLEND_MATERIAL_STORAGE_INVALID", "Principled BSDF is missing a required constant socket");
      }
    }
    if (linked) {
      Warn("BLEND_MATERIAL_UNSUPPORTED_NODE", "Linked inputs outside the supported subset use socket constants");
    }
    if (unsupported) {
      Warn("BLEND_MATERIAL_UNSUPPORTED_INPUT", "Non-default unsupported Principled inputs are not authored");
    }
    if (deferred) {
      Warn("BLEND_MATERIAL_DEFERRED_INPUT", "Alpha and Emission are deferred; an opaque non-emissive surface is authored");
    }
    return material;
  }

  std::span<const std::byte> bytes_;
  std::span<const BlendBlock> blocks_;
  const DnaSchema& schema_;
  const Header& header_;
  const ScenePointers& pointers_;
  Scene& scene_;
  std::vector<Diagnostic>& diagnostics_;
  std::uint32_t context_ = 0;
  std::string name_;
};

} // namespace

void DecodeMaterials(std::span<const std::byte> bytes,
    std::span<const BlendBlock> blocks, const DnaSchema& schema,
    const Header& header, const ScenePointers& pointers,
    const SelectedSceneObjects& selected, Scene& scene,
    std::vector<Diagnostic>& diagnostics) {
  MaterialDecoder(bytes, blocks, schema, header, pointers, scene, diagnostics).Run(selected);
}

} // namespace blend::detail
