#include <blendScene/Decode.h>
#include <blendScene/Naming.h>

#include "DecodeInternal.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <stdexcept>
#include <unordered_map>

namespace blend {
namespace {

using detail::Take;

Matrix4 Multiply(const Matrix4& left, const Matrix4& right) {
  Matrix4 result{};
  for (std::size_t row = 0; row < 4; ++row) {
    for (std::size_t column = 0; column < 4; ++column) {
      for (std::size_t axis = 0; axis < 4; ++axis) {
        result[row][column] += left[row][axis] * right[axis][column];
      }
    }
  }
  return result;
}

Matrix4 Euler(const Vector3& angles, std::int64_t mode) {
  constexpr std::array<std::array<std::size_t, 3>, 6> orders = {{{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}}};
  Matrix4 result = IdentityMatrix;
  for (const auto axis : orders[static_cast<std::size_t>(mode - 1)]) {
    const auto first = (axis + 1) % 3;
    const auto second = (axis + 2) % 3;
    Matrix4 rotation = IdentityMatrix;
    rotation[first][first] = rotation[second][second] = std::cos(angles[axis]);
    rotation[second][first] = std::sin(angles[axis]);
    rotation[first][second] = -rotation[second][first];
    result = Multiply(rotation, result);
  }
  return result;
}

Matrix4 Quaternion(const std::array<double, 4>& value) {
  double squaredLength = 0;
  for (const auto component : value) {
    squaredLength += component * component;
  }
  if (squaredLength == 0) {
    return IdentityMatrix;
  }
  const auto length = std::sqrt(squaredLength);
  const auto w = value[0] / length;
  const auto x = value[1] / length;
  const auto y = value[2] / length;
  const auto z = value[3] / length;
  return {{{1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y), 0},
      {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x), 0},
      {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y), 0},
      {0, 0, 0, 1}}};
}

Matrix4 AxisAngle(const Vector3& axis, double angle) {
  const auto length = std::hypot(axis[0], axis[1], axis[2]);
  if (length == 0) {
    return IdentityMatrix;
  }
  const auto x = axis[0] / length;
  const auto y = axis[1] / length;
  const auto z = axis[2] / length;
  const auto c = std::cos(angle);
  const auto s = std::sin(angle);
  const auto t = 1 - c;
  return {{{c + x * x * t, x * y * t - z * s, x * z * t + y * s, 0},
      {y * x * t + z * s, c + y * y * t, y * z * t - x * s, 0},
      {z * x * t - y * s, z * y * t + x * s, c + z * z * t, 0},
      {0, 0, 0, 1}}};
}

class SceneDecoder {
public:
  SceneDecoder(std::span<const std::byte> bytes,
      std::span<const BlendBlock> blocks, const DnaSchema& schema,
      const Header& header, const detail::ScenePointers& pointers)
      : bytes_(bytes), blocks_(blocks), schema_(schema), header_(header),
        pointers_(pointers) {
  }

  Result<Scene> Run(const SelectedSceneObjects& selected) {
    Scene scene;
    scene.metadata = selected.scene.metadata;
    const UnitConversion units(scene.metadata.sourceUnitScale);
    std::unordered_map<std::uint32_t, std::size_t> indices;
    for (const auto& object : selected.objects) {
      indices.emplace(object.blockIndex, indices.size());
    }
    scene.objects.reserve(selected.objects.size());
    std::unordered_map<std::uint32_t, std::size_t> meshIndices;
    for (const auto& selectedObject : selected.objects) {
      const auto sourceWorld = World(selectedObject.blockIndex);
      Object object;
      object.sourceName = selectedObject.sourceName;
      object.hiddenForRender = selectedObject.values->hiddenForRender;
      if (selectedObject.values->type == 1) {
        const auto meshIndex = *selectedObject.dataBlockIndex;
        const auto [entry, inserted] = meshIndices.emplace(meshIndex, scene.meshes.size());
        if (inserted) {
          scene.meshes.push_back(detail::DecodeMesh(bytes_, blocks_, schema_, header_,
              pointers_, meshIndex, units, diagnostics_));
        }
        object.mesh = entry->second;
      }
      if (selectedObject.parentBlockIndex) {
        const auto parent = indices.find(*selectedObject.parentBlockIndex);
        if (parent != indices.end()) {
          object.parent = parent->second;
        }
      }
      try {
        object.worldTransform = units.WorldTransform(sourceWorld);
      } catch (const std::invalid_argument& error) {
        UnitFailure(error, selectedObject);
      } catch (const std::overflow_error& error) {
        UnitFailure(error, selectedObject);
      }
      scene.objects.push_back(std::move(object));
    }
    const auto names = ObjectIdentifiers(scene);
    const auto contextualize = [&](Diagnostic diagnostic, std::size_t occurrence = 0) {
      const auto object = std::find_if(selected.objects.begin(), selected.objects.end(),
          [&](const auto& entry) {
            if (entry.sourceName != diagnostic.datablock) {
              return false;
            }
            if (occurrence != 0) {
              --occurrence;
              return false;
            }
            return true;
          });
      if (object != selected.objects.end() && diagnostic.code != "BLEND_NAME_ALLOCATION") {
        diagnostic.blockIndex = object->blockIndex;
        diagnostic.byteOffset = blocks_[object->blockIndex].offset;
      }
      return diagnostic;
    };
    if (!names.HasValue()) {
      throw contextualize(names.GetError());
    }
    for (std::size_t index = 0; index < scene.objects.size(); ++index) {
      scene.objects[index].identifier = names.GetValue()[index];
    }
    std::unordered_map<std::string, std::size_t> nameOccurrences;
    for (const auto& diagnostic : names.Diagnostics()) {
      diagnostics_.push_back(contextualize(diagnostic, nameOccurrences[diagnostic.datablock]++));
    }
    return Result<Scene>(std::move(scene), std::move(diagnostics_));
  }

private:
  [[noreturn]] void Fail(const char* code, const char* message, std::uint32_t index) const {
    throw Diagnostic{code, Severity::Fatal, message, blocks_[index].offset, index, {}, false};
  }

  [[noreturn]] void UnitFailure(const std::exception& error,
      const SelectedObject& object) const {
    const std::string message = error.what();
    throw Diagnostic{message.substr(0, message.find(':')), Severity::Fatal,
        message, blocks_[object.blockIndex].offset, object.blockIndex, object.sourceName, false};
  }

  std::int64_t Short(const DnaValueView& object, std::string_view member,
      std::uint32_t index) const {
    const auto value = Take(object.Member(member));
    if (value.Type().name != "short" || value.Type().length != 2 ||
        value.PointerLevel() != 0 || !value.ArrayDimensions().empty()) {
      Fail("BLEND_SCENE_TRANSFORM_INVALID", "Transform mode must be a scalar short", index);
    }
    return Take(value.SignedInteger());
  }

  std::uint64_t Pointer(const DnaValueView& object, std::string_view member,
      std::string_view type, std::uint32_t index) const {
    const auto value = Take(object.Member(member));
    if (value.Type().name != type || value.PointerLevel() != 1 ||
        !value.ArrayDimensions().empty()) {
      Fail("BLEND_SCENE_REFERENCE_INVALID", "Expected a typed scalar pointer", index);
    }
    return Take(value.Pointer());
  }

  DnaValueView FloatArray(const DnaValueView& object, std::string_view member,
      std::span<const std::uint64_t> dimensions, std::uint32_t index) const {
    const auto value = Take(object.Member(member));
    if (value.Type().name != "float" || value.Type().length != 4 ||
        value.PointerLevel() != 0 ||
        !std::equal(dimensions.begin(), dimensions.end(),
            value.ArrayDimensions().begin(), value.ArrayDimensions().end())) {
      Fail("BLEND_SCENE_TRANSFORM_INVALID", "Transform storage must be a float array of the required shape", index);
    }
    return value;
  }

  template <std::size_t Size>
  std::array<double, Size> Floats(const DnaValueView& object, std::string_view member,
      std::uint32_t index) const {
    constexpr std::array<std::uint64_t, 1> dimensions = {Size};
    const auto value = FloatArray(object, member, dimensions, index);
    std::array<double, Size> result{};
    for (std::size_t axis = 0; axis < Size; ++axis) {
      result[axis] = Take(Take(value.Element(axis)).FloatingPoint());
      if (!std::isfinite(result[axis])) {
        Fail("BLEND_SCENE_TRANSFORM_INVALID", "Source transform values must be finite", index);
      }
    }
    return result;
  }

  double Float(const DnaValueView& object, std::string_view member,
      std::uint32_t index) const {
    const auto value = FloatArray(object, member, {}, index);
    const auto result = Take(value.FloatingPoint());
    if (!std::isfinite(result)) {
      Fail("BLEND_SCENE_TRANSFORM_INVALID", "Source transform values must be finite", index);
    }
    return result;
  }

  Matrix4 Rotation(const DnaValueView& object, std::uint32_t index) const {
    const auto mode = Short(object, "rotmode", index);
    if (mode >= 1 && mode <= 6) {
      return Multiply(Euler(Floats<3>(object, "drot", index), mode),
          Euler(Floats<3>(object, "rot", index), mode));
    }
    if (mode == 0) {
      return Multiply(Quaternion(Floats<4>(object, "dquat", index)),
          Quaternion(Floats<4>(object, "quat", index)));
    }
    if (mode == -1) {
      // Blender does not apply delta rotation in axis-angle mode.
      return AxisAngle(Floats<3>(object, "rotAxis", index), Float(object, "rotAngle", index));
    }
    Fail("BLEND_SCENE_TRANSFORM_UNSUPPORTED", "Unknown Object rotation mode", index);
  }

  Matrix4 ParentInverse(const DnaValueView& object, std::uint32_t index) const {
    constexpr std::array<std::uint64_t, 2> dimensions = {4, 4};
    const auto value = FloatArray(object, "parentinv", dimensions, index);
    Matrix4 result{};
    for (std::size_t column = 0; column < 4; ++column) {
      const auto savedColumn = Take(value.Element(column));
      for (std::size_t row = 0; row < 4; ++row) {
        result[row][column] = Take(Take(savedColumn.Element(row)).FloatingPoint());
      }
    }
    ValidateMatrix(result, index);
    return result;
  }

  void ValidateMatrix(const Matrix4& matrix, std::uint32_t index) const {
    for (const auto& row : matrix) {
      for (const auto value : row) {
        if (!std::isfinite(value)) {
          Fail("BLEND_SCENE_TRANSFORM_INVALID", "Transform construction must remain finite", index);
        }
      }
    }
    if (matrix[3] != std::array<double, 4>{0, 0, 0, 1}) {
      Fail("BLEND_SCENE_TRANSFORM_INVALID", "Source transform must be affine", index);
    }
  }

  std::string SourceName(const DnaValueView& object) const {
    const auto name = Take(Take(object.Member("id")).Member("name")).Bytes();
    const auto end = std::find(name.begin() + 2, name.end(), std::byte{0});
    return std::string(reinterpret_cast<const char*>(name.data() + 2),
        static_cast<std::size_t>(end - name.begin() - 2));
  }

  void ReportEvaluation(const DnaValueView& object, std::uint32_t index) {
    const auto animation = Pointer(object, "adt", "AnimData", index);
    const auto constraints = Take(object.Member("constraints"));
    if (constraints.Type().name != "ListBase" || constraints.PointerLevel() != 0 ||
        !constraints.ArrayDimensions().empty()) {
      Fail("BLEND_SCENE_REFERENCE_INVALID", "Object constraints must be an embedded ListBase", index);
    }
    const auto first = Pointer(constraints, "first", "void", index);
    const auto last = Pointer(constraints, "last", "void", index);
    bool modifiersPresent = false;
    if (Short(object, "type", index) == 1) {
      const auto modifiers = Take(object.Member("modifiers"));
      if (modifiers.Type().name != "ListBase" || modifiers.PointerLevel() != 0 ||
          !modifiers.ArrayDimensions().empty()) {
        Fail("BLEND_SCENE_REFERENCE_INVALID", "Object modifiers must be an embedded ListBase", index);
      }
      modifiersPresent = Pointer(modifiers, "first", "void", index) != 0;
      modifiersPresent = Pointer(modifiers, "last", "void", index) != 0 || modifiersPresent;
    }
    if (animation != 0 || first != 0 || last != 0 || modifiersPresent) {
      diagnostics_.push_back({"BLEND_SCENE_EVALUATION_UNAPPLIED", Severity::Unsupported,
          "Saved source transforms and geometry are used; animation, drivers, constraints and modifiers are not evaluated",
          blocks_[index].offset, index, SourceName(object), true});
    }
  }

  struct SourceObject {
    std::optional<std::uint32_t> parent;
    Matrix4 local;
    Matrix4 parentInverse;
  };

  SourceObject ReadObject(std::uint32_t index) {
    const auto object = Take(ViewDnaBlock(bytes_, blocks_, schema_, header_, index));
    const auto type = Short(object, "type", index);
    const auto data = Take(Take(object.Member("data")).Pointer());
    const auto flags = Short(object, "transflag", index);
    if ((flags & (1 << 8)) != 0) {
      Fail("BLEND_SCENE_INSTANCE_UNSUPPORTED", "Collection instances are validated, not expanded into the IR", index);
    }
    // Bit 2 records world handedness; signed source channels already reconstruct it.
    if ((flags & ~(1 << 2)) != 0) {
      Fail("BLEND_SCENE_TRANSFORM_UNSUPPORTED", "Object transform flags are not decoded", index);
    }
    const auto parentAddress = Pointer(object, "parent", "Object", index);
    std::optional<std::uint32_t> parent;
    if (parentAddress != 0) {
      parent = Take(pointers_.Resolve(parentAddress));
      if (!parent) {
        Fail("BLEND_SCENE_REFERENCE_INVALID", "Parent does not resolve to a saved Object", index);
      }
      if (Short(object, "partype", index) != 0) {
        Fail("BLEND_SCENE_TRANSFORM_UNSUPPORTED", "Only ordinary Object parenting is decoded", index);
      }
    }
    const auto location = Floats<3>(object, "loc", index);
    const auto deltaLocation = Floats<3>(object, "dloc", index);
    const auto scale = Floats<3>(object, "size", index);
    const auto deltaScale = Floats<3>(object, "dscale", index);
    auto local = Rotation(object, index);
    for (std::size_t axis = 0; axis < 3; ++axis) {
      local[axis][3] = location[axis] + deltaLocation[axis];
      for (std::size_t row = 0; row < 3; ++row) {
        local[row][axis] *= scale[axis] * deltaScale[axis];
      }
    }
    ValidateMatrix(local, index);
    ReportEvaluation(object, index);
    if (type != 1 && data != 0) {
      diagnostics_.push_back({"BLEND_SCENE_OBJECT_DATA_UNSUPPORTED", Severity::Unsupported,
          "Object data is not decoded; source hierarchy, transforms and visibility are preserved as an Empty",
          blocks_[index].offset, index, SourceName(object), true});
    }
    return {parent, local, parent ? ParentInverse(object, index) : IdentityMatrix};
  }

  Matrix4 World(std::uint32_t start) {
    std::vector<std::pair<std::uint32_t, SourceObject>> chain;
    auto current = start;
    while (!worlds_.contains(current)) {
      auto object = ReadObject(current);
      const auto parent = object.parent;
      chain.emplace_back(current, std::move(object));
      if (!parent) {
        break;
      }
      current = *parent;
    }
    while (!chain.empty()) {
      const auto& [index, object] = chain.back();
      auto world = object.local;
      if (object.parent) {
        world = Multiply(Multiply(worlds_.at(*object.parent), object.parentInverse), world);
      }
      ValidateMatrix(world, index);
      worlds_.emplace(index, world);
      chain.pop_back();
    }
    return worlds_.at(start);
  }

  std::span<const std::byte> bytes_;
  std::span<const BlendBlock> blocks_;
  const DnaSchema& schema_;
  const Header& header_;
  const detail::ScenePointers& pointers_;
  std::unordered_map<std::uint32_t, Matrix4> worlds_;
  std::vector<Diagnostic> diagnostics_;
};

} // namespace

Result<Scene> DecodeScene(std::span<const std::byte> bytes,
    std::span<const BlendBlock> blocks, const DnaSchema& schema,
    const Header& header, const SceneTraversalLimits& limits) {
  try {
    const auto selected = Take(SelectSceneObjectValues(bytes, blocks, schema, header, limits));
    const auto pointers = Take(detail::BuildScenePointers(bytes, blocks, schema, header));
    return SceneDecoder(bytes, blocks, schema, header, pointers).Run(selected);
  } catch (const Diagnostic& error) {
    return Result<Scene>(error);
  } catch (const std::bad_alloc&) {
    return Result<Scene>(Diagnostic{"BLEND_SCENE_ALLOCATION", Severity::Fatal,
        "Unable to allocate native Scene decoding", {}, {}, {}, false});
  } catch (const std::length_error&) {
    return Result<Scene>(Diagnostic{"BLEND_SCENE_ALLOCATION", Severity::Fatal,
        "Native Scene decoding exceeds allocation limits", {}, {}, {}, false});
  }
}

} // namespace blend
