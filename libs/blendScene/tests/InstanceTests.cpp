#include <blendScene/Decode.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>

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
  std::uint32_t master;

  explicit Fixture(const std::filesystem::path& path) {
    blend::FileByteSource file(path);
    bytes = Take(blend::ReadFileBytes(file, {4 * 1024 * 1024, 4 * 1024 * 1024, 16, 23}));
    blend::MemoryByteSource memory(bytes);
    header = Take(blend::ReadHeader(memory));
    blocks = Take(blend::ReadBlocks(memory, 10000));
    const auto dna = std::find_if(blocks.begin(), blocks.end(),
        [](const auto& block) { return block.code == std::array<char, 4>{'D', 'N', 'A', '1'}; });
    Require(dna != blocks.end(), "Instance fixture has DNA1");
    schema = Take(blend::ReadDna(std::span<const std::byte>(bytes).subspan(
                                     static_cast<std::size_t>(dna->offset), static_cast<std::size_t>(dna->length)),
        header));
    Require(header.version == 502 && header.pointerSize == 8 && header.byteOrder == blend::ByteOrder::Little,
        "Unchanged Blender 5.2.2 fixture");
    for (const auto& id : Take(blend::ListDatablocks(bytes, blocks, schema))) {
      Require(ids.emplace(id.name, id.blockIndex).second, "Unique fixture ID names");
    }
    const auto scene = Take(blend::SelectScene(bytes, blocks, schema, header));
    master = Resolve(Take(Take(View(scene.blockIndex).Member("master_collection")).Pointer()));
    ids.emplace("GRScene Collection", master);
  }

  blend::DnaValueView View(std::uint32_t index) const {
    return Take(blend::ViewDnaBlock(bytes, blocks, schema, header, index));
  }

  std::uint32_t Resolve(std::uint64_t address) const {
    const auto found = std::find_if(blocks.begin(), blocks.end(),
        [&](const auto& block) { return block.oldAddress == address; });
    Require(address != 0 && found != blocks.end(), "Fixture reference resolves exactly");
    return static_cast<std::uint32_t>(found - blocks.begin());
  }

  void Store(std::vector<std::byte>& changed, const blend::DnaValueView& value, std::uint64_t bits) const {
    const auto offset = static_cast<std::size_t>(value.Bytes().data() - bytes.data());
    Require(value.Bytes().size() <= 8 && offset + value.Bytes().size() <= changed.size(), "Bounded scalar mutation");
    for (std::size_t byte = 0; byte < value.Bytes().size(); ++byte) {
      changed[offset + byte] = static_cast<std::byte>((bits >> (byte * 8)) & 0xff);
    }
  }

  blend::DnaValueView Instance(std::string_view name) const {
    const auto object = View(ids.at("OB" + std::string(name)));
    const auto* structure = schema.FindStruct("Object");
    return Take(object.Member(structure->FindMember("instance_collection") ? "instance_collection" : "dup_group"));
  }

  std::vector<std::string> List(std::uint32_t collection, std::string_view member,
      std::string_view target) const {
    const auto list = Take(View(collection).Member(member));
    auto address = Take(Take(list.Member("first")).Pointer());
    std::vector<std::string> names;
    while (address != 0) {
      Require(names.size() < 32, "Bounded oracle list");
      const auto node = View(Resolve(address));
      const auto object = View(Resolve(Take(Take(node.Member(target)).Pointer())));
      const auto name = Take(Take(object.Member("id")).Member("name")).Bytes();
      const auto end = std::find(name.begin(), name.end(), std::byte{0});
      Require(end != name.end(), "Terminated oracle name");
      names.emplace_back(reinterpret_cast<const char*>(name.data() + 2),
          static_cast<std::size_t>(end - name.begin() - 2));
      address = Take(Take(node.Member("next")).Pointer());
    }
    return names;
  }

  void CheckOracle(const std::filesystem::path& path) const {
    auto oracle = path;
    oracle.replace_extension(".oracle.txt");
    std::ifstream input(oracle);
    std::string marker, version, scene;
    unsigned format = 0, collections = 0, objects = 0;
    Require(static_cast<bool>(input >> marker >> format) && marker == "BLEND_INSTANCE_ORACLE" && format == 1,
        "Instance oracle header");
    Require(static_cast<bool>(input >> std::quoted(version) >> std::quoted(scene) >> collections >> objects) &&
                version == "5.2.2 LTS" && scene == "Instances" && collections == 5 && objects == 9,
        "Pinned oracle scope");
    for (unsigned index = 0; index < collections; ++index) {
      std::string name;
      unsigned children = 0, members = 0;
      Require(static_cast<bool>(input >> marker >> std::quoted(name) >> children >> members) &&
                  marker == "COLLECTION" && children <= 5 && members <= 9,
          "Collection oracle record");
      for (const auto& [member, target, count] :
          {std::tuple{"children", "collection", children}, std::tuple{"gobject", "ob", members}}) {
        std::vector<std::string> expected(count);
        for (auto& value : expected) {
          Require(static_cast<bool>(input >> std::quoted(value)), "Oracle list name");
        }
        Require(List(ids.at("GR" + name), member, target) == expected, "Saved Collection list matches oracle");
      }
    }
    const auto selected = Take(blend::SelectSceneObjectValues(bytes, blocks, schema, header, {23, 3}));
    Require(selected.objects.size() == 3 && selected.scene.metadata.sourceScene == scene,
        "Instance-only Objects and parents never enter membership");
    for (unsigned index = 0; index < objects; ++index) {
      std::string name, parent, target;
      unsigned active = 0, member = 0;
      Require(static_cast<bool>(input >> marker >> std::quoted(name) >> std::quoted(parent) >>
                                std::quoted(target) >> active >> member) &&
                  marker == "OBJECT" && active <= 1 && member <= 1,
          "Object oracle record");
      const auto block = ids.at("OB" + name);
      const auto object = View(block);
      Require(Take(Take(object.Member("type")).SignedInteger()) == 0 &&
                  Take(Take(object.Member("data")).Pointer()) == 0 &&
                  Take(Take(object.Member("parent")).Pointer()) == (parent.empty() ? 0 : blocks[ids.at("OB" + parent)].oldAddress) &&
                  Take(Instance(name).Pointer()) == (target.empty() ? 0 : blocks[ids.at("GR" + target)].oldAddress) &&
                  ((Take(Take(object.Member("transflag")).SignedInteger()) & (1 << 8)) != 0) == (active != 0),
          "Saved Object values match oracle");
      const auto found = std::find_if(selected.objects.begin(), selected.objects.end(),
          [&](const auto& value) { return value.blockIndex == block; });
      Require((found != selected.objects.end()) == (member != 0), "Saved membership matches oracle");
      if (found != selected.objects.end()) {
        Require(found->sourceName == name && found->values &&
                    found->parentBlockIndex == (parent.empty() ? std::optional<std::uint32_t>{} : ids.at("OB" + parent)) &&
                    found->values->instanceCollectionBlockIndex == (target.empty() ? std::optional<std::uint32_t>{} : ids.at("GR" + target)),
            "Owning selected references match oracle");
      }
    }
    Require(!(input >> marker), "No trailing oracle records");
  }
};

struct Case {
  std::string name;
  std::vector<std::byte> bytes;
  std::string code;
  std::uint32_t context;
};

std::vector<Case> Cases(const Fixture& fixture) {
  std::vector<Case> cases;
  cases.push_back({"active", fixture.bytes, "BLEND_SCENE_INSTANCE_UNSUPPORTED", fixture.ids.at("OBRootInstance")});
  auto inactive = fixture.bytes;
  for (const auto* name : {"RootInstance", "SharedInstance"}) {
    const auto flags = Take(fixture.View(fixture.ids.at("OB" + std::string(name))).Member("transflag"));
    fixture.Store(inactive, flags, static_cast<std::uint64_t>(Take(flags.SignedInteger())) & ~(1 << 8));
  }
  cases.push_back({"inactive", inactive, "", 0});
  const auto add = [&](std::string name, const blend::DnaValueView& member, std::uint64_t bits,
                       std::string code, std::uint32_t context) {
    auto changed = inactive;
    fixture.Store(changed, member, bits);
    cases.push_back({std::move(name), std::move(changed), std::move(code), context});
  };
  const auto nested = fixture.ids.at("OBNestedInstance");
  const auto target = fixture.ids.at("GRTargetB");
  const auto instance = fixture.Instance("NestedInstance");
  const auto missing = std::numeric_limits<std::uint64_t>::max();
  Require(std::none_of(fixture.blocks.begin(), fixture.blocks.end(),
              [&](const auto& block) { return block.oldAddress == missing; }),
      "Mutation key is absent");
  add("missing", instance, 0, "BLEND_SCENE_REFERENCE_INVALID", nested);
  add("unresolved", instance, missing, "BLEND_SCENE_REFERENCE_INVALID", nested);
  add("interior", instance, fixture.blocks[target].oldAddress + 1, "BLEND_SCENE_REFERENCE_INVALID", nested);
  const auto wrong = fixture.ids.at("OBOutsideParent");
  add("wrong_kind", instance, fixture.blocks[wrong].oldAddress, "BLEND_SCENE_REFERENCE_INVALID", wrong);
  add("linked", Take(Take(fixture.View(target).Member("id")).Member("lib")), missing,
      "BLEND_SCENE_LINKED_UNSUPPORTED", target);
  add("cycle", instance, fixture.blocks[fixture.ids.at("GRTargetA")].oldAddress, "BLEND_SCENE_CYCLE", nested);
  add("master_cycle", instance, fixture.blocks[fixture.master].oldAddress, "BLEND_SCENE_CYCLE", nested);
  add("list", Take(Take(fixture.View(target).Member("gobject")).Member("last")), 0,
      "BLEND_SCENE_LIST_INVALID", target);
  for (const auto* name : {"OutsideParent", "InstanceParent"}) {
    add(name, fixture.Instance(name), missing, "BLEND_SCENE_REFERENCE_INVALID", fixture.ids.at("OB" + std::string(name)));
  }
  const auto parent = fixture.ids.at("OBInstanceParent");
  add("linked_parent", Take(Take(fixture.View(parent).Member("id")).Member("lib")), missing,
      "BLEND_SCENE_LINKED_UNSUPPORTED", parent);
  const auto count = cases.size();
  for (std::size_t index = 2; index < count; ++index) {
    auto active = cases[index];
    active.name = "active_" + active.name;
    for (const auto* name : {"RootInstance", "SharedInstance"}) {
      const auto flags = Take(fixture.View(fixture.ids.at("OB" + std::string(name))).Member("transflag"));
      fixture.Store(active.bytes, flags, static_cast<std::uint64_t>(Take(flags.SignedInteger())));
    }
    cases.push_back(std::move(active));
  }
  return cases;
}

void CheckFailure(const blend::Diagnostic& error, std::span<const blend::BlendBlock> blocks,
    std::string_view code, std::uint32_t context) {
  Require(error.code == code && error.severity == blend::Severity::Fatal && !error.recoverable &&
              error.blockIndex == context && error.byteOffset == blocks[context].offset,
      "Expected contextual " + std::string(code) + ", got " + error.code);
}

void Run(const std::filesystem::path& path, const std::filesystem::path& output) {
  Fixture fixture(path);
  fixture.CheckOracle(path);
  for (const auto limits : {blend::SceneTraversalLimits{7, 2}, blend::SceneTraversalLimits{8, 1}}) {
    const auto limited = blend::SelectSceneObjects(fixture.bytes, fixture.blocks, fixture.schema, fixture.header, limits);
    Require(!limited.HasValue(), "One-below generic visit/depth limit fails");
    CheckFailure(limited.GetError(), fixture.blocks,
        limits.maxVisited == 7 ? "BLEND_SCENE_VISIT_LIMIT" : "BLEND_SCENE_DEPTH_LIMIT",
        fixture.ids.at("OBChild"));
  }
  const auto generic = Take(blend::SelectSceneObjects(fixture.bytes, fixture.blocks, fixture.schema, fixture.header, {8, 2}));
  Require(generic.objects.size() == 3, "Generic selection does not expand instances");
  const auto values = Take(blend::SelectSceneObjectValues(fixture.bytes, fixture.blocks, fixture.schema, fixture.header, {23, 3}));
  for (const auto limits : {blend::SceneTraversalLimits{22, 3}, blend::SceneTraversalLimits{23, 2}}) {
    const auto limited = blend::SelectSceneObjectValues(fixture.bytes, fixture.blocks, fixture.schema, fixture.header, limits);
    Require(!limited.HasValue(), "One-below visit/depth limit fails");
    const auto context = limits.maxVisited == 22
                             ? fixture.Resolve(Take(Take(Take(fixture.View(fixture.ids.at("GRParentTarget")).Member("gobject")).Member("first")).Pointer()))
                             : fixture.ids.at("GRTargetA");
    CheckFailure(limited.GetError(), fixture.blocks,
        limits.maxVisited == 22 ? "BLEND_SCENE_VISIT_LIMIT" : "BLEND_SCENE_DEPTH_LIMIT", context);
  }
  auto reversed = fixture.blocks;
  std::reverse(reversed.begin(), reversed.end());
  const auto reordered = Take(blend::SelectSceneObjectValues(fixture.bytes, reversed, fixture.schema, fixture.header, {23, 3}));
  Require(values.objects.size() == reordered.objects.size(), "Reordered membership count");
  for (std::size_t index = 0; index < values.objects.size(); ++index) {
    const auto& left = values.objects[index];
    const auto& right = reordered.objects[index];
    Require(left.sourceName == right.sourceName &&
                fixture.blocks[left.blockIndex].oldAddress == reversed[right.blockIndex].oldAddress &&
                left.values->type == right.values->type &&
                left.values->hiddenForRender == right.values->hiddenForRender &&
                left.values->transformFlags == right.values->transformFlags,
        "Reordering preserves saved discovery and values");
    for (const auto& [original, reordered] :
        {std::pair{left.parentBlockIndex, right.parentBlockIndex},
            std::pair{left.dataBlockIndex, right.dataBlockIndex},
            std::pair{left.values->instanceCollectionBlockIndex, right.values->instanceCollectionBlockIndex}}) {
      Require(original.has_value() == reordered.has_value(), "Reordering preserves optional references");
      if (original) {
        Require(fixture.blocks[*original].oldAddress == reversed[*reordered].oldAddress,
            "Reference index refers to caller's reordered blocks");
      }
    }
  }
  if (!output.empty()) {
    std::filesystem::create_directories(output);
  }
  for (const auto& test : Cases(fixture)) {
    for (const auto* blocks : {&fixture.blocks, &fixture.blocks, &reversed}) {
      const auto decoded = blend::DecodeScene(test.bytes, *blocks, fixture.schema, fixture.header, {23, 3});
      if (test.code.empty()) {
        const auto scene = Take(decoded);
        Require(decoded.Diagnostics().empty() && scene.objects.size() == 3 && scene.meshes.empty() &&
                    scene.objects[0].sourceName == "RootInstance" && scene.objects[1].sourceName == "SharedInstance" &&
                    scene.objects[2].sourceName == "Child" && !scene.objects[2].parent,
            "Inactive references validate without instance expansion or parent publication");
        Require(scene.objects[2].worldTransform[0][3] == 2 &&
                    scene.objects[2].worldTransform[1][3] == 6 && scene.objects[2].worldTransform[2][3] == -4,
            "Parent-only source transform is composed once");
      } else {
        Require(!decoded.HasValue(), "Fatal instance failure publishes no partial Scene");
        const auto context = blocks == &fixture.blocks ? test.context
                                                       : static_cast<std::uint32_t>(fixture.blocks.size() - 1 - test.context);
        CheckFailure(decoded.GetError(), *blocks, test.code, context);
        if (test.name != "active") {
          const auto selected = blend::SelectSceneObjectValues(test.bytes, *blocks, fixture.schema, fixture.header, {23, 3});
          Require(!selected.HasValue(), "Recursive value validation fails before decoding");
          CheckFailure(selected.GetError(), *blocks, test.code, context);
        }
      }
    }
    if (!output.empty()) {
      std::ofstream data(output / (test.name + ".blend"), std::ios::binary);
      data.write(reinterpret_cast<const char*>(test.bytes.data()), static_cast<std::streamsize>(test.bytes.size()));
      Require(static_cast<bool>(data), "Writing plugin mutation input");
      std::ofstream expected(output / (test.name + ".expected.txt"));
      expected << (test.code.empty() ? "SUCCESS" : test.code) << ' '
               << fixture.blocks[test.context].offset << ' ' << test.context << '\n';
      Require(static_cast<bool>(expected), "Writing plugin expected diagnostic");
    }
  }
}

} // namespace

int main(int argc, char** argv) {
  try {
    Require(argc == 2 || (argc == 4 && std::string_view(argv[2]) == "--write-cases"),
        "Usage: blendSceneInstanceTests fixture.blend [--write-cases directory]");
    Run(std::filesystem::path(argv[1]), argc == 4 ? std::filesystem::path(argv[3]) : std::filesystem::path{});
    std::cout << "Verified saved recursive instance graph, limits and contextual failures\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
