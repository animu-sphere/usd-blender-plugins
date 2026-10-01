#include <blendScene/Scene.h>

namespace blend {

Vector3 ToUsdBasis(const Vector3& value) {
  return {value[0], value[2], -value[1]};
}

Matrix4 ToUsdBasis(const Matrix4& worldTransform) {
  constexpr std::array<std::size_t, 4> sourceAxes = {0, 2, 1, 3};
  constexpr std::array<double, 4> signs = {1, 1, -1, 1};
  Matrix4 result{};
  for (std::size_t row = 0; row < 4; ++row) {
    for (std::size_t column = 0; column < 4; ++column) {
      result[row][column] = signs[row] * signs[column] *
                            worldTransform[sourceAxes[row]][sourceAxes[column]];
    }
  }
  return result;
}

}