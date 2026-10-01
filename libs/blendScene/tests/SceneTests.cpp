#include <blendScene/Scene.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {

void Require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool Near(double left, double right) {
  return std::abs(left - right) <=
         1e-12 * (1 + std::abs(left) + std::abs(right));
}

template <typename Exception, typename Operation>
void RequireFailure(Operation operation, std::string_view code) {
  try {
    operation();
  } catch (const Exception& error) {
    Require(std::string_view(error.what()).starts_with(code),
        "Unit failure has the expected diagnostic code");
    return;
  }
  throw std::runtime_error("Expected unit conversion failure");
}

blend::Vector3 TransformPoint(const blend::Matrix4& transform,
    const blend::Vector3& point) {
  blend::Vector3 result{};
  for (std::size_t row = 0; row < 3; ++row) {
    result[row] = transform[row][3];
    for (std::size_t column = 0; column < 3; ++column) {
      result[row] += transform[row][column] * point[column];
    }
  }
  return result;
}

blend::Matrix4 Multiply(const blend::Matrix4& left, const blend::Matrix4& right) {
  blend::Matrix4 result{};
  for (std::size_t row = 0; row < 4; ++row) {
    for (std::size_t column = 0; column < 4; ++column) {
      for (std::size_t axis = 0; axis < 4; ++axis) {
        result[row][column] += left[row][axis] * right[axis][column];
      }
    }
  }
  return result;
}

blend::Vector3 Cross(const blend::Vector3& left, const blend::Vector3& right) {
  return {left[1] * right[2] - left[2] * right[1],
      left[2] * right[0] - left[0] * right[2],
      left[0] * right[1] - left[1] * right[0]};
}

void CheckBasis() {
  Require(blend::ToUsdBasis(blend::Vector3{1, 2, 3}) ==
              blend::Vector3{1, 3, -2},
      "Point basis conversion");
  Require(blend::ToUsdBasis(blend::Vector3{0, 0, 1}) ==
              blend::Vector3{0, 1, 0},
      "Z-up becomes Y-up");
  Require(blend::ToUsdBasis(blend::Vector3{0, -1, 0}) ==
              blend::Vector3{0, 0, 1},
      "Front becomes +Z");
  Require(blend::ToUsdBasis(blend::IdentityMatrix) == blend::IdentityMatrix,
      "Identity transform is unchanged");
  const blend::Matrix4 source = {{{2, 3, 5, 7},
      {11, 13, 17, 19},
      {23, 29, 31, 37},
      {0, 0, 0, 1}}};
  const blend::Matrix4 expected = {{{2, 5, -3, 7},
      {23, 31, -29, 37},
      {-11, -17, 13, -19},
      {0, 0, 0, 1}}};
  const auto converted = blend::ToUsdBasis(source);
  Require(converted == expected, "Matrix conversion is C W C inverse");
  const blend::Vector3 local = {41, 43, 47};
  Require(TransformPoint(converted, blend::ToUsdBasis(local)) ==
              blend::ToUsdBasis(TransformPoint(source, local)),
      "Converted world matrix and local point compose");
  const blend::Matrix4 child = {{{0, -1, 0, 3},
      {1, 0, 0, 5},
      {0, 0, -2, 7},
      {0, 0, 0, 1}}};
  Require(blend::ToUsdBasis(Multiply(source, child)) ==
              Multiply(converted, blend::ToUsdBasis(child)),
      "Basis conversion preserves parent-child composition");
  const blend::Vector3 edgeA = {1, 2, 3};
  const blend::Vector3 edgeB = {5, 7, 11};
  Require(Cross(blend::ToUsdBasis(edgeA), blend::ToUsdBasis(edgeB)) ==
              blend::ToUsdBasis(Cross(edgeA, edgeB)),
      "Basis rotation preserves right-handed winding");
  const auto normal = blend::ToUsdBasis(blend::Vector3{2, -3, 6});
  Require(normal[0] * normal[0] + normal[1] * normal[1] +
                  normal[2] * normal[2] ==
              49,
      "Basis rotation preserves normal length");
}

void CheckUnits() {
  const blend::Matrix4 parent = {{{2, 3, 5, 7},
      {11, 13, 17, 19},
      {23, 29, 31, 37},
      {0, 0, 0, 1}}};
  const blend::Matrix4 child = {{{0, -1, 0, 3},
      {1, 0, 0, 5},
      {0, 0, -2, 7},
      {0, 0, 0, 1}}};
  for (const double scale : {1.0, 0.01, 0.001, 10.0}) {
    const blend::UnitConversion units(scale);
    Require(units.MetersPerBlenderUnit() == scale,
        "Validated source scale is retained as provenance");
    Require(Near(units.Distance(1 / scale), 1),
        "Equivalent source distances normalize to one meter");
    const auto position = units.Position({1 / scale, 2 / scale, 3 / scale});
    const blend::Vector3 expectedPosition = {1, 3, -2};
    for (std::size_t axis = 0; axis < 3; ++axis) {
      Require(Near(position[axis], expectedPosition[axis]),
          "Position normalization applies units and basis once");
    }
    const auto convertedParent = units.WorldTransform(parent);
    const auto basisParent = blend::ToUsdBasis(parent);
    for (std::size_t row = 0; row < 4; ++row) {
      for (std::size_t column = 0; column < 4; ++column) {
        const double expected = row < 3 && column == 3
                                    ? basisParent[row][column] * scale
                                    : basisParent[row][column];
        Require(Near(convertedParent[row][column], expected),
            "Only transform translation is unit-scaled");
      }
    }
    Require(units.WorldTransform(blend::IdentityMatrix) == blend::IdentityMatrix,
        "Units do not change identity or dimensionless object scale");
    const blend::Vector3 local = {41, 43, 47};
    const auto actualWorld = TransformPoint(convertedParent, units.Position(local));
    const auto expectedWorld = units.Position(TransformPoint(parent, local));
    for (std::size_t axis = 0; axis < 3; ++axis) {
      Require(Near(actualWorld[axis], expectedWorld[axis]),
          "Meter-space mesh points and world transform compose");
    }
    const auto actualChild = Multiply(convertedParent, units.WorldTransform(child));
    const auto expectedChild = units.WorldTransform(Multiply(parent, child));
    for (std::size_t row = 0; row < 4; ++row) {
      for (std::size_t column = 0; column < 4; ++column) {
        Require(Near(actualChild[row][column], expectedChild[row][column]),
            "Unit normalization preserves parent-child composition");
      }
    }
    const double halfWidth = 0.5 / scale;
    const auto minimum = units.Position({-halfWidth, -halfWidth, -halfWidth});
    const auto maximum = units.Position({halfWidth, halfWidth, halfWidth});
    for (std::size_t axis = 0; axis < 3; ++axis) {
      Require(Near(std::abs(maximum[axis] - minimum[axis]), 1),
          "Equivalent synthetic cubes normalize to one-meter extents");
    }
  }
  for (const double invalid : {0.0, -0.01,
           std::numeric_limits<double>::quiet_NaN(),
           std::numeric_limits<double>::infinity(),
           -std::numeric_limits<double>::infinity()}) {
    RequireFailure<std::invalid_argument>(
        [invalid] { blend::UnitConversion units(invalid); },
        "BLEND_SCENE_UNIT_SCALE_INVALID:");
  }
  const blend::UnitConversion units(10);
  for (const double invalid : {std::numeric_limits<double>::quiet_NaN(),
           std::numeric_limits<double>::infinity()}) {
    RequireFailure<std::invalid_argument>([&] { units.Distance(invalid); },
        "BLEND_SCENE_UNIT_VALUE_INVALID:");
    RequireFailure<std::invalid_argument>([&] { units.Position({0, invalid, 0}); },
        "BLEND_SCENE_UNIT_VALUE_INVALID:");
    auto transform = blend::IdentityMatrix;
    transform[2][3] = invalid;
    RequireFailure<std::invalid_argument>([&] { units.WorldTransform(transform); },
        "BLEND_SCENE_UNIT_VALUE_INVALID:");
  }
  RequireFailure<std::overflow_error>(
      [&] { units.Distance(std::numeric_limits<double>::max()); },
      "BLEND_SCENE_UNIT_VALUE_INVALID:");
  auto projective = blend::IdentityMatrix;
  projective[3][0] = 1;
  RequireFailure<std::invalid_argument>([&] { units.WorldTransform(projective); },
      "BLEND_SCENE_UNIT_TRANSFORM_INVALID:");
}

void CheckScene() {
  blend::Scene scene;
  Require(scene.objects.empty() && scene.meshes.empty(), "Empty Scene IR");
  scene.metadata.sourceVersion = "4.5";
  scene.metadata.sourceScene = "Scene";
  scene.metadata.sourceUnitScale = 0.01;
  scene.meshes.emplace_back();
  scene.meshes[0].sourceName = "SharedMesh";
  const blend::UnitConversion units(scene.metadata.sourceUnitScale);
  scene.meshes[0].points.push_back(units.Position({100, 200, 300}));
  scene.objects.emplace_back();
  scene.objects[0].sourceName = "Parent";
  scene.objects.emplace_back();
  scene.objects[1].sourceName = "Child";
  scene.objects[1].parent = 0;
  scene.objects[1].mesh = 0;
  scene.objects.emplace_back();
  scene.objects[2].mesh = 0;
  Require(!scene.objects[0].parent && !scene.objects[0].mesh,
      "Empty object has no parent or mesh");
  Require(scene.objects[0].worldTransform == blend::IdentityMatrix,
      "Default object transform is identity");
  Require(scene.objects[1].parent == 0 && scene.objects[1].mesh == 0 &&
              scene.objects[2].mesh == 0,
      "Objects reference parents and shared meshes by index");
  const auto copy = scene;
  scene.meshes[0].points[0][0] = 99;
  scene.objects[1].sourceName = "Changed";
  Require(copy.meshes[0].points[0] == blend::Vector3{1, 3, -2} &&
              copy.objects[1].sourceName == "Child",
      "Scene IR owns its strings and arrays");
  Require(copy.metadata.sourceVersion == "4.5" &&
              copy.metadata.sourceScene == "Scene" &&
              copy.metadata.sourceUnitScale == 0.01,
      "Source metadata is independent of basis conversion");
}

}

int main() {
  try {
    CheckBasis();
    CheckUnits();
    CheckScene();
    std::cout << "Scene IR, coordinate basis and unit checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}