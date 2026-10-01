#include <blendScene/Scene.h>

#include <iostream>
#include <stdexcept>

namespace {

void Require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
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

void CheckScene() {
  blend::Scene scene;
  Require(scene.objects.empty() && scene.meshes.empty(), "Empty Scene IR");
  scene.metadata.sourceVersion = "4.5";
  scene.metadata.sourceScene = "Scene";
  scene.metadata.sourceUnitScale = 0.01;
  scene.meshes.emplace_back();
  scene.meshes[0].sourceName = "SharedMesh";
  scene.meshes[0].points.push_back(blend::ToUsdBasis(blend::Vector3{1, 2, 3}));
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
    CheckScene();
    std::cout << "Scene IR and coordinate basis checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}