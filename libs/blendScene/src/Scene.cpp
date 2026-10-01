#include <blendScene/Scene.h>

#include <cmath>
#include <stdexcept>

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

UnitConversion::UnitConversion(double metersPerBlenderUnit)
    : metersPerBlenderUnit_(metersPerBlenderUnit) {
  if (!std::isfinite(metersPerBlenderUnit_) || metersPerBlenderUnit_ <= 0) {
    throw std::invalid_argument(
        "BLEND_SCENE_UNIT_SCALE_INVALID: source unit scale must be finite and positive");
  }
}

double UnitConversion::MetersPerBlenderUnit() const {
  return metersPerBlenderUnit_;
}

double UnitConversion::Distance(double value) const {
  if (!std::isfinite(value)) {
    throw std::invalid_argument(
        "BLEND_SCENE_UNIT_VALUE_INVALID: source distance must be finite");
  }
  const double result = value * metersPerBlenderUnit_;
  if (!std::isfinite(result)) {
    throw std::overflow_error(
        "BLEND_SCENE_UNIT_VALUE_INVALID: meter conversion overflow");
  }
  return result;
}

Vector3 UnitConversion::Position(const Vector3& value) const {
  return ToUsdBasis(Vector3{Distance(value[0]), Distance(value[1]),
      Distance(value[2])});
}

Matrix4 UnitConversion::WorldTransform(const Matrix4& value) const {
  if (value[3] != std::array<double, 4>{0, 0, 0, 1}) {
    throw std::invalid_argument(
        "BLEND_SCENE_UNIT_TRANSFORM_INVALID: world transform must be affine");
  }
  Matrix4 scaled = value;
  for (std::size_t row = 0; row < 3; ++row) {
    scaled[row][3] = Distance(value[row][3]);
  }
  return ToUsdBasis(scaled);
}
}