#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace blend {

using Vector2 = std::array<double, 2>;
using Vector3 = std::array<double, 3>;
using Matrix4 = std::array<std::array<double, 4>, 4>;

inline constexpr Matrix4 IdentityMatrix = {{{1, 0, 0, 0},
    {0, 1, 0, 0},
    {0, 0, 1, 0},
    {0, 0, 0, 1}}};

struct SceneMetadata {
  std::string sourceVersion;
  std::string sourceScene;
  double sourceUnitScale = 1;
};

struct UvMap {
  std::string sourceName;
  std::vector<Vector2> values;
  std::vector<std::int32_t> indices;
  bool activeRender = false;
};

struct Mesh {
  std::string sourceName;
  std::vector<Vector3> points;
  std::vector<std::int32_t> faceVertexCounts;
  std::vector<std::int32_t> faceVertexIndices;
  std::vector<Vector3> cornerNormals;
  std::vector<UvMap> uvMaps;
};

struct Object {
  std::string sourceName;
  std::string identifier;
  std::optional<std::size_t> parent;
  std::optional<std::size_t> mesh;
  Matrix4 worldTransform = IdentityMatrix;
  bool hiddenForRender = false;
};

struct Scene {
  SceneMetadata metadata;
  std::vector<Object> objects;
  std::vector<Mesh> meshes;
};

Vector3 ToUsdBasis(const Vector3& value);
Matrix4 ToUsdBasis(const Matrix4& worldTransform);

}