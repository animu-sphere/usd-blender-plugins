#include <blendScene/Selection.h>

#include <algorithm>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace blend {
namespace {

template <class Value>
Value Take(const Result<Value>& result) {
  if (!result.HasValue()) {
    throw result.GetError();
  }
  return result.GetValue();
}

class CollectionWalk {
public:
  CollectionWalk(std::span<const std::byte> bytes,
      std::span<const BlendBlock> blocks, const DnaSchema& schema,
      const Header& header, const PointerMap& pointers,
      const SceneTraversalLimits& limits, bool readValues)
      : bytes_(bytes), blocks_(blocks), schema_(schema), header_(header),
        pointers_(pointers), limits_(limits), readValues_(readValues) {
  }

  std::vector<SelectedObject> Run(std::uint32_t sceneIndex) {
    const auto scene = Take(ViewDnaBlock(bytes_, blocks_, schema_, header_, sceneIndex));
    const auto root = Resolve(Pointer(scene, "master_collection", "Collection", sceneIndex), sceneIndex);
    struct Frame {
      std::uint32_t index;
      std::vector<std::uint32_t> children;
      std::size_t next = 0;
    };
    std::vector<Frame> stack;
    auto enter = [&](std::uint32_t index, std::uint32_t referrer) {
      if (stack.size() >= limits_.maxDepth) {
        Fail("BLEND_SCENE_DEPTH_LIMIT", "Collection depth exceeds the caller's limit", referrer);
      }
      Visit(referrer);
      active_.emplace(index, true);
      const auto collection = Bind(index, "Collection", {'G', 'R', 0, 0}, true);
      Name(collection, "GR", index);
      WalkList(collection, "gobject", "CollectionObject", "ob", "Object", index);
      auto children = WalkList(collection, "children", "CollectionChild", "collection", "Collection", index);
      stack.push_back({index, std::move(children)});
    };
    enter(root, sceneIndex);
    while (!stack.empty()) {
      auto& frame = stack.back();
      if (frame.next == frame.children.size()) {
        active_.at(frame.index) = false;
        stack.pop_back();
        continue;
      }
      const auto child = frame.children[frame.next++];
      const auto referrer = frame.index;
      const auto found = active_.find(child);
      if (found != active_.end()) {
        if (found->second) {
          Fail("BLEND_SCENE_CYCLE", "Collection membership contains a cycle", referrer);
        }
        continue;
      }
      enter(child, referrer);
    }
    ValidateObjects();
    return std::move(objects_);
  }

private:
  void ValidateObjects() {
    std::unordered_map<std::uint32_t, std::optional<std::uint32_t>> parents;
    std::unordered_map<std::uint32_t, std::optional<std::uint32_t>> data;
    std::unordered_map<std::uint32_t, SavedObjectValues> values;
    std::unordered_set<std::uint32_t> completed;
    for (auto& selected : objects_) {
      std::vector<std::uint32_t> chain;
      std::unordered_set<std::uint32_t> active;
      auto current = selected.blockIndex;
      auto referrer = current;
      while (!completed.contains(current)) {
        if (!active.insert(current).second) {
          Fail("BLEND_SCENE_CYCLE", "Object parenting contains a cycle", referrer);
        }
        if (chain.size() >= limits_.maxDepth) {
          Fail("BLEND_SCENE_DEPTH_LIMIT", "Object parent depth exceeds the caller's limit", referrer);
        }
        if (objectIndices_.insert(current).second && !dataIndices_.contains(current)) {
          Visit(referrer);
        }
        const auto object = Bind(current, "Object", {'O', 'B', 0, 0});
        Name(object, "OB", current);
        data.emplace(current, ValidateData(object, current));
        if (readValues_) {
          values.emplace(current, ReadValues(object, current, data.at(current)));
        }
        chain.push_back(current);
        const auto address = Pointer(object, "parent", "Object", current);
        const auto parent = address == 0 ? std::optional<std::uint32_t>{} : Resolve(address, current);
        parents.emplace(current, parent);
        if (!parent) {
          break;
        }
        referrer = current;
        current = *parent;
      }
      completed.insert(chain.begin(), chain.end());
      selected.parentBlockIndex = parents.at(selected.blockIndex);
      selected.dataBlockIndex = data.at(selected.blockIndex);
      if (readValues_) {
        selected.values = values.at(selected.blockIndex);
      }
    }
  }

  std::int16_t Short(const DnaValueView& object, std::string_view member,
      std::uint32_t index) const {
    const auto value = Take(object.Member(member));
    if (value.Type().name != "short" || value.Type().length != 2 ||
        value.PointerLevel() != 0 || !value.ArrayDimensions().empty()) {
      Fail("BLEND_SCENE_REFERENCE_INVALID", "Object value must be a scalar two-byte short", index);
    }
    return static_cast<std::int16_t>(Take(value.SignedInteger()));
  }

  SavedObjectValues ReadValues(const DnaValueView& object, std::uint32_t index,
      std::optional<std::uint32_t> data) {
    const auto type = Short(object, "type", index);
    std::string_view code;
    std::string_view dnaType;
    switch (type) {
    case 0:
      code = "IM";
      dnaType = "Image";
      break;
    case 1:
      code = "ME";
      dnaType = "Mesh";
      break;
    case 2:
    case 3:
    case 4:
      code = "CU";
      dnaType = "Curve";
      break;
    case 5:
      code = "MB";
      dnaType = "MetaBall";
      break;
    case 10:
      code = "LA";
      dnaType = "Lamp";
      break;
    case 11:
      code = "CA";
      dnaType = "Camera";
      break;
    case 12:
      code = "SK";
      dnaType = "Speaker";
      break;
    case 13:
      code = "LP";
      dnaType = "LightProbe";
      break;
    case 22:
      code = "LT";
      dnaType = "Lattice";
      break;
    case 25:
      code = "AR";
      dnaType = "bArmature";
      break;
    case 26:
      code = "GD";
      dnaType = "bGPdata";
      break;
    case 27:
      code = "CV";
      dnaType = "Curves";
      break;
    case 28:
      code = "PT";
      dnaType = "PointCloud";
      break;
    case 29:
      code = "VO";
      dnaType = "Volume";
      break;
    case 30:
      code = "GP";
      dnaType = "GreasePencil";
      break;
    default:
      Fail("BLEND_SCENE_OBJECT_TYPE_UNSUPPORTED", "Saved Object type has no verified data mapping", index);
    }
    if (!data && type != 0) {
      Fail("BLEND_SCENE_REFERENCE_INVALID", "This Object type requires a data ID", index);
    }
    if (data) {
      Bind(*data, dnaType, {code[0], code[1], 0, 0});
    }
    const auto* structure = schema_.FindStruct("Object");
    const auto visibility = Take(object.Member(
        structure->FindMember("visibility_flag") ? "visibility_flag" : "restrictflag"));
    if (((visibility.Type().name != "short" || visibility.Type().length != 2) &&
            (visibility.Type().name != "int" || visibility.Type().length != 4)) ||
        visibility.PointerLevel() != 0 || !visibility.ArrayDimensions().empty()) {
      Fail("BLEND_SCENE_REFERENCE_INVALID", "Object visibility must be a scalar short or int", index);
    }
    const auto visibilityFlags = Take(visibility.SignedInteger());
    const auto flags = Short(object, "transflag", index);
    const auto address = Pointer(object,
        structure->FindMember("instance_collection") ? "instance_collection" : "dup_group", "Collection", index);
    std::optional<std::uint32_t> collection;
    if (address != 0) {
      collection = Resolve(address, index);
      if (instanceIndices_.insert(*collection).second && !active_.contains(*collection) &&
          !dataIndices_.contains(*collection) && !objectIndices_.contains(*collection)) {
        Visit(index);
      }
      const auto target = Bind(*collection, "Collection", {'G', 'R', 0, 0}, true);
      Name(target, "GR", *collection);
    } else if ((flags & (1 << 8)) != 0) {
      Fail("BLEND_SCENE_REFERENCE_INVALID", "Collection instancing requires a saved Collection", index);
    }
    return {type, (visibilityFlags & (1 << 2)) != 0, flags, collection};
  }

  std::optional<std::uint32_t> ValidateData(const DnaValueView& object,
      std::uint32_t referrer) {
    const auto member = Take(object.Member("data"));
    if ((member.Type().name != "void" && member.Type().name != "ID") ||
        member.PointerLevel() != 1 || !member.ArrayDimensions().empty()) {
      Fail("BLEND_SCENE_REFERENCE_INVALID", "Object data must be a scalar void or ID pointer", referrer);
    }
    const auto address = Take(member.Pointer());
    if (address == 0) {
      return {};
    }
    const auto index = Resolve(address, referrer);
    if (dataIndices_.insert(index).second) {
      if (!objectIndices_.contains(index)) {
        Visit(referrer);
      }
      const auto& block = blocks_[index];
      if (block.count != 1 || block.code[0] < 'A' || block.code[0] > 'Z' ||
          block.code[1] < 'A' || block.code[1] > 'Z' ||
          block.code[2] != 0 || block.code[3] != 0) {
        Fail("BLEND_SCENE_REFERENCE_INVALID", "Object data must target a single ID block", index);
      }
      const auto view = Take(ViewDnaBlock(bytes_, blocks_, schema_, header_, index));
      Name(view, std::string_view(block.code.data(), 2), index);
    }
    return index;
  }

  [[noreturn]] void Fail(const char* code, const char* message, std::uint32_t index) const {
    throw Diagnostic{code, Severity::Fatal, message, blocks_[index].offset, index, {}, false};
  }

  void Visit(std::uint32_t referrer) {
    if (visited_ == limits_.maxVisited) {
      Fail("BLEND_SCENE_VISIT_LIMIT", "Scene graph exceeds the caller's visit limit", referrer);
    }
    ++visited_;
  }

  DnaValueView Bind(std::uint32_t index, std::string_view type,
      std::array<char, 4> code, bool allowData = false) const {
    if (blocks_[index].count != 1 ||
        (blocks_[index].code != code &&
            !(allowData && blocks_[index].code == std::array<char, 4>{'D', 'A', 'T', 'A'}))) {
      Fail("BLEND_SCENE_REFERENCE_INVALID", "Reference target has incompatible code or count", index);
    }
    const auto view = Take(ViewDnaBlock(bytes_, blocks_, schema_, header_, index));
    if (view.Type().name != type) {
      Fail("BLEND_SCENE_REFERENCE_INVALID", "Reference target has incompatible SDNA type", index);
    }
    return view;
  }

  DnaValueView Embedded(const DnaValueView& view, std::string_view member,
      std::string_view type, std::uint32_t index) const {
    const auto value = Take(view.Member(member));
    if (value.Type().name != type || value.PointerLevel() != 0 ||
        !value.ArrayDimensions().empty()) {
      Fail("BLEND_SCENE_REFERENCE_INVALID", "Required member is not an embedded scalar", index);
    }
    return value;
  }

  std::uint64_t Pointer(const DnaValueView& view, std::string_view member,
      std::string_view type, std::uint32_t index) const {
    const auto value = Take(view.Member(member));
    if (value.Type().name != type || value.PointerLevel() != 1 ||
        !value.ArrayDimensions().empty()) {
      Fail("BLEND_SCENE_REFERENCE_INVALID", "Required member is not a compatible scalar pointer", index);
    }
    return Take(value.Pointer());
  }

  std::uint32_t Resolve(std::uint64_t address, std::uint32_t referrer) const {
    const auto target = Take(pointers_.Resolve(address));
    if (!target) {
      Fail("BLEND_SCENE_REFERENCE_INVALID", "Required reference is null, absent or interior", referrer);
    }
    return *target;
  }

  std::string Name(const DnaValueView& view, std::string_view prefix,
      std::uint32_t index) const {
    const auto id = Embedded(view, "id", "ID", index);
    if (Pointer(id, "lib", "Library", index) != 0) {
      Fail("BLEND_SCENE_LINKED_UNSUPPORTED", "Linked IDs are not followed", index);
    }
    const auto name = Take(id.Member("name"));
    const auto bytes = name.Bytes();
    const auto end = std::find(bytes.begin(), bytes.end(), std::byte{0});
    if (name.Type().name != "char" || name.Type().length != 1 ||
        name.PointerLevel() != 0 || name.ArrayDimensions().size() != 1 ||
        bytes.size() < 3 || bytes[0] != static_cast<std::byte>(prefix[0]) ||
        bytes[1] != static_cast<std::byte>(prefix[1]) ||
        end == bytes.end() || end < bytes.begin() + 2) {
      Fail("BLEND_SCENE_NAME_INVALID", "ID.name has invalid prefix or termination", index);
    }
    return {reinterpret_cast<const char*>(bytes.data() + 2),
        static_cast<std::size_t>(end - bytes.begin() - 2)};
  }

  std::vector<std::uint32_t> WalkList(const DnaValueView& collection,
      std::string_view member, std::string_view nodeType,
      std::string_view targetMember, std::string_view targetType,
      std::uint32_t owner) {
    const auto list = Embedded(collection, member, "ListBase", owner);
    auto current = Pointer(list, "first", "void", owner);
    const auto last = Pointer(list, "last", "void", owner);
    if ((current == 0) != (last == 0)) {
      Fail("BLEND_SCENE_LIST_INVALID", "ListBase endpoints disagree", owner);
    }
    std::uint64_t previous = 0;
    auto referrer = owner;
    std::unordered_set<std::uint32_t> seen;
    std::vector<std::uint32_t> targets;
    while (current != 0) {
      const auto index = Resolve(current, referrer);
      if (!seen.insert(index).second) {
        Fail("BLEND_SCENE_CYCLE", "ListBase next chain contains a cycle", index);
      }
      if (!listNodes_.insert(index).second) {
        Fail("BLEND_SCENE_LIST_INVALID", "A list node belongs to more than one list", index);
      }
      Visit(index);
      const auto node = Bind(index, nodeType, {'D', 'A', 'T', 'A'});
      if (Pointer(node, "prev", nodeType, index) != previous) {
        Fail("BLEND_SCENE_LIST_INVALID", "List node prev does not match its predecessor", index);
      }
      const auto next = Pointer(node, "next", nodeType, index);
      if ((current == last) != (next == 0)) {
        Fail("BLEND_SCENE_LIST_INVALID", "ListBase last does not match the terminal node", index);
      }
      const auto target = Resolve(Pointer(node, targetMember, targetType, index), index);
      if (targetType == "Object") {
        if (objectIndices_.insert(target).second) {
          Visit(index);
          const auto object = Bind(target, "Object", {'O', 'B', 0, 0});
          objects_.push_back({target, Name(object, "OB", target), {}, {}, {}});
        }
      } else {
        targets.push_back(target);
      }
      previous = current;
      current = next;
      referrer = index;
    }
    return targets;
  }

  std::span<const std::byte> bytes_;
  std::span<const BlendBlock> blocks_;
  const DnaSchema& schema_;
  const Header& header_;
  const PointerMap& pointers_;
  const SceneTraversalLimits& limits_;
  bool readValues_;
  std::uint32_t visited_ = 0;
  std::unordered_map<std::uint32_t, bool> active_;
  std::unordered_set<std::uint32_t> listNodes_;
  std::unordered_set<std::uint32_t> objectIndices_;
  std::unordered_set<std::uint32_t> dataIndices_;
  std::unordered_set<std::uint32_t> instanceIndices_;
  std::vector<SelectedObject> objects_;
};

Result<SelectedSceneObjects> SelectObjects(std::span<const std::byte> bytes,
    std::span<const BlendBlock> blocks, const DnaSchema& schema,
    const Header& header, const SceneTraversalLimits& limits, bool readValues) {
  try {
    if (limits.maxVisited == 0 || limits.maxDepth == 0) {
      return Result<SelectedSceneObjects>(Diagnostic{"BLEND_SCENE_LIMITS", Severity::Fatal,
          "Positive visit and depth limits are required", {}, {}, {}, false});
    }
    const auto scene = SelectScene(bytes, blocks, schema, header);
    if (!scene.HasValue()) {
      return Result<SelectedSceneObjects>(scene.GetError());
    }
    const auto pointers = BuildPointerMap(blocks);
    if (!pointers.HasValue()) {
      return Result<SelectedSceneObjects>(pointers.GetError());
    }
    CollectionWalk walk(bytes, blocks, schema, header, pointers.GetValue(), limits, readValues);
    return Result<SelectedSceneObjects>(SelectedSceneObjects{
        scene.GetValue(), walk.Run(scene.GetValue().blockIndex)});
  } catch (const Diagnostic& error) {
    return Result<SelectedSceneObjects>(error);
  } catch (const std::bad_alloc&) {
    return Result<SelectedSceneObjects>(Diagnostic{"BLEND_SCENE_ALLOCATION", Severity::Fatal,
        "Unable to allocate scene traversal", {}, {}, {}, false});
  } catch (const std::length_error&) {
    return Result<SelectedSceneObjects>(Diagnostic{"BLEND_SCENE_ALLOCATION", Severity::Fatal,
        "Scene traversal exceeds allocation limits", {}, {}, {}, false});
  }
}

} // namespace

Result<SelectedSceneObjects> SelectSceneObjects(std::span<const std::byte> bytes,
    std::span<const BlendBlock> blocks, const DnaSchema& schema,
    const Header& header, const SceneTraversalLimits& limits) {
  return SelectObjects(bytes, blocks, schema, header, limits, false);
}

Result<SelectedSceneObjects> SelectSceneObjectValues(std::span<const std::byte> bytes,
    std::span<const BlendBlock> blocks, const DnaSchema& schema,
    const Header& header, const SceneTraversalLimits& limits) {
  return SelectObjects(bytes, blocks, schema, header, limits, true);
}

} // namespace blend