#include <blendScene/Selection.h>

#include <algorithm>
#include <cmath>
#include <new>
#include <stdexcept>

namespace blend {
namespace {

Diagnostic Failure(const char* code, const char* message,
    std::span<const BlendBlock> blocks,
    std::optional<std::uint32_t> index = std::nullopt) {
  return {code, Severity::Fatal, message,
      index ? std::optional<std::uint64_t>(blocks[*index].offset) : std::nullopt,
      index, {}, false};
}

bool ScalarPointer(const DnaValueView& view, std::string_view type) {
  return view.Type().name == type && view.PointerLevel() == 1 &&
         view.ArrayDimensions().empty();
}

}

Result<SelectedScene> SelectScene(std::span<const std::byte> bytes,
    std::span<const BlendBlock> blocks, const DnaSchema& schema,
    const Header& header) {
  try {
    const auto pointers = BuildPointerMap(blocks);
    if (!pointers.HasValue()) {
      return Result<SelectedScene>(pointers.GetError());
    }
    std::optional<std::uint32_t> globalIndex;
    for (std::size_t index = 0; index < blocks.size(); ++index) {
      if (blocks[index].code != std::array<char, 4>{'G', 'L', 'O', 'B'}) {
        continue;
      }
      if (globalIndex) {
        return Result<SelectedScene>(Failure("BLEND_SCENE_GLOBAL_INVALID",
            "More than one GLOB block", blocks, static_cast<std::uint32_t>(index)));
      }
      globalIndex = static_cast<std::uint32_t>(index);
    }
    if (!globalIndex) {
      return Result<SelectedScene>(Failure("BLEND_SCENE_GLOBAL_INVALID",
          "A saved scene requires one GLOB block", blocks));
    }
    if (blocks[*globalIndex].count != 1) {
      return Result<SelectedScene>(Failure("BLEND_SCENE_GLOBAL_INVALID",
          "GLOB must contain one FileGlobal", blocks, globalIndex));
    }
    const auto global = ViewDnaBlock(bytes, blocks, schema, header, *globalIndex);
    if (!global.HasValue()) {
      return Result<SelectedScene>(global.GetError());
    }
    if (global.GetValue().Type().name != "FileGlobal") {
      return Result<SelectedScene>(Failure("BLEND_SCENE_GLOBAL_INVALID",
          "GLOB must contain one FileGlobal", blocks, globalIndex));
    }
    const auto current = global.GetValue().Member("curscene");
    if (!current.HasValue()) {
      return Result<SelectedScene>(current.GetError());
    }
    if (!ScalarPointer(current.GetValue(), "Scene")) {
      return Result<SelectedScene>(Failure("BLEND_SCENE_REFERENCE_INVALID",
          "FileGlobal.curscene must be a scalar Scene pointer", blocks, globalIndex));
    }
    const auto address = current.GetValue().Pointer();
    if (!address.HasValue()) {
      return Result<SelectedScene>(address.GetError());
    }
    if (address.GetValue() == 0) {
      return Result<SelectedScene>(Failure("BLEND_SCENE_ACTIVE_MISSING",
          "FileGlobal.curscene is null; no scene fallback is selected", blocks, globalIndex));
    }
    const auto target = pointers.GetValue().Resolve(address.GetValue());
    if (!target.HasValue()) {
      return Result<SelectedScene>(target.GetError());
    }
    if (!target.GetValue()) {
      return Result<SelectedScene>(Failure("BLEND_SCENE_REFERENCE_INVALID",
          "FileGlobal.curscene does not resolve to a saved block", blocks, globalIndex));
    }
    const auto sceneIndex = *target.GetValue();
    if (blocks[sceneIndex].code != std::array<char, 4>{'S', 'C', 0, 0} ||
        blocks[sceneIndex].count != 1) {
      return Result<SelectedScene>(Failure("BLEND_SCENE_REFERENCE_INVALID",
          "FileGlobal.curscene must resolve to one SC datablock", blocks, sceneIndex));
    }
    const auto scene = ViewDnaBlock(bytes, blocks, schema, header, sceneIndex);
    if (!scene.HasValue()) {
      return Result<SelectedScene>(scene.GetError());
    }
    if (scene.GetValue().Type().name != "Scene") {
      return Result<SelectedScene>(Failure("BLEND_SCENE_REFERENCE_INVALID",
          "SC must have Scene SDNA type", blocks, sceneIndex));
    }
    const auto id = scene.GetValue().Member("id");
    if (!id.HasValue()) {
      return Result<SelectedScene>(id.GetError());
    }
    if (id.GetValue().Type().name != "ID" || id.GetValue().PointerLevel() != 0 ||
        !id.GetValue().ArrayDimensions().empty()) {
      return Result<SelectedScene>(Failure("BLEND_SCENE_REFERENCE_INVALID",
          "Scene.id must be an embedded scalar ID", blocks, sceneIndex));
    }
    const auto library = id.GetValue().Member("lib");
    if (!library.HasValue()) {
      return Result<SelectedScene>(library.GetError());
    }
    if (!ScalarPointer(library.GetValue(), "Library")) {
      return Result<SelectedScene>(Failure("BLEND_SCENE_REFERENCE_INVALID",
          "ID.lib must be a scalar Library pointer", blocks, sceneIndex));
    }
    const auto libraryAddress = library.GetValue().Pointer();
    if (!libraryAddress.HasValue()) {
      return Result<SelectedScene>(libraryAddress.GetError());
    }
    if (libraryAddress.GetValue() != 0) {
      return Result<SelectedScene>(Failure("BLEND_SCENE_LINKED_UNSUPPORTED",
          "Linked active scenes are reported, not followed", blocks, sceneIndex));
    }
    const auto name = id.GetValue().Member("name");
    if (!name.HasValue()) {
      return Result<SelectedScene>(name.GetError());
    }
    const auto& nameView = name.GetValue();
    const auto nameBytes = nameView.Bytes();
    const auto terminator = std::find(nameBytes.begin(), nameBytes.end(), std::byte{0});
    if (nameView.Type().name != "char" || nameView.Type().length != 1 ||
        nameView.PointerLevel() != 0 ||
        nameView.ArrayDimensions().size() != 1 || nameBytes.size() < 3 ||
        nameBytes[0] != std::byte{'S'} || nameBytes[1] != std::byte{'C'} ||
        terminator == nameBytes.end() || terminator < nameBytes.begin() + 2) {
      return Result<SelectedScene>(Failure("BLEND_SCENE_NAME_INVALID",
          "Scene ID.name must be a terminated SC-prefixed char array", blocks, sceneIndex));
    }
    const auto unit = scene.GetValue().Member("unit");
    if (!unit.HasValue()) {
      return Result<SelectedScene>(unit.GetError());
    }
    if (unit.GetValue().Type().name != "UnitSettings" ||
        unit.GetValue().PointerLevel() != 0 || !unit.GetValue().ArrayDimensions().empty()) {
      return Result<SelectedScene>(Failure("BLEND_SCENE_REFERENCE_INVALID",
          "Scene.unit must be embedded scalar UnitSettings", blocks, sceneIndex));
    }
    const auto scale = unit.GetValue().Member("scale_length");
    if (!scale.HasValue()) {
      return Result<SelectedScene>(scale.GetError());
    }
    const auto value = scale.GetValue().FloatingPoint();
    if (!value.HasValue()) {
      return Result<SelectedScene>(value.GetError());
    }
    if (!std::isfinite(value.GetValue()) || value.GetValue() <= 0) {
      return Result<SelectedScene>(Failure("BLEND_SCENE_UNIT_SCALE_INVALID",
          "Scene.unit.scale_length must be finite and positive", blocks, sceneIndex));
    }
    SelectedScene selected{sceneIndex,
        {header.SourceVersion(),
            std::string(reinterpret_cast<const char*>(nameBytes.data() + 2),
                static_cast<std::size_t>(terminator - nameBytes.begin() - 2)),
            value.GetValue()}};
    return Result<SelectedScene>(std::move(selected));
  } catch (const std::bad_alloc&) {
    return Result<SelectedScene>(Failure("BLEND_SCENE_ALLOCATION",
        "Unable to allocate scene selection", blocks));
  } catch (const std::length_error&) {
    return Result<SelectedScene>(Failure("BLEND_SCENE_ALLOCATION",
        "Scene selection exceeds allocation limits", blocks));
  }
}

}