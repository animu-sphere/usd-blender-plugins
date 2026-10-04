#include <blendScene/Decode.h>

#include <algorithm>
#include <cmath>
#include <new>
#include <stdexcept>
#include <unordered_map>

namespace blend {
namespace {

template <class Value>
Value Take(const Result<Value>& result) {
  if (!result.HasValue()) {
    throw result.GetError();
  }
  return result.GetValue();
}

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

Matrix4 EulerXyz(const Vector3& angles) {
  Matrix4 result = IdentityMatrix;
  for (std::size_t axis = 0; axis < 3; ++axis) {
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

class SceneDecoder {
public:
  SceneDecoder(std::span<const std::byte> bytes,
      std::span<const BlendBlock> blocks, const DnaSchema& schema,
      const Header& header, const PointerMap& pointers)
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
    for (const auto& selectedObject : selected.objects) {
      const auto sourceWorld = World(selectedObject.blockIndex);
      Object object;
      object.sourceName = selectedObject.sourceName;
      object.hiddenForRender = selectedObject.values->hiddenForRender;
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

  Vector3 Vector(const DnaValueView& object, std::string_view member,
      std::uint32_t index) const {
    constexpr std::array<std::uint64_t, 1> dimensions = {3};
    const auto value = FloatArray(object, member, dimensions, index);
    Vector3 result{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
      result[axis] = Take(Take(value.Element(axis)).FloatingPoint());
      if (!std::isfinite(result[axis])) {
        Fail("BLEND_SCENE_TRANSFORM_INVALID", "Source transform values must be finite", index);
      }
    }
    return result;
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

  void ReportEvaluation(const DnaValueView& object, std::uint32_t index) {
    const auto animation = Pointer(object, "adt", "AnimData", index);
    const auto constraints = Take(object.Member("constraints"));
    if (constraints.Type().name != "ListBase" || constraints.PointerLevel() != 0 ||
        !constraints.ArrayDimensions().empty()) {
      Fail("BLEND_SCENE_REFERENCE_INVALID", "Object constraints must be an embedded ListBase", index);
    }
    const auto first = Pointer(constraints, "first", "void", index);
    const auto last = Pointer(constraints, "last", "void", index);
    if (animation != 0 || first != 0 || last != 0) {
      diagnostics_.push_back({"BLEND_SCENE_EVALUATION_UNAPPLIED", Severity::Unsupported,
          "Saved source transforms are used; animation, drivers and constraints are not evaluated",
          blocks_[index].offset, index, {}, true});
    }
  }

  struct SourceObject {
    std::optional<std::uint32_t> parent;
    Matrix4 local;
    Matrix4 parentInverse;
  };

  SourceObject ReadObject(std::uint32_t index) {
    const auto object = Take(ViewDnaBlock(bytes_, blocks_, schema_, header_, index));
    if (Short(object, "type", index) != 0) {
      Fail("BLEND_SCENE_OBJECT_TYPE_UNSUPPORTED", "Native Scene decoding currently supports only Empty objects", index);
    }
    const auto data = Take(Take(object.Member("data")).Pointer());
    if (data != 0) {
      Fail("BLEND_SCENE_OBJECT_DATA_UNSUPPORTED", "Image Empty data is not decoded", index);
    }
    const auto flags = Short(object, "transflag", index);
    if ((flags & (1 << 8)) != 0) {
      Fail("BLEND_SCENE_INSTANCE_UNSUPPORTED", "Collection instances are validated, not expanded into the IR", index);
    }
    if (flags != 0 || Short(object, "rotmode", index) != 1) {
      Fail("BLEND_SCENE_TRANSFORM_UNSUPPORTED", "Only XYZ Euler objects without transform flags are decoded", index);
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
    const auto location = Vector(object, "loc", index);
    const auto deltaLocation = Vector(object, "dloc", index);
    const auto scale = Vector(object, "size", index);
    const auto deltaScale = Vector(object, "dscale", index);
    auto local = Multiply(EulerXyz(Vector(object, "drot", index)),
        EulerXyz(Vector(object, "rot", index)));
    for (std::size_t axis = 0; axis < 3; ++axis) {
      local[axis][3] = location[axis] + deltaLocation[axis];
      for (std::size_t row = 0; row < 3; ++row) {
        local[row][axis] *= scale[axis] * deltaScale[axis];
      }
    }
    ValidateMatrix(local, index);
    ReportEvaluation(object, index);
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
  const PointerMap& pointers_;
  std::unordered_map<std::uint32_t, Matrix4> worlds_;
  std::vector<Diagnostic> diagnostics_;
};

} // namespace

Result<Scene> DecodeScene(std::span<const std::byte> bytes,
    std::span<const BlendBlock> blocks, const DnaSchema& schema,
    const Header& header, const SceneTraversalLimits& limits) {
  try {
    const auto selected = Take(SelectSceneObjectValues(bytes, blocks, schema, header, limits));
    const auto pointers = Take(BuildPointerMap(blocks));
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
