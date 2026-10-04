#include <blendScene/Scene.h>

#include <algorithm>
#include <cmath>
#include <limits>
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

Matrix4 ParentRelativeTransform(const Matrix4& worldTransform,
    const Matrix4& parentWorldTransform) {
  for (const auto* matrix : {&worldTransform, &parentWorldTransform}) {
    for (const auto& row : *matrix) {
      for (const auto value : row) {
        if (!std::isfinite(value)) {
          throw std::invalid_argument(
              "BLEND_SCENE_TRANSFORM_INVALID: world transforms must be finite");
        }
      }
    }
    if ((*matrix)[3] != IdentityMatrix[3]) {
      throw std::invalid_argument(
          "BLEND_SCENE_TRANSFORM_INVALID: world transforms must be affine");
    }
  }
  const auto finite = [](double value) {
    if (!std::isfinite(value)) {
      throw std::overflow_error(
          "BLEND_SCENE_TRANSFORM_INVALID: parent-relative construction overflow");
    }
    return value;
  };
  const auto singular = [] {
    throw std::invalid_argument(
        "BLEND_SCENE_TRANSFORM_SINGULAR: parent world transform is not numerically invertible");
  };

  // Solve parentLinear * local = [worldLinear, worldTranslation - parentTranslation].
  std::array<std::array<double, 7>, 3> rows{};
  for (std::size_t row = 0; row < 3; ++row) {
    const auto scale = std::max({std::abs(parentWorldTransform[row][0]),
        std::abs(parentWorldTransform[row][1]), std::abs(parentWorldTransform[row][2])});
    if (scale == 0) {
      singular();
    }
    for (std::size_t column = 0; column < 3; ++column) {
      rows[row][column] = parentWorldTransform[row][column] / scale;
      rows[row][column + 3] = finite(worldTransform[row][column] / scale);
    }
    const auto translation = worldTransform[row][3] - parentWorldTransform[row][3];
    const auto scaledTranslation = std::isfinite(translation) ? translation / scale : worldTransform[row][3] / scale - parentWorldTransform[row][3] / scale;
    rows[row][6] = finite(scaledTranslation);
  }
  for (std::size_t axis = 0; axis < 3; ++axis) {
    std::size_t pivot = axis;
    for (std::size_t row = axis + 1; row < 3; ++row) {
      if (std::abs(rows[row][axis]) > std::abs(rows[pivot][axis])) {
        pivot = row;
      }
    }
    // Row scaling makes this a dimensionless double-precision rank check.
    if (std::abs(rows[pivot][axis]) <= 8 * std::numeric_limits<double>::epsilon()) {
      singular();
    }
    std::swap(rows[axis], rows[pivot]);
    const auto divisor = rows[axis][axis];
    rows[axis][axis] = 1;
    for (std::size_t column = axis + 1; column < 7; ++column) {
      rows[axis][column] = finite(rows[axis][column] / divisor);
    }
    for (std::size_t row = 0; row < 3; ++row) {
      if (row == axis) {
        continue;
      }
      const auto factor = rows[row][axis];
      rows[row][axis] = 0;
      for (std::size_t column = axis + 1; column < 7; ++column) {
        rows[row][column] = finite(std::fma(-factor, rows[axis][column], rows[row][column]));
      }
    }
  }
  Matrix4 result = IdentityMatrix;
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t column = 0; column < 4; ++column) {
      result[row][column] = rows[row][column + 3];
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