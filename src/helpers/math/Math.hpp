#pragma once

#include <limits>o

// includes box and vector as well
#include <hyprutils/math/Region.hpp>
#include <hyprutils/math/Mat3x3.hpp>

// NOLINTNEXTLINE
using namespace Hyprutils::Math;

namespace Math
{
	constexpr const Vector2D VECTOR2D_MAX = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};

	eTransform		cgRotationToHyprutils(double degrees);
	eTransform		invertTransform(eTransform tr);
	eTransform		composeTransform(eTransform a, eTransform b);
	Vector2D		transformNormalized(const Vector2D& point, eTransform transform);
	Vector2D		mapNormalizedToBox(const Vector2D& point, const CBox& box, eTransform transform = HYPRUTILS_TRANSFORM_NORMAL);
}